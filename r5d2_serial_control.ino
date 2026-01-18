// ====== R5D2: ESP32 Serial Drive + IMU Heading Lock ======
#include <Wire.h>
#include <MPU6050_light.h>

// ================= IMU =================
MPU6050 mpu(Wire);
volatile float imuYaw = 0;
unsigned long lastImuMs = 0;

// ================= Heading lock =================
bool  headingLockActive = false;
float targetHeading = 0;

// ===== Heading lock params =====
int   GOLD_SPEED = 80;
float HEADING_KP = 3.0;
float HEADING_DEADBAND = 1.0;

// ================= Motor pins =================
#define R_RPWM 33
#define R_LPWM 27
#define L_RPWM 26
#define L_LPWM 25

// ================= Motion state =================
int curL = 0, curR = 0;
int tgtL = 0, tgtR = 0;

// ================= Parameters =================
int RAMP_STEP  = 5;
unsigned long RAMP_DT = 25;
unsigned long lastRamp = 0;

// ================= Failsafes =================
unsigned long lastCmdMs = 0;
const unsigned long CMD_TIMEOUT_MS = 40000;
const unsigned long IMU_TIMEOUT_MS = 50;

// ================= Serial command parser =================
static const size_t LINE_BUF_SIZE = 128;
char lineBuf[LINE_BUF_SIZE];
size_t lineLen = 0;

void sendImuSnapshot(const char* idStr) {
  Serial.printf("TEL|%s|IMU,%.2f|\n", idStr, (double)imuYaw);
}

float parseDriveValue(const char* valueStr) {
  float value = atof(valueStr);
  if (abs(value) <= 1.5f) {
    value *= 255.0f;
  }
  return value;
}

void setLR(int l, int r) {
  l = constrain(l, -255, 255);
  r = constrain(r, -255, 255);

  if (l >= 0) { analogWrite(L_RPWM, l); analogWrite(L_LPWM, 0); }
  else        { analogWrite(L_RPWM, 0); analogWrite(L_LPWM, -l); }

  if (r >= 0) { analogWrite(R_RPWM, r); analogWrite(R_LPWM, 0); }
  else        { analogWrite(R_RPWM, 0); analogWrite(R_LPWM, -r); }
}

void handleSerialCommand(const char* type, const char* idStr, const char* payload) {
  if (strcmp(type, "CMD") != 0) {
    return;
  }

  char payloadCopy[64];
  strncpy(payloadCopy, payload, sizeof(payloadCopy) - 1);
  payloadCopy[sizeof(payloadCopy) - 1] = '\0';

  char* cmd = strtok(payloadCopy, ",");
  if (!cmd) {
    return;
  }

  lastCmdMs = millis();

  if (strcmp(cmd, "DRV") == 0) {
    char* leftStr = strtok(NULL, ",");
    char* rightStr = strtok(NULL, ",");
    if (!leftStr || !rightStr) {
      return;
    }

    float left = parseDriveValue(leftStr);
    float right = parseDriveValue(rightStr);

    headingLockActive = false;
    tgtL = constrain((int)left, -255, 255);
    tgtR = constrain((int)right, -255, 255);
  } else if (strcmp(cmd, "HDG") == 0) {
    char* headingStr = strtok(NULL, ",");
    char* speedStr = strtok(NULL, ",");
    if (!headingStr) {
      return;
    }

    targetHeading = atof(headingStr);
    headingLockActive = true;

    if (speedStr) {
      float speedValue = parseDriveValue(speedStr);
      GOLD_SPEED = constrain((int)speedValue, 0, 255);
    }
  } else if (strcmp(cmd, "STOP") == 0) {
    headingLockActive = false;
    tgtL = 0;
    tgtR = 0;
  } else if (strcmp(cmd, "IMU?") == 0) {
    sendImuSnapshot(idStr);
  }

  Serial.printf("ACK|%s|OK|\n", idStr);
}

void parseSerialLine(char* line) {
  char* type = strtok(line, "|");
  char* idStr = strtok(NULL, "|");
  char* payload = strtok(NULL, "|");

  if (!type || !idStr || !payload) {
    return;
  }

  handleSerialCommand(type, idStr, payload);
}

void readSerialCommands() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n') {
      lineBuf[lineLen] = '\0';
      parseSerialLine(lineBuf);
      lineLen = 0;
    } else if (lineLen < LINE_BUF_SIZE - 1) {
      lineBuf[lineLen++] = c;
    } else {
      lineLen = 0;
    }
  }
}

// ================= Heading lock =================
void applyHeadingLock() {
  float err = targetHeading - imuYaw;

  if (abs(err) < HEADING_DEADBAND) return;

  int corr = (int)(err * HEADING_KP);
  tgtL -= corr;
  tgtR += corr;
}

void setup() {
  Serial.begin(115200);

  pinMode(L_RPWM,OUTPUT); pinMode(L_LPWM,OUTPUT);
  pinMode(R_RPWM,OUTPUT); pinMode(R_LPWM,OUTPUT);

  Wire.begin();
  Wire.setClock(100000);
  Wire.setTimeOut(5);

  mpu.begin();
  delay(500);
  mpu.calcOffsets();
}

void loop() {
  readSerialCommands();

  // ---- IMU update (local only) ----
  mpu.update();
  imuYaw = mpu.getAngleZ();
  lastImuMs = millis();

  // ---- IMU health ----
  if (millis() - lastImuMs > IMU_TIMEOUT_MS) {
    headingLockActive = false;
  }

  // ---- Command timeout ----
  if (millis() - lastCmdMs > CMD_TIMEOUT_MS) {
    headingLockActive = false;
    tgtL = 0;
    tgtR = 0;
  }

  // ---- Heading lock ----
  if (headingLockActive) {
    tgtL = GOLD_SPEED;
    tgtR = GOLD_SPEED;
    applyHeadingLock();
  }

  // ---- Ramp ----
  unsigned long now = millis();
  if (now - lastRamp >= RAMP_DT) {
    lastRamp = now;
    curL += constrain(tgtL - curL, -RAMP_STEP, RAMP_STEP);
    curR += constrain(tgtR - curR, -RAMP_STEP, RAMP_STEP);
    setLR(curL, curR);
  }
}
