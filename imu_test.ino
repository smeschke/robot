#include <Wire.h>
#include <MPU6050_light.h>

MPU6050 mpu(Wire);

void setup() {
  Serial.begin(115200);
  Wire.begin();
  mpu.begin();
  mpu.calcOffsets(); // calibrate while board is still
}

void loop() {
  mpu.update();
  Serial.print("Angle X: "); Serial.print(mpu.getAngleX());
  Serial.print("  Y: "); Serial.print(mpu.getAngleY());
  Serial.print("  Z: "); Serial.println(mpu.getAngleZ());
  delay(500);
}
