// JSN-SR04T triple-sensor test (Stephen's pin setup)
// Sensor1: Trig=7, Echo=8
// Sensor2: Trig=4, Echo=3
// Sensor3: Trig=12, Echo=13

#define TRIG1 7
#define ECHO1 8
#define TRIG2 4
#define ECHO2 3
#define TRIG3 12
#define ECHO3 13

// ---- measure one sensor, return -1 if no valid echo ----
long readDistanceCM(int trigPin, int echoPin) {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(3);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  unsigned long duration = pulseIn(echoPin, HIGH, 40000UL); // 40 ms timeout (~6.8 m)
  if (duration == 0) return -1;   // no echo
  long distance = duration * 0.0343 / 2; // cm
  if (distance > 200) return -1;  // filter out >4m
  return distance;
}

// ---- get a stable reading by taking 3 samples and using the median ----
long getStableDistance(int trigPin, int echoPin) {
  long vals[3];
  for (int i = 0; i < 3; i++) {
    vals[i] = readDistanceCM(trigPin, echoPin);
    delay(50); // small gap between samples
  }
  // simple bubble sort for 3 values
  for (int i = 0; i < 2; i++) {
    for (int j = i + 1; j < 3; j++) {
      if (vals[j] < vals[i]) {
        long temp = vals[i];
        vals[i] = vals[j];
        vals[j] = temp;
      }
    }
  }
  return vals[1]; // median
}

void setup() {
  Serial.begin(115200);
  pinMode(TRIG1, OUTPUT); pinMode(ECHO1, INPUT);
  pinMode(TRIG2, OUTPUT); pinMode(ECHO2, INPUT);
  pinMode(TRIG3, OUTPUT); pinMode(ECHO3, INPUT);
}

void loop() {
  long d1 = getStableDistance(TRIG1, ECHO1);
  delay(234);
  long d2 = getStableDistance(TRIG2, ECHO2);
  delay(234);
  long d3 = getStableDistance(TRIG3, ECHO3);
  delay(234);

  Serial.print("S1: ");
  if (d1 == -1) Serial.print("----"); else Serial.print(d1);
  Serial.print(" cm\tS2: ");
  if (d2 == -1) Serial.print("----"); else Serial.print(d2);
  Serial.print(" cm\tS3: ");
  if (d3 == -1) Serial.print("----"); else Serial.print(d3);
  Serial.println(" cm");
}
