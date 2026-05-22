#include <WiFi.h>

void setup() {
  Serial.begin(115200);
  delay(3000);
  WiFi.softAP("DRONE-TEST", "12345678");
  Serial.println(WiFi.softAPIP());
}

void loop() {
}
