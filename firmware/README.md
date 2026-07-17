# Firmware ⚡

Arduino sketches for every ESP32 in the system — two controllers, two boards on the robot. All of it is plain Arduino with no libraries beyond `esp_now.h` (plus a display library for the dual stick's screen). Everything talks ESP-NOW on channel 1, no encryption, packet types distinguished purely by length.

## Sketches

| Sketch | Flashes onto | Does |
|---|---|---|
| [`single_analog_stick_espnow.ino`](single_analog_stick_espnow.ino) | single stick controller | Streams drive packets every 5 ms; stick click cycles the speed cap 25/50/75/100% |
| [`dual_analog_stick.ino`](dual_analog_stick.ino) | dual stick controller | Left stick streams drive, right stick flicks light commands, screen renders the robot heartbeat as a HUD |
| [`skidsteer_espnow.ino`](skidsteer_espnow.ino) | robot — motor board | Arcade-mixed skid steer on four BTS7960 channels, with ramping and a 300 ms fail-safe stop |
| [`relay_receiver.ino`](relay_receiver.ino) | robot — relay board | Latched headlights + momentary turn signals on two relays; sends the heartbeat back |

## Setup

Standard ESP32 Arduino toolchain — install the ESP32 board package, pick your board, flash over USB. The controllers flash through the same USB-C port that powers them, no disassembly.

The only per-build configuration is MAC addresses: controllers send to hardcoded peers, so flash the robot boards first, grab each board's MAC from the serial monitor at boot, and paste them into the controller sketch (`robotMac` in the single stick; `motorMac` and `relayMac` in the dual stick). The robot boards need no controller config — they auto-register whichever controller they hear first.

Wiring diagrams, BOMs, and protocol documents are in [`build_documents/`](../build_documents/).
