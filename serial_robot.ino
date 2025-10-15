// === BTS7960 dual-motor with ramping ===
// Left:  FWD=5, REV=6
// Right: FWD=10, REV=11
// Serial commands: 'F','B','L','R','S' and '0'..'9' (speed %)

const int L_FWD = 5;
const int L_REV = 6;
const int R_FWD = 9;
const int R_REV = 10;

// --- Ramp tuning ---
const uint8_t  RAMP_STEP   = 6;    // PWM counts per tick (bigger = snappier)
const uint16_t RAMP_DT_MS  = 12;   // tick period in ms (smaller = faster updates)

// --- Speed control ---
int maxPwm = 120;                  // start gentle; '0'..'9' will change this

// --- Ramp state ---
int targetL = 0, targetR = 0;      // desired values (-maxPwm..+maxPwm)
int actualL = 0, actualR = 0;      // what we’re currently outputting
unsigned long lastTick = 0;

void setup() {
  pinMode(L_FWD, OUTPUT); pinMode(L_REV, OUTPUT);
  pinMode(R_FWD, OUTPUT); pinMode(R_REV, OUTPUT);
  stopAll();
  Serial.begin(115200);
}

void loop() {
  // Handle incoming commands
  while (Serial.available()) {
    char c = (char)Serial.read();

    if (c >= '0' && c <= '9') {
      int pct = (c - '0') * 10;          // 0,10,...,90%
      maxPwm = (255L * pct) / 100;
      if (pct == 0) setTargets(0, 0);
      continue;
    }

    switch (c) {
      case 'F': setTargets(+maxPwm, +maxPwm); break;
      case 'B': setTargets(-maxPwm, -maxPwm); break;
      case 'L': setTargets(-maxPwm, +maxPwm); break;
      case 'R': setTargets(+maxPwm, -maxPwm); break;
      case 'S': default: setTargets(0, 0);     break;
    }
  }

  // Ramp toward targets at fixed rate
  unsigned long now = millis();
  if (now - lastTick >= RAMP_DT_MS) {
    lastTick = now;
    actualL = stepToward(actualL, targetL, RAMP_STEP);
    actualR = stepToward(actualR, targetR, RAMP_STEP);
    applyOutputs(actualL, actualR);
  }
}

// --- Helpers ---
int stepToward(int cur, int tgt, int step) {
  if (cur < tgt) { cur += step; if (cur > tgt) cur = tgt; }
  else if (cur > tgt) { cur -= step; if (cur < tgt) cur = tgt; }
  return cur;
}

void setTargets(int L, int R) {
  // clamp to +/-255 just in case
  if (L > 255) L = 255; if (L < -255) L = -255;
  if (R > 255) R = 255; if (R < -255) R = -255;
  targetL = L; targetR = R;
}

void applyOutputs(int leftVal, int rightVal) {
  driveMotor(L_FWD, L_REV, leftVal);
  driveMotor(R_FWD, R_REV, rightVal);
}

void driveMotor(int pinFwd, int pinRev, int val) {
  // val: -255..+255 (sign = direction)
  if (val >= 0) {
    analogWrite(pinRev, 0);
    analogWrite(pinFwd, val);
  } else {
    analogWrite(pinFwd, 0);
    analogWrite(pinRev, -val);
  }
}

void stopAll() {
  actualL = targetL = 0;
  actualR = targetR = 0;
  analogWrite(L_FWD, 0); analogWrite(L_REV, 0);
  analogWrite(R_FWD, 0); analogWrite(R_REV, 0);
}
