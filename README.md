Robot Platform 🛠️

![robot with trailer](robot_with_trailer.jpg)

Modular ESP32-based robot platform for outdoor navigation, motor control, and sensor integration.

This is a human-scale, differential-drive utility robot designed to move comfortably through real-world, ADA-compliant spaces at walking speed.

I chose 10-inch pneumatic tires so the robot can handle sidewalks, ramps, and grass reliably without suspension. The motors, gearboxes, and battery come from ride-on toys—they're inexpensive, readily available, and deliver excellent torque at low speeds.

The robot is controlled by an ESP32. The ESP32 uses ESPNOW to communicate with a manual joystick, or an onboard computer. This allows the user to switch from 'auto' to 'manual' without touching any cords.

🔧 Technical Snapshot

The robot uses 550-type brushed DC motors with 100:1 reduction gearboxes driving 10-inch pneumatic wheels. It's powered by 12-volt, 7 amp-hour sealed lead-acid battery(s). Motor control is handled by BTS7960 H-bridge motor drivers.

🧩 Why This Platform Exists

The core idea behind R5D2 is the base platform. The name encodes the key specs: R5 means 5-inch wheel radius (10-inch diameter), and D2 means two driven wheels. So an R4D4 would have four driven wheels with 8-inch diameter tires.

It already does the hardest—and least glamorous—part of most human-scale robots: moving reliably through real, accessible environments while supporting basic sensing and control. Everything above that base is modular.

You could add heavier compute, a camera mast, a vacuum or lawn tool, a sensor package, a trailer, or even a robotic arm. The platform stays the same—the attachment defines the job.

What task or problem would a robot like this solve for you?
