// ===== Single Analog Stick Controller: ESP-NOW, Arcade-Drive Mixing =====
//
// Replaces single_analog_stick_espnow.ino. That version sent raw joystick
// x/y and let the receiver do arcade-drive mixing; the receiver
// (motor_receiver.ino) no longer mixes -- it just ramps and applies
// whatever left/right PWM it's handed. So the mixing has moved here: this
// sketch reads the stick, computes left/right motor PWM itself, and sends
// a DrivePacket{left, right} straight to motor_receiver.ino.
//
// Pin mapping confirmed via single_analog_stick_debug.ino:
//   joyX (pin 34): 0 = left,     4095 = right
//   joyY (pin 35): 0 = backward, 4095 = forward
//   switch (pin 33): joystick's built-in click button

#include <WiFi.h>
#include <esp_now.h>

// uint8_t robotMac[] = {0xC0, 0xCD, 0xD6, 0xCA, 0x0E, 0x4C};
uint8_t robotMac[] = {0xD4, 0xE9, 0xF4, 0xE5, 0xF6, 0x58};

// const int joyX      = 35;
// const int joyY      = 34;
const int joyX      = 34;
const int joyY      = 35;
const int switchPin = 33;  // joystick click button -- cycles speed

const uint32_t SEND_INTERVAL_MS = 5;
const uint32_t DEBOUNCE_MS      = 50;
const int      JOY_DEADBAND     = 40;  // raw ADC counts, out of a ~2048 half-range

typedef struct __attribute__((packed)) {
  int16_t left;   // -255..255
  int16_t right;  // -255..255
} DrivePacket;

DrivePacket pkt{};
uint32_t lastSend = 0;

int centerX = 2048;
int centerY = 2048;

// Speed levels: 25%, 50%, 75%, 100%
const float speedLevels[] = {0.25f, 0.50f, 0.75f, 1.00f};
const char* speedNames[]  = {"25%", "50%", "75%", "FULL"};
const int numLevels       = 4;
int speedIndex            = 0;  // start at 25%

bool lastSpeedBtn     = HIGH;
bool stableBtn        = HIGH;
uint32_t lastDebounce = 0;

void setup() {
  Serial.begin(115200);

  pinMode(joyX,      INPUT);
  pinMode(joyY,      INPUT);
  pinMode(switchPin, INPUT_PULLUP);

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
  bool reading = digitalRead(switchPin);
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

  // --- Read, mix, send ---
  if (now - lastSend >= SEND_INTERVAL_MS) {
    lastSend = now;

    int dx = analogRead(joyX) - centerX;  // + = right
    int dy = analogRead(joyY) - centerY;  // + = forward
    if (abs(dx) <= JOY_DEADBAND) dx = 0;
    if (abs(dy) <= JOY_DEADBAND) dy = 0;

    float turn     = constrain(dx / 2048.0f, -1.0f, 1.0f);
    float throttle = constrain(dy / 2048.0f, -1.0f, 1.0f);

    // Arcade-drive mixing.
    float left  = throttle + turn;
    float right = throttle - turn;

    // Preserve the throttle/turn ratio instead of clipping it.
    float mag = max(1.0f, max(fabsf(left), fabsf(right)));
    left  /= mag;
    right /= mag;

    float scale = speedLevels[speedIndex];
    pkt.left  = (int16_t)constrain((int)(left  * scale * 255), -255, 255);
    pkt.right = (int16_t)constrain((int)(right * scale * 255), -255, 255);

    esp_now_send(robotMac, (uint8_t*)&pkt, sizeof(pkt));
  }
}
