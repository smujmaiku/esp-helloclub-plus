#include <Arduino.h>
#include <WiFi.h>

#define LED_PIN 48

#if !defined(WIFI_SSID) || !defined(WIFI_PASS)
#error "Define secrets.ini"
#endif

void setup() {
  pinMode(LED_PIN, OUTPUT);

  Serial.begin(115200);
  Serial.println("[main] start");

  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(250);
  }
  Serial.printf("[wifi] ip %s\n", WiFi.localIP().toString().c_str());
}

void loop() {
  digitalWrite(LED_PIN, HIGH);
  Serial.println("LED is ON");
  delay(1000);

  digitalWrite(LED_PIN, LOW);
  Serial.println("LED is OFF");
  delay(1000);
}
