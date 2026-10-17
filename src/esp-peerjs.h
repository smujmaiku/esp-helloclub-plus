#ifndef ESP32_PEERJS_H
#define ESP32_PEERJS_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WebSocketsClient.h>
#include <peer.h>

class ESP32PeerJS {
 public:
  typedef void (*DataCallback)(const String& jsonString);

  ESP32PeerJS(String host, int port, String path, String peerId,
              bool secure = true);
  ~ESP32PeerJS();

  bool begin(DataCallback callback);
  bool sendJSON(const String& jsonPayload);
  bool isSignalingConnected();
  bool isPeerConnected();
  void handle();

 private:
  String _host;
  int _port;
  String _path;
  String _peerId;
  bool _secure;
  String _calculatedPath;

  WebSocketsClient _wsClient;
  PeerConnection* _peerConnection;
  DataCallback _onDataCallback;

  bool _isConnectedToSignaling;
  bool _isDataChannelOpen;
  unsigned long _lastHeartbeat;

  // Remote peer that sent the current offer
  String _remotePeerId;
  String _connectionId;
  String _offerMid;

  // Offer held back until the remote stops trickling candidates
  String _pendingOfferSdp;
  bool _hasPendingOffer;
  unsigned long _pendingOfferAt;
  unsigned long _lastCandidateAt;

  void _handleWsEvent(WStype_t type, uint8_t* payload, size_t length);
  void _handleSignal(JsonDocument& msg);
  void _handleOffer(JsonDocument& msg);
  void _handleCandidate(JsonDocument& msg);
  void _applyPendingOffer();
  void _sendAnswer(const char* localSdp);
  void _sendSignal(JsonDocument& msg);
  bool _createPeerConnection();
  void _destroyPeerConnection();

  static void _static_onicecandidate(char* sdp, void* user_ctx);
  static void _static_onstatechange(PeerConnectionState state, void* user_ctx);
  static void _static_onmessage(char* msg, size_t len, void* user_ctx,
                                uint16_t sid);
  static void _static_onopen(void* user_ctx);
  static void _static_onclose(void* user_ctx);
};

#endif
