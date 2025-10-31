# 🤖 R5D2 Robot Platform

A modular **Raspberry Pi + Arduino** robot built for outdoor navigation, motor control, and data logging.  
This repo includes firmware, Python control servers, and detailed wiring for the full hardware stack.

---

## 🧩 Project Overview

**R5D2** (RoboVac × 5 scale) is a 2-wheel-drive robot platform featuring:

- 🧠 Raspberry Pi (4 or 5) brain  
- 🎮 Arduino Uno motor controller  
- ⚙️ Dual BTS7960 H-bridge motor drivers  
- 📡 Ultrasonic sensors (JSN-SR04T)  
- 🛰️ GPS + Compass (M10-25Q + QMC5883L)  
- 🦯 GY-521 / MPU6050 IMU  
- 🔋 Dual 12 V brushed DC motors  
- 🖥️ Onboard 7-inch HDMI touch display  
- 🔌 Separate battery packs for compute and drive systems  

---

## ⚙️ Hardware Summary

- **Compute:** Raspberry Pi 4 / 5  
- **Motor Controller:** Arduino Uno + 2× BTS7960 (43 A)  
- **Drive Motors:** Dual 12 V brushed DC  
- **Power:**  
  - 🔋 12 V 7 Ah SLA battery (for motors)  
  - 🔌 20 000 mAh USB battery (for Pi)  
  - 🔋 5 000 mAh USB battery (for screen)  
- **Sensors:** 3× JSN-SR04T ultrasonics, GY-521 IMU, QMC5883L compass, GPS M10-25Q  
- **Display:** 7-inch HDMI touchscreen  
- **Chassis:** Custom wood/ply base with pneumatic drive wheels and caster  
