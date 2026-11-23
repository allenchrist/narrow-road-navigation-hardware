#pragma once

#include <Arduino.h>
#include <WebSocketsClient.h>

#include <string>

#include "fleet.h"

// Connects to the server the same way the website's dashboard does:
//   1. log in:  POST /api/auth/login → token
//   2. Socket.IO with the token, then emit "session:identify"
//   3. receive "session:assigned", then "vehicles:update" every second
//
// Socket.IO v4 is spoken directly over a WebSocket (Engine.IO protocol 4),
// which takes only a few lines and needs no extra library.
class ServerSession {
 public:
  enum class State { WaitWifi, LoggingIn, Connecting, Identifying, Live, Retry };

  void begin();
  void loop();

  State state() const { return state_; }
  bool online() const { return state_ == State::Identifying || state_ == State::Live; }

  // Why the screen has no car yet, for the screen to show, or empty
  const std::string &problem() const { return problem_; }

  // True when a new vehicles:update arrived since the last call
  bool takeFleet(Fleet &out);

 private:
  void setState(State s);
  bool logIn();
  void connectSocket();
  void onWebSocketEvent(WStype_t type, uint8_t *payload, size_t length);
  void onSocketIoEvent(const char *json, size_t length);
  void identify();
  void retryLater(uint32_t ms);

  State state_ = State::WaitWifi;
  std::string problem_;
  String token_;
  std::string vehicleId_;
  WebSocketsClient ws_;
  bool wsStarted_ = false;

  Fleet fleet_;
  bool freshFleet_ = false;

  uint32_t retryAtMs_ = 0;
  uint32_t backoffMs_ = 5000;
  uint32_t identifyAgainAtMs_ = 0;
};
