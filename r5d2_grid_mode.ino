// ====== R5D2: ESP32 Web Drive + IMU Heading Lock + Grid Mode (Hardened + No-String Pages) ======
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

// ================= Grid mode =================
enum GridState { GRID_IDLE, GRID_STRAIGHT, GRID_TURN };
bool gridActive = false;
GridState gridState = GRID_IDLE;
// ---- Grid heading targets ----
float gridRefHeading = 0;     // "0°" heading for this grid run (captured at start)
bool  gridLegIs180 = false;   // false => drive on ref, true => drive on ref+180

// ---- Grid IMU trim params (separate from your manual GOLD lock) ----
float GRID_KP = 2.0f;         // start ~2.0, adjust later
float GRID_DEADBAND = 1.0f;   // degrees
int   GRID_TRIM_MAX = 25;     // max PWM delta from trim

unsigned long gridStateStart = 0;
bool gridTurnLeft = true;

// ---- Grid parameters (ONLY these) ----
unsigned long GRID_STRAIGHT_TIME_MS = 5000;
int GRID_STRAIGHT_SPEED = 80;

int GRID_TURN_OUTER_SPEED = 80;
int GRID_TURN_INNER_SPEED = 40;
unsigned long GRID_TURN_TIME_MS = 1200;


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
const unsigned long CMD_TIMEOUT_MS = 400000; // stop if UI dies
const unsigned long IMU_TIMEOUT_MS = 50;    // IMU health (kept as you had it)

// ================= Wi-Fi =================
WebServer server(80);
const char* ssid = "R5D2_grid";

// ================= Angle helper =================
float normalizeAngle(float a) {
  while (a > 180) a -= 360;
  while (a < -180) a += 360;
  return a;
}

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
  a{color:#aaa;display:block;margin-top:18px;text-decoration:none}
  .row{display:flex;justify-content:center;gap:10px}
</style>
</head>
<body>
<h1>R5D2</h1>

<div class='row'>
  <button class='btn lr'
    onmousedown="fetch('/L/on')" onmouseup="fetch('/L/off')"
    ontouchstart="fetch('/L/on')" ontouchend="fetch('/L/off')"></button>

  <button class='btn lr'
    onmousedown="fetch('/R/on')" onmouseup="fetch('/R/off')"
    ontouchstart="fetch('/R/on')" ontouchend="fetch('/R/off')"></button>
</div>

<div class='row'>
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
<a href='/grid'>grid mode</a>
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
  .card{max-width:520px;margin:0 auto;padding:10px}
</style>
</head>
<body>
<div class='card'>
<h1>Parameters</h1>

<p>Speed</p>
<input id='spd' type='range' min='0' max='255'
  oninput="fetch(`/speed/set?val=${this.value}`)">

<p>Acceleration (ramp step)</p>
<input id='acc' type='range' min='1' max='20'
  oninput="fetch(`/accel?val=${this.value}`)">

<p>Ramp Interval (ms)</p>
<input id='rt' type='range' min='5' max='200' step='5'
  oninput="fetch(`/ramptime?val=${this.value}`)">

<p>Gold Speed</p>
<input id='gold' type='range' min='0' max='255'
  oninput="fetch(`/goldspeed?val=${this.value}`)">

<p>Heading KP</p>
<input id='kp' type='range' min='0' max='10' step='0.1'
  oninput="fetch(`/kp?val=${this.value}`)">

<p>Deadband (deg)</p>
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
</div>
</body>
</html>
)rawliteral";



// ================= GRID PAGE (PROGMEM) =================
const char GRID_HTML[] PROGMEM = R"rawliteral(
<!doctype html>
<html>
<head>
<meta name='viewport' content='width=device-width,initial-scale=1'>
<style>
  body{background:#111;color:#fff;font-family:sans-serif;text-align:center}
  .card{max-width:560px;margin:0 auto;padding:12px}
  .row{display:flex;gap:10px;justify-content:center;flex-wrap:wrap}
  .btn{width:160px;height:56px;border-radius:14px;border:none;outline:none;font-size:16px}
  .start{background:#2e7d32;color:#fff}
  .stop{background:#b23a48;color:#fff}
  a{color:#aaa;display:block;margin-top:18px;text-decoration:none}
  .status{margin-top:12px;color:#bbb}
  .hint{margin-top:10px;color:#888;font-size:14px;line-height:1.3}

  .blk{margin-top:16px;padding:10px;border:1px solid #222;border-radius:12px;background:#0b0b0b}
  .lbl{display:flex;justify-content:space-between;align-items:baseline;color:#bbb;margin-bottom:8px}
  .lbl span{font-size:14px}
  .val{color:#fff;font-size:16px}
  input[type=range]{width:100%}
</style>
</head>

<body>
<div class='card'>
  <h1>Grid Mode</h1>

  <div class='row'>
    <button class='btn start' onclick="fetch('/grid/start')">START</button>
    <button class='btn stop'  onclick="fetch('/grid/stop')">STOP</button>
  </div>

  <div class='status' id='st'>status: (loading)</div>

  <div class='blk'>
    <div class='lbl'>
      <span>Straight time</span><span class='val'><span id='t_v'>5000</span> ms</span>
    </div>
    <input id='t' type='range' min='0' max='10000' step='50'>
  </div>

  <div class='blk'>
    <div class='lbl'>
      <span>Straight speed</span><span class='val'><span id='ss_v'>80</span></span>
    </div>
    <input id='ss' type='range' min='0' max='255' step='1'>
  </div>

  <div class='blk'>
    <div class='lbl'>
      <span>Turn outer speed</span><span class='val'><span id='to_v'>80</span></span>
    </div>
    <input id='to' type='range' min='0' max='255' step='1'>
  </div>

  <div class='blk'>
    <div class='lbl'>
      <span>Turn inner speed</span><span class='val'><span id='ti_v'>40</span></span>
    </div>
    <input id='ti' type='range' min='-255' max='255' step='1'>
  </div>

  <div class='blk'>
    <div class='lbl'>
      <span>Turn time</span><span class='val'><span id='tt_v'>1200</span> ms</span>
    </div>
    <input id='tt' type='range' min='0' max='5000' step='25'>
  </div>

  <div class='hint'>
    Timed differential-wheel grid motion. Inner wheel may be reversed for spin-in-place turns.
  </div>

  <a href='/'>back</a>
</div>

<script>
let suppressSync = false;
let suppressTimer = null;

function userTouched(){
  suppressSync = true;
  if (suppressTimer) clearTimeout(suppressTimer);
  suppressTimer = setTimeout(()=>{ suppressSync=false; }, 600);
}

function setLabel(id, val){
  document.getElementById(id).textContent = val;
}

async function pushAll(){
  // push current slider values to ESP
  await fetch(`/grid/straight_time?val=${t.value}`);
  await fetch(`/grid/straight_speed?val=${ss.value}`);
  await fetch(`/grid/turn_outer?val=${to.value}`);
  await fetch(`/grid/turn_inner?val=${ti.value}`);
  await fetch(`/grid/turn_time?val=${tt.value}`);
}

function hookSlider(sliderId, labelId){
  const s = document.getElementById(sliderId);
  s.addEventListener('input', async () => {
    userTouched();
    setLabel(labelId, s.value);
    // push immediately while dragging
    await pushAll();
  });
}

// hook all sliders
hookSlider('t','t_v');
hookSlider('ss','ss_v');
hookSlider('to','to_v');
hookSlider('ti','ti_v');
hookSlider('tt','tt_v');

async function sync(){
  if (suppressSync) return;

  let r = await fetch('/grid/get');
  let j = await r.json();

  // set sliders
  t.value  = j.straight_time;  setLabel('t_v',  t.value);
  ss.value = j.straight_speed; setLabel('ss_v', ss.value);
  to.value = j.turn_outer;     setLabel('to_v', to.value);
  ti.value = j.turn_inner;     setLabel('ti_v', ti.value);
  tt.value = j.turn_time;      setLabel('tt_v', tt.value);

  st.textContent = `status: ${j.active ? 'ACTIVE' : 'IDLE'} | state: ${j.state}`;
}

setInterval(sync, 500);
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
  float err = normalizeAngle(targetHeading - imuYaw);

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

  // Heading-lock manual mode behavior (your original)
  if (headingLockActive) {
    throttle = GOLD_SPEED;

    // Trim heading while locked
    if (driveL) targetHeading += 1.0f;
    if (driveR) targetHeading -= 1.0f;
  }

  tgtL = constrain(throttle - turn, -255, 255);
  tgtR = constrain(throttle + turn, -255, 255);
}


void updateGridMode() {
  if (!gridActive) return;

  switch (gridState) {

case GRID_STRAIGHT: {
  tgtL = GRID_STRAIGHT_SPEED;
  tgtR = GRID_STRAIGHT_SPEED;

  float desired = gridRefHeading + (gridLegIs180 ? 180.0f : 0.0f);
  desired = normalizeAngle(desired);

  float err = normalizeAngle(desired - imuYaw);

  if (abs(err) > GRID_DEADBAND) {
    int trim = (int)(err * GRID_KP);
    trim = constrain(trim, -GRID_TRIM_MAX, GRID_TRIM_MAX);

    tgtL -= trim;
    tgtR += trim;
  }

  // ✅ prevent reverse during straight
  tgtL = constrain(tgtL, 0, 255);
  tgtR = constrain(tgtR, 0, 255);

  if (millis() - gridStateStart >= GRID_STRAIGHT_TIME_MS) {
    gridState = GRID_TURN;
    gridStateStart = millis();
  }
  break;
}


    case GRID_TURN:
      // NO IMU correction during timed turns
      if (gridTurnLeft) {
        tgtL = GRID_TURN_INNER_SPEED;
        tgtR = GRID_TURN_OUTER_SPEED;
      } else {
        tgtL = GRID_TURN_OUTER_SPEED;
        tgtR = GRID_TURN_INNER_SPEED;
      }

      if (millis() - gridStateStart >= GRID_TURN_TIME_MS) {
        // Alternate turn direction AND flip which straight heading we want next
        gridTurnLeft = !gridTurnLeft;
        gridLegIs180 = !gridLegIs180;

        gridState = GRID_STRAIGHT;
        gridStateStart = millis();
      }
      break;

    default:
      break;
  }
}


// ================= Handlers =================
void cancelAutonomy() {
  // called on manual input
  gridActive = false;
  gridState = GRID_IDLE;
}

void handleDrive() {
  lastCmdMs = millis();
  cancelAutonomy();

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
  char buf[224];
  snprintf(buf, sizeof(buf),
    "{\"speed\":%d,\"gold\":%d,\"accel\":%d,"
    "\"ramp\":%lu,\"kp\":%.2f,\"dead\":%.1f}",
    speedLevel, GOLD_SPEED, RAMP_STEP,
    (unsigned long)RAMP_DT, (double)HEADING_KP, (double)HEADING_DEADBAND);

  server.send(200, "application/json", buf);
}

// ---- Grid endpoints ----
void handleGridStart() {
  lastCmdMs = millis();

  // ensure manual gold lock doesn't interfere
  headingLockActive = false;

  gridActive = true;
  gridState = GRID_STRAIGHT;
  gridTurnLeft = true;
  gridStateStart = millis();

  // capture the run's reference heading ("0°")
  gridRefHeading = imuYaw;
  gridLegIs180 = false;   // first straight is on ref heading

  server.send(200, "text/plain", "GRID START");
}







void handleGridStop() {
  gridActive = false;
  gridState = GRID_IDLE;
  tgtL = tgtR = 0;
  server.send(200, "text/plain", "GRID STOP");
}





void handleGridGet() {
  char buf[256];
  const char* st =
    (gridState==GRID_IDLE) ? "IDLE" :
    (gridState==GRID_STRAIGHT) ? "STRAIGHT" : "TURN";

  snprintf(buf, sizeof(buf),
    "{\"active\":%s,\"state\":\"%s\","
    "\"straight_time\":%lu,"
    "\"straight_speed\":%d,"
    "\"turn_outer\":%d,"
    "\"turn_inner\":%d,"
    "\"turn_time\":%lu}",
    gridActive ? "true" : "false",
    st,
    GRID_STRAIGHT_TIME_MS,
    GRID_STRAIGHT_SPEED,
    GRID_TURN_OUTER_SPEED,
    GRID_TURN_INNER_SPEED,
    GRID_TURN_TIME_MS
  );

  server.send(200, "application/json", buf);
}

// ================= Setup =================
void setup() {
  Serial.begin(115200);

  // Watchdog (your config)
  esp_task_wdt_config_t wdt_config = {
    .timeout_ms = 5000,
    .idle_core_mask = (1 << 0) | (1 << 1),
    .trigger_panic = true
  };
  esp_task_wdt_init(&wdt_config);
  esp_task_wdt_add(NULL);

  // Pins
  pinMode(L_RPWM,OUTPUT); pinMode(L_LPWM,OUTPUT);
  pinMode(R_RPWM,OUTPUT); pinMode(R_LPWM,OUTPUT);

  // IMU
  Wire.begin();
  Wire.setClock(100000);
  Wire.setTimeOut(5);

  mpu.begin();
  delay(500);
  mpu.calcOffsets();

  // Wi-Fi AP
  WiFi.softAP(ssid);
  WiFi.setSleep(false);

  // Pages
  server.on("/",       [](){ server.send_P(200, "text/html", DRIVE_HTML); });
  server.on("/params", [](){ server.send_P(200, "text/html", PARAM_HTML); });
  server.on("/grid",   [](){ server.send_P(200, "text/html", GRID_HTML); });

  // Drive controls
  server.on("/F/on",handleDrive); server.on("/F/off",handleDrive);
  server.on("/B/on",handleDrive); server.on("/B/off",handleDrive);
  server.on("/L/on",handleDrive); server.on("/L/off",handleDrive);
  server.on("/R/on",handleDrive); server.on("/R/off",handleDrive);
  server.on("/G/on",handleDrive); server.on("/G/off",handleDrive);

  // Params readback
  server.on("/params/get", handleParamsGet);

  // Param setters
  server.on("/speed/set", [](){ lastCmdMs=millis(); speedLevel=server.arg("val").toInt(); server.send(200,"text/plain","OK"); });
  server.on("/accel",    [](){ lastCmdMs=millis(); RAMP_STEP=server.arg("val").toInt();    server.send(200,"text/plain","OK"); });
  server.on("/ramptime", [](){ lastCmdMs=millis(); RAMP_DT=server.arg("val").toInt();      server.send(200,"text/plain","OK"); });
  server.on("/goldspeed",[](){ lastCmdMs=millis(); GOLD_SPEED=server.arg("val").toInt();   server.send(200,"text/plain","OK"); });
  server.on("/kp",       [](){ lastCmdMs=millis(); HEADING_KP=server.arg("val").toFloat(); server.send(200,"text/plain","OK"); });
  server.on("/deadband", [](){ lastCmdMs=millis(); HEADING_DEADBAND=server.arg("val").toFloat(); server.send(200,"text/plain","OK"); });

  // Grid endpoints
  server.on("/grid/start", handleGridStart);
  server.on("/grid/stop",  handleGridStop);
  server.on("/grid/get",   handleGridGet);
server.on("/grid/straight_time", [](){
  GRID_STRAIGHT_TIME_MS = server.arg("val").toInt();
  server.send(200,"text/plain","OK");
});

server.on("/grid/straight_speed", [](){
  GRID_STRAIGHT_SPEED = server.arg("val").toInt();
  server.send(200,"text/plain","OK");
});

server.on("/grid/turn_outer", [](){
  GRID_TURN_OUTER_SPEED = server.arg("val").toInt();
  server.send(200,"text/plain","OK");
});

server.on("/grid/turn_inner", [](){
  GRID_TURN_INNER_SPEED = server.arg("val").toInt();
  server.send(200,"text/plain","OK");
});

server.on("/grid/turn_time", [](){
  GRID_TURN_TIME_MS = server.arg("val").toInt();
  server.send(200,"text/plain","OK");
});


  server.begin();
}

// ================= Loop =================
void loop() {
  server.handleClient();
  esp_task_wdt_reset();

    // ---- Grid counts as an active command ----
  if (gridActive) lastCmdMs = millis();

  // ---- IMU update (local only) ----
  mpu.update();
  imuYaw = normalizeAngle(mpu.getAngleZ());
  lastImuMs = millis();

  // ---- IMU health ----
  if (millis() - lastImuMs > IMU_TIMEOUT_MS) {
    headingLockActive = false;
  }

  // ---- Command timeout ----
  if (millis() - lastCmdMs > CMD_TIMEOUT_MS) {
    headingLockActive = false;
    gridActive = false;
    gridState = GRID_IDLE;

    driveF=driveB=driveL=driveR=false;
    tgtL=tgtR=0;
  }

// ---- Decide targets ----
if (gridActive) {
  updateGridMode();
} else {
  computeDriveTargets();

  if (headingLockActive) {
    tgtL = GOLD_SPEED;
    tgtR = GOLD_SPEED;
    applyHeadingLock();
  }
}


  // ---- Ramp ----
  unsigned long now = millis();
  if (now - lastRamp >= RAMP_DT) {
    lastRamp = now;
    curL += constrain(tgtL - curL, -RAMP_STEP, RAMP_STEP);
    curR += constrain(tgtR - curR, -RAMP_STEP, RAMP_STEP);
    setLR(curL, curR);
  }

  // ---- Optional health print (uncomment if needed) ----
  /*
  static unsigned long lastPrint = 0;
  if (millis() - lastPrint > 5000) {
    lastPrint = millis();
    Serial.print("heap=");
    Serial.println(ESP.getFreeHeap());
  }
  */
}
