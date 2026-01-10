// ======================================================
// R5D2 ESP32 Serial Drive (Ramped, State-Based)
// ------------------------------------------------------
// - Differential drive with signed motor commands
// - Serial command protocol: /F/on, /L/off, /speed=120
// - Motor ramping lives on the ESP32 for gearbox safety
// - Host (Pi / PC / phone) expresses INTENT only
// - ESP32 enforces physical limits (accel / decel)
// ======================================================

// ------------------ Motor Pins ------------------
// Each motor uses two PWM pins: forward and reverse
#define L_RPWM 26   // Left motor forward
#define L_LPWM 25   // Left motor reverse
#define R_RPWM 33   // Right motor forward
#define R_LPWM 27   // Right motor reverse

// ------------------ Motion State ------------------
// Current motor outputs (what is actually being applied)
int curL = 0;
int curR = 0;

// Target motor outputs (where we want to go)
int tgtL = 0;
int tgtR = 0;

// User-adjustable speed limit (via /speed= command)
int speedLevel = 80;

// ------------------ Ramping ------------------
// Max change per update (motor safety!)
int RAMP_STEP = 5;

// Ramp update interval (ms)
const unsigned long RAMP_DT = 25;
unsigned long lastRamp = 0;

// ------------------ Drive Flags ------------------
// These represent "button states" sent over serial
// They persist until explicitly turned off
bool F = false;   // Forward
bool B = false;   // Backward
bool L = false;   // Turn left
bool R = false;   // Turn right

// ======================================================
// Low-level motor output
// Converts signed speed (-255..255) into PWM signals
// ======================================================
void setLR(int l, int r) {
  l = constrain(l, -255, 255);
  r = constrain(r, -255, 255);

  // Left motor
  analogWrite(L_RPWM, l > 0 ? l : 0);
  analogWrite(L_LPWM, l < 0 ? -l : 0);

  // Right motor
  analogWrite(R_RPWM, r > 0 ? r : 0);
  analogWrite(R_LPWM, r < 0 ? -r : 0);
}

// ======================================================
// Compute target motor speeds from drive flags
// Implements standard differential-drive mixing
// ======================================================
void computeTargets() {
  int throttle = 0;
  int turn     = 0;

  // Forward / backward
  if (F && !B) throttle =  speedLevel;
  if (B && !F) throttle = -speedLevel;

  // Turning (reduced strength for smoother arcs)
  if (L && !R) turn =  speedLevel / 2;
  if (R && !L) turn = -speedLevel / 2;

  // Differential mix
  tgtL = throttle - turn;
  tgtR = throttle + turn;
}

// ======================================================
// Serial command handler
// Commands are state changes, NOT motor commands
// ======================================================
void handleCmd(String cmd) {
  if      (cmd.startsWith("/F/")) F = cmd.endsWith("on");
  else if (cmd.startsWith("/B/")) B = cmd.endsWith("on");
  else if (cmd.startsWith("/L/")) L = cmd.endsWith("on");
  else if (cmd.startsWith("/R/")) R = cmd.endsWith("on");
  else if (cmd.startsWith("/speed")) {
    speedLevel = cmd.substring(cmd.indexOf("=") + 1).toInt();
  }

  // Recompute targets whenever state changes
  computeTargets();
}

// ======================================================
// Setup
// ======================================================
void setup() {
  Serial.begin(115200);

  pinMode(L_RPWM, OUTPUT);
  pinMode(L_LPWM, OUTPUT);
  pinMode(R_RPWM, OUTPUT);
  pinMode(R_LPWM, OUTPUT);

  // Ensure motors are stopped on boot
  setLR(0, 0);
}

// ======================================================
// Main loop
// ======================================================
void loop() {

  // -------- Serial input --------
  if (Serial.available()) {
    String line = Serial.readStringUntil('\n');

    // Allow metadata after '|' (for logging on host)
    int sep = line.indexOf('|');
    String cmd = (sep > 0) ? line.substring(0, sep) : line;

    handleCmd(cmd);

    // Echo acknowledgment for synchronization/logging
    Serial.print("ACK ");
    Serial.println(line);
  }

  // -------- Motor ramping --------
  // Enforces acceleration limits regardless of host behavior
  if (millis() - lastRamp > RAMP_DT) {
    lastRamp = millis();

    curL += constrain(tgtL - curL, -RAMP_STEP, RAMP_STEP);
    curR += constrain(tgtR - curR, -RAMP_STEP, RAMP_STEP);

    setLR(curL, curR);
  }
}
