// ESP32 v3.0+ TEK MOTOR ADAPTÖR TEST KODU

// Sadece 1. Motor (Pin 10) çalışacak, diğerleri kapalı kalacak.



const int motorPinleri[] = {10, 7, 11, 6};

const int pinSayisi = 4;



const int frekans = 5000;

const int cozunurluk = 8;



void setup() {

  // 4 pini de sisteme tanıtıyoruz

  for (int i = 0; i < pinSayisi; i++) {

    ledcAttach(motorPinleri[i], frekans, cozunurluk);

    ledcWrite(motorPinleri[i], 0); // Başlangıçta hepsini durdur

  }

 

  delay(3000); // Sistemin oturması için 3 saniye bekle

}

  int hiz=50;

void loop() {

  // SADECE 1. MOTORA (Pin 10) GÜÇ VERİYORUZ (Hız: 100)

  ledcWrite(motorPinleri[0], hiz);

  int hiz=25;

  // DİĞER MOTORLARI TAMAMEN KAPALI TUTUYORUZ

  ledcWrite(motorPinleri[1], hiz);

  ledcWrite(motorPinleri[2], hiz);

  ledcWrite(motorPinleri[3], hiz);

 

  delay(100);

}