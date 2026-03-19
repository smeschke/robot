// ===== Orin Bridge: Serial → ESP-NOW =====
// Receives joystick commands from Orin NX over Serial,
// forwards them as ESP-NOW packets to the robot.
//
// Serial protocol (from Orin): "x,y,b\n"
//   x, y : 0–4095  (2048 = center/neutral)
//   b    : 0 or 1  (speed-cycle button)
//
// Watchdog: if no valid packet received within JOY_TIMEOUT_MS,
// sends neutral (2048, 2048, 0) to stop the robot.

#include <WiFi.h>
#include <esp_now.h>

uint8_t robotMac[] = {0xC0, 0xCD, 0xD6, 0xCA, 0x0E, 0x4C};

const uint32_t SEND_INTERVAL_MS = 5;
const uint32_t JOY_TIMEOUT_MS   = 300;   // stop if Orin goes silent

typedef struct __attribute__((packed)) {
  uint16_t x;
  uint16_t y;
  bool     button;
  uint32_t seq;
  uint32_t ms;
} JoyPacket;

JoyPacket pkt{};
uint32_t lastSend    = 0;
uint32_t lastCmdMs   = 0;   // when we last got a valid line from Orin
bool     timedOut    = false;

String serialBuf;

// ---- helpers ----
void sendNeutral() {
  pkt.x      = 2048;
  pkt.y      = 2048;
  pkt.button = false;
}

bool parseLine(const String& line) {
  // expect "x,y,b"
  int c1 = line.indexOf(',');
  if (c1 < 0) return false;
  int c2 = line.indexOf(',', c1 + 1);
  if (c2 < 0) return false;

  int xv = line.substring(0, c1).toInt();
  int yv = line.substring(c1 + 1, c2).toInt();
  int bv = line.substring(c2 + 1).toInt();

  pkt.x      = (uint16_t)constrain(xv, 0, 4095);
  pkt.y      = (uint16_t)constrain(yv, 0, 4095);
  pkt.button = (bv != 0);
  return true;
}

void setup() {
  Serial.begin(115200);
  serialBuf.reserve(32);

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setChannel(1);

  esp_now_init();

  esp_now_peer_info_t peer{};
  memcpy(peer.peer_addr, robotMac, 6);
  peer.channel = 1;
  peer.encrypt = false;
  esp_now_add_peer(&peer);

  sendNeutral();
  lastCmdMs = millis();

  Serial.println("Orin Bridge Ready.");
}

void loop() {
  uint32_t now = millis();

  // ---- read serial lines from Orin ----
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      if (serialBuf.length() > 0) {
        if (parseLine(serialBuf)) {
          lastCmdMs = now;
          timedOut  = false;
        }
        serialBuf = "";
      }
    } else {
      serialBuf += c;
      if (serialBuf.length() > 32) serialBuf = "";  // overflow guard
    }
  }

  // ---- watchdog: Orin silent → send neutral ----
  if (!timedOut && (now - lastCmdMs > JOY_TIMEOUT_MS)) {
    timedOut = true;
    sendNeutral();
    Serial.println("WARN: Orin timeout — sending neutral");
  }

  // ---- send ESP-NOW at fixed rate ----
  if (now - lastSend >= SEND_INTERVAL_MS) {
    lastSend = now;
    pkt.seq++;
    pkt.ms = now;
    esp_now_send(robotMac, (uint8_t*)&pkt, sizeof(pkt));
  }
}
