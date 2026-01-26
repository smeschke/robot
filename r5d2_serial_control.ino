// ======================================================
// R5D2 ESP32 – Always-Ramped Controller w/ Debounced E-STOP
// ======================================================

#include <Wire.h>
#include <MPU6050_light.h>

// ---------------- PINS ----------------
#define R_FWD 25
#define R_REV 26
#define L_FWD 27
#define L_REV 33

#define ESTOP_PIN 14   // 32 or 33 also excellent

#define PWM_MAX 255

// ---------------- IMU ----------------
MPU6050 imu(Wire);
float yaw_offset = 0.0;

// ---------------- STATE ----------------
enum Mode { IDLE, RUN, TURN, PWM, STOP };
Mode mode = IDLE;

uint32_t last_cmd_ms = 0;
const uint32_t WATCHDOG_MS = 5000;

// ---------------- CONTROL ----------------
float target_heading = 0.0;
int   run_speed = 0;

float run_kp = 2.0;
float turn_kp = 3.0;
float turn_deadband = 2.0;

// ---------------- MOTOR (ALWAYS RAMPED) ----------------
int cur_L = 0, cur_R = 0;
int tgt_L = 0, tgt_R = 0;

const int RAMP_STEP = 1;
const uint32_t RAMP_INTERVAL_MS = 5;
uint32_t last_ramp_ms = 0;

// ---------------- E-STOP (PROVEN DEBOUNCER) ----------------
const uint32_t ESTOP_DEBOUNCE_MS = 30;

bool estop_latched = false;

bool estop_state = false;        // debounced state (true = pressed)
bool estop_candidate = false;    // raw candidate
uint32_t estop_change_ms = 0;

// ---------------- UTILS ----------------
float wrap180(float a) {
  while (a >= 180) a -= 360;
  while (a < -180) a += 360;
  return a;
}

int clampi(int v) {
  return constrain(v, -PWM_MAX, PWM_MAX);
}

void setMotor(int pwm, int fwd, int rev) {
  pwm = clampi(pwm);
  if (pwm >= 0) {
    analogWrite(fwd, pwm);
    analogWrite(rev, 0);
  } else {
    analogWrite(fwd, 0);
    analogWrite(rev, -pwm);
  }
}

void applyMotors() {
  setMotor(cur_L, L_FWD, L_REV);
  setMotor(cur_R, R_FWD, R_REV);
}

int ramp(int cur, int tgt) {
  if (cur < tgt) cur += RAMP_STEP;
  else if (cur > tgt) cur -= RAMP_STEP;
  if (abs(cur - tgt) < RAMP_STEP) cur = tgt;
  return cur;
}

// ---------------- IMU ----------------
float yawDeg() {
  return wrap180(imu.getAngleZ() - yaw_offset);
}

// ---------------- COMMANDS ----------------
String rx;

void stop(bool timeout, bool estop=false) {
  mode = STOP;
  tgt_L = 0;
  tgt_R = 0;

  if (estop)        Serial.println("ESTOP HIT");
  else if (timeout) Serial.println("TIMEOUT STOP");
  else              Serial.println("ACK STOP");
}

void handleCommand(String s) {
  if (estop_latched) return;  // ignore commands during E-STOP

  s.trim();
  if (!s.length()) return;

  last_cmd_ms = millis();

  if (s == "IMU?") {
    Serial.print("IMU ");
    Serial.print(yawDeg(), 2);
    Serial.print(" 0.0 ");
    Serial.println(millis());
    return;
  }

  if (s == "STOP") {
    stop(false);
    return;
  }

  if (s.startsWith("RUN ")) {
    sscanf(s.c_str(), "RUN %d %f", &run_speed, &target_heading);
    run_speed = clampi(run_speed);
    mode = RUN;
    Serial.println("ACK RUN");
    return;
  }

  if (s.startsWith("TURN ")) {
    sscanf(s.c_str(), "TURN %f", &target_heading);
    mode = TURN;
    Serial.println("ACK TURN");
    return;
  }

  if (s.startsWith("PWM ")) {
    sscanf(s.c_str(), "PWM %d %d", &tgt_L, &tgt_R);
    tgt_L = clampi(tgt_L);
    tgt_R = clampi(tgt_R);
    mode = PWM;
    Serial.println("ACK PWM");
    return;
  }

  if (s.startsWith("SET ")) {
    char key[20];
    float val;
    sscanf(s.c_str(), "SET %s %f", key, &val);
    if      (!strcmp(key, "RUN_KP")) run_kp = val;
    else if (!strcmp(key, "TURN_KP")) turn_kp = val;
    else if (!strcmp(key, "TURN_DEADBAND")) turn_deadband = val;
    else { Serial.println("ERR SET"); return; }
    Serial.println("ACK SET");
    return;
  }

  Serial.println("ERR");
}

// ---------------- SETUP ----------------
void setup() {
  Serial.begin(115200);
  Wire.begin();

  pinMode(L_FWD, OUTPUT);
  pinMode(L_REV, OUTPUT);
  pinMode(R_FWD, OUTPUT);
  pinMode(R_REV, OUTPUT);

  pinMode(ESTOP_PIN, INPUT_PULLUP);

  analogWrite(L_FWD, 0);
  analogWrite(L_REV, 0);
  analogWrite(R_FWD, 0);
  analogWrite(R_REV, 0);

  imu.begin();
  imu.calcOffsets(true, true);
  yaw_offset = imu.getAngleZ();

  // --- initialize E-STOP debouncer from real pin ---
  bool raw = (digitalRead(ESTOP_PIN) == LOW);
  estop_state = raw;
  estop_candidate = raw;
  estop_change_ms = millis();
  estop_latched = raw;

  last_cmd_ms = millis();
  Serial.println("READY");
}

// ---------------- LOOP ----------------
void loop() {
  // ---- Serial ----
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n') {
      handleCommand(rx);
      rx = "";
    } else if (c != '\r') {
      rx += c;
    }
  }

  imu.update();

  // ---- E-STOP (debounced) ----
  bool raw = (digitalRead(ESTOP_PIN) == LOW);

  if (raw != estop_candidate) {
    estop_candidate = raw;
    estop_change_ms = millis();
  }

  if ((millis() - estop_change_ms) >= ESTOP_DEBOUNCE_MS) {
    if (estop_state != estop_candidate) {
      estop_state = estop_candidate;

      if (estop_state) {
        estop_latched = true;
        stop(false, true);   // ESTOP HIT
      } else {
        estop_latched = false;
        Serial.println("ESTOP RELEASED");
        mode = IDLE;
      }
    }
  }

  // ---- Watchdog ----
  if (!estop_latched &&
      mode != IDLE && mode != STOP &&
      millis() - last_cmd_ms > WATCHDOG_MS) {
    stop(true);
  }

  // ---- Target computation ----
  float err = wrap180(yawDeg() - target_heading);

  if (!estop_latched) {
    if (mode == RUN) {
      int trim = clampi((int)(run_kp * err));
      tgt_L = clampi(run_speed + trim);
      tgt_R = clampi(run_speed - trim);
    }

    else if (mode == TURN) {
      if (abs(err) <= turn_deadband) {
        stop(false);
        mode = IDLE;
        Serial.print("DONE TURN ");
        Serial.println(yawDeg(), 2);
      } else {
        int pwm = clampi((int)(turn_kp * err));
        if (abs(pwm) < 30) pwm = (pwm > 0 ? 30 : -30);
        tgt_L =  pwm;
        tgt_R = -pwm;
      }
    }

    else {
      tgt_L = 0;
      tgt_R = 0;
    }
  } else {
    tgt_L = 0;
    tgt_R = 0;
  }

  // ---- Always ramp ----
  uint32_t now = millis();
  if (now - last_ramp_ms >= RAMP_INTERVAL_MS) {
    last_ramp_ms = now;
    cur_L = ramp(cur_L, tgt_L);
    cur_R = ramp(cur_R, tgt_R);
  }

  applyMotors();

  if (mode == STOP && cur_L == 0 && cur_R == 0)
    mode = IDLE;
}
