#!/usr/bin/env python3
"""
Minimal serial command sender for the R5D2 ESP32 serial control sketch.

Designed to be imported by other scripts as a small API, or run standalone.
"""
from __future__ import annotations

import argparse
import time
from dataclasses import dataclass

import serial


@dataclass
class SerialConfig:
    port: str
    baud: int = 115200
    timeout: float = 0.5


class R5D2SerialSender:
    def __init__(self, config: SerialConfig) -> None:
        self.config = config
        self._ser: serial.Serial | None = None
        self._msg_id = 1

    def open(self) -> None:
        if self._ser and self._ser.is_open:
            return
        self._ser = serial.Serial(
            self.config.port,
            self.config.baud,
            timeout=self.config.timeout,
            write_timeout=self.config.timeout,
        )

    def close(self) -> None:
        if self._ser and self._ser.is_open:
            self._ser.close()

    def __enter__(self) -> "R5D2SerialSender":
        self.open()
        return self

    def __exit__(self, exc_type, exc, tb) -> None:
        self.close()

    def send(self, payload: str) -> str:
        if not self._ser or not self._ser.is_open:
            raise RuntimeError("Serial port not open. Call open() first.")
        msg_id = self._msg_id
        self._msg_id += 1
        line = f"CMD|{msg_id}|{payload}|\n"
        self._ser.write(line.encode("utf-8"))
        return line

    def send_drive(self, left: float, right: float) -> str:
        return self.send(f"DRV,{left:.3f},{right:.3f}")

    def send_heading(self, heading_deg: float, speed: float | None = None) -> str:
        if speed is None:
            return self.send(f"HDG,{heading_deg:.2f}")
        return self.send(f"HDG,{heading_deg:.2f},{speed:.3f}")

    def send_stop(self) -> str:
        return self.send("STOP")

    def request_imu(self) -> str:
        return self.send("IMU?")

    def read_line(self) -> str | None:
        if not self._ser or not self._ser.is_open:
            raise RuntimeError("Serial port not open. Call open() first.")
        line = self._ser.readline()
        if not line:
            return None
        return line.decode("utf-8", errors="replace").strip()


def _parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Send commands to R5D2 over serial.")
    parser.add_argument("--port", required=True, help="Serial port (e.g. /dev/ttyUSB0)")
    parser.add_argument("--baud", type=int, default=115200, help="Serial baud rate")

    sub = parser.add_subparsers(dest="cmd", required=True)
    drive = sub.add_parser("drive", help="Set left/right drive values.")
    drive.add_argument("left", type=float, help="Left value (normalized or PWM).")
    drive.add_argument("right", type=float, help="Right value (normalized or PWM).")

    heading = sub.add_parser("heading", help="Set heading lock target.")
    heading.add_argument("heading_deg", type=float, help="Heading in degrees.")
    heading.add_argument("--speed", type=float, default=None, help="Optional speed value.")

    sub.add_parser("stop", help="Stop motors.")
    sub.add_parser("imu", help="Request one IMU snapshot.")

    return parser.parse_args()


def main() -> None:
    args = _parse_args()
    config = SerialConfig(port=args.port, baud=args.baud)

    with R5D2SerialSender(config) as sender:
        if args.cmd == "drive":
            sender.send_drive(args.left, args.right)
        elif args.cmd == "heading":
            sender.send_heading(args.heading_deg, args.speed)
        elif args.cmd == "stop":
            sender.send_stop()
        elif args.cmd == "imu":
            sender.request_imu()

        time.sleep(0.1)
        response = sender.read_line()
        if response:
            print(response)


if __name__ == "__main__":
    main()
