#include "esp-peerjs.h"

#include <WiFi.h>

ESP32PeerJS::ESP32PeerJS(String host, int port, String path, String peerId) {
  _host = host;
  _port = port;
  _path = path;
  _peerId = peerId;
  _peerConnection = NULL;
  _isConnectedToSignaling = false;
  _isPeerConnected = false;
  _lastReconnectAttempt = 0;
}

bool ESP32PeerJS::begin(DataCallback callback) {
  _onDataCallback = callback;

  _peerConnection = peer_create();
  if (!_peerConnection) {
    Serial.println("[PeerJS] libpeer core framework creation failed.");
    return false;
  }

  _peerConnection->user_data = this;
  _peerConnection->on_msg = _static_webrtc_to_signaling_cb;
  _peerConnection->on_data = _static_webrtc_data_incoming_cb;

  if (peer_init(_peerConnection) != 0) {
    Serial.println("[PeerJS] libpeer initialization failed.");
    return false;
  }

  _calculatedPath = _path + "/peerjs/id/" + _peerId;
  _wsClient.begin(_host, _port, _calculatedPath);

  _wsClient.onEvent([this](WStype_t type, uint8_t* payload, size_t length) {
    this->_handleWsEvent(type, payload, length);
  });

  _wsClient.setReconnectInterval(5000);
  return true;
}

bool ESP32PeerJS::sendJSON(const String& jsonPayload) {
  if (!_peerConnection || !_isPeerConnected) return false;

  return (peer_send_data(_peerConnection, (const uint8_t*)jsonPayload.c_str(),
                         jsonPayload.length()) == 0);
}

bool ESP32PeerJS::isSignalingConnected() { return _isConnectedToSignaling; }

bool ESP32PeerJS::isPeerConnected() {
  if (_peerConnection) {
    _isPeerConnected = (_peerConnection->state == PEER_STATE_CONNECTED);
  }
  return _isPeerConnected;
}

void ESP32PeerJS::handle() {
  _wsClient.loop();

  if (!_isConnectedToSignaling && (millis() - _lastReconnectAttempt > 10000)) {
    _lastReconnectAttempt = millis();
    if (WiFi.status() == WL_CONNECTED) {
      _wsClient.begin(_host, _port, _calculatedPath);
    }
  }

  if (_peerConnection) {
    peer_loop(_peerConnection);
  }
}

void ESP32PeerJS::_handleWsEvent(WStype_t type, uint8_t* payload,
                                 size_t length) {
  switch (type) {
    case WStype_DISCONNECTED:
      _isConnectedToSignaling = false;
      break;
    case WStype_CONNECTED:
      _isConnectedToSignaling = true;
      break;
    case WStype_TEXT:
      if (_peerConnection) {
        peer_parse_msg(_peerConnection, (const char*)payload);
      }
      break;
    default:
      break;
  }
}

void ESP32PeerJS::_static_webrtc_to_signaling_cb(const char* msg,
                                                 void* user_ctx) {
  ESP32PeerJS* instance = static_cast<ESP32PeerJS*>(user_ctx);
  if (instance->_isConnectedToSignaling) {
    instance->_wsClient.sendTXT(msg);
  }
}

void ESP32PeerJS::_static_webrtc_data_incoming_cb(const uint8_t* data,
                                                  size_t len, void* user_ctx) {
  ESP32PeerJS* instance = static_cast<ESP32PeerJS*>(user_ctx);
  if (instance->_onDataCallback) {
    String payload = "";
    for (size_t i = 0; i < len; i++) {
      payload += (char)data[i];
    }
    instance->_onDataCallback(payload);
  }
}
