#include <Arduino.h>
#include <WiFi.h>

// ESP Status LED signals
// 4 blinks: Disconnected from wifi
// 3 blinks: Connected to wifi, Disconnected from peerjs signaling
// 2 blinks: Connected to signaling, has no peer
// 1 blink: Connected to peer, ready
// Solid: Crashed
// Nothing: Crashed

#define STS_LED 48
const unsigned long MAX_MILLIS = 3900000000;  // ~45 days

#if !defined(WIFI_SSID) || !defined(WIFI_PASS)
#error "Define secrets.ini"
#endif

// Blink the status light over 2s
void statusLED(byte status) {
  const unsigned long speed = 200;  // 5 fps
  bool odd = millis() % speed < 50;
  unsigned long limit = (millis() / speed) % (2000 / speed);
  digitalWrite(STS_LED, odd && (limit < status) ? HIGH : LOW);
}

void delayPlus(unsigned long ms, byte status) {
  statusLED(status);
  for (unsigned long i = 0; i < (ms / 10); i++) {
    delay(10);
    statusLED(status);
  }
}

// Status light reflects connectivity
void handleStatus() {
  if (WiFi.status() != WL_CONNECTED) {  // Wifi disconnected
    statusLED(4);
    // } else if (!peer.isSignalOpen()) {  // Peerjs disconnected
    //   statusLED(3);
    // } else if (!peer.isOpen()) {  // Waiting on Peer
    //   statusLED(2);
  } else {  // All good
    statusLED(1);
  }
}

void setup() {
  pinMode(STS_LED, OUTPUT);

  Serial.begin(115200);
  Serial.println("[main] start");

  // Bring up wifi
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delayPlus(500, 5);
  }

  Serial.printf("[wifi] ip %s\n", WiFi.localIP().toString().c_str());
}

void loop() {
  // TODO: Handle Wifi
  // TODO: Handle Peerjs
  // TODO: Buffer keypad

  handleStatus();

  // TODO: Pop keypad buffer

  // Restart esp before millis overflow
  if (millis() > MAX_MILLIS) {  // TODO: !peer.isOpen() &&
    ESP.restart();
  }

  delay(10);
}
