
#!/usr/bin/env python3
"""
follow_me_realsense.py - Robot follow-me using PoseNet + RealSense depth

Uses PoseNet for person detection (turn control) and a RealSense D-series
depth camera to maintain a set distance (forward/backward control).

Turn control: proportional, identical to turntoface_p.py (X joystick axis).
Distance control: proportional, drives toward/away from person (Y joystick axis).

Serial protocol:  x,y,b\\n   (0-4095, 2048 = neutral)
  x > 2048  → turn left     x < 2048  → turn right
  y > 2048  → forward       y < 2048  → backward  (flip with --invert_y)

Usage:

    LD_PRELOAD=/usr/lib/aarch64-linux-gnu/libgomp.so.1 python3 follow_me_realsense.py 

    python3 follow_me_realsense.py
    python3 follow_me_realsense.py display://0
    python3 follow_me_realsense.py --target_dist 1.5 --max_power 0.85
"""

import sys
import time
import argparse
import numpy as np
import serial
import pyrealsense2 as rs

# ── jetson-inference path setup ───────────────────────────────────────────────
_JLIB = "/home/ubuntu/jetson-inference/build/aarch64/lib/python/3.8"
_JPKG = "/home/ubuntu/jetson-inference/python/python"
for _p in (_JLIB, _JPKG):
    if _p not in sys.path:
        sys.path.insert(0, _p)

import os
import jetson_utils_python as _ju
sys.modules.setdefault("jetson_utils", _ju)

# poseNet needs 'networks/' symlink in cwd
os.chdir("/home/ubuntu/jetson-inference/build/aarch64/bin")

from jetson_inference import poseNet
from jetson_utils import videoOutput, Log, cudaFromNumpy

# ── Serial settings ────────────────────────────────────────────────────────────
SERIAL_PORT = "/dev/ttyUSB0"
BAUD_RATE   = 115200

# ── Joystick protocol (0-4095, 2048 = neutral) ────────────────────────────────
X_NEUTRAL = 2048
Y_NEUTRAL = 2048
X_MAX     = 4095
Y_MAX     = 4095

# ── Turn control tuning (same as turntoface_p.py) ─────────────────────────────
MAX_POWER_TURN = 0.85   # deflection at frame edge
MIN_POWER_TURN = 0.25   # deflection just outside deadband
DEADBAND_TURN  = 0.08   # fraction of half-frame

# ── Distance control tuning ───────────────────────────────────────────────────
MAX_POWER_DRIVE = 0.70  # max forward/backward deflection
MIN_POWER_DRIVE = 0.20  # min forward/backward deflection (just outside deadband)
DEADBAND_DIST   = 0.20  # metres — stay still if within this of target
TARGET_DIST     = 1.2   # metres — desired person-to-robot distance
MAX_DIST_ERROR  = 1.5   # metres — error that maps to full drive power

# ── RealSense settings ────────────────────────────────────────────────────────
RS_WIDTH    = 640
RS_HEIGHT   = 480
RS_FPS      = 30
DEPTH_PATCH = 12        # half-size of depth sampling patch (pixels)

SEND_HZ = 10

# ──────────────────────────────────────────────────────────────────────────────

def send_cmd(ser: serial.Serial, x: int, y: int = Y_NEUTRAL, b: int = 0):
    ser.write(f"{x},{y},{b}\n".encode())


def proportional_axis(error: float, deadband: float,
                      min_power: float, max_power: float,
                      neutral: int, axis_max: int) -> int:
    """
    Proportional control for one joystick axis.

    error > 0  → positive deflection (axis > neutral)
    error < 0  → negative deflection (axis < neutral)
    |error| <= deadband → neutral
    """
    if abs(error) <= deadband:
        return neutral

    t = (abs(error) - deadband) / (1.0 - deadband)
    t = max(0.0, min(1.0, t))

    power      = min_power + t * (max_power - min_power)
    deflection = int(power * (axis_max - neutral))

    return neutral + deflection if error > 0 else neutral - deflection


def pose_center_x(pose) -> float:
    """Horizontal center: nose → neck → shoulder midpoint → bbox center."""
    for name in ('nose', 'neck'):
        idx = pose.FindKeypoint(name)
        if idx >= 0:
            return pose.Keypoints[idx].x

    ls = pose.FindKeypoint('left_shoulder')
    rs = pose.FindKeypoint('right_shoulder')
    if ls >= 0 and rs >= 0:
        return (pose.Keypoints[ls].x + pose.Keypoints[rs].x) / 2.0

    return (pose.Left + pose.Right) / 2.0


def pose_depth_point(pose):
    """
    Return (cx, cy) pixel to sample depth at.
    Prefers the torso center (neck/shoulder midpoint) for stable depth reads.
    """
    ls = pose.FindKeypoint('left_shoulder')
    rs = pose.FindKeypoint('right_shoulder')
    if ls >= 0 and rs >= 0:
        cx = (pose.Keypoints[ls].x + pose.Keypoints[rs].x) / 2.0
        cy = (pose.Keypoints[ls].y + pose.Keypoints[rs].y) / 2.0
        return cx, cy

    neck = pose.FindKeypoint('neck')
    if neck >= 0:
        return pose.Keypoints[neck].x, pose.Keypoints[neck].y

    return (pose.Left + pose.Right) / 2.0, (pose.Top + pose.Bottom) / 2.0


def sample_depth(depth_frame, cx: float, cy: float,
                 patch: int = DEPTH_PATCH):
    """
    Median depth (metres) in a patch around (cx, cy).
    Returns None if no valid pixels found.
    """
    w  = depth_frame.get_width()
    h  = depth_frame.get_height()
    x0 = max(0, int(cx) - patch)
    x1 = min(w, int(cx) + patch + 1)
    y0 = max(0, int(cy) - patch)
    y1 = min(h, int(cy) + patch + 1)

    depth_arr = np.asanyarray(depth_frame.get_data())[y0:y1, x0:x1]
    valid = depth_arr[depth_arr > 0]
    if len(valid) == 0:
        return None

    return float(np.median(valid)) * depth_frame.get_units()   # → metres


def main():
    parser = argparse.ArgumentParser(
        description="PoseNet + RealSense follow-me (proportional turn + distance control)",
        formatter_class=argparse.RawTextHelpFormatter,
        epilog=poseNet.Usage() + videoOutput.Usage() + Log.Usage(),
    )
    parser.add_argument("output",            type=str,   default="",             nargs="?",
                        help="display URI, e.g. display://0 (omit for headless)")
    parser.add_argument("--network",         type=str,   default="resnet18-body",
                        help="PoseNet model (default: resnet18-body)")
    parser.add_argument("--threshold",       type=float, default=0.15,
                        help="PoseNet confidence threshold (default: 0.15)")

    # Turn tuning
    parser.add_argument("--deadband",        type=float, default=DEADBAND_TURN,
                        help=f"turn deadband fraction (default: {DEADBAND_TURN})")
    parser.add_argument("--max_power",       type=float, default=MAX_POWER_TURN,
                        help=f"max turn deflection 0-1 (default: {MAX_POWER_TURN})")
    parser.add_argument("--min_power",       type=float, default=MIN_POWER_TURN,
                        help=f"min turn deflection 0-1 (default: {MIN_POWER_TURN})")

    # Distance tuning
    parser.add_argument("--target_dist",     type=float, default=TARGET_DIST,
                        help=f"target distance in metres (default: {TARGET_DIST})")
    parser.add_argument("--deadband_dist",   type=float, default=DEADBAND_DIST,
                        help=f"distance deadband in metres (default: {DEADBAND_DIST})")
    parser.add_argument("--max_power_drive", type=float, default=MAX_POWER_DRIVE,
                        help=f"max drive deflection 0-1 (default: {MAX_POWER_DRIVE})")
    parser.add_argument("--min_power_drive", type=float, default=MIN_POWER_DRIVE,
                        help=f"min drive deflection 0-1 (default: {MIN_POWER_DRIVE})")
    parser.add_argument("--max_dist_error",  type=float, default=MAX_DIST_ERROR,
                        help=f"distance error (m) for full drive power (default: {MAX_DIST_ERROR})")
    parser.add_argument("--invert_y",        action="store_true",
                        help="invert Y axis (if y<2048 is forward on your robot)")

    try:
        args = parser.parse_known_args()[0]
    except SystemExit:
        parser.print_help()
        sys.exit(0)

    # ── Serial ─────────────────────────────────────────────────────────────────
    try:
        ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1)
        time.sleep(2)
        print(f"Serial connected: {SERIAL_PORT} @ {BAUD_RATE}")
    except Exception as e:
        print(f"Serial error: {e}")
        sys.exit(1)

    # ── RealSense pipeline ─────────────────────────────────────────────────────
    pipeline = rs.pipeline()
    cfg      = rs.config()
    cfg.enable_stream(rs.stream.color, RS_WIDTH, RS_HEIGHT, rs.format.bgr8, RS_FPS)
    cfg.enable_stream(rs.stream.depth, RS_WIDTH, RS_HEIGHT, rs.format.z16,  RS_FPS)
    pipeline.start(cfg)

    # Align depth to color frame so pixel coords match
    align = rs.align(rs.stream.color)

    # ── PoseNet ────────────────────────────────────────────────────────────────
    net = poseNet(args.network, sys.argv, args.threshold)

    # ── Optional display ───────────────────────────────────────────────────────
    display = videoOutput(args.output, argv=sys.argv) if args.output else None
    overlay = "links,keypoints" if display else "none"

    send_dt   = 1.0 / SEND_HZ
    last_send = 0.0

    target_dist  = args.target_dist
    db_dist_norm = args.deadband_dist / args.max_dist_error   # normalised deadband

    print(f"Follow-Me (RealSense) running\n"
          f"  turn:  deadband={args.deadband:.2f}  "
          f"power={args.min_power:.2f}-{args.max_power:.2f}\n"
          f"  drive: target={target_dist:.2f}m  "
          f"deadband=±{args.deadband_dist:.2f}m  "
          f"power={args.min_power_drive:.2f}-{args.max_power_drive:.2f}\n"
          f"  invert_y={args.invert_y} | Ctrl-C to quit")

    try:
        while True:
            # ── Grab RealSense frames ──────────────────────────────────────────
            frames       = pipeline.wait_for_frames()
            aligned      = align.process(frames)
            color_frame  = aligned.get_color_frame()
            depth_frame  = aligned.get_depth_frame()

            if not color_frame or not depth_frame:
                continue

            # BGR → RGB → CUDA image for PoseNet
            bgr    = np.asanyarray(color_frame.get_data())   # H×W×3 uint8
            rgb    = bgr[:, :, ::-1].copy()                  # contiguous RGB
            cuda_img = cudaFromNumpy(rgb)

            poses = net.Process(cuda_img, overlay=overlay)

            frame_w  = color_frame.get_width()
            frame_cx = frame_w / 2.0
            x_cmd    = X_NEUTRAL
            y_cmd    = Y_NEUTRAL

            if poses:
                # Track the largest detected person (closest / most prominent)
                best = max(poses,
                           key=lambda p: (p.Right - p.Left) * (p.Bottom - p.Top))

                # ── Turn control (X axis) ──────────────────────────────────────
                cx = pose_center_x(best)
                turn_error = (frame_cx - cx) / (frame_w / 2.0)   # −1..+1
                #   turn_error > 0 → person is left → turn left (x > neutral)
                x_cmd = proportional_axis(
                    turn_error,
                    args.deadband, args.min_power, args.max_power,
                    X_NEUTRAL, X_MAX,
                )

                # ── Distance control (Y axis) ──────────────────────────────────
                dcx, dcy = pose_depth_point(best)
                dist = sample_depth(depth_frame, dcx, dcy)

                if dist is not None and 0.1 < dist < 8.0:
                    raw_err  = dist - target_dist          # metres, signed
                    dist_err = raw_err / args.max_dist_error  # normalised −1..+1
                    dist_err = max(-1.0, min(1.0, dist_err))
                    # dist_err > 0 → too far → drive forward → y > neutral
                    y_cmd = proportional_axis(
                        dist_err,
                        db_dist_norm, args.min_power_drive, args.max_power_drive,
                        Y_NEUTRAL, Y_MAX,
                    )
                    if args.invert_y:
                        y_cmd = Y_NEUTRAL - (y_cmd - Y_NEUTRAL)
                    dist_str = f"{dist:.2f}m"
                else:
                    dist_str = "no depth"

                turn_lbl = ("LEFT " if x_cmd > X_NEUTRAL else
                            "RIGHT" if x_cmd < X_NEUTRAL else "FACE ")
                fwd_lbl  = ("FWD" if y_cmd > Y_NEUTRAL else
                            "REV" if y_cmd < Y_NEUTRAL else "HOLD")

                print(f"  {turn_lbl} {fwd_lbl}  "
                      f"turn={turn_error:+.3f}  dist={dist_str}  "
                      f"x={x_cmd}  y={y_cmd}  "
                      f"({net.GetNetworkFPS():.1f} FPS)")
            else:
                print(f"  NO PERSON  x={x_cmd}  y={y_cmd}  "
                      f"({net.GetNetworkFPS():.1f} FPS)")

            # ── Rate-limited serial send ───────────────────────────────────────
            now = time.monotonic()
            if now - last_send >= send_dt:
                send_cmd(ser, x_cmd, y_cmd)
                last_send = now

            if display:
                display.Render(cuda_img)
                display.SetStatus(
                    f"Follow Me RS | {net.GetNetworkFPS():.0f} FPS | "
                    f"x={x_cmd} y={y_cmd}"
                )
                if not display.IsStreaming():
                    break

    except KeyboardInterrupt:
        print("\nStopped by user.")
    finally:
        print("Sending stop command...")
        send_cmd(ser, X_NEUTRAL, Y_NEUTRAL)
        time.sleep(0.1)
        ser.close()
        pipeline.stop()
        print("Done.")


if __name__ == "__main__":
    main()
