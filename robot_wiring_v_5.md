### 🧠 Raspberry Pi

- Connected to **Arduino Uno** with **USB-A to USB-B Cable**
- Connected to **GPS Module** with **Jumper Wires**
  - GPIO 15 (TXD) → TX
  - GPIO 14 (RXD) → RX
  - GPIO 2 (SDA1) → SDA
  - GPIO 3 (SCL1) → SCL
  - Raspberry Pi 3.3 V → VCC
  - Raspberry Pi Ground → GND
- Connected to **Viking 20,000 mAh Battery Pack** via **USB-A to USB-C Cable (5 V 2 A)**
- Connected to **7-Inch Screen** with **HDMI Cable**
- Connected to devices via **Mobile Hotspot**

---

### 🖥️ 7-Inch Screen

- Connected to **Raspberry Pi** with **HDMI Cable**
- Connected to **5,000 mAh Battery Pack** with **USB-C to USB-C Cable**

---

### ⚖️ Arduino Uno

- Connected to **Raspberry Pi** with **USB-B to USB-A Cable**

- Connected to **Left BTS7960 Motor Driver with** Jumper Wires

  - D5 → RPWM
  - D6 → LPWM
  - Arduino 5 V → EN1
  - Arduino 5 V → EN2
  - Arduino 5 V → VCC
  - Arduino GND → GND

- Connected to **Right BTS7960 Motor Driver**

  - D10 → RPWM
  - D11 → LPWM
  - Arduino 5 V → EN1
  - Arduino 5 V → EN2
  - Arduino 5 V → VCC
  - Arduino GND → GND

- Connected to **GY-521 Accelerometer (MPU6050)**

  - A4 → RX
  - A5 → TX
  - Arduino 5 V → VCC
  - Arduino Ground → GND

- Connected to **Front-Center Sensor** with **Jumper Wires**

  - D7 → TRIG
  - D8 → ECHO
  - VCC → Arduino 5 V
  - GND → Arduino Ground

- Connected to **Front-Left Sensor** with **Jumper Wires**

  - D4 → TRIG
  - D3 → ECHO
  - VCC → Arduino 5 V
  - GND → Arduino Ground

- Connected to **Front-Right Sensor** with **Jumper Wires**

  - D12 → TRIG
  - D13 → ECHO
  - VCC → Arduino 5 V
  - GND → Arduino Ground

---

### ⚙️ Left  Motor Driver

- Connected to **Arduino Uno** with **Jumper Wires**
  - RPWM → D5
  - LPWM → D6
  - EN1 → Arduino 5 V
  - EN2 → Arduino 5 V
  - VCC → Arduino 5 V
  - GND → Arduino GND
- Connected to Motor with **14 AWG Wire**
  - Motor + → M+
  - Motor - → M-
- Connected to Switch with **14 AWG Wire**
  - B+ → Switch Terminal A
- Connected to Battery with **14 AWG Wire**
  - B- → Battery Negative Terminal

---

### ⚙️ Right  Motor Driver

- Connected to **Arduino Uno** with **Jumper Wires**
  - RPWM → D10
  - LPWM → D11
  - EN1 → 5 V Rail
  - EN2 → 5 V Rail
  - VCC → Arduino 5 V
  - GND → Arduino GND
- Connected to Motor with **14 AWG Wire**
  - Motor + → M+
  - Motor - → M-
- Connected to Switch with **14 AWG Wire**
  - B+ → Switch Terminal A
- Connected to Battery with **14 AWG Wire**
  - B- → Battery Negative Terminal

---

### 🔘 Main Power Switch

- Connected to **Left Motor Driver** with **14 AWG Wire**
  - Terminal A → B+
- Connected to **Right Motor Driver** with **14 AWG Wire**
  - Terminal A → B+
- Connected to **12 V SLA Battery** with **14 AWG Wire**
  - Terminal B → Battery Positive Terminal

---

### 🔋 12 V 7 Ah SLA Battery

- Connected to **Main Power Switch** with **14 AWG Wire**
  - Positive Terminal → Switch Terminal B
- Connected to **Left Motor Driver** with **14 AWG Wire**
  - Negative Battery Terminal → B-
- Connected to **Right Motor Driver** with **14 AWG Wire**
  - Negative Battery Terminal → B-

---

### 🔌 Viking 20,000 mAh Battery Pack

- Connected to **Raspberry Pi** with **USB-A to USB-C Cable (5 V 2 A)**

---

### 🔋 USB 5,000 mAh Battery Pack

- Connected to **7-Inch Screen** with **USB-C to USB-C Cable**

---

### ⚡ Left Brushed DC Motor

- Connected to **Left  Motor Driver** with **14 AWG Wire**
  - M+ → Motor Green
  - M- → Motor Yellow

---

### ⚡ Right Brushed DC Motor

- Connected to **Right  Motor Driver** with **14 AWG Wire**
  - M+ → Motor Green
  - M- → Motor Yellow

---

### 📡 Front-Center Ultrasonic Sensor (JSN-SR04T)

- Connected to **Arduino Uno** with **Jumper Wires**
  - TRIG → D7
  - ECHO → D8
  - 5 V → Arduino 5 V
  - GND → Arduino Ground

---

### 📡 Front-Left Ultrasonic Sensor (JSN-SR04T)

- Connected to **Arduino Uno** with **Jumper Wires**
  - TRIG → D4
  - ECHO → D3
  - 5 V → Arduino 5 V
  - GND → Arduino Ground

---

### 📡 Front-Right Ultrasonic Sensor (JSN-SR04T)

- Connected to **Arduino Uno** with **Jumper Wires**
  - TRIG → D12
  - ECHO → D13
  - 5 V → Arduino 5 V
  - GND → Arduino Ground

---

### 🦯 GY-521 Accelerometer (MPU6050)

- Connected to **Arduino Uno** with **Jumper Wires**
  - RX → A4
  - TX → A5
  - VCC → Arduino 5 V
  - GND → Arduino Ground

---

### 🛰️ GPS Module M10-25Q

- Connected to **Raspberry Pi** with **Jumper Wires**
  - TX → GPIO 15 (TXD)
  - RX → GPIO 14 (RXD)
  - SDA → GPIO 2 (SDA1)
  - SCL → GPIO 3 (SCL1)
  - VCC → Raspberry Pi 3.3V
  - GND → Raspberry Pi Ground

