// ============================================================================
// ESP32-S3 SUPER MINI - MICRO DRONE FIRMWARE (SIMPLE)
// Joystick → Motor doğrudan kontrol, ARM/kilit YOK
// Core 0: WiFi + WebSocket
// Core 1: MPU6050 + PID + Motor
// ============================================================================

#include <Wire.h>
#include <SPI.h>
#include <RF24.h>

// ==========================================
// PIN CONFIG
// ==========================================
#define SDA_PIN 12
#define SCL_PIN 13
#define MPU_ADDR 0x68
#define LED_PIN 47

const int motorPins[] = {10, 7, 11, 6};
const int pwmFreq = 5000;
const int pwmResolution = 8;

// ==========================================
// FLIGHT PARAMS
// ==========================================
const float MAX_ANGLE = 20.0;
const float MAX_YAW_RATE = 90.0;
const float MAX_I_TERM = 40.0;

float Kp_roll  = 1.80, Ki_roll  = 0.04, Kd_roll  = 0.40;
float Kp_pitch = 1.80, Ki_pitch = 0.04, Kd_pitch = 0.40;
float Kp_yaw   = 2.50, Ki_yaw   = 0.05, Kd_yaw   = 0.00;

// ==========================================
// SHARED DATA (volatile)
// ==========================================
volatile int shared_throttle = 0;
volatile int shared_pitch = 0;
volatile int shared_roll = 0;
volatile int shared_yaw = 0;

volatile float tel_pitch = 0, tel_roll = 0, tel_yaw = 0;
volatile int tel_throttle = 0;

volatile int shared_m1 = 0;
volatile int shared_m2 = 0;
volatile int shared_m3 = 0;
volatile int shared_m4 = 0;

volatile int i2c_errors = 0;

volatile bool isCalibrated = false;
volatile bool isCalibrating = false;
volatile bool mpuOK = false;

// ==========================================
// FAILSAFE - WiFi Watchdog & Ramp-down
// ==========================================
volatile unsigned long lastCmdTime = 0;   // Son komut zamanı (millis)
const unsigned long CMD_TIMEOUT_MS = 500; // 500ms komut gelmezse failsafe
volatile bool failsafeActive = false;     // Failsafe durumu
volatile int failsafe_throttle = 0;       // Yavaş düşüş için anlık gaz
const int RAMPDOWN_STEP = 3;              // Her döngüde (500Hz) düşürülecek miktar
                                          // 255 / 3 = ~85 adım = ~170ms'de sıfıra iner

// ==========================================
// MPU6050 DATA
// ==========================================
int16_t raw_acc_x, raw_acc_y, raw_acc_z;
int16_t raw_gyro_x, raw_gyro_y, raw_gyro_z;
float gyroXOffset = 0, gyroYOffset = 0, gyroZOffset = 0;
float accRollOffset = 0, accPitchOffset = 0;

// ==========================================
// ATTITUDE & PID
// ==========================================
float roll_angle = 0, pitch_angle = 0, yaw_angle = 0;
float roll_i = 0, pitch_i = 0, yaw_i = 0;
float roll_pid_out = 0, pitch_pid_out = 0, yaw_pid_out = 0;

// ==========================================
// NRF24L01 CONFIG
// ==========================================
#define NRF_CE_PIN 1
#define NRF_CSN_PIN 2
#define NRF_SCK_PIN 3
#define NRF_MOSI_PIN 4
#define NRF_MISO_PIN 5

RF24 radio(NRF_CE_PIN, NRF_CSN_PIN);
const byte address[6] = "00001";

struct NRF_Payload {
  int16_t throttle;
  int16_t pitch;
  int16_t roll;
  int16_t yaw;
  int16_t cmd; // 1: Calibrate vb.
};
NRF_Payload nrfData;

struct NRF_Telemetry {
  float pitch;
  float roll;
  float yaw;
  int32_t cal_status; // 0=normal, 1=kalibre ediliyor, 2=kalibre edildi, 3=basarisiz
};
NRF_Telemetry telemetryData;

volatile bool nrfOK = false;

// ==========================================
// MPU6050 DRIVER
// ==========================================
bool initMPU6050() {
  Wire.begin(SDA_PIN, SCL_PIN, 400000);
  Wire.beginTransmission(MPU_ADDR);
  if (Wire.endTransmission() != 0) return false;

  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x6B); Wire.write(0x00);
  Wire.endTransmission();

  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x1A); Wire.write(0x03);
  Wire.endTransmission();

  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x1B); Wire.write(0x08);
  Wire.endTransmission();

  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x1C); Wire.write(0x08);
  Wire.endTransmission();

  return true;
}

void readMPU6050() {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x3B);
  if (Wire.endTransmission(false) != 0) {
    i2c_errors++;
    return;
  }
  int bytesReceived = Wire.requestFrom(MPU_ADDR, 14, true);
  if (bytesReceived < 14 || Wire.available() < 14) {
    i2c_errors++;
    return;
  }
  i2c_errors = 0;
  raw_acc_x  = (Wire.read() << 8) | Wire.read();
  raw_acc_y  = (Wire.read() << 8) | Wire.read();
  raw_acc_z  = (Wire.read() << 8) | Wire.read();
  Wire.read(); Wire.read();
  raw_gyro_x = (Wire.read() << 8) | Wire.read();
  raw_gyro_y = (Wire.read() << 8) | Wire.read();
  raw_gyro_z = (Wire.read() << 8) | Wire.read();
}

void calibrateGyro() {
  // GÜVENLİK: Gaz açıkken kalibrasyon yapma!
  if (shared_throttle > 5) {
    Serial.println("[CAL] REDDEDILDI - Önce gazi sifirla!");
    return;
  }

  // Motorları kapat
  for (int i = 0; i < 4; i++) ledcWrite(motorPins[i], 0);
  shared_throttle = 0;
  shared_pitch = 0;
  shared_roll = 0;
  shared_yaw = 0;

  isCalibrating = true;
  isCalibrated = false;
  Serial.println("[CAL] Kalibrasyon basliyor... Drone'u SABIT tutun!");

  // Ön bekleme - titreşim azalsın
  delay(500);

  long sum_x = 0, sum_y = 0, sum_z = 0;
  float sum_acc_roll = 0, sum_acc_pitch = 0;
  int validSamples = 0;
  const int targetSamples = 1000;
  int maxRetries = targetSamples + 200; // I2C hatalı okumaları tolere et

  // Titreşim tespiti için
  int16_t prev_gx = 0, prev_gy = 0, prev_gz = 0;
  int largeJumps = 0;

  for (int i = 0; i < maxRetries && validSamples < targetSamples; i++) {
    int prevErrors = i2c_errors;
    readMPU6050();

    // I2C okuma başarısız olduysa atla
    if (i2c_errors > prevErrors) {
      delay(2);
      continue;
    }

    // Titreşim kontrolü: önceki okumaya göre aşırı sapma var mı?
    if (validSamples > 0) {
      int16_t dx = abs(raw_gyro_x - prev_gx);
      int16_t dy = abs(raw_gyro_y - prev_gy);
      int16_t dz = abs(raw_gyro_z - prev_gz);
      if (dx > 500 || dy > 500 || dz > 500) {
        largeJumps++;
      }
    }
    prev_gx = raw_gyro_x;
    prev_gy = raw_gyro_y;
    prev_gz = raw_gyro_z;

    sum_x += raw_gyro_x;
    sum_y += raw_gyro_y;
    sum_z += raw_gyro_z;
    
    float ax = raw_acc_x / 8192.0;
    float ay = raw_acc_y / 8192.0;
    float az = raw_acc_z / 8192.0;
    sum_acc_roll += atan2(ay, sqrt(ax*ax + az*az)) * 180.0 / M_PI;
    sum_acc_pitch += -atan2(ax, sqrt(ay*ay + az*az)) * 180.0 / M_PI;
    
    validSamples++;

    if (validSamples % 100 == 0) digitalWrite(LED_PIN, !digitalRead(LED_PIN));
    delay(2);
  }

  if (validSamples < targetSamples) {
    Serial.printf("[CAL] BASARISIZ! Sadece %d/%d ornek okundu\n", validSamples, targetSamples);
    isCalibrating = false;
    isCalibrated = false;
    digitalWrite(LED_PIN, LOW);
    return;
  }

  // Titreşim uyarısı (kalibrasyon kabul edilir ama uyarı verilir)
  if (largeJumps > 50) {
    Serial.printf("[CAL] UYARI: %d titresim tespit edildi! Drone sabit degildi.\n", largeJumps);
  }

  gyroXOffset = (float)sum_x / validSamples;
  gyroYOffset = (float)sum_y / validSamples;
  gyroZOffset = (float)sum_z / validSamples;
  
  accRollOffset = sum_acc_roll / validSamples;
  accPitchOffset = sum_acc_pitch / validSamples;
  
  pitch_angle = 0;
  roll_angle = 0;
  yaw_angle = 0;

  Serial.printf("[CAL] OK! Offsets: X=%.1f Y=%.1f Z=%.1f (%d ornek, %d titresim)\n",
                gyroXOffset, gyroYOffset, gyroZOffset, validSamples, largeJumps);

  isCalibrating = false;
  isCalibrated = true;
  digitalWrite(LED_PIN, HIGH);
}

// ==========================================
// ATTITUDE & PID
// ==========================================
void computeAttitude(float dt) {
  float gr = (raw_gyro_x - gyroXOffset) / 65.5;
  float gp = (raw_gyro_y - gyroYOffset) / 65.5;
  float gy = (raw_gyro_z - gyroZOffset) / 65.5;

  float ax = raw_acc_x / 8192.0;
  float ay = raw_acc_y / 8192.0;
  float az = raw_acc_z / 8192.0;

  float acc_roll  = (atan2(ay, sqrt(ax*ax + az*az)) * 180.0 / M_PI) - accRollOffset;
  float acc_pitch = (-atan2(ax, sqrt(ay*ay + az*az)) * 180.0 / M_PI) - accPitchOffset;

  roll_angle  = 0.98 * (roll_angle + gr * dt) + 0.02 * acc_roll;
  pitch_angle = 0.98 * (pitch_angle + gp * dt) + 0.02 * acc_pitch;
  yaw_angle  += gy * dt;
  if (yaw_angle > 180.0)  yaw_angle -= 360.0;
  if (yaw_angle < -180.0) yaw_angle += 360.0;
}

void resetPID() {
  roll_i = 0; pitch_i = 0; yaw_i = 0;
  roll_pid_out = 0; pitch_pid_out = 0; yaw_pid_out = 0;
}

void computePID(float dt) {
  float target_roll  = (shared_roll / 100.0) * MAX_ANGLE;
  float target_pitch = (shared_pitch / 100.0) * MAX_ANGLE;
  float target_yaw_rate = (shared_yaw / 100.0) * MAX_YAW_RATE;

  float gr = (raw_gyro_x - gyroXOffset) / 65.5;
  float gp = (raw_gyro_y - gyroYOffset) / 65.5;
  float gy = (raw_gyro_z - gyroZOffset) / 65.5;

  float e_roll = target_roll - roll_angle;
  roll_i += e_roll * dt;
  roll_i = constrain(roll_i, -MAX_I_TERM, MAX_I_TERM);
  roll_pid_out = Kp_roll * e_roll + Ki_roll * roll_i + Kd_roll * (-gr);

  float e_pitch = target_pitch - pitch_angle;
  pitch_i += e_pitch * dt;
  pitch_i = constrain(pitch_i, -MAX_I_TERM, MAX_I_TERM);
  pitch_pid_out = Kp_pitch * e_pitch + Ki_pitch * pitch_i + Kd_pitch * (-gp);

  float e_yaw = target_yaw_rate - gy;
  yaw_i += e_yaw * dt;
  yaw_i = constrain(yaw_i, -MAX_I_TERM, MAX_I_TERM);
  yaw_pid_out = Kp_yaw * e_yaw + Ki_yaw * yaw_i;
}

void applyMotors() {
  int thr = shared_throttle;

  // ---- FAILSAFE KONTROL ----
  // Watchdog: Son komuttan bu yana CMD_TIMEOUT_MS geçtiyse failsafe aktif
  if (lastCmdTime > 0 && (millis() - lastCmdTime) > CMD_TIMEOUT_MS) {
    if (!failsafeActive) {
      failsafeActive = true;
      failsafe_throttle = thr; // Mevcut gazdan yavaşça düşür
      Serial.println("[FAILSAFE] WiFi timeout! Motor ramp-down basliyor...");
    }
  }

  // Failsafe aktifse: gazı kademeli düşür
  if (failsafeActive) {
    failsafe_throttle -= RAMPDOWN_STEP;
    if (failsafe_throttle < 0) failsafe_throttle = 0;
    thr = failsafe_throttle;
    shared_pitch = 0;
    shared_roll = 0;
    shared_yaw = 0;

    if (failsafe_throttle == 0) {
      for (int i = 0; i < 4; i++) ledcWrite(motorPins[i], 0);
      resetPID();
      shared_m1 = 0; shared_m2 = 0; shared_m3 = 0; shared_m4 = 0;
      static unsigned long lastFsLog = 0;
      if (millis() - lastFsLog >= 1000) {
        lastFsLog = millis();
        Serial.println("[FAILSAFE] Motorlar durduruldu. Baglanti bekleniyor...");
      }
      return;
    }
  }

  // Gaz 0 veya I2C hatası varsa motorları kapat, PID sıfırla
  if (thr < 5 || i2c_errors > 10) {
    for (int i = 0; i < 4; i++) ledcWrite(motorPins[i], 0);
    resetPID();
    shared_m1 = 0;
    shared_m2 = 0;
    shared_m3 = 0;
    shared_m4 = 0;
    failsafe_throttle = 0;
    return;
  }

  int m1, m2, m3, m4;

  if (mpuOK && isCalibrated && !isCalibrating) {
    // PID aktif: stabilize uçuş
    m1 = thr - pitch_pid_out + roll_pid_out - yaw_pid_out;
    m2 = thr - pitch_pid_out - roll_pid_out + yaw_pid_out;
    m3 = thr + pitch_pid_out + roll_pid_out + yaw_pid_out;
    m4 = thr + pitch_pid_out - roll_pid_out - yaw_pid_out;
  } else {
    // PID yok: doğrudan joystick mixing
    int p = shared_pitch;
    int r = shared_roll;
    int y = shared_yaw;
    m1 = thr - p + r - y;
    m2 = thr - p - r + y;
    m3 = thr + p + r + y;
    m4 = thr + p - r - y;
  }

  int m[4] = {m1, m2, m3, m4};

  // Taşma düzeltme
  int max_m = max(max(m[0], m[1]), max(m[2], m[3]));
  int min_m = min(min(m[0], m[1]), min(m[2], m[3]));
  if (max_m > 255) {
    int diff = max_m - 255;
    for (int i = 0; i < 4; i++) m[i] -= diff;
  }
  if (min_m < 0) {
    int diff = -min_m;
    for (int i = 0; i < 4; i++) m[i] += diff;
  }

  for (int i = 0; i < 4; i++) {
    m[i] = constrain(m[i], 0, 255);
    ledcWrite(motorPins[i], m[i]);
  }
  shared_m1 = m[0];
  shared_m2 = m[1];
  shared_m3 = m[2];
  shared_m4 = m[3];

  static unsigned long lastLog = 0;
  if (millis() - lastLog >= 500) {
    lastLog = millis();
    Serial.printf("PWM: M1:%d | M2:%d | M3:%d | M4:%d (Thr: %d)\n", m[0], m[1], m[2], m[3], thr);
  }
}

// ==========================================
// CORE 0: COMMUNICATION (NRF Only)
// ==========================================
void communicationTask(void* param) {
  while (1) {
    if (nrfOK && radio.available()) {
      radio.read(&nrfData, sizeof(NRF_Payload));
      
      lastCmdTime = millis(); // Failsafe watchdog sifirla
      if (failsafeActive) {
        failsafeActive = false;
        failsafe_throttle = 0;
        Serial.println("[FAILSAFE] NRF Baglanti geri geldi!");
      }

      if (nrfData.cmd == 1) { // Calibrate komutu
        if (shared_throttle <= 5) calibrateGyro();
      } else {
        shared_throttle = constrain(nrfData.throttle, 0, 255);
        shared_pitch = constrain(nrfData.pitch, -100, 100);
        shared_roll = constrain(nrfData.roll, -100, 100);
        shared_yaw = constrain(nrfData.yaw, -100, 100);
      }
      
      // Ack Payload hazirla (Bir sonraki paket icin)
      telemetryData.pitch = pitch_angle;
      telemetryData.roll = roll_angle;
      telemetryData.yaw = yaw_angle;
      if (isCalibrating) telemetryData.cal_status = 1;
      else if (isCalibrated) telemetryData.cal_status = 2;
      else telemetryData.cal_status = 0;
      radio.writeAckPayload(0, &telemetryData, sizeof(NRF_Telemetry));
    }

    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

// ==========================================
// SETUP
// ==========================================
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== DRONE SIMPLE START ===");

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  // 1. Motor PWM
  Serial.println("[1] Motors...");
  for (int i = 0; i < 4; i++) {
    ledcAttach(motorPins[i], pwmFreq, pwmResolution);
    ledcWrite(motorPins[i], 0);
  }
  Serial.println("Motor pinleri: 10, 7, 11, 6");

  // 2. MPU6050
  Serial.println("[4] MPU6050...");
  mpuOK = initMPU6050();
  if (mpuOK) {
    Serial.println("MPU6050 OK - kalibre ediliyor...");
    Serial.println("Drone'u SABIT bir yuzey uzerine koyun!");
    delay(1000); // Kullanıcıya zaman tanı
    calibrateGyro();
  } else {
    Serial.println("MPU6050 YOK - PID'siz mod");
  }

  // 3. NRF24L01
  Serial.println("[3] NRF24L01 Init...");
  SPI.begin(NRF_SCK_PIN, NRF_MISO_PIN, NRF_MOSI_PIN, NRF_CSN_PIN);
  if (radio.begin()) {
    radio.openReadingPipe(0, address);
    radio.setPALevel(RF24_PA_MAX);
    radio.setDataRate(RF24_250KBPS);
    radio.setChannel(108);
    radio.enableDynamicPayloads();
    radio.enableAckPayload();
    radio.setCRCLength(RF24_CRC_16);
    radio.startListening();
    nrfOK = true;
    Serial.println("NRF24L01 Baglantisi Basarili!");
    
    // Ilk AckPayload
    telemetryData.pitch = 0; telemetryData.roll = 0; telemetryData.yaw = 0; telemetryData.cal_status = 0;
    radio.writeAckPayload(0, &telemetryData, sizeof(NRF_Telemetry));
  } else {
    Serial.println("NRF24L01 BULUNAMADI! Lutfen baglantilari kontrol edin.");
  }

  // 4. CommTask Core 0
  Serial.println("[4] CommTask...");
  xTaskCreatePinnedToCore(communicationTask, "Comm", 8192, NULL, 1, NULL, 0);

  Serial.println("=== HAZIR ===");
  Serial.println("Sadece NRF ile haberlesiliyor. PC Alicisini acin.");
  digitalWrite(LED_PIN, HIGH);
}

// ==========================================
// LOOP - Core 1: Flight Control
// ==========================================
void loop() {
  static unsigned long lastMicros = 0;

  unsigned long now = micros();
  if (now - lastMicros >= 2000) { // 500Hz
    float dt = (now - lastMicros) / 1000000.0;
    lastMicros = now;

    if (mpuOK) {
      readMPU6050();
      computeAttitude(dt);
      tel_pitch = pitch_angle;
      tel_roll = roll_angle;
      tel_yaw = yaw_angle;

      if (shared_throttle >= 5 && isCalibrated && !isCalibrating) {
        computePID(dt);
      }
    }

    applyMotors();
  } else {
    delayMicroseconds(100);
  }
}
