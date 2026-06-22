// ===== Joystick Sender: ESP-NOW + Speed Button + Cool LEDs =====
// LED behavior:
//   - Idle (stick centered): smooth red<->blue cross-fade "breathing".
//                            Breathe speeds up at higher speed settings.
//   - Driving (stick pushed): fast alternating red/blue blink = transmitting.
//   - Speed change:           quick both-LED strobe burst as confirmation.
// All animation is non-blocking (millis-based) so it never stalls the 5ms
// send loop. Brightness uses LEDC PWM.
// NOTE: Arduino-ESP32 core 3.x API (ledcAttach/ledcWrite). If it won't
// compile on an older core, say so and I'll swap to the ledcSetup channel API.

#include <WiFi.h>
#include <esp_now.h>

uint8_t robotMac[] = {0xC0, 0xCD, 0xD6, 0xCA, 0x0E, 0x4C};

const int joyX        = 35;
const int joyY        = 34;
const int buttonPin   = 33;  // joystick button — cycles speed

// ---- LEDs: PWM-capable output pins, ~330R resistor in series each ----
const int LED_BLUE    = 2;   // onboard LED on most DevKit V1 boards
const int LED_RED     = 4;   // external red LED
const int LEDC_FREQ   = 5000;
const int LEDC_RES    = 8;   // 8-bit duty: 0..255

const uint32_t SEND_INTERVAL_MS   = 5;
const uint32_t DEBOUNCE_MS        = 50;
const int      JOY_DEADBAND       = 40;
const uint32_t LED_INTERVAL_MS    = 16;  // ~60 fps LED refresh

typedef struct __attribute__((packed)) {
  uint16_t x;
  uint16_t y;
  uint32_t seq;
  uint32_t ms;
} JoyPacket;

JoyPacket pkt{};
uint32_t lastSend = 0;
uint32_t lastLed  = 0;

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

// ---- LED animation state ----
bool     stickActive       = false;  // joystick pushed past deadband
uint32_t flashUntil        = 0;      // speed-change confirmation flash window

// Gamma-correct a 0..1 brightness so the fade looks smooth to the eye.
uint8_t gamma8(float v) {
  v = constrain(v, 0.0f, 1.0f);
  return (uint8_t)(v * v * 255.0f + 0.5f);
}

void updateLeds(uint32_t now) {
  // 1) Speed-change flash (highest priority): both strobe together.
  if ((int32_t)(flashUntil - now) > 0) {
    bool on = (now / 60) & 1;
    ledcWrite(LED_RED,  on ? 255 : 0);
    ledcWrite(LED_BLUE, on ? 255 : 0);
    return;
  }

  // 2) Driving: fast alternating red/blue.
  if (stickActive) {
    bool on = (now / 80) & 1;
    ledcWrite(LED_RED,  on ? 255 : 0);
    ledcWrite(LED_BLUE, on ? 0   : 255);
    return;
  }

  // 3) Idle: smooth red<->blue cross-fade. Faster at higher speed setting.
  uint32_t period = 2600 - (uint32_t)speedIndex * 500;  // 2600..1100 ms
  float phase = (now % period) / (float)period;         // 0..1
  float s = (sinf(phase * 2.0f * PI) + 1.0f) * 0.5f;    // 0..1
  ledcWrite(LED_RED,  gamma8(s));
  ledcWrite(LED_BLUE, gamma8(1.0f - s));
}

void setup() {
  Serial.begin(115200);

  pinMode(joyX,      INPUT);
  pinMode(joyY,      INPUT);
  pinMode(buttonPin, INPUT_PULLUP);

  ledcAttach(LED_RED,  LEDC_FREQ, LEDC_RES);
  ledcAttach(LED_BLUE, LEDC_FREQ, LEDC_RES);

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

  // Boot sweep so you know it powered up.
  for (int i = 0; i <= 255; i += 5) {
    ledcWrite(LED_BLUE, i);
    ledcWrite(LED_RED, 255 - i);
    delay(4);
  }
  ledcWrite(LED_RED, 0);
  ledcWrite(LED_BLUE, 0);

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
      flashUntil = now + 400;           // trigger confirmation strobe
      Serial.print("Speed: ");
      Serial.println(speedNames[speedIndex]);
    }
    stableBtn = reading;
  }

  // --- Send packet ---
  if (now - lastSend >= SEND_INTERVAL_MS) {
    lastSend = now;

    float scale = speedLevels[speedIndex];

    int rawX = (analogRead(joyX) - centerX);
    int rawY = -(analogRead(joyY) - centerY);
    if (abs(rawX) <= JOY_DEADBAND) rawX = 0;
    if (abs(rawY) <= JOY_DEADBAND) rawY = 0;

    stickActive = (rawX != 0 || rawY != 0);

    pkt.x = (uint16_t)constrain((int)(rawX * scale) + 2048, 0, 4095);
    pkt.y = (uint16_t)constrain((int)(rawY * scale) + 2048, 0, 4095);

    pkt.seq++;
    pkt.ms = now;

    esp_now_send(robotMac, (uint8_t*)&pkt, sizeof(pkt));
  }

  // --- Animate LEDs (~60 fps, non-blocking) ---
  if (now - lastLed >= LED_INTERVAL_MS) {
    lastLed = now;
    updateLeds(now);
  }
}
