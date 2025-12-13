// ====== R5D2: ESP32 Web Drive (Green Drive + Gold Heading Lock) ======
#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <MPU6050_light.h>

// ================= IMU =================
MPU6050 mpu(Wire);

float targetHeading = 0;
bool headingLockActive = false;

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
bool driveF = false, driveB = false;
bool driveL = false, driveR = false;


// ================= Wi-Fi =================
WebServer server(80);
const char* ssid = "R5D2_Wide";

// ================= Motor helper =================
void setLR(int l, int r) {
  l = constrain(l, -255, 255);
  r = constrain(r, -255, 255);

  if (l >= 0) {
    analogWrite(L_RPWM, l);
    analogWrite(L_LPWM, 0);
  }
  else        {
    analogWrite(L_RPWM, 0);
    analogWrite(L_LPWM, -l);
  }

  if (r >= 0) {
    analogWrite(R_RPWM, r);
    analogWrite(R_LPWM, 0);
  }
  else        {
    analogWrite(R_RPWM, 0);
    analogWrite(R_LPWM, -r);
  }
}

// ================= Heading lock =================
void applyHeadingLock() {
  float error = targetHeading - mpu.getAngleZ();

  if (abs(error) < HEADING_DEADBAND) return;

  int corr = error * HEADING_KP;

  tgtL -= corr;
  tgtR += corr;
}


// ================= Drive mixing =================
void computeDriveTargets() {
  int throttle = 0;
  int turn     = 0;

  // ---------- Throttle ----------
  if (driveF && !driveB) throttle = speedLevel;
  else if (driveB && !driveF) throttle = -speedLevel;

  // ---------- Turn ----------
  if (driveL && !driveR) turn = speedLevel / 2;
  else if (driveR && !driveL) turn = -speedLevel / 2;

  // ---------- GOLD (Heading Lock) ----------
  if (headingLockActive) {
    throttle = GOLD_SPEED;

    // allow steering to TRIM heading instead of canceling it
    if (driveL) targetHeading += 1.0;
    if (driveR) targetHeading -= 1.0;
  }

  // ---------- Mix ----------
  tgtL = throttle - turn;
  tgtR = throttle + turn;

  tgtL = constrain(tgtL, -255, 255);
  tgtR = constrain(tgtR, -255, 255);
}


// ================= DRIVE PAGE =================
String drivePage() {
  return F(
           "<!doctype html><html><head>"
           "<meta name='viewport' content='width=device-width,initial-scale=1'>"
           "<style>"
           "body{background:#111;color:#fff;text-align:center;font-family:sans-serif;user-select:none}"
           ".btn{width:120px;height:100px;margin:10px;border-radius:16px;border:none}"
           ".green{background:#2e7d32}"
           ".gold{background:#ffb300}"
           ".rev{background:#b23a48}"
           ".lr{background:#1976d2}"
           "a{color:#aaa;display:block;margin-top:20px}"
           "</style></head><body>"

           "<h1>R5D2</h1>"

           "<div>"
           "<button class='btn lr' onmousedown=\"fetch('/L/on')\" onmouseup=\"fetch('/L/off')\" "
           "ontouchstart=\"fetch('/L/on')\" ontouchend=\"fetch('/L/off')\"></button>"
           "<button class='btn lr' onmousedown=\"fetch('/R/on')\" onmouseup=\"fetch('/R/off')\" "
           "ontouchstart=\"fetch('/R/on')\" ontouchend=\"fetch('/R/off')\"></button>"
           "</div>"

           "<div>"
           "<button class='btn green' onmousedown=\"fetch('/F/on')\" onmouseup=\"fetch('/F/off')\" "
           "ontouchstart=\"fetch('/F/on')\" ontouchend=\"fetch('/F/off')\"></button>"

           "<button class='btn gold' onmousedown=\"fetch('/G/on')\" onmouseup=\"fetch('/G/off')\" "
           "ontouchstart=\"fetch('/G/on')\" ontouchend=\"fetch('/G/off')\"></button>"

           "<button class='btn rev' onmousedown=\"fetch('/B/on')\" onmouseup=\"fetch('/B/off')\" "
           "ontouchstart=\"fetch('/B/on')\" ontouchend=\"fetch('/B/off')\"></button>"
           "</div>"

           "<a href='/params'>parameters</a>"
           "<a href='/heading'>heading</a>"
           "</body></html>");
}

// ================= PARAM PAGE =================
String paramPage() {
  return F(
           "<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>"
           "<style>body{background:#111;color:#fff;text-align:center;font-family:sans-serif}"
           "input{width:80%}a{color:#aaa;display:block;margin-top:20px}</style></head><body>"
           "<h1>Parameters</h1>"
           "<p>Speed</p><input id='spd' type='range' min='0' max='255' oninput='fetch(`/speed/set?val=${this.value}`)'>"
           "<p>Acceleration</p><input id='acc' type='range' min='1' max='20' oninput='fetch(`/accel?val=${this.value}`)'>"
           "<p>Ramp Interval</p><input id='rt' type='range' min='5' max='200' step='5' oninput='fetch(`/ramptime?val=${this.value}`)'>"
           "<p>Gold Speed</p>"
           "<input id='gold' type='range' min='0' max='255' "
           "oninput='fetch(`/goldspeed?val=${this.value}`)'>"

           "<p>Heading KP</p>"
           "<input id='kp' type='range' min='0' max='10' step='0.1' "
           "oninput='fetch(`/kp?val=${this.value}`)'>"

           "<p>Deadband (deg)</p>"
           "<input id='dead' type='range' min='0' max='5' step='0.1' "
           "oninput='fetch(`/deadband?val=${this.value}`)'>"

           "<a href='/'>back</a>"
           "<script>"

           "async function sync(){"
           "  let r = await fetch('/params/get');"
           "  let j = await r.json();"
           "  spd.value  = j.speed;"
           "  gold.value = j.gold;"
           "  acc.value  = j.accel;"
           "  rt.value   = j.ramp;"
           "  kp.value   = j.kp;"
           "  dead.value = j.dead;"
           "}"
           "sync();"

           "</script>"
           "</body></html>");
}

// ================= HEADING PAGE =================
String headingPage() {
  return F(
           "<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>"
           "<style>"
           "body{background:#111;color:#fff;text-align:center;font-family:sans-serif}"
           "#compass{width:200px;height:200px;border:4px solid #888;border-radius:50%;margin:20px auto;position:relative}"
           "#needle{width:2px;height:90px;background:#ff5252;position:absolute;top:10px;left:50%;transform-origin:50% 90%}"
           "</style></head><body>"
           "<h1>Heading</h1>"
           "<div id='compass'><div id='needle'></div></div>"
           "<p>Yaw: <span id='yaw'>0</span></p>"
           "<p>Pitch: <span id='pitch'>0</span></p>"
           "<p>Roll: <span id='roll'>0</span></p>"
           "<a href='/'>back</a>"
           "<script>"
           "async function u(){let r=await fetch('/imu/get');let j=await r.json();"
           "yaw.innerText=j.yaw.toFixed(1);pitch.innerText=j.pitch.toFixed(1);roll.innerText=j.roll.toFixed(1);"
           "needle.style.transform=`rotate(${-j.yaw}deg)`;}"
           "setInterval(u,100);</script>"
           "</body></html>");
}

// ================= Handlers =================
void handleRoot() {
  server.send(200, "text/html", drivePage());
}
void handleParams() {
  server.send(200, "text/html", paramPage());
}
void handleHeading() {
  server.send(200, "text/html", headingPage());
}

void handleDrive() {
  char c = server.uri().charAt(1);
  bool on = server.uri().endsWith("/on");

  if (c == 'F') driveF = on;
  else if (c == 'B') driveB = on;
  else if (c == 'L') driveL = on;
  else if (c == 'R') driveR = on;
else if (c == 'G') {
  if (on) {
    targetHeading = mpu.getAngleZ();
    headingLockActive = true;
  } else {
    headingLockActive = false;
  }
}



  computeDriveTargets();
  server.send(200, "text/plain", "OK");
}

// ================= JSON =================
void handleParamsGet() {
  String j = "{";
  j += "\"speed\":" + String(speedLevel) + ",";
  j += "\"gold\":"  + String(GOLD_SPEED) + ",";
  j += "\"accel\":" + String(RAMP_STEP) + ",";
  j += "\"ramp\":"  + String(RAMP_DT) + ",";
  j += "\"kp\":"    + String(HEADING_KP, 2) + ",";
  j += "\"dead\":"  + String(HEADING_DEADBAND, 1);
  j += "}";
  server.send(200, "application/json", j);
}

void handleSpeedSet() {
  speedLevel = server.arg("val").toInt();
  computeDriveTargets();
  server.send(200, "OK");
}
void handleAccel() {
  RAMP_STEP = server.arg("val").toInt();
  server.send(200, "OK");
}
void handleRampTime() {
  RAMP_DT = server.arg("val").toInt();
  server.send(200, "OK");
}
void handleGoldSpeed() {
  GOLD_SPEED = server.arg("val").toInt();
  server.send(200, "OK");
}

void handleKP() {
  HEADING_KP = server.arg("val").toFloat();
  server.send(200, "OK");
}

void handleDeadband() {
  HEADING_DEADBAND = server.arg("val").toFloat();
  server.send(200, "OK");
}

void handleImuGet() {
  String j = "{";
  j += "\"yaw\":"   + String(mpu.getAngleZ(), 1) + ",";
  j += "\"pitch\":" + String(mpu.getAngleX(), 1) + ",";
  j += "\"roll\":"  + String(mpu.getAngleY(), 1);
  j += "}";
  server.send(200, "application/json", j);
}



// ================= Setup =================
void setup() {
  Serial.begin(115200);
  pinMode(L_RPWM, OUTPUT); pinMode(L_LPWM, OUTPUT);
  pinMode(R_RPWM, OUTPUT); pinMode(R_LPWM, OUTPUT);

  Wire.begin();
  mpu.begin();
  delay(1000);
  mpu.calcOffsets();

  WiFi.softAP(ssid);

  server.on("/", handleRoot);
  server.on("/params", handleParams);
  server.on("/heading", handleHeading);
  server.on("/imu/get", handleImuGet);

  server.on("/F/on", handleDrive); server.on("/F/off", handleDrive);
  server.on("/B/on", handleDrive); server.on("/B/off", handleDrive);
  server.on("/L/on", handleDrive); server.on("/L/off", handleDrive);
  server.on("/R/on", handleDrive); server.on("/R/off", handleDrive);
  server.on("/G/on", handleDrive); server.on("/G/off", handleDrive);

  server.on("/speed/set", handleSpeedSet);
  server.on("/accel", handleAccel);
  server.on("/ramptime", handleRampTime);
  server.on("/params/get", handleParamsGet);
  server.on("/goldspeed", handleGoldSpeed);
  server.on("/kp", handleKP);
  server.on("/deadband", handleDeadband);


  server.begin();
}

// ================= Loop =================
void loop() {
  server.handleClient();
  mpu.update();

  if (headingLockActive) {
    tgtL = GOLD_SPEED;
    tgtR = GOLD_SPEED;
    applyHeadingLock();
  }


  unsigned long now = millis();
  if (now - lastRamp >= RAMP_DT) {
    lastRamp = now;
    if (curL < tgtL) curL = min(curL + RAMP_STEP, tgtL);
    else if (curL > tgtL) curL = max(curL - RAMP_STEP, tgtL);
    if (curR < tgtR) curR = min(curR + RAMP_STEP, tgtR);
    else if (curR > tgtR) curR = max(curR - RAMP_STEP, tgtR);
    setLR(curL, curR);
  }
}
