Robot Platform 🛠️

Modular ESP32-based robot platform for outdoor navigation, motor control, and sensor integration.

This is a human-scale, differential-drive utility robot designed to move comfortably through real-world, ADA-compliant spaces at walking speed.

I chose 10-inch pneumatic tires so the robot can handle sidewalks, ramps, and grass reliably without suspension. The motors, gearboxes, and battery come from ride-on toys—they're inexpensive, readily available, and deliver excellent torque at low speeds.

The robot is controlled by an ESP32. The ESP32 uses ESPNOW to communicate with a manual joystick, or a computer. This allows the user to switch from 'auto' to 'manual' without touching any cords.

🔧 Technical Snapshot

The robot uses 550-type brushed DC motors with 100:1 reduction gearboxes driving 10-inch pneumatic wheels. It's powered by 12-volt, 7 amp-hour sealed lead-acid battery(s). Motor control is handled by BTS7960 H-bridge motor drivers.
