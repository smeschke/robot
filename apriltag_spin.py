#!/usr/bin/env python3
import cv2
import apriltag
import serial
import time

### this script will drive toward an apriltag, spin and repeat
### use with serial_input.ino flashed to ESP32 on robot


# --- Grace-period tuning ---
GRACE_AREA_MAX = 10000     # below this = far (use grace)
TAG_LOST_GRACE = 0.6       # seconds of motion commitment when far
last_seen_t = 0
last_area   = 0

# ===================== CONFIG =====================
SERIAL_PORT = "/dev/ttyUSB0"
BAUD        = 115200

CAM_INDEX   = 0
FRAME_W     = 640
FRAME_H     = 480

TAG_FAMILY      = "tag36h11"
TAG_AREA_TARGET = 50000
SIZE_SMALL = 15000
SIZE_LARGE = 30000
CENTER_TOL      = 60

# ---- Speeds (match your robot) ----
SPEED_SEARCH = 40
SPEED_FAR    = 80
SPEED_MID    = 60
SPEED_CLOSE  = 40
SPEED_TURN   = 110


PAUSE_TIME = 1.0
TURN_TIME  = 3.33
LOOP_DT    = 0.05
# ==================================================

# ---------- Serial ----------
ser = serial.Serial(SERIAL_PORT, BAUD, timeout=0)
time.sleep(1)

def send(cmd):
    ser.write((cmd + "\n").encode("ascii"))

def stop_all():
    send("/F/off")
    send("/L/off")
    send("/R/off")
    send("/B/off")

last_speed = None
def set_speed(val):
    global last_speed
    val = int(val)
    if val != last_speed:
        send(f"/speed={val}")
        last_speed = val

# ---------- Camera ----------
cap = cv2.VideoCapture(CAM_INDEX)
cap.set(cv2.CAP_PROP_FRAME_WIDTH, FRAME_W)
cap.set(cv2.CAP_PROP_FRAME_HEIGHT, FRAME_H)

# ---------- AprilTag ----------
detector = apriltag.Detector(
    apriltag.DetectorOptions(families=TAG_FAMILY)
)

# ---------- States ----------
SEARCH = "SEARCH"
DRIVE  = "DRIVE"
PAUSE1 = "PAUSE1"
TURN   = "TURN"
PAUSE2 = "PAUSE2"

state = SEARCH
state_t0 = time.time()

print("[INFO] AprilTag homing loop (speed-controlled) started")

# =================================================
try:
    while True:
        ret, frame = cap.read()
        cv2.imshow('frame', frame)
        cv2.waitKey(1)
        if not ret:
            print("[ERROR] Camera read failed")
            break

        gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
        detections = detector.detect(gray)

        now = time.time()

        # ---------------- SEARCH ----------------
        if state == SEARCH:
            stop_all()
            set_speed(SPEED_SEARCH)

            if detections:
                print("[STATE] Tag detected ? DRIVE")
                send("/F/on")
                state = DRIVE

        # ---------------- DRIVE ----------------
        elif state == DRIVE:
            if detections:
                tag = detections[0]
                area = cv2.contourArea(tag.corners.astype("float32"))
                last_seen_t = now
                last_area   = area
            else:
                # Decide whether grace applies based on last known distance
                if last_area < GRACE_AREA_MAX:
                    # FAR: allow grace period
                    if now - last_seen_t <= TAG_LOST_GRACE:
                        # keep moving with last steering
                        time.sleep(LOOP_DT)
                        continue
                    else:
                        stop_all()
                        state = SEARCH
                        continue
                else:
                    # NEAR: no grace, stop immediately
                    stop_all()
                    state = SEARCH
                    continue


            # ---- steering ----
            cx = tag.center[0]
            frame_cx = FRAME_W / 2
            err = cx - frame_cx

            send("/L/off")
            send("/R/off")

            # ---- distance ----
            area = cv2.contourArea(tag.corners.astype("float32"))

            if err < -CENTER_TOL:
                send("/R/on")
                
                time.sleep(.1)
                send("/R/off")
            elif err > CENTER_TOL:
                send("/L/on")
                time.sleep(.1)
                send("/L/off")



            if area < SIZE_SMALL:
                set_speed(SPEED_FAR)
            elif area < SIZE_LARGE:
                set_speed(SPEED_MID)
            else:
                set_speed(SPEED_CLOSE)

            if area >= TAG_AREA_TARGET:
                print(f"[STATE] Tag reached ({int(area)} px) ? PAUSE")
                stop_all()
                state = PAUSE1
                state_t0 = now

        # ---------------- PAUSE 1 ----------------
        elif state == PAUSE1:
            if now - state_t0 >= PAUSE_TIME:
                print("[STATE] Turning left")
                set_speed(SPEED_TURN)
                send("/L/on")
                state = TURN
                state_t0 = now

        # ---------------- TURN ----------------
        elif state == TURN:
            if now - state_t0 >= TURN_TIME:
                send("/L/off")
                print("[STATE] Turn complete ? PAUSE")
                state = PAUSE2
                state_t0 = now

        # ---------------- PAUSE 2 ----------------
        elif state == PAUSE2:
            if now - state_t0 >= PAUSE_TIME:
                print("[STATE] Restart loop ? SEARCH")
                state = SEARCH

        time.sleep(LOOP_DT)

# =================================================
finally:
    stop_all()
    cap.release()
    ser.close()
