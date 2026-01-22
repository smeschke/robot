# Raspberry Pi ↔ ESP32 Command Protocol

**Version:** 1.0  
**Status:** Stable  
**Transport:** Serial (ASCII, line-based)

---

## 1. Overview

This document defines the serial command protocol used between a Raspberry Pi (host) and an ESP32 (motor + IMU controller).

The protocol is designed to be:
- Minimal and deterministic
- Safe under communication loss
- Easy to debug by humans
- Suitable for real-time motor control firmware

---

## 2. System Roles

### Raspberry Pi (Host)
- Sends high-level intent only
- Periodically refreshes motion commands
- Sets control parameters
- Requests IMU snapshots
- Does **not** run control loops

### ESP32 (Controller)
- Owns motors, IMU, ramping, and PID control
- Executes motion commands
- Enforces safety timeouts
- Reports acknowledgments, IMU data, and completion events

---

## 3. Transport Rules

- ASCII text
- One command per line
- Lines terminated by `\n`
- Space-separated parameters
- Commands are case-sensitive (uppercase recommended)

---

## 4. Safety Watchdog

### Description
The ESP32 enforces a **command watchdog** to prevent runaway motion.

- **Timeout duration:** 5 seconds
- Applies only while motors are active
- Reset by receipt of any *motion command*

### Watchdog Reset Commands
- `RUN`
- `TURN`
- `PWM`

### Commands That Do NOT Reset the Watchdog
- `SET`
- `IMU?`

### Timeout Behavior
When the watchdog expires:
1. ESP32 ramps motors to zero
2. Active motion mode is cleared
3. ESP32 emits a timeout notification

---

## 5. Command Set (Pi → ESP32)

### 5.1 Motion Commands  
(Motion commands are mutually exclusive and reset the watchdog.)

---

### RUN — Run to Heading

Drive forward or backward while maintaining an absolute heading.

RUN <speed_pwm> <heading_deg>

**Parameters**
- `speed_pwm`: signed integer (−255 to +255)
- `heading_deg`: absolute heading in degrees

**Response**
ACK RUN

---

### TURN — Turn to Heading

Rotate in place until the target heading is reached.

TURN <heading_deg>

- Direction is chosen automatically by the ESP32
- Completion is determined by internal deadband logic

**Responses**
ACK TURN
DONE TURN <final_heading>

---

### PWM — Raw Motor Control

Direct motor control with no IMU or heading logic.

PWM <left_pwm> <right_pwm>

**Parameters**
- `left_pwm`, `right_pwm`: signed integers (−255 to +255)

**Response**
ACK PWM

---

### STOP — Ramped Stop (Always Ramped)

Immediately cancels any active motion and ramps motors to zero.

STOP

**Response**
ACK STOP

yaml
Copy code

> **Note:** STOP always ramps. Hard stops are not permitted.

---

## 6. Parameter Commands (Pi → ESP32)

Parameter commands configure control behavior but do **not** start or stop motion and do **not** reset the watchdog.

SET <param> <value>

### Common Parameters

| Parameter        | Description                          |
|------------------|--------------------------------------|
| `RUN_KP`         | Heading-hold proportional gain       |
| `TURN_KP`        | Turn controller proportional gain    |
| `TURN_DEADBAND`  | Turn completion deadband (degrees)   |
| `MAX_PWM`        | Maximum allowed motor PWM            |
| `RAMP_RATE`      | PWM ramp rate (PWM per second)       |

**Responses**
ACK SET
ERR SET <reason>

---

## 7. IMU Commands

### IMU Snapshot Request

Request a single IMU reading.

IMU?

markdown
Copy code

**Response**
IMU <yaw_deg> <yaw_rate_dps> <timestamp_ms>

---

## 8. Responses (ESP32 → Pi)

### Acknowledgments

Every valid command produces exactly one acknowledgment.

ACK <command>
ERR <command> <reason>

yaml
Copy code

---

### IMU Snapshot

IMU <yaw_deg> <yaw_rate_dps> <timestamp_ms>

---

### Motion Completion

DONE TURN <final_heading>

---

### Safety Event

Issued when the watchdog expires.

TIMEOUT STOP

---

## 9. State Rules (Normative)

- Only one motion mode may be active at a time
- Any motion command cancels the previous motion
- `STOP` always ramps to zero
- Parameter commands never affect motion state
- IMU requests never affect motion
- All timing, ramping, and completion logic lives on the ESP32

---

## 10. Example Session

SET TURN_KP 2.4
ACK SET

RUN 120 10
ACK RUN

RUN 120 10
ACK RUN

IMU?
IMU 9.8 0.02 512334

(no motion command refresh)

TIMEOUT STOP

---

## 11. Design Intent

This protocol is intentionally simple:

- No streaming data
- No host-side control loops
- Safe failure on communication loss
- Easy to version and extend

This document defines the complete v1.0 interface and may be used directly for firmware and host implementations.

---

## 12. ESP32 Motion State Diagram (Mermaid)

```mermaid
stateDiagram-v2
    [*] --> IDLE

    IDLE --> RUNNING : RUN
    IDLE --> TURNING : TURN
    IDLE --> PWM_RAW : PWM

    RUNNING --> STOPPING : STOP
    TURNING --> STOPPING : STOP
    PWM_RAW --> STOPPING : STOP

    RUNNING --> STOPPING : Watchdog Timeout
    TURNING --> STOPPING : Watchdog Timeout
    PWM_RAW --> STOPPING : Watchdog Timeout

    TURNING --> STOPPING : Turn Complete / DONE TURN

    STOPPING --> IDLE : Ramp Complete

    RUNNING --> RUNNING : RUN (refresh)
    PWM_RAW --> PWM_RAW : PWM (refresh)
    TURNING --> TURNING : TURN (refresh)
