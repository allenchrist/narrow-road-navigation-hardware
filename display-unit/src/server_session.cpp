#include "server_session.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_heap_caps.h>

#include "config.h"
#include "root_ca.h"

namespace {

constexpr uint32_t kHttpTimeoutMs = 60000;  // Render can take a minute to wake
constexpr uint32_t kMaxBackoffMs = 60000;
constexpr uint32_t kIdentifyRetryMs = 5000;

// vehicles:update can be tens of kilobytes, so its JSON lives in PSRAM
struct PsramAllocator : ArduinoJson::Allocator {
  void *allocate(size_t size) override {
    return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  }
  void deallocate(void *p) override { heap_caps_free(p); }
  void *reallocate(void *p, size_t size) override {
    return heap_caps_realloc(p, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  }
};

PsramAllocator psram;

ArduinoJson::Allocator *jsonAllocator() {
  return psramFound() ? static_cast<ArduinoJson::Allocator *>(&psram)
                      : ArduinoJson::detail::DefaultAllocator::instance();
}

// What to tell the driver for each session:error from the server
std::string explain(const std::string &serverMessage) {
  if (serverMessage == "Device is not currently connected" ||
      serverMessage == "No device is associated with this account") {
    return "Waiting for the main unit to connect";
  }
  if (serverMessage == "User not found") return "Account not found";
  return serverMessage;
}

}  // namespace

void ServerSession::begin() {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  ws_.onEvent([this](WStype_t type, uint8_t *payload, size_t length) {
    onWebSocketEvent(type, payload, length);
  });

  problem_ = "Connecting to Wi-Fi";
}

void ServerSession::setState(State s) {
  if (s == state_) return;
  state_ = s;
  static const char *names[] = {"waiting for Wi-Fi", "logging in", "connecting",
                                "identifying", "live", "retrying soon"};
  Serial.printf("[Server] %s\n", names[static_cast<int>(s)]);
}

void ServerSession::retryLater(uint32_t ms) {
  retryAtMs_ = millis() + ms;
  setState(State::Retry);
}

bool ServerSession::logIn() {
  JsonDocument body;
  body["username"] = ACCOUNT_USERNAME;
  body["password"] = ACCOUNT_PASSWORD;
  String text;
  serializeJson(body, text);

  HTTPClient http;
#if SERVER_USE_TLS
  WiFiClientSecure client;
  client.setCACert(ROOT_CA_BUNDLE);
#else
  WiFiClient client;
#endif
  http.setConnectTimeout(kHttpTimeoutMs);
  http.setTimeout(kHttpTimeoutMs);
  if (!http.begin(client, SERVER_HOST, SERVER_PORT, "/api/auth/login", SERVER_USE_TLS)) {
    return false;
  }
  http.addHeader("Content-Type", "application/json");
  int code = http.POST(text);
  String response = code > 0 ? http.getString() : String();
  http.end();

  if (code != 200) {
    Serial.printf("[Server] Login failed (%d): %s\n", code, response.c_str());
    problem_ = code == 401 ? "Wrong username or password in secrets.h"
                           : "Can't reach the server";
    return false;
  }

  JsonDocument reply;
  if (deserializeJson(reply, response) || !reply["token"].is<const char *>()) return false;
  token_ = reply["token"].as<String>();
  return true;
}

void ServerSession::connectSocket() {
  if (wsStarted_) ws_.disconnect();
  ws_.setReconnectInterval(5000);
  const char *path = "/socket.io/?EIO=4&transport=websocket";
#if SERVER_USE_TLS
  ws_.beginSslWithCA(SERVER_HOST, SERVER_PORT, path, ROOT_CA_BUNDLE);
#else
  ws_.begin(SERVER_HOST, SERVER_PORT, path);
#endif
  wsStarted_ = true;
  problem_ = "Connecting to the server";
  setState(State::Connecting);
}

void ServerSession::identify() {
  ws_.sendTXT("42[\"session:identify\"]");
  identifyAgainAtMs_ = 0;
}

void ServerSession::onWebSocketEvent(WStype_t type, uint8_t *payload, size_t length) {
  switch (type) {
    case WStype_DISCONNECTED:
      if (online()) {
        Serial.println("[Server] Disconnected, reconnecting");
        problem_ = "Reconnecting to the server";
        setState(State::Connecting);
      }
      break;

    case WStype_TEXT: {
      if (length == 0) break;
      const char *text = reinterpret_cast<const char *>(payload);

      switch (text[0]) {
        case '0': {  // Engine.IO open: join the default namespace with the login
          JsonDocument auth;
          auth["token"] = token_;
          String packet = "40";
          serializeJson(auth, packet);
          ws_.sendTXT(packet);
          break;
        }
        case '2':  // the server's ping
          ws_.sendTXT("3");
          break;
        case '4':  // a Socket.IO packet
          if (length < 2) break;
          if (text[1] == '0') {  // joined
            backoffMs_ = 5000;
            problem_ = "Waiting for the main unit to connect";
            setState(State::Identifying);
            identify();
          } else if (text[1] == '4') {  // refused
            JsonDocument err;
            deserializeJson(err, text + 2, length - 2);
            String message = err["message"] | "";
            Serial.printf("[Server] Refused: %s\n", message.c_str());
            ws_.disconnect();
            wsStarted_ = false;
            // "Not authorized": the login ended, so log in again
            token_ = "";
            problem_ = "Logging in again";
            retryLater(message == "Not authorized" ? 1000 : backoffMs_);
          } else if (text[1] == '2') {
            onSocketIoEvent(text + 2, length - 2);
          }
          break;
      }
      break;
    }

    default:
      break;
  }
}

void ServerSession::onSocketIoEvent(const char *json, size_t length) {
  JsonDocument doc(jsonAllocator());
  DeserializationError err = deserializeJson(doc, json, length);
  if (err) {
    Serial.printf("[Server] Bad event (%s, %u bytes)\n", err.c_str(), (unsigned)length);
    return;
  }

  const char *event = doc[0] | "";
  JsonVariantConst data = doc[1];

  if (strcmp(event, "vehicles:update") == 0) {
    Fleet fleet;
    if (parseFleet(data["vehicles"], vehicleId_, fleet) && fleet.hasOwn) {
      fleet_ = fleet;
      freshFleet_ = true;
      if (state_ == State::Identifying) setState(State::Live);
      problem_.clear();
    }
  } else if (strcmp(event, "session:assigned") == 0) {
    vehicleId_ = data["vehicleId"] | "";
    Serial.printf("[Server] Showing %s\n", vehicleId_.c_str());
    setState(State::Live);
    problem_.clear();
  } else if (strcmp(event, "session:error") == 0) {
    std::string message = data["message"] | "";
    Serial.printf("[Server] %s\n", message.c_str());
    problem_ = explain(message);
    vehicleId_.clear();
    setState(State::Identifying);
    identifyAgainAtMs_ = millis() + kIdentifyRetryMs;
  } else if (strcmp(event, "session:changed") == 0) {
    // The account's device changed: look it up again
    vehicleId_.clear();
    setState(State::Identifying);
    identify();
  }
}

void ServerSession::loop() {
  if (wsStarted_) ws_.loop();

  if (WiFi.status() != WL_CONNECTED) {
    if (state_ != State::WaitWifi) {
      problem_ = "Connecting to Wi-Fi";
      setState(State::WaitWifi);
    }
    return;
  }

  switch (state_) {
    case State::WaitWifi:
      Serial.printf("[Server] Wi-Fi connected, address %s\n", WiFi.localIP().toString().c_str());
      configTime(TIME_ZONE_OFFSET_S, 0, NTP_SERVER);
      if (token_.length() && wsStarted_) {
        setState(State::Connecting);  // the WebSocket reconnects by itself
      } else {
        problem_ = "Logging in";
        setState(State::LoggingIn);
      }
      break;

    case State::LoggingIn:
      if (logIn()) {
        connectSocket();
      } else {
        retryLater(backoffMs_);
        backoffMs_ = min(backoffMs_ * 2, kMaxBackoffMs);
      }
      break;

    case State::Retry:
      if ((int32_t)(millis() - retryAtMs_) >= 0) {
        if (token_.length()) connectSocket();
        else setState(State::LoggingIn);
      }
      break;

    case State::Identifying:
      if (identifyAgainAtMs_ && (int32_t)(millis() - identifyAgainAtMs_) >= 0) identify();
      break;

    case State::Connecting:
    case State::Live:
      break;
  }
}

bool ServerSession::takeFleet(Fleet &out) {
  if (!freshFleet_) return false;
  freshFleet_ = false;
  out = fleet_;
  return true;
}
