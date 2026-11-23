// Narrow Road Navigation: main unit
//
// The car's connectivity module, doing what the Android app does: sends the
// car's position, heading and motion readings to the server about once a
// second. It also listens for road widths from the camera unit, and sends
// any narrow road it finds as a suggestion for the admin to review.
//
// Settings: include/config.h and include/secrets.h

#include <Arduino.h>
#include <WiFi.h>
#include <math.h>

#include "camera_listener.h"
#include "config.h"
#include "gps_reader.h"
#include "imu_reader.h"
#include "narrow_detector.h"
#include "server_link.h"

namespace {

GpsReader gps;
ImuReader imu;
CameraListener camera;
ServerLink server;
NarrowDetector detector([] {
  NarrowRules r;
  r.maxWidthM = NARROW_MAX_WIDTH_M;
  r.minLengthM = NARROW_MIN_LENGTH_M;
  r.minFraction = NARROW_MIN_FRACTION;
  r.endGapM = NARROW_END_GAP_M;
  r.minConfidence = NARROW_MIN_CONFIDENCE;
  r.maxGpsErrorM = NARROW_MAX_GPS_ERROR_M;
  return r;
}());

uint32_t lastTelemetryMs = 0;
uint32_t lastHeadingMs = 0;
uint32_t lastStatusMs = 0;
float lastSentHeading = -1000;

// A permanent ID from the chip's factory MAC address, such as esp32-a1b2c3d4e5f6
String makeDeviceId() {
  uint64_t mac = ESP.getEfuseMac();
  char id[24];
  snprintf(id, sizeof(id), "esp32-%012llx", mac & 0xFFFFFFFFFFFFULL);
  return String(id);
}

float angleDifference(float a, float b) {
  float d = fabsf(fmodf(a - b + 540.0f, 360.0f) - 180.0f);
  return d;
}

// The compass, or the GPS direction of travel if there's no IMU
float currentHeading() {
  if (imu.hasHeading()) return imu.reading().heading;
  if (gps.hasCourse()) return gps.courseDeg();
  return 0;
}

void sendTelemetry() {
  if (!gps.hasFix(GPS_MAX_AGE_MS)) return;

  const ImuReading &m = imu.reading();
  Telemetry t;
  t.lat = gps.lat();
  t.lon = gps.lon();
  t.heading = currentHeading();
  t.accelX = m.accelX;
  t.accelY = m.accelY;
  t.accelZ = m.accelZ;
  t.gyroX = m.gyroX;
  t.gyroY = m.gyroY;
  t.gyroZ = m.gyroZ;
  t.speed = gps.speedMps();
  t.hasBearing = gps.hasCourse();
  t.bearing = gps.courseDeg();
  t.accuracy = gps.accuracyM();

  server.sendTelemetry(t);
  lastSentHeading = t.heading;

  detector.onPosition(t.lat, t.lon, t.speed, t.hasBearing ? t.bearing : t.heading,
                      t.accuracy, millis());
}

void printStatus() {
  Serial.printf("[Status] server: %s %s | GPS: %s, %u sats, %u chars | IMU: %s",
                server.stateName(), server.vehicleId().c_str(),
                gps.hasFix(GPS_MAX_AGE_MS) ? "fix" : "no fix", gps.satellites(),
                gps.charsProcessed(), imu.ok() ? "ok" : "missing");
  if (imu.hasHeading()) {
    Serial.printf(", heading %.0f (raw %.0f)", imu.reading().heading, imu.rawHeading());
  }
  if (camera.everHeard()) {
    const WidthReading &w = camera.last();
    Serial.printf(" | camera: %s%.1f m, %lus ago", w.wide ? ">" : "", w.width,
                  (millis() - w.receivedMs) / 1000);
  }
  Serial.println();
  if (gps.charsProcessed() < 10 && millis() > 10000) {
    Serial.println("[Status] No data from the GPS: check its TX goes to GPS_RX_PIN");
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\nNarrow Road Navigation: main unit");

  String deviceId = makeDeviceId();
  Serial.printf("Device ID: %s\n", deviceId.c_str());

  gps.begin(GPS_RX_PIN, GPS_TX_PIN, GPS_BAUD);
  imu.begin(IMU_SDA_PIN, IMU_SCL_PIN);
  server.begin(deviceId);
}

void loop() {
  uint32_t now = millis();

  gps.update();
  imu.update();
  server.loop();
  camera.update(CAMERA_UDP_PORT);

  if (now - lastTelemetryMs >= TELEMETRY_INTERVAL_MS) {
    lastTelemetryMs = now;
    sendTelemetry();
  }

  // Between packets, a turn is sent straight away so the car on the map turns
  if (imu.hasHeading() && server.ready() && now - lastHeadingMs >= HEADING_MIN_INTERVAL_MS &&
      angleDifference(imu.reading().heading, lastSentHeading) >= HEADING_MIN_CHANGE_DEG) {
    lastHeadingMs = now;
    lastSentHeading = imu.reading().heading;
    server.sendHeading(lastSentHeading);
  }

  WidthReading width;
  if (camera.take(width)) {
    detector.onWidth(width.width, width.wide, width.distance, width.confidence, now);
  }

  RoadSuggestion road;
  if (detector.poll(now, road)) {
    Serial.printf("[Narrow] Found a narrow road: %.1f m wide along %.0f m\n",
                  road.medianWidthM, road.lengthM);
#if SUGGEST_ROADS
    if (!server.suggestRoad(road)) Serial.println("[Narrow] Busy or not logged in; not sent");
#endif
  }

  if (now - lastStatusMs >= 5000) {
    lastStatusMs = now;
    printStatus();
  }

  delay(2);
}
