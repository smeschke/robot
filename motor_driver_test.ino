// ======================================================
// R5D2 ? ESP32 Motor Ramp Test
// ======================================================
// Tests:
// 1) Left FWD
// 2) Left REV
// 3) Right FWD
// 4) Right REV
// 5) Both FWD
// 6) Both REV
//
// Speed: 60
// Run time: 2s
// Pause time: 2s
// All motion ramped
// ======================================================

// ---------------- Motor pins ----------------
#define R_RPWM 33
#define R_LPWM 27
#define L_RPWM 26
#define L_LPWM 25

// ---------------- Parameters ----------------
const int TEST_SPEED = 60;
const int RAMP_STEP  = 2;
const unsigned long RAMP_DT = 25;

const unsigned long RUN_TIME   = 2000;
const unsigned long PAUSE_TIME = 2000;

// ---------------- State ----------------
int curL = 0, curR = 0;
int tgtL = 0, tgtR = 0;

unsigned long lastRamp = 0;
unsigned long stateStart = 0;
int testStep = 0;
bool inPause = false;

// ---------------- Motor helper ----------------
void setLR(int l, int r) {
  l = constrain(l, -255, 255);
  r = constrain(r, -255, 255);

  if (l >= 0) {
    analogWrite(L_RPWM, l);
    analogWrite(L_LPWM, 0);
  } else {
    analogWrite(L_RPWM, 0);
    analogWrite(L_LPWM, -l);
  }

  if (r >= 0) {
    analogWrite(R_RPWM, r);
    analogWrite(R_LPWM, 0);
  } else {
    analogWrite(R_RPWM, 0);
    analogWrite(R_LPWM, -r);
  }
}

// ---------------- Test pattern ----------------
void setTestTargets(int step) {
  switch (step) {
    case 0: tgtL =  TEST_SPEED; tgtR =  0;           break; // Left FWD
    case 1: tgtL = -TEST_SPEED; tgtR =  0;           break; // Left REV
    case 2: tgtL =  0;           tgtR =  TEST_SPEED; break; // Right FWD
    case 3: tgtL =  0;           tgtR = -TEST_SPEED; break; // Right REV
    case 4: tgtL =  TEST_SPEED; tgtR =  TEST_SPEED; break; // Both FWD
    case 5: tgtL = -TEST_SPEED; tgtR = -TEST_SPEED; break; // Both REV
    default:
      tgtL = 0;
      tgtR = 0;
  }
}

// ---------------- Setup ----------------
void setup() {
  Serial.begin(115200);

  pinMode(L_RPWM, OUTPUT);
  pinMode(L_LPWM, OUTPUT);
  pinMode(R_RPWM, OUTPUT);
  pinMode(R_LPWM, OUTPUT);

  setLR(0, 0);

  stateStart = millis();
  setTestTargets(testStep);

  Serial.println("R5D2 Motor Ramp Test Started");
}

// ---------------- Loop ----------------
void loop() {
  unsigned long now = millis();

  // ---- State timing ----
  if (!inPause && now - stateStart >= RUN_TIME) {
    // Enter pause
    tgtL = 0;
    tgtR = 0;
    inPause = true;
    stateStart = now;
    Serial.println("Pause");
  }

  if (inPause && now - stateStart >= PAUSE_TIME) {
    // Advance to next test
    inPause = false;
    testStep++;
    if (testStep > 5) testStep = 0;
    setTestTargets(testStep);
    stateStart = now;

    Serial.print("Test step ");
    Serial.println(testStep);
  }

  // ---- Ramp motors ----
  if (now - lastRamp >= RAMP_DT) {
    lastRamp = now;

    curL += constrain(tgtL - curL, -RAMP_STEP, RAMP_STEP);
    curR += constrain(tgtR - curR, -RAMP_STEP, RAMP_STEP);

    setLR(curL, curR);
  }
}
