// ===== R5D2 Robot: ESP-NOW Receiver =====
#include <WiFi.h>
#include <esp_now.h>

// ================= Motor pins =================
#define FL_LPWM 33
#define FL_RPWM 25
#define BL_LPWM 32
#define BL_RPWM 26
#define BR_LPWM 14
#define BR_RPWM 27
#define FR_LPWM 19
#define FR_RPWM 18

// ================= Settings =================
int rampStep   = 1;
uint32_t rampDtMs = 3;


const uint32_t JOY_TIMEOUT_MS = 300;

// ================= Motion state =================
int curL = 0, curR = 0;
int tgtL = 0, tgtR = 0;

volatile int throttleCmd = 0;
volatile int turnCmd     = 0;

volatile uint32_t lastJoyMs = 0;
uint32_t lastRampMs = 0;

// ================= ESP-NOW packet =================
typedef struct __attribute__((packed)) {
  uint16_t x;
  uint16_t y;
  uint32_t seq;
  uint32_t ms;
} JoyPacket;


void setAll(int l, int r) {
  l = constrain(l, -255, 255);
  r = constrain(r, -255, 255);

  if (l >= 0) {
    analogWrite(FL_LPWM, l); analogWrite(FL_RPWM, 0);
    analogWrite(BL_LPWM, l); analogWrite(BL_RPWM, 0);
  } else {
    analogWrite(FL_LPWM, 0); analogWrite(FL_RPWM, -l);
    analogWrite(BL_LPWM, 0); analogWrite(BL_RPWM, -l);
  }

  if (r >= 0) {
    analogWrite(FR_LPWM, r); analogWrite(FR_RPWM, 0);
    analogWrite(BR_LPWM, r); analogWrite(BR_RPWM, 0);
  } else {
    analogWrite(FR_LPWM, 0); analogWrite(FR_RPWM, -r);
    analogWrite(BR_LPWM, 0); analogWrite(BR_RPWM, -r);
  }
}

void mixToTargets(int thr, int turn)
{
  float f = -thr  / 255.0f;
  float s = turn / 255.0f;

  if (fabs(f) < .2 && fabs(s) < .2) {
    tgtL = 0;
    tgtR = 0;
    return;
  }

  float left  = f + s;
  float right = f - s;

  tgtL = (int)(left  * 255);
  tgtR = (int)(right * 255);
}




// ================= ESP-NOW receive =================
void onRecv(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
  if (len != sizeof(JoyPacket)) return;

  JoyPacket p;
  memcpy(&p, data, sizeof(p));

  int sx = (int)p.x - 2048;
  int sy = (int)p.y - 2048;
  sy = -sy;

  turnCmd     = constrain((int)(sx * 255.0f / 2048.0f), -255, 255);
  throttleCmd = constrain((int)(sy * 255.0f / 2048.0f), -255, 255);

  lastJoyMs = millis();
}

// ================= Setup =================
void setup() {
  Serial.begin(115200);

  pinMode(FL_LPWM, OUTPUT); pinMode(FL_RPWM, OUTPUT);
  pinMode(BL_LPWM, OUTPUT); pinMode(BL_RPWM, OUTPUT);
  pinMode(FR_LPWM, OUTPUT); pinMode(FR_RPWM, OUTPUT);
  pinMode(BR_LPWM, OUTPUT); pinMode(BR_RPWM, OUTPUT);

  setAll(0, 0);

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);

  esp_now_init();
  esp_now_register_recv_cb(onRecv);

  Serial.println("Robot Ready.");
}

// ================= Loop =================
void loop() {
  uint32_t now = millis();

  // --- Motion ---
  if (now - lastJoyMs > JOY_TIMEOUT_MS) {
    tgtL = 0; tgtR = 0;
  } else {
    mixToTargets(throttleCmd, turnCmd);
  }

  if (now - lastRampMs >= rampDtMs) {
    lastRampMs = now;
    curL += constrain(tgtL - curL, -rampStep, rampStep);
    curR += constrain(tgtR - curR, -rampStep, rampStep);
    setAll(curL, curR);
  }
}
