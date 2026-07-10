/*
  ESP32 — Relay Receiver (lights only)
  (shared_protocol.md / dual_analog_stick.ino: "Relay receiver (robot)")

  Lives on the robot alongside a separate motor-control ESP32. This board
  does nothing but listen for LightPacket events from the dual-analog-stick
  controller and drive two relays:

    GPIO25 -> LEFT  light relay
    GPIO26 -> RIGHT light relay

  Command mapping (LightPacket.cmd, matches dual_analog_stick.ino):
    LIGHT_CMD_OFF        -> both relays off (headlights off)
    LIGHT_CMD_ON         -> both relays on  (headlights on)
    LIGHT_CMD_TURN_LEFT  -> LEFT relay forced on for TURN_SIGNAL_MS, then
                             reverts to the headlight state
    LIGHT_CMD_TURN_RIGHT -> RIGHT relay forced on for TURN_SIGNAL_MS, then
                             reverts to the headlight state
  A turn signal on one side never touches the other side's relay.

  Wiring: this assumes a common active-LOW relay module (module ON when the
  GPIO is driven LOW), which is the norm for cheap 2-channel relay boards.
  If your module is active-HIGH instead, flip RELAY_ACTIVE_LOW to false.

  Packet typing follows the shared protocol: no header byte, receivers tell
  packets apart purely by length. This board only cares about LightPacket
  (5 bytes) and ignores everything else (e.g. the 12-byte JoyPacket destined
  for the motor board).

  Sends a HeartbeatPacket back to the last-seen controller every ~500ms
  (RSSI, relay state, uptime) so the dual-stick HUD's LIGHTS panel has
  something to show. The motor board sends its own heartbeat for
  drive-side telemetry; the controller doesn't distinguish which board a
  heartbeat came from, per the shared protocol.

  First packet heard from a given controller MAC is auto-registered as a
  peer, same convention as robot_receiver.ino. Print this board's MAC on
  boot and put that address into the controller's relayMac[].
*/

#include <WiFi.h>
#include <esp_now.h>

// ---- Relay pins ----
#define RELAY_LEFT_PIN  25
#define RELAY_RIGHT_PIN 26
#define RELAY_ACTIVE_LOW true // flip to false for an active-HIGH relay module

// ---- Wire format (must match dual_analog_stick.ino / shared_protocol.md) ----

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
  int8_t   rssi;       // this board's RSSI reading of the controller, dBm
  uint8_t  relayState; // 0 = off, 1 = on (headlight baseline, either relay)
  uint32_t uptimeMs;
  uint32_t seq;
} HeartbeatPacket;     // 10 bytes -- robot -> controller, ~every 500ms

const uint32_t HEARTBEAT_INTERVAL_MS = 500;
const uint32_t TURN_SIGNAL_MS        = 2000; // must match dual_analog_stick.ino

// ---- State ----

bool headlightsOn = false; // latched baseline, both sides
bool turnLeftActive  = false;
bool turnRightActive = false;
unsigned long turnLeftOffAtMs  = 0;
unsigned long turnRightOffAtMs = 0;
uint32_t lastLightSeq = 0;

uint8_t controllerMac[6] = {0};
bool haveController = false;
int8_t lastRssi = 0;
uint32_t hbSeq = 0;
unsigned long lastHeartbeatMs = 0;

void setLeftRelay(bool on) {
  digitalWrite(RELAY_LEFT_PIN, (on != RELAY_ACTIVE_LOW) ? HIGH : LOW);
}
void setRightRelay(bool on) {
  digitalWrite(RELAY_RIGHT_PIN, (on != RELAY_ACTIVE_LOW) ? HIGH : LOW);
}

void applyRelays() {
  setLeftRelay(turnLeftActive || headlightsOn);
  setRightRelay(turnRightActive || headlightsOn);
}

void ensurePeer(const uint8_t *mac) {
  if (esp_now_is_peer_exist(mac)) return;
  esp_now_peer_info_t peer{};
  memcpy(peer.peer_addr, mac, 6);
  peer.channel = 1;
  peer.encrypt = false;
  esp_now_add_peer(&peer);
}

void rememberController(const uint8_t *mac, int8_t rssi) {
  bool isNew = !haveController || memcmp(controllerMac, mac, 6) != 0;
  memcpy(controllerMac, mac, 6);
  haveController = true;
  lastRssi = rssi;
  ensurePeer(mac);

  if (isNew) {
    Serial.printf("Controller linked: %02X:%02X:%02X:%02X:%02X:%02X\n",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  }
}

void onRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  if (len != sizeof(LightPacket)) return; // not for us (e.g. JoyPacket)

  LightPacket pkt;
  memcpy(&pkt, data, sizeof(pkt));
  int8_t rssi = (info->rx_ctrl != nullptr) ? info->rx_ctrl->rssi : 0;
  rememberController(info->src_addr, rssi);

  bool isNewEvent = (pkt.seq != lastLightSeq);
  lastLightSeq = pkt.seq;
  if (!isNewEvent) return; // duplicate resend from the controller's burst

  unsigned long now = millis();
  switch (pkt.cmd) {
    case LIGHT_CMD_OFF:
      headlightsOn = false;
      Serial.println("[LIGHT] headlights OFF");
      break;
    case LIGHT_CMD_ON:
      headlightsOn = true;
      Serial.println("[LIGHT] headlights ON");
      break;
    case LIGHT_CMD_TURN_LEFT:
      turnLeftActive = true;
      turnLeftOffAtMs = now + TURN_SIGNAL_MS;
      Serial.println("[LIGHT] LEFT turn signal");
      break;
    case LIGHT_CMD_TURN_RIGHT:
      turnRightActive = true;
      turnRightOffAtMs = now + TURN_SIGNAL_MS;
      Serial.println("[LIGHT] RIGHT turn signal");
      break;
    default:
      Serial.printf("[WARN] unknown light cmd: %d\n", pkt.cmd);
      return;
  }
  applyRelays();
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n--- Relay Receiver (lights) ---");

  pinMode(RELAY_LEFT_PIN, OUTPUT);
  pinMode(RELAY_RIGHT_PIN, OUTPUT);
  applyRelays(); // both off at boot

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setChannel(1);

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
  }
  esp_now_register_recv_cb(onRecv);

  Serial.print("Relay receiver MAC: ");
  Serial.println(WiFi.macAddress());
  Serial.println("Waiting for a controller...");
}

void loop() {
  unsigned long now = millis();

  if (turnLeftActive && (long)(now - turnLeftOffAtMs) >= 0) {
    turnLeftActive = false;
    applyRelays();
  }
  if (turnRightActive && (long)(now - turnRightOffAtMs) >= 0) {
    turnRightActive = false;
    applyRelays();
  }

  if (haveController && (now - lastHeartbeatMs >= HEARTBEAT_INTERVAL_MS)) {
    lastHeartbeatMs = now;
    HeartbeatPacket hb;
    hb.rssi       = lastRssi;
    hb.relayState = headlightsOn ? 1 : 0;
    hb.uptimeMs   = now;
    hb.seq        = ++hbSeq;
    esp_now_send(controllerMac, (uint8_t*)&hb, sizeof(hb));
  }
}
