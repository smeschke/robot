// serial_robot.ino ? full rewrite for U-turn combos (X/Y) with ramping
// Board: Arduino UNO/Nano-style PWM on 5,6,10,11 (adjust as needed)

// ----------------------- Pin mapping (edit if needed) -----------------------
#define L_PWM_FWD 5    // Left motor forward PWM
#define L_PWM_REV 6    // Left motor reverse PWM
#define R_PWM_FWD 10   // Right motor forward PWM
#define R_PWM_REV 11   // Right motor reverse PWM

// If your BTS7960 board has enable pins that must be HIGH, uncomment & set:
// #define L_EN_PIN 7
// #define R_EN_PIN 8

// ----------------------- Motion/ramp settings -------------------------------
static const uint16_t CONTROL_DT_MS = 20;   // ~50 Hz control tick
static const int      RAMP_STEP     = 8;    // per-tick change toward target (tune feel)
// Optional safety stop if no commands arrive (0 = disabled)
static const unsigned long DEADMAN_MS = 0;  // e.g., 1500 for 1.5s timeout
static const int RAMP_UP_STEP   = 4;  // accelerate / speed up
static const int RAMP_DOWN_STEP = 2;  // decelerate / slow down (smaller = softer stop)

// ----------------------- Globals -------------------------------------------
int baseSpeed = 50;     // default ~50% (0..255)
int tgtL = 0, tgtR = 0;  // targets  (-255..+255)
int curL = 0, curR = 0;  // outputs  (-255..+255)

char lastCmd = 'S';
unsigned long lastCmdAt = 0;

// ----------------------- Helpers -------------------------------------------
void driveMotorSigned(int pwmFwd, int pwmRev, int speed) {
  int v = speed;
  if (v >= 0) {
    analogWrite(pwmFwd, v > 255 ? 255 : v);
    analogWrite(pwmRev, 0);
  } else {
    v = -v;
    analogWrite(pwmFwd, 0);
    analogWrite(pwmRev, v > 255 ? 255 : v);
  }
}

void applyOutputs() {
  driveMotorSigned(L_PWM_FWD, L_PWM_REV, curL);
  driveMotorSigned(R_PWM_FWD, R_PWM_REV, curR);
}

void rampStep() {
  // Left wheel
  if (curL != tgtL) {
    int stepL = (abs(tgtL) < abs(curL)) ? RAMP_DOWN_STEP : RAMP_UP_STEP; // smaller when slowing
    if (curL < tgtL) curL = min(curL + stepL, tgtL);
    else             curL = max(curL - stepL, tgtL);
  }

  // Right wheel
  if (curR != tgtR) {
    int stepR = (abs(tgtR) < abs(curR)) ? RAMP_DOWN_STEP : RAMP_UP_STEP; // smaller when slowing
    if (curR < tgtR) curR = min(curR + stepR, tgtR);
    else             curR = max(curR - stepR, tgtR);
  }

  applyOutputs();
}

void setTargetsFromCommand(char c) {
  lastCmd = c;
  lastCmdAt = millis();

  switch (c) {
    case 'F': // Forward
      tgtL = +baseSpeed;
      tgtR = +baseSpeed;
      break;

    case 'L': // Spin-in-place left
      tgtL = -baseSpeed;
      tgtR = +baseSpeed;
      break;

    case 'R': // Spin-in-place right
      tgtL = +baseSpeed;
      tgtR = -baseSpeed;
      break;

    case 'S': // Stop
      tgtL = 0;
      tgtR = 0;
      break;

    case 'B':  // Backup (reverse straight)
  tgtL = -baseSpeed;
  tgtR = -baseSpeed;
  break;

    // --- NEW: U-turn combos from Python ---
    case 'X': // F+L ? U-turn left: right +V, left ? 0
      tgtL = 0;
      tgtR = +baseSpeed;
      break;

    case 'Y': // F+R ? U-turn right: left +V, right ? 0
      tgtL = +baseSpeed;
      tgtR = 0;
      break;

    // Speed digits handled below; default fallthrough does nothing
  }
}

void handleSpeedDigit(char d) {

// replace your mapTable in handleSpeedDigit()
static const uint8_t mapTable[10] = {
  0,   // '0'
  25,  // '1'
  35,  // '2'
  45,  // '3'  <-- use these
  89,  // '4'
  255,  // '5'
  90,  // '6'
  180, // '7'
  110, // '8'
  110  // '9'
};
  int idx = d - '0';
  baseSpeed = mapTable[idx];

  // Re-apply last command so targets reflect the new baseSpeed (still ramped)
  setTargetsFromCommand(lastCmd);
}

// ----------------------- Arduino lifecycle ----------------------------------
void setup() {
  Serial.begin(115200);

  pinMode(L_PWM_FWD, OUTPUT);
  pinMode(L_PWM_REV, OUTPUT);
  pinMode(R_PWM_FWD, OUTPUT);
  pinMode(R_PWM_REV, OUTPUT);

  // If using enable pins:
  // pinMode(L_EN_PIN, OUTPUT); digitalWrite(L_EN_PIN, HIGH);
  // pinMode(R_EN_PIN, OUTPUT); digitalWrite(R_EN_PIN, HIGH);

  // Start safely stopped
  analogWrite(L_PWM_FWD, 0);
  analogWrite(L_PWM_REV, 0);
  analogWrite(R_PWM_FWD, 0);
  analogWrite(R_PWM_REV, 0);

  lastCmdAt = millis();
}

void loop() {
  // Read all incoming bytes
  while (Serial.available() > 0) {
    char c = (char)Serial.read();

    if (c >= '0' && c <= '9') {
      handleSpeedDigit(c);
    } else {
      setTargetsFromCommand(c);
    }
  }

  // Optional deadman stop if no recent command
  if (DEADMAN_MS > 0 && (millis() - lastCmdAt) > DEADMAN_MS) {
    if (lastCmd != 'S') {
      lastCmd = 'S';
      tgtL = 0; tgtR = 0;
    }
  }

  // Smooth toward targets and push to hardware
  rampStep();

  delay(CONTROL_DT_MS);
}
