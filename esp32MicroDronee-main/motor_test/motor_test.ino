const int motorPins[] = {10, 7, 11, 6};
const int pwmFreq = 5000;//sol ön sol arka sağ arka sağ ön
const int pwmResolution = 8;

void setup() {
  Serial.begin(115200);
  delay(2000); // Sistemin hazır olması için kısa bir bekleme
  Serial.println("Motor Testi Başlıyor...");
  Serial.println("LÜTFEN GÜVENLİK İÇİN PERVANELERİ ÇIKARINIZ!");

  // Motor pinlerini PWM için ayarla
  for (int i = 0; i < 4; i++) {
    ledcAttach(motorPins[i], pwmFreq, pwmResolution);
    ledcWrite(motorPins[i], 0); // Başlangıçta hepsi kapalı
  }
}

void loop() {
  // Motorları sırayla çalıştır
  for (int i = 0; i < 4; i++) {
    Serial.print("Şu an çalışan motor pini: ");
    Serial.println(motorPins[i]);
    
    // Motoru hafif bir güçte çalıştır (0-255 arası)
    // Motorunuzun tepkisine göre bu değeri artırabilir veya azaltabilirsiniz. 
    // 70-100 arası genellikle pervanesiz dönüşü görmek için idealdir.
    ledcWrite(motorPins[i], 80); 
    
    delay(2000); // 2 saniye boyunca çalışsın
    
    // Motoru durdur
    ledcWrite(motorPins[i], 0);
    
    delay(2000); // Sonraki motora geçmeden önce 2 saniye bekle
  }
}