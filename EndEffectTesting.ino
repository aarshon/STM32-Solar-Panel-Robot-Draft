#include <Wire.h>
#include <math.h>
#include <HX711.h>

// ================================================================
// PIN DEFINITIONS
// ================================================================
#define MPU_ADDR          0x68

#define PIN_SERVO_PITCH    9
#define PIN_SERVO_ROLL    10
#define PIN_SERVO_YAW     11

#define PIN_TORQUE_BTN     2    // INT1
#define PIN_LED            8

#define PIN_HX711_DAT      4
#define PIN_HX711_SCK     3

// ================================================================
// IMU STATE
// ================================================================
int16_t accelX, accelY, accelZ;
int16_t gyroX,  gyroY,  gyroZ;
float   Ax, Ay, Az, Gx, Gy;
float   pitch = 0, roll = 0;
float   gx_off = 0, gy_off = 0, gz_off = 0;
unsigned long prevTime;
float   dt;
float   alpha = 0.92f;

// ================================================================
// OUTPUT SMOOTHING
// ================================================================
#define SMOOTH_SAMPLES 8
float pitchBuf[SMOOTH_SAMPLES] = {0};
float rollBuf[SMOOTH_SAMPLES]  = {0};
int   smoothIdx = 0;
float prev_pulse_pitch = 1350.0f;
float prev_pulse_roll  =  850.0f;

// ================================================================
// PD GAINS
// ================================================================
float Kp = 3.0f;
float Kd = 0.5f;

// ================================================================
// TORQUE TOGGLE
// ================================================================
volatile bool torqueEnabled = true;
volatile bool btnFlag       = false;
unsigned long lastDebounce  = 0;
#define DEBOUNCE_MS 50

void ISR_torqueBtn() { btnFlag = true; }

void disableServoPWM() {
  TCCR1A &= ~((1 << COM1A1) | (1 << COM1B1));
  TCCR2A &= ~(1 << COM2A1);
  digitalWrite(PIN_SERVO_PITCH, LOW);
  digitalWrite(PIN_SERVO_ROLL,  LOW);
  digitalWrite(PIN_SERVO_YAW,   LOW);
}
void enableServoPWM() {
  TCCR1A |= (1 << COM1A1) | (1 << COM1B1);
  TCCR2A |= (1 << COM2A1);
}

// ================================================================
// PWM SETUP
// ================================================================
void setupPWM() {
  pinMode(PIN_SERVO_PITCH, OUTPUT);
  pinMode(PIN_SERVO_ROLL,  OUTPUT);
  pinMode(PIN_SERVO_YAW,   OUTPUT);

  TCCR1A = (1 << COM1A1) | (1 << COM1B1) | (1 << WGM11);
  TCCR1B = (1 << WGM13)  | (1 << WGM12)  | (1 << CS11);
  ICR1   = 40000;

  TCCR2A = (1 << COM2A1) | (1 << WGM20);
  TCCR2B = (1 << CS22) | (1 << CS21) | (1 << CS20);
  OCR2A  = 125;
}

void setServoA(float pulse_us) { OCR1A = (uint16_t)(pulse_us * 2.0f); }
void setServoB(float pulse_us) { OCR1B = (uint16_t)(pulse_us * 2.0f); }

// ================================================================
// LOAD CELL
// ================================================================
HX711 scale;

// Idle noise band: ~2100 to ~3000
// Tension  (operator pushes UP)   = negative, drops BELOW 2100
// Compression (operator pushes DOWN) = positive, rises ABOVE 3000
//
// Thresholds sit well outside noise band to prevent false triggers.
// Tune via Serial: U<val>, L<val>, T<val>
long FORCE_TRIGGER_DOWN    =  2075;  // tension:  
long FORCE_TRIGGER_UP  =  2225;  // compression: 
long FORCE_RESET_LOWER  =  2200;  // return from compression (drop back into band)
long FORCE_RESET_UPPER   =  2100;  // return from tension (rise back into band)
long forceThreshold      =  500;  // delta noise floor (~900 unit idle swing + margin)

// Arm state
// 0 = idle/holding
// 1 = moving up   (tension detected)
// 2 = moving down (compression detected)
int  armState  = 0;
long forcePrev = 0;

// LED blink
unsigned long lastBlink    = 0;
bool          ledState     = false;
const int     blinkInterval = 150;

// ================================================================
// LOAD CELL CALIBRATION
// ================================================================
void calibrateLoadCell() {
  Serial.println(F("\n--- Load Cell Tare ---"));
  Serial.println(F("Taring in 2s..."));
  delay(2000);
  scale.tare();
  forcePrev = scale.read_average(20);
  Serial.print(F("Baseline: "));
  Serial.println(forcePrev);
}

// ================================================================
// STEPPER SIMULATION
// ================================================================
unsigned long lastStepperPrint = 0;
#define STEPPER_PRINT_INTERVAL 200

void simulateSteppers(int direction, long delta) {
  unsigned long now = millis();
  if (now - lastStepperPrint < STEPPER_PRINT_INTERVAL) return;
  lastStepperPrint = now;

  Serial.println(F("\n  ┌─ STEPPER CMD ──────────────────────────┐"));
  if (direction == 1) {
    Serial.println(F("  │ Direction  : UP   (tension detected)   │"));
    Serial.println(F("  │ J2 : STEP x3  DIR=FWD                 │"));
    Serial.println(F("  │ J3 : STEP x3  DIR=REV (opposes J2)    │"));
  } else {
    Serial.println(F("  │ Direction  : DOWN (compression detected)│"));
    Serial.println(F("  │ J2 : STEP x3  DIR=REV                 │"));
    Serial.println(F("  │ J3 : STEP x3  DIR=FWD (opposes J2)    │"));
  }
  Serial.print(  F("  │ Force delta: "));
  Serial.print(delta);
  Serial.println(F("                          │"));
  Serial.println(F("  └────────────────────────────────────────┘"));
}

// ================================================================
// SMOOTHING HELPER
// ================================================================
float movingAvg(float* buf, float newVal) {
  buf[smoothIdx % SMOOTH_SAMPLES] = newVal;
  float sum = 0;
  for (int i = 0; i < SMOOTH_SAMPLES; i++) sum += buf[i];
  return sum / SMOOTH_SAMPLES;
}

// ================================================================
// I2C / MPU6050
// ================================================================
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
  Wire.read(); Wire.read();
  gyroX  = Wire.read() << 8 | Wire.read();
  gyroY  = Wire.read() << 8 | Wire.read();
  gyroZ  = Wire.read() << 8 | Wire.read();
}

void calibrateGyro() {
  Serial.println(F("Calibrating gyro — keep still..."));
  const int samples = 500;
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
  Serial.println(F("Gyro done."));
}

// ================================================================
// SERIAL COMMAND PARSER
// ================================================================
// Commands:
//   U<int>   — FORCE_TRIGGER_UP    e.g. "U1700"
//   L<int>   — FORCE_TRIGGER_DOWN  e.g. "L3400"
//   P<int>   — FORCE_RESET_UPPER   e.g. "P2900"
//   Q<int>   — FORCE_RESET_LOWER   e.g. "Q2200"
//   T<int>   — delta threshold     e.g. "T1000"
//   K<float> — Kp                  e.g. "K3.5"
//   D<float> — Kd                  e.g. "D0.4"
//   A<float> — alpha               e.g. "A0.93"
//   R        — re-tare load cell
//   ?        — print all values
void handleSerial() {
  if (!Serial.available()) return;
  char cmd = Serial.read();
  switch (cmd) {
    case 'U':
      FORCE_TRIGGER_UP = Serial.parseInt();
      Serial.print(F("FORCE_TRIGGER_UP -> ")); Serial.println(FORCE_TRIGGER_UP); break;
    case 'L':
      FORCE_TRIGGER_DOWN = Serial.parseInt();
      Serial.print(F("FORCE_TRIGGER_DOWN -> ")); Serial.println(FORCE_TRIGGER_DOWN); break;
    case 'P':
      FORCE_RESET_UPPER = Serial.parseInt();
      Serial.print(F("FORCE_RESET_UPPER -> ")); Serial.println(FORCE_RESET_UPPER); break;
    case 'Q':
      FORCE_RESET_LOWER = Serial.parseInt();
      Serial.print(F("FORCE_RESET_LOWER -> ")); Serial.println(FORCE_RESET_LOWER); break;
    case 'T':
      forceThreshold = Serial.parseInt();
      Serial.print(F("Delta threshold -> ")); Serial.println(forceThreshold); break;
    case 'K':
      Kp = Serial.parseFloat();
      Serial.print(F("Kp -> ")); Serial.println(Kp); break;
    case 'D':
      Kd = Serial.parseFloat();
      Serial.print(F("Kd -> ")); Serial.println(Kd); break;
    case 'A':
      alpha = constrain(Serial.parseFloat(), 0.0f, 1.0f);
      Serial.print(F("Alpha -> ")); Serial.println(alpha); break;
    case 'R':
      calibrateLoadCell(); break;
    case '?':
      Serial.println(F("\n--- Current Values ---"));
      Serial.print(F("FORCE_TRIGGER_UP  : ")); Serial.println(FORCE_TRIGGER_UP);
      Serial.print(F("FORCE_TRIGGER_DOWN: ")); Serial.println(FORCE_TRIGGER_DOWN);
      Serial.print(F("FORCE_RESET_UPPER : ")); Serial.println(FORCE_RESET_UPPER);
      Serial.print(F("FORCE_RESET_LOWER : ")); Serial.println(FORCE_RESET_LOWER);
      Serial.print(F("Delta threshold   : ")); Serial.println(forceThreshold);
      Serial.print(F("Kp / Kd           : ")); Serial.print(Kp); Serial.print(F(" / ")); Serial.println(Kd);
      Serial.print(F("Alpha             : ")); Serial.println(alpha);
      Serial.print(F("Torque            : ")); Serial.println(torqueEnabled ? F("ON") : F("OFF"));
      Serial.print(F("Arm state         : "));
      Serial.println(armState == 0 ? F("IDLE") : armState == 1 ? F("MOVING UP") : F("MOVING DOWN"));
      Serial.print(F("Last force        : ")); Serial.println(forcePrev);
      Serial.println(F("----------------------\n"));
      break;
    default: break;
  }
}

// ================================================================
// SETUP
// ================================================================
void setup() {
  Wire.begin();
  Wire.setTimeout(50);
  Serial.begin(115200);

  setupPWM();

  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, HIGH);

  pinMode(PIN_TORQUE_BTN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_TORQUE_BTN), ISR_torqueBtn, FALLING);

  writeReg(0x6B, 0x00);
  writeReg(0x1A, 0x03);
  delay(100);
  calibrateGyro();

  scale.begin(PIN_HX711_DAT, PIN_HX711_SCK);
  scale.set_gain(64);       // reduced from 128 — tightens noise band
  calibrateLoadCell();

  prevTime = millis();

  Serial.println(F("\nReady. Send '?' for current values."));
  Serial.println(F("Commands: U L P Q T K D A R ?"));
}

// ================================================================
// LOOP
// ================================================================
void loop() {
  handleSerial();

  // ---- Torque toggle ----
  if (btnFlag) {
    unsigned long now = millis();
    if (now - lastDebounce > DEBOUNCE_MS) {
      torqueEnabled = !torqueEnabled;
      if (torqueEnabled) {
        enableServoPWM();
        Serial.println(F("[TORQUE ON]"));
      } else {
        disableServoPWM();
        Serial.println(F("[TORQUE OFF — joints free]"));
      }
      lastDebounce = now;
    }
    btnFlag = false;
  }

  // ---- Loop timing ----
  unsigned long now = millis();
  dt = (now - prevTime) * 0.001f;
  prevTime = now;

  // ==============================================================
  // IMU + SERVO AUTO-LEVELING
  // ==============================================================
  if (torqueEnabled) {
    readMPU();

    Ax = accelX / 16384.0f;
    Ay = accelY / 16384.0f;
    Az = accelZ / 16384.0f;
    Gx = (gyroX - gx_off) / 131.0f;
    Gy = (gyroY - gy_off) / 131.0f;

    float accelPitch = atan2(Ay, sqrt(Ax*Ax + Az*Az)) * 57.2958f;
    float accelRoll  = atan2(-Ax, Az) * 57.2958f;

    pitch += Gx * dt;
    roll  += Gy * dt;
    pitch  = alpha * pitch + (1.0f - alpha) * accelPitch;
    roll   = alpha * roll  + (1.0f - alpha) * accelRoll;

    float pitch_error = -pitch;
    float roll_error  = -roll;

    if (fabs(pitch_error) < 0.5f) pitch_error = 0.0f;
    if (fabs(roll_error)  < 0.5f) roll_error  = 0.0f;

    float pitch_cmd = Kp * pitch_error - Kd * Gx;
    float roll_cmd  = Kp * roll_error  - Kd * Gy;

    float pulse_pitch = 1350.0f + pitch_cmd * 10.0f;
    float pulse_roll  =  850.0f + roll_cmd  * 10.0f;

    pulse_pitch = constrain(pulse_pitch, 1000.0f, 1900.0f);
    pulse_roll  = constrain(pulse_roll,   600.0f, 1200.0f);

    float maxStep = 8.0f;
    pulse_pitch = constrain(pulse_pitch, prev_pulse_pitch - maxStep, prev_pulse_pitch + maxStep);
    pulse_roll  = constrain(pulse_roll,  prev_pulse_roll  - maxStep, prev_pulse_roll  + maxStep);
    prev_pulse_pitch = pulse_pitch;
    prev_pulse_roll  = pulse_roll;

    smoothIdx++;
    pulse_pitch = movingAvg(pitchBuf, pulse_pitch);
    pulse_roll  = movingAvg(rollBuf,  pulse_roll);

    setServoA(pulse_pitch);
    setServoB(pulse_roll);

    Serial.print(F("P:")); Serial.print(pitch, 1);
    Serial.print(F(" R:")); Serial.print(roll, 1);
    Serial.print(F(" Pp:")); Serial.print(pulse_pitch, 0);
    Serial.print(F(" Pr:")); Serial.print(pulse_roll, 0);
    Serial.print(F(" | "));
  }

  // ==============================================================
  // LOAD CELL + STEPPER LOGIC
  // ==============================================================
  if (scale.is_ready()) {
    long force = scale.read_average(3);  // light averaging to reduce spikes
    long delta = force - forcePrev;

    // --- Always print raw values for monitoring ---
    Serial.print(F("F:")); Serial.print(force);
    Serial.print(F(" dF:")); Serial.print(delta);
    Serial.print(F(" State:"));

    // --- State machine ---
    // Tension  (push UP)   = force drops BELOW FORCE_TRIGGER_UP   (~1700)
    // Compression (push DOWN) = force rises ABOVE FORCE_TRIGGER_DOWN (~3400)
    // Returns to IDLE when force re-enters the reset band
    switch (armState) {

      case 0:  // IDLE — watching for trigger
        Serial.print(F("IDLE"));
        if (force < FORCE_TRIGGER_UP) {
          armState = 1;
          Serial.print(F(" [-> MOVING UP]"));
        } else if (force > FORCE_TRIGGER_DOWN) {
          armState = 2;
          Serial.print(F(" [-> MOVING DOWN]"));
        }
        // Solid LED = idle/holding
        digitalWrite(PIN_LED, HIGH);
        break;

      case 1:  // MOVING UP — tension, force below idle band
        Serial.print(F("UP"));
        simulateSteppers(1, delta);
        // Return to idle when force rises back into band
        if (force >= FORCE_RESET_LOWER) {
          armState = 0;
          Serial.print(F(" [-> IDLE]"));
        }
        // Fast blink = moving up
        {
          unsigned long t = millis();
          if (t - lastBlink >= blinkInterval) {
            lastBlink = t;
            ledState = !ledState;
            digitalWrite(PIN_LED, ledState);
          }
        }
        break;

      case 2:  // MOVING DOWN — compression, force above idle band
        Serial.print(F("DOWN"));
        simulateSteppers(2, delta);
        // Return to idle when force drops back into band
        if (force <= FORCE_RESET_UPPER) {
          armState = 0;
          Serial.print(F(" [-> IDLE]"));
        }
        // Slow blink = moving down
        {
          unsigned long t = millis();
          if (t - lastBlink >= (blinkInterval * 3)) {
            lastBlink = t;
            ledState = !ledState;
            digitalWrite(PIN_LED, ledState);
          }
        }
        break;
    }

    forcePrev = force;
  }

  Serial.println();
  delay(50);
}