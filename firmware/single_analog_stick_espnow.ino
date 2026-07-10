// ===== Joystick Sender: ESP-NOW with Speed Button =====
#include <WiFi.h>
#include <esp_now.h>

uint8_t robotMac[] = {0xC0, 0xCD, 0xD6, 0xCA, 0x0E, 0x4C};

const int joyX        = 34;
const int joyY        = 35;
const int buttonPin   = 32;  // joystick button — cycles speed

const uint32_t SEND_INTERVAL_MS   = 5;
const uint32_t DEBOUNCE_MS        = 50;
const int      JOY_DEADBAND       = 40;

typedef struct __attribute__((packed)) {
  uint16_t x;
  uint16_t y;
  uint32_t seq;
  uint32_t ms;
} JoyPacket;

JoyPacket pkt{};
uint32_t lastSend = 0;

int centerX = 2048;
int centerY = 2048;

// Speed levels: 25%, 50%, 75%, 100%
const float speedLevels[]  = {0.25f, 0.50f, 0.75f, 1.00f};
const char* speedNames[]   = {"25%", "50%", "75%", "FULL"};
const int numLevels        = 4;
int speedIndex             = 0;  // start at 25%

bool lastSpeedBtn          = HIGH;
bool stableBtn             = HIGH;
uint32_t lastDebounce      = 0;

void setup() {
  Serial.begin(115200);

  pinMode(joyX,      INPUT);
  pinMode(joyY,      INPUT);
  pinMode(buttonPin, INPUT_PULLUP);

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
  Serial.print("Speed: ");
  Serial.println(speedNames[speedIndex]);
}

void loop() {
  uint32_t now = millis();

  // --- Joystick button cycles speed (active LOW, debounced) ---
  bool reading = digitalRead(buttonPin);
  if (reading != lastSpeedBtn) {
    lastDebounce = now;
  }
  lastSpeedBtn = reading;
  if ((now - lastDebounce) >= DEBOUNCE_MS) {
    if (stableBtn == HIGH && reading == LOW) {
      // falling edge confirmed after debounce
      speedIndex = (speedIndex + 1) % numLevels;
      Serial.print("Speed: ");
      Serial.println(speedNames[speedIndex]);
    }
    stableBtn = reading;
  }

  // --- Send packet ---
  if (now - lastSend >= SEND_INTERVAL_MS) {
    lastSend = now;

    float scale = speedLevels[speedIndex];

    int rawX = -(analogRead(joyX) - centerX);
    int rawY = -(analogRead(joyY) - centerY);
    if (abs(rawX) <= JOY_DEADBAND) rawX = 0;
    if (abs(rawY) <= JOY_DEADBAND) rawY = 0;

    pkt.x = (uint16_t)constrain((int)(rawX * scale) + 2048, 0, 4095);
    pkt.y = (uint16_t)constrain((int)(rawY * scale) + 2048, 0, 4095);

    pkt.seq++;
    pkt.ms = now;

    esp_now_send(robotMac, (uint8_t*)&pkt, sizeof(pkt));
  }
}
