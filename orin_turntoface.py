#!/usr/bin/env python3
"""
turntoface_p.py - Robot turn-to-face using PoseNet body detection (proportional control)

Detects a person with PoseNet and sends proportional turn commands via serial
to face them. Turn speed scales with how far off-center the person is:
- Near center (just outside deadband) → minimum turn power
- Near edge of frame → maximum turn power

Usage:
    python3 turntoface_p.py /dev/video0
    python3 turntoface_p.py /dev/video0 display://0
    python3 turntoface_p.py /dev/video0 --max_power 0.85 --min_power 0.25 --deadband 0.08

All standard jetson-inference video/network args are supported.
"""

import sys
import time
import argparse
import serial

_JLIB = "/home/ubuntu/jetson-inference/build/aarch64/lib/python/3.8"
_JPKG = "/home/ubuntu/jetson-inference/python/python"
for _p in (_JLIB, _JPKG):
    if _p not in sys.path:
        sys.path.insert(0, _p)

import importlib as _il
import os
import jetson_utils_python as _ju
sys.modules.setdefault("jetson_utils", _ju)

os.chdir("/home/ubuntu/jetson-inference/build/aarch64/bin")

from jetson_inference import poseNet
from jetson_utils import videoSource, videoOutput, Log

# ── Serial settings ────────────────────────────────────────────────────────────
SERIAL_PORT = "/dev/ttyUSB0"
BAUD_RATE   = 115200

# ── Joystick protocol (0-4095, 2048 = neutral) ────────────────────────────────
X_NEUTRAL = 2048
Y_NEUTRAL = 2048
X_MIN     = 0
X_MAX     = 4095

# ── Control tuning ────────────────────────────────────────────────────────────
MAX_POWER  = 0.85   # 0.0–1.0, turn deflection at the edge of the frame
MIN_POWER  = 0.25   # 0.0–1.0, minimum turn deflection just outside deadband
DEADBAND   = 0.08   # fraction of half-frame — ignore small errors
SEND_HZ    = 10     # serial command rate

# ──────────────────────────────────────────────────────────────────────────────

def send_cmd(ser: serial.Serial, x: int, y: int = Y_NEUTRAL, b: int = 0):
    ser.write(f"{x},{y},{b}\n".encode())


def proportional_x(error: float, deadband: float, min_power: float, max_power: float) -> int:
    """
    Proportional turn control with a minimum speed floor.

    error > 0  →  person is left of center  →  turn left  (x < neutral)
    error < 0  →  person is right of center →  turn right (x > neutral)

    Within the deadband: neutral.
    Outside the deadband: power scales linearly from min_power (at deadband edge)
    to max_power (at |error| == 1.0).
    """
    if abs(error) <= deadband:
        return X_NEUTRAL

    # Normalise error to 0..1 range beyond the deadband
    t = (abs(error) - deadband) / (1.0 - deadband)
    t = max(0.0, min(1.0, t))

    power      = min_power + t * (max_power - min_power)
    deflection = int(power * (X_MAX - X_NEUTRAL))

    if error > 0:
        return X_NEUTRAL + deflection   # turn right
    else:
        return X_NEUTRAL - deflection   # turn left


def pose_center_x(pose) -> float:
    """
    Return the best estimate of the person's horizontal center.
    Prefers nose → neck → midpoint of shoulders → bounding box center.
    """
    for name in ('nose', 'neck'):
        idx = pose.FindKeypoint(name)
        if idx >= 0:
            return pose.Keypoints[idx].x

    ls = pose.FindKeypoint('left_shoulder')
    rs = pose.FindKeypoint('right_shoulder')
    if ls >= 0 and rs >= 0:
        return (pose.Keypoints[ls].x + pose.Keypoints[rs].x) / 2.0

    return (pose.Left + pose.Right) / 2.0


def main():
    parser = argparse.ArgumentParser(
        description="PoseNet-based robot turn-to-face (proportional control)",
        formatter_class=argparse.RawTextHelpFormatter,
        epilog=poseNet.Usage() + videoSource.Usage() + videoOutput.Usage() + Log.Usage(),
    )
    parser.add_argument("input",          type=str,   default="/dev/video0", nargs="?",
                        help="input stream URI (default: /dev/video0)")
    parser.add_argument("output",         type=str,   default="",            nargs="?",
                        help="output stream URI, e.g. display://0 (omit for headless)")
    parser.add_argument("--network",      type=str,   default="resnet18-body",
                        help="PoseNet model (default: resnet18-body)")
    parser.add_argument("--threshold",    type=float, default=0.15,
                        help="detection confidence threshold (default: 0.15)")
    parser.add_argument("--deadband",     type=float, default=DEADBAND,
                        help=f"center deadband fraction (default: {DEADBAND})")
    parser.add_argument("--max_power",    type=float, default=MAX_POWER,
                        help=f"max turn deflection 0.0-1.0 (default: {MAX_POWER})")
    parser.add_argument("--min_power",    type=float, default=MIN_POWER,
                        help=f"min turn deflection 0.0-1.0 (default: {MIN_POWER})")

    try:
        args = parser.parse_known_args()[0]
    except SystemExit:
        parser.print_help()
        sys.exit(0)

    deadband  = args.deadband
    min_power = args.min_power
    max_power = args.max_power

    # ── Serial ─────────────────────────────────────────────────────────────────
    try:
        ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1)
        time.sleep(2)
        print(f"Serial connected: {SERIAL_PORT} @ {BAUD_RATE}")
    except Exception as e:
        print(f"Serial error: {e}")
        sys.exit(1)

    # ── PoseNet ────────────────────────────────────────────────────────────────
    net = poseNet(args.network, sys.argv, args.threshold)

    # ── Video I/O ──────────────────────────────────────────────────────────────
    camera  = videoSource(args.input, argv=sys.argv)
    display = videoOutput(args.output, argv=sys.argv) if args.output else None

    overlay   = "links,keypoints" if display else "none"
    send_dt   = 1.0 / SEND_HZ
    last_send = 0.0

    print(f"Turn-to-face (P) running | network={args.network} | "
          f"min_power={min_power:.2f} | max_power={max_power:.2f} | "
          f"deadband={deadband:.2f} | Ctrl-C to quit")

    try:
        while True:
            img = camera.Capture()
            if img is None:
                continue

            poses = net.Process(img, overlay=overlay)

            frame_cx = img.width / 2.0
            x_cmd    = X_NEUTRAL

            if poses:
                best = max(poses,
                           key=lambda p: (p.Right - p.Left) * (p.Bottom - p.Top))

                cx    = pose_center_x(best)
                error = (frame_cx - cx) / (img.width / 2.0)   # −1..+1

                x_cmd = proportional_x(error, deadband, min_power, max_power)

                if x_cmd > X_NEUTRAL:
                    label = "TURN LEFT "
                elif x_cmd < X_NEUTRAL:
                    label = "TURN RIGHT"
                else:
                    label = "STOP      "

                print(f"  {label}  error={error:+.3f}  x_cmd={x_cmd}"
                      f"  ({net.GetNetworkFPS():.1f} FPS)")
            else:
                print(f"  NO PERSON                x_cmd={x_cmd}"
                      f"  ({net.GetNetworkFPS():.1f} FPS)")

            now = time.monotonic()
            if now - last_send >= send_dt:
                send_cmd(ser, x_cmd)
                last_send = now

            if display:
                display.Render(img)
                display.SetStatus(
                    f"Turn-to-Face (P) | {args.network} | {net.GetNetworkFPS():.0f} FPS | x={x_cmd}"
                )
                if not display.IsStreaming():
                    break

            if not camera.IsStreaming():
                break

    except KeyboardInterrupt:
        print("\nStopped by user.")
    finally:
        print("Sending stop command...")
        send_cmd(ser, X_NEUTRAL, Y_NEUTRAL)
        time.sleep(0.1)
        ser.close()
        print("Done.")


if __name__ == "__main__":
    main()
