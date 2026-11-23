#include "server_link.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include "config.h"
#include "root_ca.h"

namespace {

// Render's free plan can take up to a minute to wake up
constexpr uint32_t kHttpTimeoutMs = 60000;
constexpr uint32_t kMaxBackoffMs = 60000;
constexpr uint32_t kReconnectMs = 5000;

TaskHandle_t suggestionTaskHandle = nullptr;

}  // namespace

int httpPostJson(const char *path, const String &body, const String &token,
                 String &response, uint32_t timeoutMs) {
  HTTPClient http;
#if SERVER_USE_TLS
  WiFiClientSecure client;
  client.setCACert(ROOT_CA_BUNDLE);
#else
  WiFiClient client;
#endif

  http.setConnectTimeout(timeoutMs);
  http.setTimeout(timeoutMs > 65000 ? 65000 : timeoutMs);

  if (!http.begin(client, SERVER_HOST, SERVER_PORT, path, SERVER_USE_TLS)) {
    return -1;
  }

  http.addHeader("Content-Type", "application/json");
  if (token.length() > 0) http.addHeader("Authorization", "Bearer " + token);

  int code = http.POST(body);
  response = code > 0 ? http.getString() : String();
  http.end();
  return code;
}

const char *ServerLink::stateName() const {
  switch (state_) {
    case State::WaitWifi: return "waiting for Wi-Fi";
    case State::LogIn: return "logging in";
    case State::LinkDevice: return "linking device";
    case State::Connecting: return "connecting";
    case State::Registered: return "connected";
    case State::Backoff: return "retrying soon";
  }
  return "?";
}

void ServerLink::begin(const String &deviceId) {
  deviceId_ = deviceId;

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  ws_.onEvent([this](WStype_t type, uint8_t *payload, size_t length) {
    onWebSocketEvent(type, payload, length);
  });

  xTaskCreatePinnedToCore(suggestionTask, "suggest", 12288, this, 1,
                          &suggestionTaskHandle, 0);
}

void ServerLink::setState(State s) {
  bool changed = s != state_;
  state_ = s;
  if (changed) Serial.printf("[Server] %s\n", stateName());
}

void ServerLink::retryLater() {
  afterBackoff_ = State::LogIn;
  backoffUntilMs_ = millis() + backoffMs_;
  Serial.printf("[Server] Trying again in %lu s\n", backoffMs_ / 1000);
  backoffMs_ = min(backoffMs_ * 2, kMaxBackoffMs);
  setState(State::Backoff);
}

bool ServerLink::logIn() {
  JsonDocument doc;
  doc["username"] = ACCOUNT_USERNAME;
  doc["password"] = ACCOUNT_PASSWORD;
  String body;
  serializeJson(doc, body);

  String response;
  int code = httpPostJson("/api/auth/login", body, "", response, kHttpTimeoutMs);

  if (code != 200) {
    Serial.printf("[Server] Login failed (%d): %s\n", code, response.c_str());
    return false;
  }

  JsonDocument reply;
  if (deserializeJson(reply, response) != DeserializationError::Ok ||
      !reply["token"].is<const char *>()) {
    Serial.println("[Server] Login reply had no token");
    return false;
  }

  token_ = reply["token"].as<String>();
  Serial.printf("[Server] Logged in as %s\n", ACCOUNT_USERNAME);
  return true;
}

bool ServerLink::linkDevice() {
  JsonDocument doc;
  doc["deviceId"] = deviceId_;
  String body;
  serializeJson(doc, body);

  String response;
  int code = httpPostJson("/api/auth/device", body, token_, response, kHttpTimeoutMs);

  if (code != 200) {
    Serial.printf("[Server] Linking device failed (%d): %s\n", code, response.c_str());
    return false;
  }

  Serial.printf("[Server] Device %s linked to %s\n", deviceId_.c_str(), ACCOUNT_USERNAME);
  return true;
}

void ServerLink::startWebSocket() {
  if (wsStarted_) ws_.disconnect();

  String header = "Authorization: Bearer " + token_;
  ws_.setExtraHeaders(header.c_str());
  ws_.setReconnectInterval(kReconnectMs);
  // Notices a dead connection within about 20 seconds
  ws_.enableHeartbeat(15000, 3000, 2);

#if SERVER_USE_TLS
  ws_.beginSslWithCA(SERVER_HOST, SERVER_PORT, VEHICLE_WS_PATH, ROOT_CA_BUNDLE);
#else
  ws_.begin(SERVER_HOST, SERVER_PORT, VEHICLE_WS_PATH);
#endif

  wsStarted_ = true;
  connectingSinceMs_ = millis();
  setState(State::Connecting);
}

void ServerLink::onWebSocketEvent(WStype_t type, uint8_t *payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED: {
      JsonDocument doc;
      doc["type"] = "register";
      doc["deviceId"] = deviceId_;
      String msg;
      serializeJson(doc, msg);
      ws_.sendTXT(msg);
      break;
    }

    case WStype_DISCONNECTED:
      if (state_ == State::Registered) {
        Serial.println("[Server] Disconnected, reconnecting");
        setState(State::Connecting);
      }
      break;

    case WStype_TEXT: {
      JsonDocument doc;
      if (deserializeJson(doc, payload, length) != DeserializationError::Ok) break;
      const char *kind = doc["type"] | "";

      if (strcmp(kind, "registered") == 0) {
        vehicleId_ = doc["vehicleId"].as<String>();
        backoffMs_ = 5000;
        Serial.printf("[Server] Registered as %s\n", vehicleId_.c_str());
        setState(State::Registered);
      } else if (strcmp(kind, "mountain") == 0) {
        ghatRoad_ = doc["roadName"].isNull() ? String() : doc["roadName"].as<String>();
        Serial.printf("[Server] Mountain Mode: %s\n",
                      ghatRoad_.length() ? ghatRoad_.c_str() : "off");
      } else if (strcmp(kind, "error") == 0) {
        String message = doc["message"] | "";
        Serial.printf("[Server] Error: %s\n", message.c_str());
        // The login has ended, or the account moved to another phone
        if (message.startsWith("Not authorized")) {
          ws_.disconnect();
          wsStarted_ = false;
          retryLater();
        }
      }
      break;
    }

    default:
      break;
  }
}

void ServerLink::loop() {
  if (wsStarted_) ws_.loop();

  if (WiFi.status() != WL_CONNECTED) {
    if (state_ != State::WaitWifi && state_ != State::Backoff) {
      Serial.println("[Server] Wi-Fi lost");
      setState(State::WaitWifi);
    }
    return;
  }

  switch (state_) {
    case State::WaitWifi:
      Serial.printf("[Server] Wi-Fi connected, address %s\n",
                    WiFi.localIP().toString().c_str());
      // Already logged in: just let the WebSocket reconnect by itself
      setState(token_.length() && wsStarted_ ? State::Connecting : State::LogIn);
      break;

    case State::LogIn:
      if (logIn()) setState(State::LinkDevice);
      else retryLater();
      break;

    case State::LinkDevice:
      if (linkDevice()) startWebSocket();
      else retryLater();
      break;

    case State::Backoff:
      if ((int32_t)(millis() - backoffUntilMs_) >= 0) setState(afterBackoff_);
      break;

    case State::Connecting:
    case State::Registered:
      break;
  }
}

void ServerLink::sendTelemetry(const Telemetry &t) {
  if (!ready()) return;

  // Built by hand so latitude and longitude keep 7 decimal places (about 1 cm)
  char msg[384];
  int n = snprintf(msg, sizeof(msg),
                   "{\"lat\":%.7f,\"lon\":%.7f,\"heading\":%.1f,"
                   "\"accelX\":%.3f,\"accelY\":%.3f,\"accelZ\":%.3f,"
                   "\"gyroX\":%.4f,\"gyroY\":%.4f,\"gyroZ\":%.4f,"
                   "\"speed\":%.2f,\"accuracy\":%.1f",
                   t.lat, t.lon, t.heading, t.accelX, t.accelY, t.accelZ, t.gyroX,
                   t.gyroY, t.gyroZ, t.speed, t.accuracy);
  if (t.hasBearing) {
    n += snprintf(msg + n, sizeof(msg) - n, ",\"bearing\":%.1f", t.bearing);
  }
  snprintf(msg + n, sizeof(msg) - n, "}");

  ws_.sendTXT(msg);
}

void ServerLink::sendHeading(float heading) {
  if (!ready()) return;
  char msg[48];
  snprintf(msg, sizeof(msg), "{\"type\":\"heading\",\"heading\":%.1f}", heading);
  ws_.sendTXT(msg);
}

bool ServerLink::suggestRoad(const RoadSuggestion &road) {
  if (suggestionBusy_ || token_.length() == 0) return false;

  String body;
  body.reserve(64 + road.lats.size() * 28);
  body += "{\"name\":\"Narrow road (car camera)\",\"polygon\":[";
  char point[48];
  for (size_t i = 0; i < road.lats.size(); i++) {
    snprintf(point, sizeof(point), "%s[%.7f,%.7f]", i ? "," : "", road.lats[i],
             road.lons[i]);
    body += point;
  }
  char reason[160];
  snprintf(reason, sizeof(reason),
           "],\"reason\":\"Found by the car camera: about %.1f m wide along %.0f m "
           "(%.0f%% of readings narrow)\"}",
           road.medianWidthM, road.lengthM, road.narrowFraction * 100);
  body += reason;

  pendingSuggestion_ = body;
  suggestionBusy_ = true;
  xTaskNotifyGive(suggestionTaskHandle);
  return true;
}

void ServerLink::suggestionTask(void *arg) {
  ServerLink *self = static_cast<ServerLink *>(arg);

  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    String response;
    int code = httpPostJson("/api/narrow-road-suggestions", self->pendingSuggestion_,
                            self->token_, response, kHttpTimeoutMs);
    if (code == 201) {
      Serial.println("[Server] Narrow road suggested; review it on the admin page");
    } else {
      Serial.printf("[Server] Suggestion failed (%d): %s\n", code, response.c_str());
    }
    self->pendingSuggestion_ = String();
    self->suggestionBusy_ = false;
  }
}
