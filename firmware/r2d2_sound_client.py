#!/usr/bin/env python3
"""
r2d2_sound_client.py -- runs on YOUR laptop, not the Pi. Connects to
r2d2_sound_server.py and triggers R2D2-style beeps/boops on the Pi's
speaker by clicking buttons in a small window (same cv2 GUI approach as
llama_teleop_client.py).

Independent of llama_teleop_client.py -- separate socket, separate port,
safe to run at the same time (e.g. in another terminal tab).

  python3 r2d2_sound_client.py --host <pi-ip-or-hostname>
"""

import argparse
import socket
import time

import cv2
import numpy as np

DEFAULT_SOUND_PORT = 8092

BUTTONS = [
    ("RANDOM", "random", (90, 90, 90)),
    ("HAPPY", "happy", (0, 110, 0)),
    ("SAD", "sad", (110, 60, 0)),
    ("WARBLE", "warble", (110, 0, 110)),
    ("WHISTLE", "whistle", (0, 90, 110)),
]

WIN_W = 520
BTN_H = 90
PAD = 10
FLASH_S = 0.25


class SoundLink:
    def __init__(self, host, port):
        self.sock = socket.create_connection((host, port), timeout=5)
        self.sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self.connected = True

    def send(self, name):
        try:
            self.sock.sendall(f"BEEP {name}\n".encode())
        except OSError:
            self.connected = False

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", required=True, help="Pi hostname or IP")
    ap.add_argument("--port", type=int, default=DEFAULT_SOUND_PORT)
    args = ap.parse_args()

    print(f"connecting to {args.host}:{args.port}")
    link = SoundLink(args.host, args.port)
    print("connected.")

    win = "r2d2_sounds"
    cv2.namedWindow(win, cv2.WINDOW_AUTOSIZE)

    n = len(BUTTONS)
    btn_w = (WIN_W - PAD * (n + 1)) // n
    rects = []
    x = PAD
    for label, name, color in BUTTONS:
        rects.append((x, PAD, x + btn_w, PAD + BTN_H, label, name, color))
        x += btn_w + PAD

    state = {"pressed": None, "pressed_at": 0.0}

    def in_rect(px, py, rect):
        x0, y0, x1, y1 = rect[:4]
        return x0 <= px <= x1 and y0 <= py <= y1

    def on_mouse(event, x, y, flags, param):
        if event != cv2.EVENT_LBUTTONDOWN:
            return
        for rect in rects:
            if in_rect(x, y, rect):
                label, name = rect[4], rect[5]
                link.send(name)
                state["pressed"] = name
                state["pressed_at"] = time.time()
                print(label)
                return

    cv2.setMouseCallback(win, on_mouse)
    print("click a button to play a sound. q to quit.")

    height = BTN_H + PAD * 2
    try:
        while True:
            if not link.connected:
                print("connection lost")
                break

            frame = np.zeros((height, WIN_W, 3), dtype=np.uint8)
            for x0, y0, x1, y1, label, name, color in rects:
                pressed = (state["pressed"] == name
                           and time.time() - state["pressed_at"] < FLASH_S)
                c = tuple(min(255, v + 90) for v in color) if pressed else color
                cv2.rectangle(frame, (x0, y0), (x1, y1), c, -1)
                cv2.rectangle(frame, (x0, y0), (x1, y1), (200, 200, 200), 1)
                text_w = cv2.getTextSize(label, cv2.FONT_HERSHEY_SIMPLEX, 0.5, 1)[0][0]
                tx = x0 + (x1 - x0 - text_w) // 2
                ty = (y0 + y1) // 2 + 5
                cv2.putText(frame, label, (tx, ty),
                            cv2.FONT_HERSHEY_SIMPLEX, 0.5, (255, 255, 255), 1)

            cv2.imshow(win, frame)
            k = cv2.waitKey(30) & 0xFF
            if k == ord("q"):
                break
            if cv2.getWindowProperty(win, cv2.WND_PROP_VISIBLE) < 1:
                break
    except KeyboardInterrupt:
        pass
    finally:
        link.close()
        cv2.destroyAllWindows()


if __name__ == "__main__":
    main()
