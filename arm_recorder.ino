/*
 * RS550 + 100:1 Gearbox + 20" Arm — Position Hold + Data Recording
 * -------------------------------------------------------
 * Based on position_hold_serial_control.
 * Changes for recording:
 *   - telemEvery defaults to 1 (50 Hz — every PID tick)
 *   - Streaming auto-starts on boot
 *   - Telemetry includes a tick counter for precise timing
 *
 * Encoder: Taiss 600P/R on GPIO 18 (A) and 19 (B)
 *   - 2400 counts/rev at motor shaft
 *   - 240,000 counts/rev at output (100:1 gearbox)
 *
 * BTS7960:
 *   - RPWM → GPIO 25  (forward)
 *   - LPWM → GPIO 26  (reverse)
 *   - R_EN + L_EN → 3.3V (always enabled)
 *
 * Serial commands (115200 baud, newline terminated):
 *   HOME                — zero encoder, setpoint = 0
 *   GOTO:<n>            — move to absolute count
 *   NUDGE:<n>           — move relative to current setpoint (+ or -)
 *   POS                 — reply with current encoder count
 *   TELEM               — single snapshot: tick,pos,setpoint,error,pwm
 *   STREAM:ON           — start streaming telem every telemEvery ticks
 *   STREAM:OFF          — stop streaming
 *   STREAM:RATE:<n>     — set stream interval in ticks (1 tick = 20ms)
 *   LOWER:<rate>:<target> — ramp setpoint down at rate counts/tick to target
 *   LOWER:STOP          — stop lowering, hold current setpoint
 */

// ── Pins ──────────────────────────────────────────────────
#define PIN_A   18
#define PIN_B   19
#define RPWM    25
#define LPWM    26

// ── Encoder ───────────────────────────────────────────────
volatile long encoderCount = 0;
volatile int  lastEncoded  = 0;

void IRAM_ATTR encoderISR() {
  int a = digitalRead(PIN_A);
  int b = digitalRead(PIN_B);
  int encoded = (a << 1) | b;
  int sum     = (lastEncoded << 2) | encoded;
  lastEncoded = encoded;
  if      (sum == 0b1101 || sum == 0b0100 || sum == 0b0010 || sum == 0b1011) encoderCount++;
  else if (sum == 0b1110 || sum == 0b0111 || sum == 0b0001 || sum == 0b1000) encoderCount--;
}

// ── PID tuning ────────────────────────────────────────────
float Kp     = 1.0f;
float Ki     = 0.05f;
float Kd     = 0.3f;

const int   PWM_MAX        = 100;
const long  DEADBAND       = 4;
const long  POLL_MS        = 20;        // 50 Hz PID loop
const float INTEGRAL_CLAMP = 150.0f;
const float OUTPUT_SLEW    = 4.0f;

// ── State ─────────────────────────────────────────────────
long  setpoint   = 0;
float integral   = 0.0f;
long  lastError  = 0;
long  lastMs     = 0;
float lastOutput = 0.0f;
long  tickCount  = 0;   // counts every PID tick — used as telemetry timestamp

// ── Lowering ──────────────────────────────────────────────
long lowerRate   = 0;
long lowerTarget = -2000000000L;

// ── Telemetry ─────────────────────────────────────────────
bool telemStreaming = true;   // auto-start on boot
int  telemEvery    = 1;       // 1 tick = 20ms = 50 Hz
int  telemTick     = 0;

void sendTelem() {
  long count = encoderCount;
  long err   = setpoint - count;
  // format: tick,pos,setpoint,err,pwm
  Serial.print(tickCount);    Serial.print(',');
  Serial.print(count);        Serial.print(',');
  Serial.print(setpoint);     Serial.print(',');
  Serial.print(err);          Serial.print(',');
  Serial.println((int)lastOutput);
}

// ── Serial command parsing ─────────────────────────────────
String serialBuf = "";

void parseCommand(String cmd) {
  if (cmd == "HOME") {
    noInterrupts(); encoderCount = 0; interrupts();
    setpoint = 0; integral = 0; lastError = 0; lastOutput = 0;
    lowerRate = 0;
    Serial.println("OK HOME");

  } else if (cmd.startsWith("GOTO:")) {
    setpoint  = cmd.substring(5).toInt();
    lowerRate = 0;
    Serial.print("OK GOTO "); Serial.println(setpoint);

  } else if (cmd.startsWith("NUDGE:")) {
    setpoint += cmd.substring(6).toInt();
    lowerRate = 0;
    Serial.print("OK NUDGE "); Serial.println(setpoint);

  } else if (cmd == "POS") {
    Serial.println(encoderCount);

  } else if (cmd == "TELEM") {
    sendTelem();

  } else if (cmd == "STREAM:ON") {
    telemStreaming = true;
    telemTick = 0;
    Serial.println("OK STREAM ON");

  } else if (cmd == "STREAM:OFF") {
    telemStreaming = false;
    Serial.println("OK STREAM OFF");

  } else if (cmd.startsWith("STREAM:RATE:")) {
    telemEvery = (int)max(1L, cmd.substring(12).toInt());
    Serial.print("OK STREAM:RATE "); Serial.println(telemEvery);

  } else if (cmd == "LOWER:STOP") {
    lowerRate = 0;
    Serial.println("OK LOWER STOP");

  } else if (cmd.startsWith("LOWER:")) {
    int colon = cmd.indexOf(':', 6);
    if (colon < 0) {
      Serial.println("ERR LOWER format: LOWER:<rate>:<target>");
    } else {
      lowerRate   = cmd.substring(6, colon).toInt();
      lowerTarget = cmd.substring(colon + 1).toInt();
      Serial.print("OK LOWER rate="); Serial.print(lowerRate);
      Serial.print(" target=");       Serial.println(lowerTarget);
    }

  } else {
    Serial.print("ERR unknown: "); Serial.println(cmd);
  }
}

void handleSerial() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      serialBuf.trim();
      if (serialBuf.length() > 0) parseCommand(serialBuf);
      serialBuf = "";
    } else {
      serialBuf += c;
    }
  }
}

// ── Motor ─────────────────────────────────────────────────
void driveMotor(int dir, int pwm) {
  pwm = constrain(pwm, 0, 255);
  if (dir >= 0) {
    ledcWrite(RPWM, pwm);
    ledcWrite(LPWM, 0);
  } else {
    ledcWrite(RPWM, 0);
    ledcWrite(LPWM, pwm);
  }
}

void stopMotor() {
  ledcWrite(RPWM, 0);
  ledcWrite(LPWM, 0);
}

// ── Setup ─────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);

  pinMode(PIN_A, INPUT_PULLUP);
  pinMode(PIN_B, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_A), encoderISR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_B), encoderISR, CHANGE);

  ledcAttach(RPWM, 20000, 8);
  ledcAttach(LPWM, 20000, 8);

  stopMotor();
  delay(500);

  noInterrupts();
  encoderCount = 0;
  interrupts();
  setpoint  = 0;
  integral  = 0.0f;
  lastError = 0;
  lastMs    = millis();

  Serial.println("READY");
  // streaming starts immediately — Python recorder can begin capturing right away
}

// ── Loop ──────────────────────────────────────────────────
void loop() {
  handleSerial();

  long now = millis();
  if (now - lastMs < POLL_MS) return;
  float dt = (now - lastMs) / 1000.0f;
  lastMs = now;
  tickCount++;

  // ── Ramp setpoint if lowering ─────────────────────────────
  if (lowerRate != 0) {
    setpoint -= lowerRate;
    if (setpoint <= lowerTarget) {
      setpoint  = lowerTarget;
      lowerRate = 0;
      Serial.println("LOWER DONE");
    }
  }

  // ── Read encoder ──────────────────────────────────────────
  long count = encoderCount;
  long error = setpoint - count;

  // ── PID ───────────────────────────────────────────────────
  if (abs(error) <= DEADBAND) {
    stopMotor();
    lastOutput = 0.0f;
  } else {
    float P = Kp * (float)error;

    integral += Ki * (float)error * dt;
    integral  = constrain(integral, -INTEGRAL_CLAMP, INTEGRAL_CLAMP);

    float D = Kd * ((float)(error - lastError) / dt);

    float output = P + integral + D;
    output = constrain(output, -(float)PWM_MAX, (float)PWM_MAX);
    output = constrain(output, lastOutput - OUTPUT_SLEW, lastOutput + OUTPUT_SLEW);
    lastOutput = output;

    int cmdDir = (output >= 0.0f) ? 1 : -1;
    int cmdPwm = (int)abs(output);
    driveMotor(cmdDir, cmdPwm);
  }

  lastError = error;

  // ── Telemetry ─────────────────────────────────────────────
  if (telemStreaming) {
    if (++telemTick >= telemEvery) {
      telemTick = 0;
      sendTelem();
    }
  }
}
