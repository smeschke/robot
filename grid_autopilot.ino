// ====== RAMPED MOTOR CONTROL TEST ======
// Board: Arduino UNO
// Works with /F, /B, /L, /R, /S, /G, /T
// Motors ramp smoothly up/down instead of instant on/off
// and uses the MPU6050 to perform a true 180� spin.

#include <Wire.h>
#include <MPU6050_light.h>
MPU6050 mpu(Wire);

// ---------------- Pin Mapping ----------------
#define L_PWM_FWD 10
#define L_PWM_REV 11
#define R_PWM_FWD 6
#define R_PWM_REV 5

// ---------------- Ramp Tuning ----------------
const int CONTROL_DT_MS = 20;    // ~50 Hz
const int RAMP_UP_STEP   = 4;
const int RAMP_DOWN_STEP = 3;

// ---------------- Globals ----------------
int baseSpeed = 50;
int tgtL = 0, tgtR = 0;
int curL = 0, curR = 0;
char lastCmd = 'S';
unsigned long lastCmdAt = 0;

// --- forward declarations ---
void setTargetsFromCommand(char c);
void handleSpeedDigit(char d);
void rampStep();
void spin180_IMU(bool clockwise = true);
void stopMotors();

// ================================================================
// SETUP
// ================================================================
void setup() {
  Serial.begin(115200);
  Wire.begin();
  mpu.begin();
  mpu.calcOffsets();          // keep robot still during calibration
  Serial.println("MPU ready.");

  pinMode(L_PWM_FWD, OUTPUT);
  pinMode(L_PWM_REV, OUTPUT);
  pinMode(R_PWM_FWD, OUTPUT);
  pinMode(R_PWM_REV, OUTPUT);
  stopMotors();
  Serial.println("Ramped drive ready.");
}

// ================================================================
// LOOP
// ================================================================
void loop() {
  // --- handle any new serial commands ---
  while (Serial.available() > 0) {
    char c = Serial.read();
    lastCmdAt = millis();

    // --- Speed buttons (1?5) ---
    if (c >= '1' && c <= '5') {
      handleSpeedDigit(c);
      continue;                 // skip normal handling
    }

    // --- IMU-based spin ---
    if (c == 'T') {             // from ?Spin 180�? button
      spin180_IMU(true);        // clockwise
      continue;                 // skip normal ramp control while spinning
    }

    // --- Future: autonomous straight run (/G) ---
    if (c == 'G') {
      // placeholder for goStraight_IMU() if you add later
      setTargetsFromCommand('F');
      continue;
    }

    // --- Manual drive commands ---
    setTargetsFromCommand(c);
  }

  // --- Continuous ramp control ---
  rampStep();

  // --- Control tick rate (~50 Hz) ---
  delay(CONTROL_DT_MS);
}

// ================================================================
// MOTOR HELPERS
// ================================================================
void driveMotorSigned(int pwmFwd, int pwmRev, int v) {
  if (v >= 0) {
    analogWrite(pwmFwd, v);
    analogWrite(pwmRev, 0);
  } else {
    v = -v;
    analogWrite(pwmFwd, 0);
    analogWrite(pwmRev, v);
  }
}

void applyOutputs() {
  driveMotorSigned(L_PWM_FWD, L_PWM_REV, curL);
  driveMotorSigned(R_PWM_FWD, R_PWM_REV, curR);
}

void rampStep() {
  if (curL != tgtL) {
    int stepL = (abs(tgtL) < abs(curL)) ? RAMP_DOWN_STEP : RAMP_UP_STEP;
    if (curL < tgtL) curL = min(curL + stepL, tgtL);
    else curL = max(curL - stepL, tgtL);
  }
  if (curR != tgtR) {
    int stepR = (abs(tgtR) < abs(curR)) ? RAMP_DOWN_STEP : RAMP_UP_STEP;
    if (curR < tgtR) curR = min(curR + stepR, tgtR);
    else curR = max(curR - stepR, tgtR);
  }
  applyOutputs();
}

// ================================================================
// COMMAND LOGIC
// ================================================================
void setTargetsFromCommand(char c) {
  lastCmd = c;
  switch (c) {
    case 'F': tgtL = baseSpeed;  tgtR = baseSpeed;  break;
    case 'B': tgtL = -baseSpeed; tgtR = -baseSpeed; break;
    case 'L': tgtL = -baseSpeed; tgtR = baseSpeed;  break;
    case 'R': tgtL = baseSpeed;  tgtR = -baseSpeed; break;
    case 'S': tgtL = 0;          tgtR = 0;          break;

    // --- Autonomous (G = straight, T = spin) ---
    case 'G': tgtL = baseSpeed;  tgtR = baseSpeed;  break;
    case 'T': tgtL = baseSpeed;  tgtR = -baseSpeed; break;

    default: break;
  }
}

// ================================================================
// SPEED CONTROL (1?5)
// ================================================================
void handleSpeedDigit(char d) {
  static const uint8_t speedTable[6] = {
    0,   // index 0 unused
    20,  // '1' = crawl
    40,  // '2' = slow
    80,  // '3' = medium
    140, // '4' = fast
    255  // '5' = max
  };

  int idx = d - '0';
  if (idx >= 1 && idx <= 5) {
    baseSpeed = speedTable[idx];
    Serial.print("Base speed set to ");
    Serial.println(baseSpeed);
    // Only reapply if currently moving
    if (lastCmd != 'S') setTargetsFromCommand(lastCmd);
  }
}

// ================================================================
// UTILITY
// ================================================================
void stopMotors() {
  analogWrite(L_PWM_FWD, 0);
  analogWrite(L_PWM_REV, 0);
  analogWrite(R_PWM_FWD, 0);
  analogWrite(R_PWM_REV, 0);
}

// ================================================================
// IMU-BASED 180� SPIN (adaptive version of the simple two-phase)
// - For low PWM: same as before (140� / 178�)
// - For high PWM: slow phase starts earlier
// ================================================================
void spin180_IMU(bool clockwise) {
  Serial.println("IMU Spin 180 adaptive start");

  // ---- Tunables ----
  const unsigned long TIMEOUT_MS = 12000;
  const int FAST_MIN = 60;
  const int SLOW_MIN = 35;
  const float SLOW_FRAC = 0.33f;     // slow phase speed fraction
  const float STOP_TOL = 2.0f;       // acceptable degrees past final stop

  // ---- Adaptive thresholds ----
  // Higher speed => earlier slowdown & slightly smaller final angle
  float COARSE_STOP = 140.0f;
  float FINAL_STOP  = 178.0f;

  if (baseSpeed >= 100) {            // very fast
    COARSE_STOP = 50.0f;
    FINAL_STOP  = 178.0f;
  } else if (baseSpeed >= 120) {     // medium-fast
    COARSE_STOP = 130.0f;
    FINAL_STOP  = 175.0f;
  }

  // ---- Compute actual PWM levels ----
  int fastPWM = max(baseSpeed, FAST_MIN);
  int slowPWM = max((int)(baseSpeed * SLOW_FRAC), SLOW_MIN);

  auto setSpinTargets = [&](int pwm){
    tgtL = clockwise ?  pwm : -pwm;
    tgtR = clockwise ? -pwm :  pwm;
  };

  // ---- Zero gyro heading ----
  mpu.update();
  float heading = 0.0f;
  unsigned long last = millis();
  unsigned long start = last;

  // ---------------- Phase A: fast spin ----------------
  setSpinTargets(fastPWM);
  while (fabs(heading) < COARSE_STOP && (millis() - start) < TIMEOUT_MS) {
    unsigned long now = millis();
    float dt = (now - last) / 1000.0f;
    last = now;

    mpu.update();
    float gz = mpu.getGyroZ();
    heading += (clockwise ? gz : -gz) * dt;

    rampStep();
    delay(CONTROL_DT_MS);
  }

  // ---------------- Phase B: slow spin ----------------
  setSpinTargets(slowPWM);
  while (fabs(heading) < FINAL_STOP && (millis() - start) < TIMEOUT_MS) {
    unsigned long now = millis();
    float dt = (now - last) / 1000.0f;
    last = now;

    mpu.update();
    float gz = mpu.getGyroZ();
    heading += (clockwise ? gz : -gz) * dt;

    rampStep();
    delay(CONTROL_DT_MS);
  }

  // ---------------- Stop & brake ----------------
  setTargetsFromCommand('S');
  for (int i = 0; i < 25; ++i) {
    rampStep();
    delay(CONTROL_DT_MS);
  }

  Serial.print("IMU spin done, heading=");
  Serial.println(heading, 1);

  // Small correction print if it overshot
  if (heading > (FINAL_STOP + STOP_TOL))
    Serial.println("Note: slight overshoot (momentum).");
}
