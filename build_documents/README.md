# Build Documents 📐

Everything you need to build the robot and its controllers. Each folder has its own README, plus a bill of materials, wiring diagram, protocol details, and STL files:

- **[robot/](robot/)** — the robot itself: drivetrain, motor board, relay board
- **[single_analog_stick/](single_analog_stick/)** — one-handed controller (and spare ESP32 dev board)
- **[dual_analog_stick/](dual_analog_stick/)** — two-handed controller with lights control and a live HUD

Documents shared across the whole system live at this level:

- [System architecture overview](overview_system_architecture.pdf)
- [Shared ESP-NOW protocol](shared_protocol.pdf)

All the firmware is in [`firmware/`](../firmware/) at the repo root.
