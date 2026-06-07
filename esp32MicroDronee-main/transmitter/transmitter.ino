#include <SPI.h>
#include <RF24.h>

// Arduino Uno / Nano SPI Pinleri: 
// MOSI: 11, MISO: 12, SCK: 13
// CE ve CSN pinleri (istediğiniz dijital pinleri kullanabilirsiniz)
#define NRF_CE_PIN 9
#define NRF_CSN_PIN 10

RF24 radio(NRF_CE_PIN, NRF_CSN_PIN);
const byte address[6] = "00001";

// Drone'daki ile BİREBİR AYNI payload yapısı
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

void setup() {
  Serial.begin(115200); // Bilgisayar ile hızlı haberleşmek için baud rate
  
  if (!radio.begin()) {
    Serial.println("NRF24L01 Verici Bulunamadi! Baglantilari kontrol edin.");
    while (1) {} // Modül yoksa burada bekle
  }
  
  radio.openWritingPipe(address);
  radio.setPALevel(RF24_PA_MAX);      // MAX güç = Maksimum menzil!
  radio.setDataRate(RF24_250KBPS);    // 250KBPS = En uzun menzil (düşük hız = yüksek duyarlılık)
  radio.setChannel(108);              // WiFi kanallarından uzak, sabit kanal
  radio.setRetries(5, 15);            // 5x250us=1250us bekleme, 15 tekrar deneme
  radio.enableDynamicPayloads();
  radio.enableAckPayload();
  radio.setCRCLength(RF24_CRC_16);    // 16-bit CRC = Daha güvenilir veri kontrolü
  radio.stopListening();              // Verici modunda
  
  // Güvenli Başlangıç Değerleri
  nrfData.throttle = 0;
  nrfData.pitch = 0;
  nrfData.roll = 0;
  nrfData.yaw = 0;
  nrfData.cmd = 0;
  
  Serial.println("Verici Hazir!");
  Serial.println("Format su sekilde olmali -> T:100,P:0,R:0,Y:0,C:0");
}

void loop() {
  // 1. Bilgisayardan Veri Geliyor mu Kontrol Et
  if (Serial.available() > 0) {
    String input = Serial.readStringUntil('\n');
    input.trim();
    int t, p, r, y, c;
    if (sscanf(input.c_str(), "T:%d,P:%d,R:%d,Y:%d,C:%d", &t, &p, &r, &y, &c) == 5) {
      nrfData.throttle = constrain(t, 0, 255);
      nrfData.pitch    = constrain(p, -100, 100);
      nrfData.roll     = constrain(r, -100, 100);
      nrfData.yaw      = constrain(y, -100, 100);
      nrfData.cmd      = c;
      // Echo kaldırıldı - serial buffer'ı dolduruyordu
    }
  }

  // 2. Drone'a paketi gönder ve telemetri oku
  static unsigned long lastSend = 0;
  if (millis() - lastSend > 50) {
    bool basarili = radio.write(&nrfData, sizeof(NRF_Payload));
    
    if (basarili) {
      // AckPayload varsa değerleri güncelle
      if (radio.isAckPayloadAvailable()) {
        radio.read(&telemetryData, sizeof(NRF_Telemetry));
      }
    }
    
    // Her durumda TEL yaz (AckPayload gelse de gelmese de)
    // Böylece Python her zaman veri alır
    Serial.print("TEL:P:"); Serial.print(telemetryData.pitch, 1);
    Serial.print(",R:");    Serial.print(telemetryData.roll, 1);
    Serial.print(",Y:");    Serial.print(telemetryData.yaw, 1);
    Serial.print(",S:");    Serial.println((int)telemetryData.cal_status);
    
    lastSend = millis();
  }
}
