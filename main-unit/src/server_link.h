#pragma once

#include <Arduino.h>
#include <WebSocketsClient.h>

#include "narrow_detector.h"

// One telemetry packet, the same fields the Android app sends
struct Telemetry {
  double lat, lon;
  float heading;
  float accelX, accelY, accelZ;
  float gyroX, gyroY, gyroZ;
  float speed;        // m/s
  bool hasBearing;
  float bearing;      // GPS direction of travel, degrees
  float accuracy;     // metres
};

// Everything that talks to the server, doing what the app does:
//   1. join Wi-Fi
//   2. log in:             POST /api/auth/login   → token
//   3. link this device:   POST /api/auth/device  (moves the account here)
//   4. connect:            wss://.../vehicle with "Authorization: Bearer"
//      send {"type":"register","deviceId":...}, then telemetry
// Narrow-road suggestions go to POST /api/narrow-road-suggestions from a
// background task, so sending one never pauses the telemetry.
class ServerLink {
 public:
  void begin(const String &deviceId);
  void loop();

  // Connected and registered: telemetry will be accepted
  bool ready() const { return state_ == State::Registered; }
  const String &vehicleId() const { return vehicleId_; }
  const char *stateName() const;
  const String &ghatRoad() const { return ghatRoad_; }

  void sendTelemetry(const Telemetry &t);
  void sendHeading(float heading);

  // Queues a suggestion; false if one is already being sent
  bool suggestRoad(const RoadSuggestion &road);

 private:
  enum class State { WaitWifi, LogIn, LinkDevice, Connecting, Registered, Backoff };

  void setState(State s);
  bool logIn();
  bool linkDevice();
  void startWebSocket();
  void onWebSocketEvent(WStype_t type, uint8_t *payload, size_t length);
  void retryLater();

  static void suggestionTask(void *self);

  State state_ = State::WaitWifi;
  State afterBackoff_ = State::LogIn;
  uint32_t backoffUntilMs_ = 0;
  uint32_t backoffMs_ = 5000;
  uint32_t connectingSinceMs_ = 0;

  String deviceId_;
  String token_;
  String vehicleId_;
  String ghatRoad_;
  bool wsStarted_ = false;

  WebSocketsClient ws_;

  String pendingSuggestion_;
  volatile bool suggestionBusy_ = false;
};

// Low-level HTTPS helper, shared with the suggestion task
int httpPostJson(const char *path, const String &body, const String &token,
                 String &response, uint32_t timeoutMs);
