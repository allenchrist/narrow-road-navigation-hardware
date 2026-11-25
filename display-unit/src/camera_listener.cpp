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
    uint32_t now = millis();
    if (doc["type"] == "position") {
      PositionReading p;
      p.fix = doc["fix"] | false;
      p.lat = doc["lat"] | 0.0;
      p.lon = doc["lon"] | 0.0;
      p.speedMps = doc["speed"] | 0.0f;
      p.hasHeading = doc["hasHeading"] | false;
      p.heading = doc["heading"] | 0.0f;
      p.receivedMs = now;
      position_ = p;
      continue;
    }
    if (doc["type"] == "fuel") {
      FuelReading f;
      f.ok = doc["ok"] | false;
      f.calibrated = doc["calibrated"] | false;
      f.percent = doc["percent"] | -1.0f;
      f.litres = doc["litres"] | -1.0f;
      f.low = doc["low"] | false;
      f.receivedMs = now;
      fuel_ = f;
      continue;
    }
    if (doc["type"] == "battery") {
      BatteryReading b;
      b.ok = doc["ok"] | false;
      b.volts = doc["volts"] | 0.0f;
      b.amps = doc["amps"] | 0.0f;
      b.watts = doc["watts"] | 0.0f;
      b.percent = doc["percent"] | -1.0f;
      b.minutesLeft = doc["minutesLeft"] | -1.0f;
      b.low = doc["low"] | false;
      b.receivedMs = now;
      battery_ = b;
      continue;
    }
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
