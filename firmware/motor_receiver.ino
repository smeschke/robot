// ===== R5D2 Robot: Motor Receiver (ESP-NOW) =====
//
// Wire format: DrivePacket{left, right} -- signed PWM per side, -255..255.
// Works with either a human joystick remote (which does its own arcade-
// drive mixing before sending) or a computer plugged into an ESP32 bridge
// (which can send raw left/right values directly). This receiver does no
// mixing -- it only ramps toward the commanded values (to protect the
// gearboxes from instant direction reversals) and writes PWM to the
// motors. Stopping is just PWM 0; the motors' gear reduction holds them,
// so no active braking is needed.

#include <WiFi.h>
#include <esp_now.h>

// ================= Motor pins =================


#define FL_LPWM 14
#define FL_RPWM 27
#define BL_LPWM 33
#define BL_RPWM 32
#define BR_LPWM 26
#define BR_RPWM 25
#define FR_LPWM 18
#define FR_RPWM 19

// ================= Settings =================
const int      RAMP_STEP      = 1;   // max PWM change per ramp tick
const uint32_t RAMP_DT_MS     = 3;   // ramp tick period
const uint32_t CMD_TIMEOUT_MS = 300; // stop if no packet received in this long

// ================= Wire format =================
typedef struct __attribute__((packed)) {
  int16_t left;   // -255..255
  int16_t right;  // -255..255
} DrivePacket;

// ================= State =================
int curL = 0, curR = 0;
volatile int tgtL = 0, tgtR = 0;
volatile uint32_t lastRecvMs = 0;
uint32_t lastRampMs = 0;

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

// ================= ESP-NOW receive =================
void onRecv(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
  if (len != sizeof(DrivePacket)) return;

  DrivePacket p;
  memcpy(&p, data, sizeof(p));

  tgtL = constrain((int)p.left, -255, 255);
  tgtR = constrain((int)p.right, -255, 255);
  lastRecvMs = millis();
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

  if (now - lastRecvMs > CMD_TIMEOUT_MS) {
    tgtL = 0;
    tgtR = 0;
  }

  if (now - lastRampMs >= RAMP_DT_MS) {
    lastRampMs = now;
    curL += constrain(tgtL - curL, -RAMP_STEP, RAMP_STEP);
    curR += constrain(tgtR - curR, -RAMP_STEP, RAMP_STEP);
    setAll(curL, curR);
  }
}
