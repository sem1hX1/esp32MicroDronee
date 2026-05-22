// ============================================================================
// ESP32-S3 MICRO DRONE - PROFESSIONAL FLIGHT CONTROLLER FIRMWARE
// Features: 250Hz Loop, MPU6050 complementary filter, Angle PID, Fail-safe, 
//           Dual WebSocket/Serial Control, Cyberpunk Web GUI & Telemetry HUD
// ============================================================================

#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <Wire.h>

// ==========================================
// 1. HARDWARE PIN & SENSOR CONFIGURATION
// ==========================================
#define SDA_PIN 8         // MPU6050 I2C SDA Pin
#define SCL_PIN 9         // MPU6050 I2C SCL Pin
#define MPU_ADDR 0x68     // MPU6050 I2C Address
#define LED_PIN 47        // Onboard LED Pin (ESP32-S3 Super Mini)

// Motor Pin Definitions (MOSFET Gates)
// M1: Front Left  (CW/CCW depending on prop, standard CCW)
// M2: Front Right (Standard CW)
// M3: Rear Left   (Standard CW)
// M4: Rear Right  (Standard CCW)
const int motorPins[] = {10, 7, 11, 6};

// PWM Configuration (Brushed Motors)
const int pwmFreq = 10000;     // 10 kHz frequency for smooth brushed motor control
const int pwmResolution = 8;   // 8-bit resolution (0-255 duty cycle)

// ==========================================
// 2. FLIGHT LIMITS & PID CONFIGURATION
// ==========================================
const float MAX_ANGLE = 20.0;       // Maximum tilt angle (Roll/Pitch) in degrees
const float MAX_YAW_RATE = 90.0;    // Maximum yaw rate in degrees per second
const float MAX_I_TERM = 40.0;      // Anti-windup limit for I term in PID

// Angle Mode PID Constants (Adjust these for your drone's specific dynamics)
float Kp_roll  = 1.80;  // Roll Proportional Gain
float Ki_roll  = 0.04;  // Roll Integral Gain
float Kd_roll  = 0.40;  // Roll Derivative Gain (damping)

float Kp_pitch = 1.80;  // Pitch Proportional Gain
float Ki_pitch = 0.04;  // Pitch Integral Gain
float Kd_pitch = 0.40;  // Pitch Derivative Gain

float Kp_yaw   = 2.50;  // Yaw Rate Proportional Gain
float Ki_yaw   = 0.05;  // Yaw Rate Integral Gain
float Kd_yaw   = 0.00;  // Yaw Rate Derivative Gain (typically not used)

// ==========================================
// 3. FLIGHT CONTROLLER STATE VARIABLES
// ==========================================
int throttle = 0;   // Command throttle: 0 - 255
int pitch = 0;      // Command pitch: -100 to 100
int roll = 0;       // Command roll: -100 to 100
int yaw = 0;        // Command yaw: -100 to 100
bool armed = false; // Arming state
unsigned long lastCmdTime = 0; // Failsafe watchdog timer

// MPU6050 Raw Readings
int16_t raw_acc_x, raw_acc_y, raw_acc_z;
int16_t raw_gyro_x, raw_gyro_y, raw_gyro_z;
int16_t raw_temp;

// Gyro calibration offsets
float gyroXOffset = 0, gyroYOffset = 0, gyroZOffset = 0;
bool isCalibrated = false;
volatile bool isCalibrating = false;

// Calculated attitude angles
float roll_angle = 0.0;
float pitch_angle = 0.0;
float yaw_angle = 0.0;

// PID Variables
float roll_i = 0, pitch_i = 0, yaw_i = 0;
float roll_pid_out = 0, pitch_pid_out = 0, yaw_pid_out = 0;

// Access Point Credentials
const char* ssid = "DRONE-NEXTGEN";
const char* password = "dronecontrol";

WebServer server(80);
WebSocketsServer webSocket(81);

// ==========================================
// 4. LOW-LEVEL MPU6050 NATIVE DRIVER
// ==========================================
bool baslatMPU6050() {
  Wire.begin(SDA_PIN, SCL_PIN, 400000); // 400kHz fast I2C mode
  
  // Verify sensor communication
  Wire.beginTransmission(MPU_ADDR);
  if (Wire.endTransmission() != 0) {
    return false;
  }
  
  // PWR_MGMT_1 (0x6B) -> Wake up sensor
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x6B);
  Wire.write(0x00);
  Wire.endTransmission();
  
  // CONFIG (0x1A) -> Set Digital Low Pass Filter (DLPF) to ~42Hz
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x1A);
  Wire.write(0x03); 
  Wire.endTransmission();
  
  // GYRO_CONFIG (0x1B) -> Set full scale range to +/- 500 deg/s (65.5 LSB / deg/s)
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x1B);
  Wire.write(0x08); 
  Wire.endTransmission();
  
  // ACCEL_CONFIG (0x1C) -> Set full scale range to +/- 4g (8192 LSB / g)
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x1C);
  Wire.write(0x08); 
  Wire.endTransmission();
  
  return true;
}

void okuMPU6050() {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x3B); // Starting register address for Accel X measurement
  Wire.endTransmission(false);
  
  Wire.requestFrom(MPU_ADDR, 14, true);
  if (Wire.available() >= 14) {
    raw_acc_x = (Wire.read() << 8) | Wire.read();
    raw_acc_y = (Wire.read() << 8) | Wire.read();
    raw_acc_z = (Wire.read() << 8) | Wire.read();
    raw_temp  = (Wire.read() << 8) | Wire.read();
    raw_gyro_x = (Wire.read() << 8) | Wire.read();
    raw_gyro_y = (Wire.read() << 8) | Wire.read();
    raw_gyro_z = (Wire.read() << 8) | Wire.read();
  }
}

void kalibreEtGyro() {
  isCalibrating = true;
  isCalibrated = false;
  Serial.println("[*] Gyro kalibrasyonu basladi. Dronu duz ve sabit tutun...");
  
  long sum_x = 0;
  long sum_y = 0;
  long sum_z = 0;
  const int samples = 1000;
  
  for (int i = 0; i < samples; i++) {
    okuMPU6050();
    sum_x += raw_gyro_x;
    sum_y += raw_gyro_y;
    sum_z += raw_gyro_z;
    
    // Blink LED to indicate calibration in progress
    if (i % 100 == 0) {
      digitalWrite(LED_PIN, !digitalRead(LED_PIN));
      Serial.print(".");
    }
    delay(2);
  }
  Serial.println();
  
  gyroXOffset = (float)sum_x / samples;
  gyroYOffset = (float)sum_y / samples;
  gyroZOffset = (float)sum_z / samples;
  
  isCalibrating = false;
  isCalibrated = true;
  digitalWrite(LED_PIN, HIGH); // Solid ON when ready
  
  Serial.print("[*] Kalibrasyon TAMAMLANDI. Offsets: ");
  Serial.printf("X:%.2f | Y:%.2f | Z:%.2f\n", gyroXOffset, gyroYOffset, gyroZOffset);
}

// ==========================================
// 5. ATTITUDE ESTIMATION (SENSOR FUSION)
// ==========================================
void hesaplaAcilar(float dt) {
  // Convert raw gyro readings to deg/s
  float gyro_roll_rate  = (raw_gyro_x - gyroXOffset) / 65.5;
  float gyro_pitch_rate = (raw_gyro_y - gyroYOffset) / 65.5;
  float gyro_yaw_rate   = (raw_gyro_z - gyroZOffset) / 65.5;
  
  // Convert raw accel readings to g units
  float acc_x_g = raw_acc_x / 8192.0;
  float acc_y_g = raw_acc_y / 8192.0;
  float acc_z_g = raw_acc_z / 8192.0;
  
  // Calculate orientation angles from accelerometer
  float acc_roll  = atan2(acc_y_g, sqrt(acc_x_g * acc_x_g + acc_z_g * acc_z_g)) * 180.0 / M_PI;
  float acc_pitch = -atan2(acc_x_g, sqrt(acc_y_g * acc_y_g + acc_z_g * acc_z_g)) * 180.0 / M_PI;
  
  // Complementary filter fusion (98% gyroscope integrated angle, 2% accelerometer angle)
  roll_angle  = 0.98 * (roll_angle + gyro_roll_rate * dt) + 0.02 * acc_roll;
  pitch_angle = 0.98 * (pitch_angle + gyro_pitch_rate * dt) + 0.02 * acc_pitch;
  
  // Yaw has no absolute accelerometer reference; integrate gyro rate directly
  yaw_angle  += gyro_yaw_rate * dt;
  if (yaw_angle > 180.0)  yaw_angle -= 360.0;
  if (yaw_angle < -180.0) yaw_angle += 360.0;
  
  // Safety: Crash / Tumble Detection
  // If the drone tilts more than 55 degrees, immediately disarm to cut power!
  if (abs(roll_angle) > 55.0 || abs(pitch_angle) > 55.0) {
    if (armed) {
      Serial.println("[WARNING] CRASH DETECTED! DISARMING FOR SAFETY!");
      acilStop();
    }
  }
}

// ==========================================
// 6. PID CONTROL LOGIC
// ==========================================
void resetPIDs() {
  roll_i = 0;
  pitch_i = 0;
  yaw_i = 0;
  roll_pid_out = 0;
  pitch_pid_out = 0;
  yaw_pid_out = 0;
}

void hesaplaPID(float dt) {
  // Map joystick commands (-100 to 100) to absolute target parameters
  float target_roll  = (roll / 100.0) * MAX_ANGLE;      // Target angle in degrees
  float target_pitch = (pitch / 100.0) * MAX_ANGLE;     // Target angle in degrees
  float target_yaw_rate = (yaw / 100.0) * MAX_YAW_RATE; // Target yaw rate in deg/s
  
  // Fetch actual angular rates
  float gyro_roll_rate  = (raw_gyro_x - gyroXOffset) / 65.5;
  float gyro_pitch_rate = (raw_gyro_y - gyroYOffset) / 65.5;
  float gyro_yaw_rate   = (raw_gyro_z - gyroZOffset) / 65.5;
  
  // --- ROLL PID (Stabilization & Damping) ---
  float error_roll = target_roll - roll_angle;
  roll_i += error_roll * dt;
  roll_i = constrain(roll_i, -MAX_I_TERM, MAX_I_TERM); // Anti-windup
  // Using direct negative gyro rate for D-term to avoid derivative kick (setpoint spikes)
  float roll_d = -gyro_roll_rate; 
  roll_pid_out = (Kp_roll * error_roll) + (Ki_roll * roll_i) + (Kd_roll * roll_d);
  
  // --- PITCH PID ---
  float error_pitch = target_pitch - pitch_angle;
  pitch_i += error_pitch * dt;
  pitch_i = constrain(pitch_i, -MAX_I_TERM, MAX_I_TERM);
  float pitch_d = -gyro_pitch_rate;
  pitch_pid_out = (Kp_pitch * error_pitch) + (Ki_pitch * pitch_i) + (Kd_pitch * pitch_d);
  
  // --- YAW RATE PID ---
  float error_yaw = target_yaw_rate - gyro_yaw_rate;
  yaw_i += error_yaw * dt;
  yaw_i = constrain(yaw_i, -MAX_I_TERM, MAX_I_TERM);
  float yaw_d = 0; // Not critical for yaw on micro-drones
  yaw_pid_out = (Kp_yaw * error_yaw) + (Ki_yaw * yaw_i) + (Kd_yaw * yaw_d);
}

// ==========================================
// 7. MOTOR MIXER (QUADCOPTER X)
// ==========================================
void miksajUygula() {
  // If disarmed or throttle command is negligible, cut power to motors
  if (!armed || throttle < 10) {
    for (int i = 0; i < 4; i++) {
      ledcWrite(motorPins[i], 0);
    }
    resetPIDs();
    return;
  }
  
  // Standard Quadcopter X motor mapping
  int m1 = throttle - pitch_pid_out + roll_pid_out - yaw_pid_out; // M1: Front Left
  int m2 = throttle - pitch_pid_out - roll_pid_out + yaw_pid_out; // M2: Front Right
  int m3 = throttle + pitch_pid_out + roll_pid_out + yaw_pid_out; // M3: Rear Left
  int m4 = throttle + pitch_pid_out - roll_pid_out - yaw_pid_out; // M4: Rear Right
  
  // Professional scaling: avoid saturation and keep correction torque priority
  int m_out[4] = {m1, m2, m3, m4};
  
  int max_m = m_out[0];
  int min_m = m_out[0];
  for (int i = 1; i < 4; i++) {
    if (m_out[i] > max_m) max_m = m_out[i];
    if (m_out[i] < min_m) min_m = m_out[i];
  }
  
  // Shift commands down if they exceed maximum throttle limit to preserve PID control authority
  if (max_m > 255) {
    int excess = max_m - 255;
    for (int i = 0; i < 4; i++) m_out[i] -= excess;
  }
  
  // Shift commands up if they drop below zero (motor stops)
  if (min_m < 0) {
    int deficit = -min_m;
    for (int i = 0; i < 4; i++) m_out[i] += deficit;
  }
  
  // Final bounds safety constraint and write to LEDC PWM controller
  for (int i = 0; i < 4; i++) {
    m_out[i] = constrain(m_out[i], 0, 255);
    ledcWrite(motorPins[i], m_out[i]);
  }
}

// Safety immediate stop
void acilStop() {
  armed = false;
  throttle = 0;
  pitch = 0;
  roll = 0;
  yaw = 0;
  resetPIDs();
  for (int i = 0; i < 4; i++) {
    ledcWrite(motorPins[i], 0);
  }
}

// ==========================================
// 8. RECEIVER & PROTOCOL HANDLING
// ==========================================
// Non-blocking hardware Serial parser for Python gamepad script (T:120 P:0 R:0 Y:0 A:1)
void handleSerial() {
  static char rxBuffer[64];
  static int rxIndex = 0;
  
  while (Serial.available() > 0) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (rxIndex > 0) {
        rxBuffer[rxIndex] = '\0';
        int t_val, p_val, r_val, y_val, a_val;
        // Parse commands
        if (sscanf(rxBuffer, "T:%d P:%d R:%d Y:%d A:%d", &t_val, &p_val, &r_val, &y_val, &a_val) == 5) {
          throttle = t_val;
          pitch = p_val;
          roll = r_val;
          yaw = y_val;
          
          if (a_val == 1) {
            // Safety: Arm only if throttle command is near zero
            if (!armed && throttle < 15 && isCalibrated) {
              armed = true;
              resetPIDs();
            }
          } else {
            armed = false;
          }
          lastCmdTime = millis();
        }
        rxIndex = 0;
      }
    } else if (rxIndex < 63) {
      rxBuffer[rxIndex++] = c;
    }
  }
}

// WebSockets receiver event callback
void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  if (type == WStype_TEXT) {
    char* msg = (char*)payload;
    if (strncmp(msg, "CMD:", 4) == 0) {
      if (strstr(msg, "CMD:CALIBRATE") != NULL) {
        if (!armed) {
          kalibreEtGyro();
        }
      } else {
        int t_val, p_val, r_val, y_val, a_val;
        // Parse format: CMD:T:0,P:0,R:0,Y:0,A:0
        if (sscanf(msg, "CMD:T:%d,P:%d,R:%d,Y:%d,A:%d", &t_val, &p_val, &r_val, &y_val, &a_val) == 5) {
          throttle = t_val;
          pitch = p_val;
          roll = r_val;
          yaw = y_val;
          
          if (a_val == 1) {
            if (!armed && throttle < 15 && isCalibrated) {
              armed = true;
              resetPIDs();
            }
          } else {
            armed = false;
          }
          lastCmdTime = millis();
        }
      }
    }
  } else if (type == WStype_DISCONNECTED) {
    acilStop();
  }
}

// Telemetry feedback: broadcast to websocket client at 10Hz
void gonderTelemetri() {
  static unsigned long lastTelemetry = 0;
  if (millis() - lastTelemetry >= 100) {
    lastTelemetry = millis();
    
    // Status encoding: 0 = disarmed, 1 = armed, 2 = calibrating, 3 = uncalibrated error
    int status_code = 0;
    if (isCalibrating) status_code = 2;
    else if (!isCalibrated) status_code = 3;
    else if (armed) status_code = 1;
    
    char telBuffer[80];
    // Send telemetry structured layout
    snprintf(telBuffer, sizeof(telBuffer), "TEL:P:%.1f,R:%.1f,Y:%.1f,S:%d,T:%d", 
             pitch_angle, roll_angle, yaw_angle, status_code, throttle);
    webSocket.broadcastTXT(telBuffer);
  }
}

// ==========================================
// 9. PREMIUM FUTURISTIC WEB DASHBOARD HTML
// ==========================================
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1, user-scalable=0">
<title>NanoCore Flight Interface v2.0</title>
<link href="https://fonts.googleapis.com/css2?family=Outfit:wght@300;400;600;800&family=Share+Tech+Mono&display=swap" rel="stylesheet">
<style>
  :root {
    --neon-blue: #00f0ff;
    --neon-pink: #ff007f;
    --neon-green: #00ff66;
    --neon-yellow: #ffdd00;
    --bg-dark: #050508;
    --panel-bg: rgba(12, 12, 20, 0.75);
    --border-color: rgba(0, 240, 255, 0.2);
  }
  
  body { 
    background: var(--bg-dark); 
    background-image: radial-gradient(circle at center, #101530 0%, var(--bg-dark) 70%);
    color: #e0e6ed; 
    font-family: 'Outfit', sans-serif; 
    text-align: center; 
    margin: 0; 
    overflow: hidden; 
    touch-action: none; 
    user-select: none; 
    -webkit-user-select: none; 
  }
  
  .header {
    padding: 10px 0 5px 0;
    border-bottom: 1px solid var(--border-color);
    background: rgba(5, 5, 10, 0.9);
  }
  
  h1 {
    font-family: 'Share Tech Mono', monospace;
    font-size: 20px;
    margin: 0;
    font-weight: 800;
    letter-spacing: 4px;
    color: var(--neon-blue);
    text-shadow: 0 0 10px rgba(0, 240, 255, 0.5);
  }
  
  .status-tag {
    font-family: 'Share Tech Mono', monospace;
    font-size: 13px;
    margin-top: 3px;
    color: var(--neon-yellow);
    letter-spacing: 1px;
    text-shadow: 0 0 5px rgba(255, 221, 0, 0.3);
  }

  .grid-container {
    display: grid;
    grid-template-columns: 1.2fr 1fr;
    gap: 10px;
    padding: 10px;
    max-width: 800px;
    margin: 0 auto;
  }

  .card {
    background: var(--panel-bg);
    border: 1px solid var(--border-color);
    border-radius: 12px;
    backdrop-filter: blur(10px);
    -webkit-backdrop-filter: blur(10px);
    box-shadow: 0 4px 20px rgba(0,0,0,0.5);
    padding: 10px;
  }

  .telemetry-title {
    font-family: 'Share Tech Mono', monospace;
    font-size: 12px;
    text-transform: uppercase;
    color: rgba(255, 255, 255, 0.5);
    border-bottom: 1px solid rgba(0, 240, 255, 0.1);
    padding-bottom: 4px;
    margin-bottom: 8px;
    text-align: left;
  }

  .telemetry-grid {
    display: grid;
    grid-template-columns: 1fr 1fr;
    gap: 8px;
    font-family: 'Share Tech Mono', monospace;
  }

  .tel-item {
    background: rgba(255,255,255,0.02);
    border-radius: 6px;
    padding: 6px;
    text-align: left;
    border-left: 2px solid var(--neon-blue);
  }

  .tel-label {
    font-size: 10px;
    color: rgba(255,255,255,0.4);
    display: block;
  }

  .tel-val {
    font-size: 16px;
    font-weight: 600;
    color: #fff;
  }

  .horizon-container {
    width: 90px;
    height: 90px;
    border: 2px solid var(--neon-blue);
    border-radius: 50%;
    position: relative;
    overflow: hidden;
    background: rgba(0, 0, 0, 0.5);
    box-shadow: 0 0 10px rgba(0, 240, 255, 0.15);
    margin: 5px auto;
  }

  .horizon-line {
    width: 200%;
    height: 200%;
    background: linear-gradient(to bottom, #001a33 50%, #2b1a0a 50%);
    position: absolute;
    left: -50%;
    top: -50%;
    transform: rotate(0deg) translateY(0px);
    opacity: 0.85;
    transition: transform 0.05s ease-out;
  }

  .horizon-crosshair {
    width: 20px;
    height: 20px;
    position: absolute;
    top: 50%;
    left: 50%;
    transform: translate(-50%, -50%);
    z-index: 2;
  }

  .horizon-crosshair::before, .horizon-crosshair::after {
    content: '';
    position: absolute;
    background: var(--neon-pink);
  }
  .horizon-crosshair::before { left: 0; top: 9px; width: 20px; height: 2px; }
  .horizon-crosshair::after { left: 9px; top: 0; width: 2px; height: 20px; }

  .joy-container {
    display: flex;
    justify-content: space-around;
    padding: 5px 10px;
    margin-top: 5px;
  }

  .joy-wrapper {
    text-align: center;
  }

  .joy-label {
    font-family: 'Share Tech Mono', monospace;
    font-size: 11px;
    color: rgba(255, 255, 255, 0.4);
    margin-top: 5px;
    letter-spacing: 1px;
  }

  .joy {
    width: 140px;
    height: 140px;
    background: radial-gradient(circle, rgba(0, 240, 255, 0.03) 0%, rgba(12, 12, 20, 0.9) 100%);
    border: 2px solid var(--border-color);
    border-radius: 50%;
    position: relative;
    box-shadow: inset 0 0 15px rgba(0, 240, 255, 0.1), 0 0 10px rgba(0,0,0,0.5);
  }

  .joy::before {
    content: '';
    position: absolute;
    top: 50%;
    left: 0;
    width: 100%;
    height: 1px;
    background: rgba(0, 240, 255, 0.1);
  }
  .joy::after {
    content: '';
    position: absolute;
    left: 50%;
    top: 0;
    width: 1px;
    height: 100%;
    background: rgba(0, 240, 255, 0.1);
  }

  .knob {
    width: 48px;
    height: 48px;
    background: radial-gradient(circle, #fff 0%, #dbe8ff 30%, #4668a8 100%);
    border: 2px solid #fff;
    border-radius: 50%;
    position: absolute;
    top: 44px;
    left: 44px;
    box-shadow: 0 0 15px rgba(0, 240, 255, 0.6), 0 4px 10px rgba(0,0,0,0.8);
    pointer-events: none;
    z-index: 5;
  }

  .btn-panel {
    display: flex;
    justify-content: center;
    gap: 20px;
    padding: 10px 20px 20px 20px;
  }

  .btn {
    flex: 1;
    max-width: 180px;
    background: rgba(255,255,255,0.02);
    color: var(--neon-green);
    border: 2px solid var(--neon-green);
    padding: 12px 20px;
    font-size: 15px;
    font-family: 'Share Tech Mono', monospace;
    font-weight: bold;
    border-radius: 8px;
    letter-spacing: 2px;
    cursor: pointer;
    box-shadow: 0 0 10px rgba(0, 255, 102, 0.15);
    transition: all 0.2s ease-in-out;
  }

  .btn:active {
    transform: scale(0.95);
  }

  .btn.armed {
    color: var(--neon-pink);
    border-color: var(--neon-pink);
    box-shadow: 0 0 15px rgba(255, 0, 127, 0.3);
    background: rgba(255, 0, 127, 0.05);
  }
  
  .btn-cal {
    color: var(--neon-yellow);
    border-color: var(--neon-yellow);
    box-shadow: 0 0 10px rgba(255, 221, 0, 0.15);
  }

  .disabled {
    opacity: 0.4;
    pointer-events: none;
  }
</style>
</head>
<body>

  <div class="header">
    <h1>NANOCORE COMMAND SHELL</h1>
    <div id="status" class="status-tag">CONNECTING LINK...</div>
  </div>

  <div class="grid-container">
    <div class="card">
      <div class="telemetry-title">Telemetry Stream</div>
      <div class="telemetry-grid">
        <div class="tel-item"><span class="tel-label">PITCH ANGLE</span><span class="tel-val" id="pitchVal">0.0°</span></div>
        <div class="tel-item"><span class="tel-label">ROLL ANGLE</span><span class="tel-val" id="rollVal">0.0°</span></div>
        <div class="tel-item"><span class="tel-label">YAW ANGLE</span><span class="tel-val" id="yawVal">0.0°</span></div>
        <div class="tel-item"><span class="tel-label">THROTTLE OUT</span><span class="tel-val" id="throttleVal">0%</span></div>
      </div>
    </div>
    
    <div class="card" style="display: flex; flex-direction: column; justify-content: center;">
      <div class="telemetry-title" style="margin-bottom: 0;">Attitude Indicator</div>
      <div class="horizon-container">
        <div class="horizon-line" id="horizonLine"></div>
        <div class="horizon-crosshair"></div>
      </div>
    </div>
  </div>

  <div class="joy-container">
    <div class="joy-wrapper">
      <div class="joy" id="joyL"><div class="knob" id="knobL"></div></div>
      <div class="joy-label">THR / YAW</div>
    </div>
    <div class="joy-wrapper">
      <div class="joy" id="joyR"><div class="knob" id="knobR"></div></div>
      <div class="joy-label">PITCH / ROLL</div>
    </div>
  </div>

  <div class="btn-panel">
    <button class="btn btn-cal" onclick="triggerCalibrate()" id="calBtn">CALIBRATE IMU</button>
    <button class="btn" onclick="toggleArm()" id="armBtn">ARM ENGINE</button>
  </div>

<script>
  let ws;
  let armed = 0;
  let t = 0, p = 0, r = 0, y = 0;
  let statusVal = 0; // 0=disarmed, 1=armed, 2=calibrating, 3=uncalibrated
  
  function initWebSocket() {
    // Dynamically connect to the ESP32 IP
    ws = new WebSocket("ws://" + window.location.hostname + ":81/");
    
    ws.onopen = () => { 
      document.getElementById("status").innerText = "LINK CONNECTED (LOCKED)"; 
      document.getElementById("status").style.color = "var(--neon-blue)"; 
    };
    
    ws.onclose = () => { 
      document.getElementById("status").innerText = "SIGNAL LOSS! SAFE MODE ACTIVE"; 
      document.getElementById("status").style.color = "var(--neon-pink)";
      document.getElementById("armBtn").classList.add("disabled");
      document.getElementById("calBtn").classList.add("disabled");
    };
    
    ws.onmessage = (event) => {
      const msg = event.data;
      if (msg.startsWith("TEL:")) {
        // Format: TEL:P:0.0,R:0.0,Y:0.0,S:0,T:0
        let parts = {};
        msg.substring(4).split(",").forEach(part => {
          let kv = part.split(":");
          parts[kv[0]] = parseFloat(kv[1]);
        });
        
        // Update dashboard values
        document.getElementById("pitchVal").innerText = parts["P"].toFixed(1) + "°";
        document.getElementById("rollVal").innerText = parts["R"].toFixed(1) + "°";
        document.getElementById("yawVal").innerText = parts["Y"].toFixed(1) + "°";
        
        // Throttle percentage conversion
        let t_pct = Math.round((parts["T"] / 255.0) * 100);
        document.getElementById("throttleVal").innerText = t_pct + "%";
        
        // Dynamic Attitude Horizon
        const horizon = document.getElementById("horizonLine");
        if (horizon) {
          // Translate 1.5 pixels per degree of pitch, rotate roll
          horizon.style.transform = `rotate(${-parts["R"]}deg) translateY(${parts["P"] * 1.5}px)`;
        }
        
        // Status and arm button state management
        statusVal = parts["S"];
        const statusText = document.getElementById("status");
        const armBtn = document.getElementById("armBtn");
        const calBtn = document.getElementById("calBtn");
        
        if (statusVal === 2) {
          statusText.innerText = "IMU CALIBRATING... DO NOT MOVE";
          statusText.style.color = "var(--neon-yellow)";
          armBtn.classList.add("disabled");
          calBtn.classList.add("disabled");
        } else if (statusVal === 3) {
          statusText.innerText = "IMU UNCALIBRATED! CALIBRATE REQUIRED";
          statusText.style.color = "var(--neon-pink)";
          armBtn.classList.add("disabled");
          calBtn.classList.remove("disabled");
        } else if (statusVal === 1) {
          statusText.innerText = "NANOCORE ARMED - MOTORS LIVE!";
          statusText.style.color = "var(--neon-pink)";
          armBtn.innerText = "KILL ENGINE";
          armBtn.classList.add("armed");
          armBtn.classList.remove("disabled");
          calBtn.classList.add("disabled");
          armed = 1;
        } else {
          statusText.innerText = "LINK CONNECTED (LOCKED)";
          statusText.style.color = "var(--neon-green)";
          armBtn.innerText = "ARM ENGINE";
          armBtn.classList.remove("armed");
          armBtn.classList.remove("disabled");
          calBtn.classList.remove("disabled");
          armed = 0;
        }
      }
    };
  }
  
  function sendCmd() {
    if(ws && ws.readyState === WebSocket.OPEN && statusVal !== 2) {
      ws.send(`CMD:T:${t},P:${p},R:${r},Y:${y},A:${armed}`);
    }
  }
  // Fast 20Hz loop for real-time gamepad input transmission
  setInterval(sendCmd, 50); 

  function toggleArm() {
    if (statusVal === 3) return; // Cannot arm if uncalibrated
    
    armed = armed ? 0 : 1;
    const btn = document.getElementById("armBtn");
    if (armed) {
      // Check throttle safety
      if (t > 15) {
        alert("Throttle must be low to arm!");
        armed = 0;
        return;
      }
    }
    sendCmd();
  }

  function triggerCalibrate() {
    if (armed) return;
    if(ws && ws.readyState === WebSocket.OPEN) {
      ws.send("CMD:CALIBRATE");
    }
  }

  function handleJoy(joyId, knobId, isLeft) {
    const joy = document.getElementById(joyId);
    const knob = document.getElementById(knobId);
    let active = false, cx = 70, cy = 70, maxR = 55;

    const move = (clientX, clientY) => {
      const rect = joy.getBoundingClientRect();
      let dx = clientX - rect.left - cx;
      let dy = clientY - rect.top - cy;
      const dist = Math.min(Math.sqrt(dx*dx + dy*dy), maxR);
      const angle = Math.atan2(dy, dx);
      dx = Math.cos(angle) * dist;
      dy = Math.sin(angle) * dist;
      
      knob.style.transform = `translate(${dx}px, ${dy}px)`;
      
      if(isLeft) { 
        // Throttle maps 0 to 255 (bottom is 0, top is 255)
        t = Math.max(0, Math.round(((maxR - dy) / (2 * maxR)) * 255));
        y = Math.round((dx / maxR) * 100); // Yaw maps -100 to 100
      } else { 
        p = Math.round((-dy / maxR) * 100); // Pitch maps -100 to 100 (up is forward/positive)
        r = Math.round((dx / maxR) * 100);  // Roll maps -100 to 100 (right is positive)
      }
    };

    joy.addEventListener('touchstart', e => { 
      active = true; 
      move(e.touches[0].clientX, e.touches[0].clientY); 
    });
    joy.addEventListener('touchmove', e => { 
      if(active) { 
        e.preventDefault(); 
        move(e.touches[0].clientX, e.touches[0].clientY); 
      } 
    }, {passive: false});
    joy.addEventListener('touchend', () => { 
      active = false; 
      if(!isLeft) { 
        p = 0; r = 0; 
        knob.style.transform = `translate(0px, 0px)`; 
      } 
      if(isLeft) { 
        y = 0; 
        // Throttle stays at its current vertical position, but Yaw centers horizontally
        let throttleOffset = maxR - (t * 2 * maxR / 255);
        knob.style.transform = `translate(0px, ${throttleOffset}px)`; 
      }
    });

    // Mouse support for desktop testing
    joy.addEventListener('mousedown', e => { 
      active = true; 
      move(e.clientX, e.clientY); 
    });
    window.addEventListener('mousemove', e => { 
      if(active) move(e.clientX, e.clientY); 
    });
    window.addEventListener('mouseup', () => { 
      if(active) {
        active = false; 
        if(!isLeft) { 
          p = 0; r = 0; 
          knob.style.transform = `translate(0px, 0px)`; 
        }
        if(isLeft) { 
          y = 0; 
          let throttleOffset = maxR - (t * 2 * maxR / 255);
          knob.style.transform = `translate(0px, ${throttleOffset}px)`; 
        }
      }
    });
  }

  // Bind controls
  handleJoy('joyL', 'knobL', true);
  handleJoy('joyR', 'knobR', false);
  
  // Set left stick knob starting position to bottom (throttle 0)
  document.getElementById('knobL').style.transform = `translate(0px, 55px)`;
  
  window.onload = initWebSocket;
</script>
</body>
</html>
)rawliteral";

// ==========================================
// 10. SETUP & INITS
// ==========================================
void setup() {
  Serial.begin(115200);
  
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW); // LED OFF initially
  
  // Initialize Motor PWM Outputs using ESP32 v3.0+ LEDC API
  for (int i = 0; i < 4; i++) {
    ledcAttach(motorPins[i], pwmFreq, pwmResolution);
    ledcWrite(motorPins[i], 0);
  }
  
  // Initialize MPU6050
  if (!baslatMPU6050()) {
    Serial.println("[FATAL] MPU6050 baslatilamadi! System halted.");
    // Fast blink LED to signal hard failure
    while (true) {
      digitalWrite(LED_PIN, HIGH);
      delay(100);
      digitalWrite(LED_PIN, LOW);
      delay(100);
    }
  }
  
  // Startup Auto-Calibration
  kalibreEtGyro();
  
  // Start WiFi Access Point
  WiFi.softAP(ssid, password);
  Serial.print("[*] WiFi AP Baslatildi. SSID: ");
  Serial.println(ssid);
  Serial.print("[*] Web Panel adresi: http://");
  Serial.println(WiFi.softAPIP());
  
  // Register webserver paths
  server.on("/", []() {
    server.send(200, "text/html", INDEX_HTML);
  });
  server.begin();
  
  // Start WebSocket Server
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
  
  lastCmdTime = millis();
}

// ==========================================
// 11. TIMED FLIGHT CONTROL LOOP (250Hz / 4ms)
// ==========================================
unsigned long lastLoopTimeMicros = 0;
const unsigned long loopTimeTargetMicros = 4000; // 4ms = 250Hz Control Loop Rate

void loop() {
  unsigned long currentTimeMicros = micros();
  
  // Only execute when the 4ms time slice arrives
  if (currentTimeMicros - lastLoopTimeMicros >= loopTimeTargetMicros) {
    float dt = (currentTimeMicros - lastLoopTimeMicros) / 1000000.0;
    lastLoopTimeMicros = currentTimeMicros;
    
    // 1. Process asynchronous communications
    handleSerial();
    webSocket.loop();
    server.handleClient();
    
    // 2. Fail-Safe: If no command received for > 500ms, shut down motors
    if (millis() - lastCmdTime > 500) {
      if (armed) {
        Serial.println("[WARNING] FAIL-SAFE TRIGGERED! SIGNAL LOSS! DISARMING...");
        acilStop();
      }
    }
    
    // 3. Read raw data from MPU6050
    okuMPU6050();
    
    // 4. Calculate angles (Complementary Filter) & check tumble safety
    hesaplaAcilar(dt);
    
    // 5. Compute PID stabilization outputs & mix outputs
    if (armed && isCalibrated && !isCalibrating) {
      // Toggle LED rapidly to indicate armed state
      static int blinkCount = 0;
      if (blinkCount++ % 25 == 0) {
        digitalWrite(LED_PIN, !digitalRead(LED_PIN));
      }
      
      hesaplaPID(dt);
      miksajUygula();
    } else {
      // Keep LED solid ON to indicate disarmed ready state (if calibrated)
      if (isCalibrated && !isCalibrating) {
        digitalWrite(LED_PIN, HIGH);
      }
      acilStop();
    }
    
    // 6. Send telemetry back to connected client
    gonderTelemetri();
  }
}