🤖 R5D2 Robot Platform

A modular Raspberry Pi + Arduino robot built for outdoor navigation, motor control, and data logging.
This repo includes firmware, Python control servers, and detailed wiring for the full hardware stack.

🧩 Project Overview

R5D2 (RoboVac × 5 scale) is a 2-wheel drive robot platform featuring:

Raspberry Pi (4 or 5) brain

Arduino Uno motor controller

Dual BTS7960 H-bridge drivers

Ultrasonic sensors (JSN-SR04T)

GPS + Compass (M10-25Q + QMC5883L)

GY-521 / MPU6050 IMU

Dual 12 V DC brushed motors

Onboard 7-inch HDMI touch display

Separate battery packs for compute and drive systems

🧠 Repository Structure
File	Purpose
serial_robot.py	Lightweight HTTP server that translates web button presses into single-character serial commands for the Arduino. Perfect for manual drive tests.
tank.py	Full-featured version with session logging, metrics pages, command/event tracking, and HTTP POST ingestion for sensors. Generates runlogs/YYYY-MM-DD/SESSION_ID/….
gps_compass.py	Continuously reads GPS (NMEA via /dev/serial0) and compass heading (QMC5883L via I²C), printing location and heading to console.
tank.ino	Arduino firmware for driving two DC motors through BTS7960 H-bridges. Accepts serial commands (F,B,L,R,S,0–9, etc.).
serial_robot.ino	Minimal Arduino firmware to pair with serial_robot.py for early testing.
imu_test.ino	Stand-alone sketch to verify MPU6050 readings.
jsn_single_sensor_test.ino	Ultrasonic sensor range test using JSN-SR04T.
gps_compass.py	Python test for GPS + magnetometer combo.
robot_wiring_v_5.md	Detailed wiring diagram listing all component interconnections (Pi, Arduino, drivers, sensors, batteries, screen, etc.).
Components.csv	Inventory of parts used.
⚙️ Hardware Summary

Compute: Raspberry Pi 4 / 5

Motor Controller: Arduino Uno + 2× BTS7960 (43 A)

Drive Motors: Dual 12 V brushed DC

Power:

12 V 7 Ah SLA battery (for motors)

20 000 mAh USB battery (for Pi)

5 000 mAh USB battery (for screen)

Sensors: 3× JSN-SR04T ultrasonics, GY-521 IMU, QMC5883L compass, GPS M10-25Q

Display: 7-inch HDMI touchscreen

Chassis: Custom wood/ply base with pneumatic drive wheels and caster

Full wiring: see robot_wiring_v_5.md
.
