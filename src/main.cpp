#include <Arduino.h>
#include <WiFi.h>

#include "./esp-peerjs.h"

// ESP Status LED signals
// 4 blinks: Disconnected from wifi
// 3 blinks: Connected to wifi, Disconnected from peerjs signaling
// 2 blinks: Connected to signaling, has no peer
// 1 blink: Connected to peer, ready
// Solid: Crashed
// Nothing: Crashed

#define STS_LED 48

// libpeer generates DTLS keys on the loop task; the 8KB default is too small
SET_LOOP_TASK_STACK_SIZE(16 * 1024);

const unsigned long MAX_MILLIS = 3900000000;  // ~45 days

#define PEERJS_HOST "0.peerjs.com"
#define PEERJS_PORT 443
#define PEERJS_PATH "/"

ESP32PeerJS* peerLink = nullptr;

#if !defined(WIFI_SSID) || !defined(WIFI_PASS) || !defined(PEER_ID)
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
  } else if (!peerLink->isSignalingConnected()) {  // Peerjs disconnected
    statusLED(3);
  } else if (!peerLink->isPeerConnected()) {  // Waiting on Peer
    statusLED(2);
  } else {  // All good
    statusLED(1);
  }
}

void handlePeerJSON(const String& rawJson) {
  printf("[peer] data:\n%s\n", rawJson.c_str());
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

  peerLink = new ESP32PeerJS(PEERJS_HOST, PEERJS_PORT, PEERJS_PATH, PEER_ID);
  peerLink->begin(handlePeerJSON);
}

void loop() {
  // TODO: Handle Wifi
  peerLink->handle();
  // TODO: Buffer keypad

  handleStatus();

  // TODO: Pop keypad buffer

  // testing
  static unsigned long lastSendTime = 0;
  if (millis() - lastSendTime > 4000) {
    lastSendTime = millis();

    if (peerLink->isPeerConnected()) {
      Serial.println("[peer] sending ok");
      String jsonPayload = "{\"type\":\"status\",\"payload\":\"ok\"}";
      peerLink->sendJSON(jsonPayload);
    } else {
      Serial.println("[peer] cannot send status");
    }
  }
  if (Serial.available()) {
    String input = Serial.readStringUntil('\n');

    String jsonPayload = "{\"type\":\"scan\",\"payload\":\"" + input + "\"}";
    peerLink->sendJSON(jsonPayload);
  }

  // Restart esp before millis overflow
  if (!peerLink->isPeerConnected() && millis() > MAX_MILLIS) {
    ESP.restart();
  }

  delay(10);
}
