// ===== Joystick Sender: ESP-NOW with Speed Button =====
#include <WiFi.h>
#include <esp_now.h>

uint8_t robotMac[] = {0xC0, 0xCD, 0xD6, 0xCA, 0x0E, 0x4C};

const int joyX = 34;
const int joyY = 35;
const int buttonPin = 32;   // <--- your joystick button

const uint32_t SEND_INTERVAL_MS = 5;

typedef struct __attribute__((packed)) {
  uint16_t x;
  uint16_t y;
  bool button;
  uint32_t seq;
  uint32_t ms;
} JoyPacket;

JoyPacket pkt{};
uint32_t lastSend = 0;

int centerX = 2048;
int centerY = 2048;

void setup() {
  Serial.begin(115200);

  pinMode(joyX, INPUT);
  pinMode(joyY, INPUT);
  pinMode(buttonPin, INPUT_PULLUP);  // button to GND

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setChannel(1);

  esp_now_init();

  esp_now_peer_info_t peer{};
  memcpy(peer.peer_addr, robotMac, 6);
  peer.channel = 1;
  peer.encrypt = false;
  esp_now_add_peer(&peer);

  centerX = analogRead(joyX);
  centerY = analogRead(joyY);
  Serial.println("Joystick Ready.");
}

void loop() {
  uint32_t now = millis();

  if (now - lastSend >= SEND_INTERVAL_MS) {
    lastSend = now;

    pkt.x = (uint16_t)constrain(analogRead(joyX) - centerX + 2048, 0, 4095);
    pkt.y = (uint16_t)constrain(analogRead(joyY) - centerY + 2048, 0, 4095);

    // Active LOW button
    pkt.button = (digitalRead(buttonPin) == LOW);

    pkt.seq++;
    pkt.ms = now;

    esp_now_send(robotMac, (uint8_t*)&pkt, sizeof(pkt));
  }
}
