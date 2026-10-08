#ifndef ESP32_PEERJS_H
#define ESP32_PEERJS_H

#include <Arduino.h>
#include <WebSocketsClient.h>
#include <peer.h>

class ESP32PeerJS {
 public:
  typedef void (*DataCallback)(const String& jsonString);

  ESP32PeerJS(String host, int port, String path, String peerId);

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
  String _calculatedPath;

  WebSocketsClient _wsClient;
  Peer* _peerConnection;
  DataCallback _onDataCallback;

  bool _isConnectedToSignaling;
  bool _isPeerConnected;
  unsigned long _lastReconnectAttempt;

  void _handleWsEvent(WStype_t type, uint8_t* payload, size_t length);

  static void _static_webrtc_to_signaling_cb(const char* msg, void* user_ctx);
  static void _static_webrtc_data_incoming_cb(const uint8_t* data, size_t len,
                                              void* user_ctx);
};

#endif
