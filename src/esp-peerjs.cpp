#include "esp-peerjs.h"

#include <WiFi.h>

// PeerJS servers drop clients that are silent for 60s
#define PEERJS_HEARTBEAT_MS 5000
// Delay before answering a candidate
#define CANDIDATE_SETTLE_MS 1000
#define CANDIDATE_MAX_WAIT_MS 5000

ESP32PeerJS::ESP32PeerJS(String host, int port, String path, String peerId,
                         bool secure) {
  _host = host;
  _port = port;
  _path = path;
  _peerId = peerId;
  _secure = secure;
  _peerConnection = NULL;
  _onDataCallback = NULL;
  _isConnectedToSignaling = false;
  _isDataChannelOpen = false;
  _lastHeartbeat = 0;
  _hasPendingOffer = false;
  _pendingOfferAt = 0;
  _lastCandidateAt = 0;
}

ESP32PeerJS::~ESP32PeerJS() { _destroyPeerConnection(); }

bool ESP32PeerJS::begin(DataCallback callback) {
  _onDataCallback = callback;

  if (peer_init() != 0) {
    Serial.println("[PeerJS] libpeer initialization failed.");
    return false;
  }

  // <path>/peerjs?key=<key>&id=<id>&token=<token>
  String basePath = _path;
  while (basePath.endsWith("/")) {
    basePath.remove(basePath.length() - 1);
  }
  String token = String(esp_random(), HEX);
  _calculatedPath =
      basePath + "/peerjs?key=peerjs&id=" + _peerId + "&token=" + token;

  _wsClient.onEvent([this](WStype_t type, uint8_t* payload, size_t length) {
    this->_handleWsEvent(type, payload, length);
  });

  _wsClient.setReconnectInterval(5000);
  if (_secure) {
    _wsClient.beginSSL(_host, _port, _calculatedPath);
  } else {
    _wsClient.begin(_host, _port, _calculatedPath);
  }
  return true;
}

bool ESP32PeerJS::sendJSON(const String& jsonPayload) {
  if (!isPeerConnected()) return false;

  // libpeer requires a non-const buffer
  String message = jsonPayload;
  return peer_connection_datachannel_send(
             _peerConnection, (char*)message.c_str(), message.length()) >= 0;
}

bool ESP32PeerJS::isSignalingConnected() { return _isConnectedToSignaling; }

bool ESP32PeerJS::isPeerConnected() {
  return _peerConnection && _isDataChannelOpen &&
         peer_connection_get_state(_peerConnection) ==
             PEER_CONNECTION_COMPLETED;
}

void ESP32PeerJS::handle() {
  _wsClient.loop();

  if (_isConnectedToSignaling &&
      millis() - _lastHeartbeat > PEERJS_HEARTBEAT_MS) {
    _lastHeartbeat = millis();
    _wsClient.sendTXT("{\"type\":\"HEARTBEAT\"}");
  }

  if (_hasPendingOffer) {
    unsigned long now = millis();
    bool settled = now - _lastCandidateAt > CANDIDATE_SETTLE_MS;
    bool timedOut = now - _pendingOfferAt > CANDIDATE_MAX_WAIT_MS;
    if (settled || timedOut) {
      _applyPendingOffer();
    }
  }

  if (_peerConnection) {
    peer_connection_loop(_peerConnection);
  }
}

// ---- Signaling ----

void ESP32PeerJS::_handleWsEvent(WStype_t type, uint8_t* payload,
                                 size_t length) {
  switch (type) {
    case WStype_DISCONNECTED:
      _isConnectedToSignaling = false;
      break;
    case WStype_CONNECTED:
      _isConnectedToSignaling = true;
      _lastHeartbeat = millis();
      break;
    case WStype_TEXT: {
      JsonDocument msg;
      DeserializationError err = deserializeJson(msg, payload, length);
      if (err) {
        Serial.printf("[PeerJS] bad signal json: %s\n", err.c_str());
        break;
      }
      _handleSignal(msg);
      break;
    }
    default:
      break;
  }
}

void ESP32PeerJS::_handleSignal(JsonDocument& msg) {
  const char* type = msg["type"] | "";
  const char* src = msg["src"] | "";

  if (strcmp(type, "OPEN") == 0) {
    Serial.printf("[PeerJS] registered as %s\n", _peerId.c_str());
  } else if (strcmp(type, "OFFER") == 0) {
    _handleOffer(msg);
  } else if (strcmp(type, "CANDIDATE") == 0) {
    _handleCandidate(msg);
  } else if (strcmp(type, "LEAVE") == 0 || strcmp(type, "EXPIRE") == 0) {
    if (_remotePeerId == src) {
      Serial.printf("[PeerJS] %s left\n", src);
      _destroyPeerConnection();
    }
  } else if (strcmp(type, "ID-TAKEN") == 0 ||
             strcmp(type, "INVALID-KEY") == 0 || strcmp(type, "ERROR") == 0) {
    const char* reason = msg["payload"]["msg"] | "";
    Serial.printf("[PeerJS] server %s: %s\n", type, reason);
  }
}

void ESP32PeerJS::_handleOffer(JsonDocument& msg) {
  JsonObject payload = msg["payload"];
  const char* mediaType = payload["type"] | "";
  if (strcmp(mediaType, "data") != 0) {
    Serial.printf("[PeerJS] ignoring %s offer\n", mediaType);
    return;
  }

  // Prefer new connections
  _destroyPeerConnection();

  _remotePeerId = msg["src"] | "";
  _connectionId = payload["connectionId"] | "";
  _pendingOfferSdp = payload["sdp"]["sdp"] | "";

  // libpeer answers with 'datachannel'
  _offerMid = "0";
  int midAt = _pendingOfferSdp.indexOf("a=mid:");
  if (midAt >= 0) {
    int midEnd = _pendingOfferSdp.indexOf("\r\n", midAt);
    _offerMid = _pendingOfferSdp.substring(midAt + 6, midEnd);
  }

  _hasPendingOffer = true;
  _pendingOfferAt = millis();
  _lastCandidateAt = millis();
  Serial.printf("[PeerJS] offer from %s\n", _remotePeerId.c_str());
}

void ESP32PeerJS::_handleCandidate(JsonDocument& msg) {
  JsonObject payload = msg["payload"];
  const char* connectionId = payload["connectionId"] | "";
  const char* candidate = payload["candidate"]["candidate"] | "";

  if (_connectionId != connectionId || strlen(candidate) == 0) return;

  // libpeer pairs only when the remote description is set
  if (!_hasPendingOffer) {
    Serial.println("[PeerJS] dropping late candidate");
    return;
  }

  _pendingOfferSdp += "a=";
  _pendingOfferSdp += candidate;
  _pendingOfferSdp += "\r\n";
  _lastCandidateAt = millis();
}

void ESP32PeerJS::_applyPendingOffer() {
  _hasPendingOffer = false;

  if (!_createPeerConnection()) {
    Serial.println("[PeerJS] libpeer connection creation failed.");
    return;
  }

  peer_connection_set_remote_description(_peerConnection,
                                         _pendingOfferSdp.c_str());
  _pendingOfferSdp = "";
}

void ESP32PeerJS::_sendAnswer(const char* localSdp) {
  String sdp = localSdp;
  sdp.replace("a=mid:datachannel", "a=mid:" + _offerMid);
  sdp.replace("BUNDLE datachannel", "BUNDLE " + _offerMid);

  JsonDocument msg;
  msg["type"] = "ANSWER";
  msg["dst"] = _remotePeerId;
  JsonObject payload = msg["payload"].to<JsonObject>();
  payload["sdp"]["type"] = "answer";
  payload["sdp"]["sdp"] = sdp;
  payload["type"] = "data";
  payload["connectionId"] = _connectionId;
  payload["browser"] = "esp32";
  _sendSignal(msg);

  Serial.printf("[PeerJS] answered %s\n", _remotePeerId.c_str());
}

void ESP32PeerJS::_sendSignal(JsonDocument& msg) {
  if (!_isConnectedToSignaling) return;
  String out;
  serializeJson(msg, out);
  _wsClient.sendTXT(out);
}

// ---- libpeer ----

bool ESP32PeerJS::_createPeerConnection() {
  PeerConfiguration config = {};
  config.ice_servers[0].urls = "stun:stun.l.google.com:19302";
  config.audio_codec = CODEC_NONE;
  config.video_codec = CODEC_NONE;
  config.datachannel = DATA_CHANNEL_STRING;
  config.user_data = this;

  _peerConnection = peer_connection_create(&config);
  if (!_peerConnection) return false;

  peer_connection_onicecandidate(_peerConnection, _static_onicecandidate);
  peer_connection_oniceconnectionstatechange(_peerConnection,
                                             _static_onstatechange);
  peer_connection_ondatachannel(_peerConnection, _static_onmessage,
                                _static_onopen, _static_onclose);
  return true;
}

void ESP32PeerJS::_destroyPeerConnection() {
  _hasPendingOffer = false;
  _isDataChannelOpen = false;
  if (_peerConnection) {
    peer_connection_destroy(_peerConnection);
    _peerConnection = NULL;
  }
}

void ESP32PeerJS::_static_onicecandidate(char* sdp, void* user_ctx) {
  ESP32PeerJS* instance = static_cast<ESP32PeerJS*>(user_ctx);
  instance->_sendAnswer(sdp);
}

void ESP32PeerJS::_static_onstatechange(PeerConnectionState state,
                                        void* user_ctx) {
  ESP32PeerJS* instance = static_cast<ESP32PeerJS*>(user_ctx);
  Serial.printf("[PeerJS] state %s\n", peer_connection_state_to_string(state));
  if (state == PEER_CONNECTION_FAILED ||
      state == PEER_CONNECTION_DISCONNECTED ||
      state == PEER_CONNECTION_CLOSED) {
    instance->_isDataChannelOpen = false;
  }
}

void ESP32PeerJS::_static_onmessage(char* msg, size_t len, void* user_ctx,
                                    uint16_t sid) {
  ESP32PeerJS* instance = static_cast<ESP32PeerJS*>(user_ctx);
  if (instance->_onDataCallback) {
    String payload;
    payload.concat(msg, len);
    instance->_onDataCallback(payload);
  }
}

void ESP32PeerJS::_static_onopen(void* user_ctx) {
  ESP32PeerJS* instance = static_cast<ESP32PeerJS*>(user_ctx);
  instance->_isDataChannelOpen = true;
  Serial.println("[PeerJS] data channel open");
}

void ESP32PeerJS::_static_onclose(void* user_ctx) {
  ESP32PeerJS* instance = static_cast<ESP32PeerJS*>(user_ctx);
  instance->_isDataChannelOpen = false;
  Serial.println("[PeerJS] data channel closed");
}
