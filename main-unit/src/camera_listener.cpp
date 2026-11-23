#include "camera_listener.h"

#include <ArduinoJson.h>
#include <WiFi.h>

void CameraListener::update(uint16_t port) {
  bool wifiUp = WiFi.status() == WL_CONNECTED;

  if (!wifiUp) {
    if (listening_) udp_.stop();
    listening_ = false;
    return;
  }

  if (!listening_) {
    listening_ = udp_.begin(port);
    if (listening_) Serial.printf("[Camera] Listening for widths on UDP %u\n", port);
  }

  int size;
  while ((size = udp_.parsePacket()) > 0) {
    char buffer[256];
    int n = udp_.read(buffer, sizeof(buffer) - 1);
    if (n <= 0) continue;
    buffer[n] = '\0';

    JsonDocument doc;
    if (deserializeJson(doc, buffer) != DeserializationError::Ok) continue;
    if (doc["type"] != "width") continue;

    WidthReading r;
    r.width = doc["width"] | 0.0f;
    r.wide = doc["wide"] | false;
    r.distance = doc["distance"] | 6.0f;
    r.confidence = doc["confidence"] | 0.0f;
    r.receivedMs = millis();

    last_ = r;
    lastMs_ = r.receivedMs;
    fresh_ = true;
  }
}

bool CameraListener::take(WidthReading &out) {
  if (!fresh_) return false;
  fresh_ = false;
  out = last_;
  return true;
}
