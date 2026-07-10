/*
  ESP32 + 2.4" ILI9341 SPI TFT + Dual Joysticks — Dual Analog Stick Controller
  (espnow-protocol.pdf: "Controller 2 — Dual Joystick (with screen)")

  Same hardware as dungeon_warden.ino / twin_stick_shooter.ino. Left (move)
  stick drives the robot; right (aim) stick controls the lights; the screen
  renders the robot's heartbeat as a live HUD.

  User_Setup.h (unchanged):
    #define ILI9341_DRIVER
    #define TFT_MISO 19
    #define TFT_MOSI 23
    #define TFT_SCLK 18
    #define TFT_CS   5
    #define TFT_DC   2
    #define TFT_RST  4
    #define SPI_FREQUENCY 40000000

  TFT wiring:
    VCC -> 3.3V   GND -> GND
    CS -> GPIO5   RST -> GPIO4   DC -> GPIO2
    MOSI -> GPIO23  SCK -> GPIO18  MISO -> GPIO19
    LED -> 3.3V

  Joystick 1 (MOVE / drive):
    VRx -> GPIO34   VRy -> GPIO35   SW -> GPIO25
    VCC -> 3.3V     GND -> GND

  Joystick 2 (AIM / lights):
    VRx -> GPIO32   VRy -> GPIO33   SW -> GPIO26
    VCC -> 3.3V     GND -> GND

  Protocol (espnow-protocol.pdf), channel 1, no encryption:
    - Left stick  -> JoyPacket (12 bytes), sent continuously (~5ms) to the
      motor receiver. A dropped packet is superseded 5ms later; the robot
      applies its own fail-safe stop if the stream goes quiet.
    - Right stick -> directional flick switch: forward past FLICK_THRESHOLD
      latches the headlights ON, back past it latches them OFF; left/right
      fire a momentary turn signal (TURN_SIGNAL_MS, timed by the relay
      receiver). The stick must return within FLICK_RELEASE of center
      before the next flick can arm, so holding a direction doesn't
      repeat-fire. Each flick is a one-shot LightPacket (5 bytes),
      burst-sent 3x to the relay receiver instead of waiting on an ACK.
    - HeartbeatPacket (robot -> controller, ~500ms) carries RSSI, relay
      state, and uptime; that's the whole HUD. Packet types are told apart
      purely by length, per the doc's convention.
*/

#include <SPI.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <esp_now.h>

TFT_eSPI tft = TFT_eSPI();
TFT_eSprite fb = TFT_eSprite(&tft);

// ---- Joystick pins (same wiring as dungeon_warden.ino) ----
#define J1_VRX 35
#define J1_VRY 34
#define J1_SW  25

#define J2_VRX 32
#define J2_VRY 33
#define J2_SW  26

#define ADC_CENTER 2048
#define STICK_DEADZONE 0.06f

int screenW = 320;
int screenH = 240;

// ---- ESP-NOW peers (espnow-protocol.pdf device table) ----
uint8_t motorMac[] = {0xD4, 0xE9, 0xF4, 0xE7, 0xC1, 0xF8}; // motor receiver: drive + heartbeat
uint8_t relayMac[] = {0x7C, 0x9E, 0xBD, 0xF5, 0x0B, 0x98}; // relay receiver: lights (relay_receiver.ino)

const uint32_t DRIVE_SEND_INTERVAL_MS = 5;    // protocol: drive stream ~every 5ms
const uint32_t DRAW_INTERVAL_MS       = 33;   // display refresh, independent of send rate
const uint32_t HEARTBEAT_TIMEOUT_MS   = 1000; // HUD goes stale if no heartbeat in this long

const float    FLICK_THRESHOLD     = 0.55f;   // right-stick deflection that triggers a flick command
const float    FLICK_RELEASE       = 0.30f;   // deflection it must fall back under before the next flick can arm
const uint32_t TURN_SIGNAL_MS      = 2000;    // turn-signal duration, applied by the relay receiver
const int      LIGHT_BURST_COUNT   = 3;       // one-shot event resend burst, per the doc
const uint32_t LIGHT_BURST_GAP_MS  = 15;

// ---- Wire format (must match robot_receiver.ino / espnow-protocol.pdf) ----

typedef struct __attribute__((packed)) {
  uint16_t x;   // joystick X, 0-4095
  uint16_t y;   // joystick Y, 0-4095
  uint32_t seq; // rolling sequence #
  uint32_t ms;  // sender timestamp
} JoyPacket;    // 12 bytes -- drive

// LightPacket.cmd values
#define LIGHT_CMD_OFF        0  // headlights off
#define LIGHT_CMD_ON         1  // headlights on
#define LIGHT_CMD_TURN_LEFT  2  // left turn signal, momentary (TURN_SIGNAL_MS)
#define LIGHT_CMD_TURN_RIGHT 3  // right turn signal, momentary (TURN_SIGNAL_MS)

typedef struct __attribute__((packed)) {
  uint8_t  cmd; // LIGHT_CMD_*, see above
  uint32_t seq;
} LightPacket;    // 5 bytes -- accessory event

typedef struct __attribute__((packed)) {
  int8_t   rssi;       // robot's RSSI reading of the controller, dBm
  uint8_t  relayState; // 0 = off, 1 = on
  uint32_t uptimeMs;
  uint32_t seq;
} HeartbeatPacket;     // 10 bytes -- robot -> controller, ~every 500ms

// ---- State ----

JoyPacket   drivePkt{};
HeartbeatPacket hb{};
bool robotEverSeen = false;
unsigned long lastHeartbeatMs = 0;
int8_t localRssi = 0; // this board's own RSSI reading of the last heartbeat

bool     headlightsOn        = false; // latched commanded headlight state
bool     flickArmed          = true;  // right stick must return near center before the next flick can arm
uint32_t lightSeq            = 0;
int      lightBurstRemaining = 0;
unsigned long lastLightBurstMs = 0;
uint8_t  lastLightCmd         = LIGHT_CMD_OFF; // most recent command sent, for the HUD
unsigned long turnSignalHudUntilMs = 0;        // local HUD shows the turn-signal label until this time

// ---- Colours (RGB565), matching remote_tft_demo's dashboard palette ----
#define C_BG     0x0000
#define C_HDR    0x0439
#define C_DIM    0x4208
#define C_MID    0x8410
#define C_WHITE  0xFFFF
#define C_GREEN  0x07E0
#define C_YELLOW 0xFFE0
#define C_ORANGE 0xFD20
#define C_RED    0xF800
#define C_CYAN   0x07FF

#define HDR_H  18
#define DIV_X  130

// ---- Small helpers ----

float readAxisNormalized(int pin) {
  int raw = analogRead(pin);
  float v = (float)(raw - ADC_CENTER) / (float)ADC_CENTER;
  if (v > 1.0f) v = 1.0f;
  if (v < -1.0f) v = -1.0f;
  if (fabsf(v) < STICK_DEADZONE) return 0.0f;
  return v;
}

int signalBars(int8_t rssi) {
  if (rssi >= -50) return 5;
  if (rssi >= -60) return 4;
  if (rssi >= -70) return 3;
  if (rssi >= -80) return 2;
  if (rssi >= -90) return 1;
  return 0;
}

// ---- ESP-NOW receive: heartbeat only (10 bytes) ----

void onDataRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  if (len != sizeof(HeartbeatPacket)) return;
  memcpy(&hb, data, sizeof(hb));
  robotEverSeen = true;
  lastHeartbeatMs = millis();
  if (info->rx_ctrl != nullptr) localRssi = info->rx_ctrl->rssi;
}

// ---- Draw: static chrome ----

void drawFrame() {
  fb.fillSprite(C_BG);

  fb.fillRect(0, 0, screenW, HDR_H, C_HDR);
  fb.setTextColor(C_WHITE, C_HDR);
  fb.setTextSize(1);
  fb.setCursor(4, 5);
  fb.print("ROBOT LINK  v1.0");

  fb.drawFastVLine(DIV_X, HDR_H, screenH - HDR_H, C_DIM);

  fb.setTextColor(C_MID, C_BG);
  fb.setCursor(4, HDR_H +  3); fb.print("SIGNAL");
  fb.setCursor(4, HDR_H + 43); fb.print("LIGHTS");
  fb.setCursor(4, HDR_H + 83); fb.print("UPTIME");
  fb.setCursor(4, HDR_H +113); fb.print("LAST SEEN");

  fb.setCursor(DIV_X + 8, HDR_H + 3);  fb.print("DRIVE");
  fb.setCursor(DIV_X + 96, HDR_H + 3); fb.print("LIGHTS");
}

// ---- Draw: link status badge ----

void drawLinkBadge(int bars) {
  const char *label;
  uint16_t col;
  if (bars <= 0)      { label = "NO LINK"; col = C_RED; }
  else if (bars < 3)  { label = "WEAK";    col = C_ORANGE; }
  else                { label = "LINKED";  col = C_GREEN; }

  fb.fillRoundRect(screenW - 60, 2, 56, HDR_H - 5, 3, col);
  fb.setTextColor(C_BG, col);
  fb.setTextSize(1);
  int16_t w = fb.textWidth(label);
  fb.setCursor(screenW - 60 + (56 - w) / 2, 5);
  fb.print(label);
}

void drawSignalBars(int x, int y, int filled) {
  for (int i = 0; i < 5; i++) {
    int bh = 4 + i * 4;
    fb.fillRect(x + i * 9, y - bh, 7, bh, (i < filled) ? C_GREEN : C_DIM);
  }
}

// ---- Draw: left status panel ----

void drawStatusPanel() {
  const int lx = 4;
  const int ty = HDR_H;
  unsigned long now = millis();
  unsigned long age = robotEverSeen ? (now - lastHeartbeatMs) : 0;
  bool stale = !robotEverSeen || (age > HEARTBEAT_TIMEOUT_MS);
  int bars = stale ? 0 : signalBars(localRssi);

  drawLinkBadge(bars);

  // Signal
  drawSignalBars(lx, ty + 36, bars);
  char buf[24];
  fb.setTextSize(1);
  fb.setTextColor(C_MID, C_BG);
  fb.setCursor(lx + 52, ty + 30);
  if (robotEverSeen) snprintf(buf, sizeof(buf), "%d dBm", localRssi);
  else                snprintf(buf, sizeof(buf), "--");
  fb.print(buf);

  // Lights -- robot's confirmed relay state, not just what we last sent
  bool robotLightOn = robotEverSeen && hb.relayState;
  fb.setTextSize(1);
  fb.setTextColor(stale ? C_DIM : (robotLightOn ? C_YELLOW : C_MID), C_BG);
  fb.setCursor(lx, ty + 56);
  fb.print(!robotEverSeen ? "-- no data --" : (robotLightOn ? "ON" : "OFF"));

  // Uptime (robot's, from heartbeat)
  fb.setTextColor(stale ? C_DIM : C_CYAN, C_BG);
  fb.setCursor(lx, ty + 93);
  if (robotEverSeen) {
    uint32_t s = hb.uptimeMs / 1000;
    snprintf(buf, sizeof(buf), "%02u:%02u:%02u", (unsigned)(s / 3600), (unsigned)((s / 60) % 60), (unsigned)(s % 60));
  } else {
    snprintf(buf, sizeof(buf), "--:--:--");
  }
  fb.print(buf);

  // Last seen
  fb.setTextColor(stale ? C_ORANGE : C_MID, C_BG);
  fb.setCursor(lx, ty + 123);
  if (robotEverSeen) snprintf(buf, sizeof(buf), "%lu ms ago", age);
  else               snprintf(buf, sizeof(buf), "never");
  fb.print(buf);
}

// ---- Draw: joystick panel (crosshair + numeric readout per stick) ----

void drawStickBox(int cx, int cy, int size, float nx, float ny, uint16_t dotCol, const char *label, const char *sub, uint16_t subCol) {
  int half = size / 2;
  fb.drawRect(cx - half, cy - half, size, size, C_DIM);
  fb.drawFastHLine(cx - half, cy, size, C_DIM);
  fb.drawFastVLine(cx, cy - half, size, C_DIM);

  int dotX = cx + (int)(nx * (half - 4));
  int dotY = cy - (int)(ny * (half - 4)); // screen Y is inverted vs "up = positive"
  fb.fillCircle(dotX, dotY, 4, dotCol);

  fb.setTextSize(1);
  fb.setTextColor(C_MID, C_BG);
  int16_t w = fb.textWidth(label);
  fb.setCursor(cx - w / 2, cy + half + 4);
  fb.print(label);

  fb.setTextColor(subCol, C_BG);
  w = fb.textWidth(sub);
  fb.setCursor(cx - w / 2, cy + half + 16);
  fb.print(sub);
}

void drawSticksPanel(float mx, float my, float ax, float ay, unsigned long now) {
  const int boxSize = 76;
  const int y = HDR_H + 14 + boxSize / 2;
  const int x1 = DIV_X + 12 + boxSize / 2;
  const int x2 = screenW - 12 - boxSize / 2;

  char driveBuf[20];
  snprintf(driveBuf, sizeof(driveBuf), "%4d,%4d", (int)(mx * 100), (int)(my * 100));
  drawStickBox(x1, y, boxSize, mx, my, C_CYAN, "DRIVE", driveBuf, C_WHITE);

  bool turnHudActive = now < turnSignalHudUntilMs;
  const char *lightLabel;
  uint16_t lightCol;
  if (turnHudActive) {
    lightLabel = (lastLightCmd == LIGHT_CMD_TURN_LEFT) ? "L SIGNAL" : "R SIGNAL";
    lightCol = C_ORANGE;
  } else {
    lightLabel = headlightsOn ? "CMD: ON" : "CMD: OFF";
    lightCol = headlightsOn ? C_YELLOW : C_MID;
  }
  drawStickBox(x2, y, boxSize, ax, ay, turnHudActive ? C_ORANGE : (headlightsOn ? C_YELLOW : C_CYAN),
               "LIGHTS", lightLabel, lightCol);

  // Footer: raw packet stats
  char buf[40];
  fb.setTextSize(1);
  fb.setTextColor(C_MID, C_BG);
  fb.setCursor(DIV_X + 8, screenH - 30);
  snprintf(buf, sizeof(buf), "DRIVE seq %lu", (unsigned long)drivePkt.seq);
  fb.print(buf);

  fb.setCursor(DIV_X + 8, screenH - 16);
  snprintf(buf, sizeof(buf), "LIGHT seq %lu (%s)", (unsigned long)lightSeq, lightLabel);
  fb.print(buf);
}

// ---- Setup / loop ----

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n--- Dual Analog Stick Controller ---");

  pinMode(J1_SW, INPUT_PULLUP);
  pinMode(J2_SW, INPUT_PULLUP);

  tft.init();
  tft.setRotation(1); // landscape
  tft.fillScreen(C_BG);
  screenW = tft.width();
  screenH = tft.height();

  fb.setColorDepth(16);
  if (fb.createSprite(screenW, screenH) == nullptr) {
    Serial.println("16bpp sprite alloc failed, retrying at 8bpp...");
    fb.setColorDepth(8);
    if (fb.createSprite(screenW, screenH) == nullptr) {
      Serial.println("8bpp sprite alloc also failed - halting.");
      tft.setCursor(0, 0);
      tft.setTextColor(TFT_RED);
      tft.println("Sprite alloc failed");
      while (true) delay(1000);
    }
  }

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setChannel(1);

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
  }
  esp_now_register_recv_cb(onDataRecv);

  esp_now_peer_info_t peer{};
  peer.channel = 1;
  peer.encrypt = false;
  memcpy(peer.peer_addr, motorMac, 6);
  esp_now_add_peer(&peer);
  memcpy(peer.peer_addr, relayMac, 6);
  esp_now_add_peer(&peer);

  drawFrame();
  fb.pushSprite(0, 0);
}

unsigned long lastDriveSendMs = 0;
unsigned long lastDrawMs = 0;

void loop() {
  unsigned long now = millis();

  int rawMX = analogRead(J1_VRX);
  int rawMY = analogRead(J1_VRY);
  float mx = -readAxisNormalized(J1_VRX);
  float my = -readAxisNormalized(J1_VRY);
  float ax = readAxisNormalized(J2_VRX);
  float ay = readAxisNormalized(J2_VRY);

  // Left stick -> drive, continuous stream
  if (now - lastDriveSendMs >= DRIVE_SEND_INTERVAL_MS) {
    lastDriveSendMs = now;
    drivePkt.x = (uint16_t)rawMX;
    drivePkt.y = (uint16_t)rawMY;
    drivePkt.seq++;
    drivePkt.ms = now;
    esp_now_send(motorMac, (uint8_t*)&drivePkt, sizeof(drivePkt));
  }

  // Right stick -> lights, directional flick switch. Forward/back latch the
  // headlights on/off; left/right fire a momentary turn signal. Edge-
  // triggered: the stick must fall back within FLICK_RELEASE of center
  // before the next flick can arm, so holding a direction doesn't
  // repeat-fire.
  float aimMag = sqrtf(ax * ax + ay * ay);
  if (!flickArmed) {
    if (aimMag <= FLICK_RELEASE) flickArmed = true;
  } else if (fabsf(ay) >= fabsf(ax) && fabsf(ay) >= FLICK_THRESHOLD) {
    bool wantOn = ay > 0; // forward = on, back = off
    if (wantOn != headlightsOn) {
      headlightsOn = wantOn;
      lastLightCmd = wantOn ? LIGHT_CMD_ON : LIGHT_CMD_OFF;
      lightSeq++;
      lightBurstRemaining = LIGHT_BURST_COUNT;
      lastLightBurstMs = 0; // fire the first burst packet immediately
    }
    flickArmed = false;
  } else if (fabsf(ax) > fabsf(ay) && fabsf(ax) >= FLICK_THRESHOLD) {
    lastLightCmd = (ax > 0) ? LIGHT_CMD_TURN_RIGHT : LIGHT_CMD_TURN_LEFT;
    lightSeq++;
    lightBurstRemaining = LIGHT_BURST_COUNT;
    lastLightBurstMs = 0;
    turnSignalHudUntilMs = now + TURN_SIGNAL_MS;
    flickArmed = false;
  }
  if (lightBurstRemaining > 0 && (now - lastLightBurstMs >= LIGHT_BURST_GAP_MS)) {
    lastLightBurstMs = now;
    LightPacket lp{ lastLightCmd, lightSeq };
    esp_now_send(relayMac, (uint8_t*)&lp, sizeof(lp));
    lightBurstRemaining--;
  }

  if (now - lastDrawMs >= DRAW_INTERVAL_MS) {
    lastDrawMs = now;
    drawFrame();
    drawStatusPanel();
    drawSticksPanel(mx, my, ax, ay, now);
    fb.pushSprite(0, 0);
  }
}
