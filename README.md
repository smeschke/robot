R5D2 Robot Platform 🛠️

Modular ESP32-based robot platform for outdoor navigation, motor control, and sensor integration.

🚀 What is R5D2?

R5D2 is a human-scale, differential-drive utility robot designed to move comfortably through real-world, ADA-compliant spaces at walking speed.

It uses two large powered wheels and a rear caster—similar in spirit to a robotic vacuum, but scaled up for sidewalks, ramps, and outdoor terrain. The platform is intentionally simple, stable, and predictable to drive.

I chose 10-inch pneumatic tires so the robot can handle sidewalks, ramps, and grass reliably without suspension. The motors, gearboxes, and battery come from ride-on toys—they’re inexpensive, readily available, and deliver excellent torque at low speeds.

The robot is controlled by an ESP32. The ESP32 can run a built-in web server, allowing manual control from any phone or computer through a simple browser interface—no app or extra hardware required. The ESP32 can also be connected to a Raspberry Pi and recieve commands serially over usb.

🔧 Technical Snapshot

The robot uses two 550-type brushed DC motors with 100:1 reduction gearboxes driving 10-inch pneumatic wheels. It’s powered by a 12-volt, 7 amp-hour sealed lead-acid battery. Motor control is handled by BTS7960 H-bridge motor drivers.

A single main power switch sits between the battery and the system. This switch powers both the motor drivers and a DC-DC buck converter, which supplies 5 volts to the ESP32. When the switch is off, everything is off—motors and control together.

The ESP32 runs a web server for manual control. For sensing, the robot uses a GY-521 IMU. It also includes 12-volt LED landscaping lights, switched through a relay.

🧩 Why This Platform Exists

The core idea behind R5D2 is the base platform.

It already does the hardest—and least glamorous—part of most human-scale robots:
moving reliably through real, accessible environments while supporting basic sensing and control.

Everything above that base is modular.

You could add heavier compute, a camera mast, a vacuum or lawn tool, a sensor package, or even a robotic arm. The platform stays the same—the attachment defines the job.

What task or problem would a robot like this solve for you?
