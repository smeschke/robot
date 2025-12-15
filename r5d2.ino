// ====== R5D2: ESP32 Web Drive + IMU Heading Lock (Hardened + No-String Pages) ======
#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <MPU6050_light.h>
#include "esp_task_wdt.h"


// ================= IMU =================
MPU6050 mpu(Wire);

// Cached IMU values (IMU NEVER touched outside loop)
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
#define L_RPWM 33
#define L_LPWM 27
#define R_RPWM 26
#define R_LPWM 25

// ================= Motion state =================
int curL = 0, curR = 0;
int tgtL = 0, tgtR = 0;

// ================= Parameters =================
int speedLevel = 80;
int RAMP_STEP  = 5;
unsigned long RAMP_DT = 25;
unsigned long lastRamp = 0;

// ================= Drive flags =================
bool driveF=false, driveB=false, driveL=false, driveR=false;

// ================= Failsafes =================
unsigned long lastCmdMs = 0;
const unsigned long CMD_TIMEOUT_MS = 40000; // stop if UI dies
const unsigned long IMU_TIMEOUT_MS = 50;    // IMU health

// ================= Wi-Fi =================
WebServer server(80);
const char* ssid = "R5D2";

// ================= DRIVE PAGE (PROGMEM) =================
const char DRIVE_HTML[] PROGMEM = R"rawliteral(
<!doctype html>
<html>
<head>
<meta name='viewport' content='width=device-width,initial-scale=1'>
<style>
  body{background:#111;color:#fff;text-align:center;font-family:sans-serif;user-select:none;-webkit-user-select:none;touch-action:manipulation}
  .btn{width:120px;height:100px;margin:10px;border-radius:16px;border:none;outline:none}
  .green{background:#2e7d32}
  .gold{background:#ffb300}
  .rev{background:#b23a48}
  .lr{background:#1976d2}
  a{color:#aaa;display:block;margin-top:20px;text-decoration:none}
</style>
</head>
<body>
<h1>R5D2</h1>

<div>
  <button class='btn lr'
    onmousedown="fetch('/L/on')" onmouseup="fetch('/L/off')"
    ontouchstart="fetch('/L/on')" ontouchend="fetch('/L/off')"></button>

  <button class='btn lr'
    onmousedown="fetch('/R/on')" onmouseup="fetch('/R/off')"
    ontouchstart="fetch('/R/on')" ontouchend="fetch('/R/off')"></button>
</div>

<div>
  <button class='btn green'
    onmousedown="fetch('/F/on')" onmouseup="fetch('/F/off')"
    ontouchstart="fetch('/F/on')" ontouchend="fetch('/F/off')"></button>

  <button class='btn gold'
    onmousedown="fetch('/G/on')" onmouseup="fetch('/G/off')"
    ontouchstart="fetch('/G/on')" ontouchend="fetch('/G/off')"></button>

  <button class='btn rev'
    onmousedown="fetch('/B/on')" onmouseup="fetch('/B/off')"
    ontouchstart="fetch('/B/on')" ontouchend="fetch('/B/off')"></button>
</div>

<a href='/params'>parameters</a>
</body>
</html>
)rawliteral";

// ================= PARAM PAGE (PROGMEM) =================
const char PARAM_HTML[] PROGMEM = R"rawliteral(
<!doctype html>
<html>
<head>
<meta name='viewport' content='width=device-width,initial-scale=1'>
<style>
  body{background:#111;color:#fff;text-align:center;font-family:sans-serif}
  input{width:80%}
  a{color:#aaa;display:block;margin-top:20px;text-decoration:none}
</style>
</head>
<body>
<h1>Parameters</h1>

<p>Speed</p>
<input id='spd' type='range' min='0' max='255'
  oninput="fetch(`/speed/set?val=${this.value}`)">

<p>Acceleration</p>
<input id='acc' type='range' min='1' max='20'
  oninput="fetch(`/accel?val=${this.value}`)">

<p>Ramp Interval</p>
<input id='rt' type='range' min='5' max='200' step='5'
  oninput="fetch(`/ramptime?val=${this.value}`)">

<p>Gold Speed</p>
<input id='gold' type='range' min='0' max='255'
  oninput="fetch(`/goldspeed?val=${this.value}`)">

<p>Heading KP</p>
<input id='kp' type='range' min='0' max='10' step='0.1'
  oninput="fetch(`/kp?val=${this.value}`)">

<p>Deadband</p>
<input id='dead' type='range' min='0' max='5' step='0.1'
  oninput="fetch(`/deadband?val=${this.value}`)">

<a href='/'>back</a>

<script>
async function sync(){
  let r = await fetch('/params/get');
  let j = await r.json();
  spd.value  = j.speed;
  gold.value = j.gold;
  acc.value  = j.accel;
  rt.value   = j.ramp;
  kp.value   = j.kp;
  dead.value = j.dead;
}
sync();
</script>

</body>
</html>
)rawliteral";

// ================= Motor helper =================
void setLR(int l, int r) {
  l = constrain(l, -255, 255);
  r = constrain(r, -255, 255);

  if (l >= 0) { analogWrite(L_RPWM, l); analogWrite(L_LPWM, 0); }
  else        { analogWrite(L_RPWM, 0); analogWrite(L_LPWM, -l); }

  if (r >= 0) { analogWrite(R_RPWM, r); analogWrite(R_LPWM, 0); }
  else        { analogWrite(R_RPWM, 0); analogWrite(R_LPWM, -r); }
}

// ================= Heading lock =================
void applyHeadingLock() {
  float err = targetHeading - imuYaw;

  if (abs(err) < HEADING_DEADBAND) return;

  int corr = (int)(err * HEADING_KP);
  tgtL -= corr;
  tgtR += corr;
}

// ================= Drive mixing =================
void computeDriveTargets() {
  int throttle = 0;
  int turn = 0;

  if (driveF && !driveB) throttle = speedLevel;
  else if (driveB && !driveF) throttle = -speedLevel;

  if (driveL && !driveR) turn = speedLevel / 2;
  else if (driveR && !driveL) turn = -speedLevel / 2;

  if (headingLockActive) {
    throttle = GOLD_SPEED;

    // Trim heading while locked (fine-tune)
    if (driveL) targetHeading += 1.0f;
    if (driveR) targetHeading -= 1.0f;
  }

  tgtL = constrain(throttle - turn, -255, 255);
  tgtR = constrain(throttle + turn, -255, 255);
}

// ================= Handlers =================
void handleDrive() {
  lastCmdMs = millis();

  char c = server.uri().charAt(1);
  bool on = server.uri().endsWith("/on");

  if      (c=='F') driveF = on;
  else if (c=='B') driveB = on;
  else if (c=='L') driveL = on;
  else if (c=='R') driveR = on;
  else if (c=='G') {
    if (on) { targetHeading = imuYaw; headingLockActive = true; }
    else    { headingLockActive = false; }
  }

  computeDriveTargets();
  server.send(200, "text/plain", "OK");
}

void handleParamsGet() {
  char buf[192];
  snprintf(buf, sizeof(buf),
    "{\"speed\":%d,\"gold\":%d,\"accel\":%d,"
    "\"ramp\":%lu,\"kp\":%.2f,\"dead\":%.1f}",
    speedLevel, GOLD_SPEED, RAMP_STEP,
    (unsigned long)RAMP_DT, (double)HEADING_KP, (double)HEADING_DEADBAND);

  server.send(200, "application/json", buf);
}

// ================= Setup =================
void setup() {
  Serial.begin(115200);


esp_task_wdt_config_t wdt_config = {
  .timeout_ms = 5000,        // 5 seconds
  .idle_core_mask = (1 << 0) | (1 << 1),  // monitor both cores
  .trigger_panic = true      // reset on timeout
};

esp_task_wdt_init(&wdt_config);
esp_task_wdt_add(NULL);      // add current task (loop task)

  pinMode(L_RPWM,OUTPUT); pinMode(L_LPWM,OUTPUT);
  pinMode(R_RPWM,OUTPUT); pinMode(R_LPWM,OUTPUT);

  Wire.begin();
  Wire.setClock(100000);
  Wire.setTimeOut(5);

  mpu.begin();
  delay(500);
  mpu.calcOffsets();

  WiFi.softAP(ssid);
  WiFi.setSleep(false);

  // Pages (served from flash; no heap churn)
  server.on("/", [](){ server.send_P(200, "text/html", DRIVE_HTML); });
  server.on("/params", [](){ server.send_P(200, "text/html", PARAM_HTML); });

  // Drive controls
  server.on("/F/on",handleDrive); server.on("/F/off",handleDrive);
  server.on("/B/on",handleDrive); server.on("/B/off",handleDrive);
  server.on("/L/on",handleDrive); server.on("/L/off",handleDrive);
  server.on("/R/on",handleDrive); server.on("/R/off",handleDrive);
  server.on("/G/on",handleDrive); server.on("/G/off",handleDrive);

  // Params readback (JSON; fixed buffer)
  server.on("/params/get", handleParamsGet);

  // Param setters
  server.on("/speed/set", [](){ lastCmdMs=millis(); speedLevel=server.arg("val").toInt(); server.send(200,"text/plain","OK"); });
  server.on("/accel",    [](){ lastCmdMs=millis(); RAMP_STEP=server.arg("val").toInt();    server.send(200,"text/plain","OK"); });
  server.on("/ramptime", [](){ lastCmdMs=millis(); RAMP_DT=server.arg("val").toInt();      server.send(200,"text/plain","OK"); });
  server.on("/goldspeed",[](){ lastCmdMs=millis(); GOLD_SPEED=server.arg("val").toInt();   server.send(200,"text/plain","OK"); });
  server.on("/kp",       [](){ lastCmdMs=millis(); HEADING_KP=server.arg("val").toFloat(); server.send(200,"text/plain","OK"); });
  server.on("/deadband", [](){ lastCmdMs=millis(); HEADING_DEADBAND=server.arg("val").toFloat(); server.send(200,"text/plain","OK"); });

  server.begin();
}

// ================= Loop =================
void loop() {
  server.handleClient();

  esp_task_wdt_reset();


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
    driveF=driveB=driveL=driveR=false;
    tgtL=tgtR=0;
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

  // ---- Optional health print (uncomment to debug long-run stability) ----
  
  static unsigned long lastPrint = 0;
  if (millis() - lastPrint > 5000) {
    lastPrint = millis();
    Serial.print("heap=");
    Serial.println(ESP.getFreeHeap());
  }
  
}
