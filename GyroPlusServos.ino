#include <Wire.h>
#include <math.h>

#define MPU_ADDR 0x68

// ====== IMU ======
int16_t accelX, accelY, accelZ;
int16_t gyroX, gyroY, gyroZ;

float Ax, Ay, Az;
float Gx, Gy, Gz;

float pitch = 0, roll = 0;

// gyro offsets
float gx_off = 0, gy_off = 0, gz_off = 0;

// timing
unsigned long prevTime;
float dt;

// filter
float alpha = 0.96;

// ====== CONTROL ======
float Kp = 3.0;
float Kd = 0.8;

// ====== PWM ======
void setupPWM() {
  pinMode(9, OUTPUT);   // pitch servo
  pinMode(10, OUTPUT);  // roll servo

  TCCR1A = (1 << COM1A1) | (1 << COM1B1) | (1 << WGM11);
  TCCR1B = (1 << WGM13) | (1 << WGM12) | (1 << CS11); // prescaler 8

  ICR1 = 40000; // 20ms period (50Hz)
}

void setServoA(float pulse_us) {
  OCR1A = pulse_us * 2; // 0.5us resolution
}

void setServoB(float pulse_us) {
  OCR1B = pulse_us * 2;
}

// ====== I2C LOW LEVEL ======
void writeReg(uint8_t reg, uint8_t data) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(reg);
  Wire.write(data);
  Wire.endTransmission();
}

void readMPU() {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x3B);
  Wire.endTransmission(false);
  Wire.requestFrom(MPU_ADDR, 14);

  accelX = Wire.read() << 8 | Wire.read();
  accelY = Wire.read() << 8 | Wire.read();
  accelZ = Wire.read() << 8 | Wire.read();

  Wire.read(); Wire.read(); // temp

  gyroX = Wire.read() << 8 | Wire.read();
  gyroY = Wire.read() << 8 | Wire.read();
  gyroZ = Wire.read() << 8 | Wire.read();
}

// ====== CALIBRATION ======
void calibrateGyro() {
  Serial.println("Calibrating... keep still");

  const int samples = 500;
  delay(1);

  for (int i = 0; i < samples; i++) {
    readMPU();
    gx_off += gyroX;
    gy_off += gyroY;
    gz_off += gyroZ;
    delay(2);
  }

  gx_off /= samples;
  gy_off /= samples;
  gz_off /= samples;

  Serial.println("Done");
}

// ====== SETUP ======
void setup() {
  Wire.begin();
  Wire.setTimeout(50);  // 50 ms max wait
  Serial.begin(115200);

  setupPWM();

  writeReg(0x6B, 0x00); // wake MPU

  delay(100);

  calibrateGyro();

  prevTime = millis();
}

// ====== LOOP ======
void loop() {
  // timing
  unsigned long now = millis();
  dt = (now - prevTime) * 0.001f;
  prevTime = now;

  // read IMU
  readMPU();

  // scale
  Ax = accelX / 16384.0f;
  Ay = accelY / 16384.0f;
  Az = accelZ / 16384.0f;

  Gx = (gyroX - gx_off) / 131.0f;
  Gy = (gyroY - gy_off) / 131.0f;
  Gz = (gyroZ - gz_off) / 131.0f;

  // accel angles
  float accelPitch = atan2(Ay, sqrt(Ax*Ax + Az*Az)) * 57.2958f;
  float accelRoll  = atan2(-Ax, Az) * 57.2958f;

  // integrate gyro
  pitch += Gx * dt;
  roll  += Gy * dt;

  // complementary filter
  pitch = alpha * pitch + (1 - alpha) * accelPitch;
  roll  = alpha * roll  + (1 - alpha) * accelRoll;

  // ====== CONTROL ======
  float pitch_error = -pitch;
  float roll_error  = -roll;

  // PD control
  float pitch_cmd = Kp * pitch_error - Kd * Gx;
  float roll_cmd  = Kp * roll_error  - Kd * Gy;

  // convert directly to pulse (no angle conversion)
  float pulse_pitch = 1350 + pitch_cmd * 10;
  float pulse_roll  = 850 + roll_cmd  * 10;

  // clamp (tune these!)
  if (pulse_pitch > 1900) pulse_pitch = 1900;
  if (pulse_pitch < 1000) pulse_pitch = 1000;

  if (pulse_roll > 1200) pulse_roll = 1200;
  if (pulse_roll < 600) pulse_roll = 600;

  // output
  setServoA(pulse_pitch);
  setServoB(pulse_roll);

  // debug
  Serial.print("P: "); Serial.print(pitch);
  Serial.print(" R: "); Serial.print(roll);
  Serial.print(" | PWM: ");
  Serial.print(pulse_pitch);
  Serial.print(", ");
  Serial.println(pulse_roll);

  delay(10);
}