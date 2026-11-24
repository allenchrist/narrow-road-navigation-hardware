// Narrow Road Navigation: main unit
//
// The car's connectivity module, doing what the Android app does: sends the
// car's position, heading and motion readings to the server about once a
// second. It also listens for road widths from the camera unit, and sends
// any narrow road it finds as a suggestion for the admin to review.
//
// Settings: include/config.h and include/secrets.h
// Type help in the serial monitor for the heading commands.

#include <Arduino.h>
#include <WiFi.h>
#include <math.h>

#include <mutex>

#include "camera_listener.h"
#include "config.h"
#include "gps_reader.h"
#include "heading_math.h"
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

// ── Server work in the background ───────────────────────────────────────────
// Logging in can wait up to a minute while the free Render server wakes up.
// So the server runs in its own task on the other core, and the main loop
// keeps reading the GPS and compass. The loop leaves what to send here.

struct Outbox {
  bool hasTelemetry = false;
  Telemetry telemetry;
  bool hasHeading = false;
  float heading = 0;
  bool hasRoad = false;
  RoadSuggestion road;
};

std::mutex outboxLock;
Outbox outbox;
char vehicleIdCopy[32] = "";

void networkTask(void *) {
  for (;;) {
    server.loop();

    Outbox o;
    {
      std::lock_guard<std::mutex> lock(outboxLock);
      o = std::move(outbox);
      outbox = Outbox();
      strlcpy(vehicleIdCopy, server.vehicleId().c_str(), sizeof(vehicleIdCopy));
    }
    if (o.hasTelemetry) server.sendTelemetry(o.telemetry);
    if (o.hasHeading) server.sendHeading(o.heading);
    if (o.hasRoad && !server.suggestRoad(o.road)) {
      Serial.println("[Narrow] Busy or not logged in; not sent");
    }
    vTaskDelay(pdMS_TO_TICKS(2));
  }
}

String vehicleIdNow() {
  std::lock_guard<std::mutex> lock(outboxLock);
  return String(vehicleIdCopy);
}

// A permanent ID from the chip's factory MAC address, such as esp32-a1b2c3d4e5f6
String makeDeviceId() {
  uint64_t mac = ESP.getEfuseMac();
  char id[24];
  snprintf(id, sizeof(id), "esp32-%012llx", mac & 0xFFFFFFFFFFFFULL);
  return String(id);
}

bool gpsCourseInUse() {
  return gps.hasCourse() && gps.speedMps() >= GPS_COURSE_MIN_SPEED_MPS;
}

bool hasHeading() { return imu.hasHeading() || gps.hasCourse(); }

// When moving, the GPS direction of travel; otherwise the heading sensor;
// with no sensor, the last direction the GPS saw
float currentHeading() {
  if (gpsCourseInUse()) return gps.courseDeg();
  if (imu.hasHeading()) return imu.reading().heading;
  if (gps.hasCourse()) return gps.courseDeg();
  return 0;
}

// ── Serial commands ─────────────────────────────────────────────────────────

String commandLine;
bool watching = false;  // print the heading 5 times a second
uint32_t lastWatchMs = 0;

void printWatch() {
  if (imu.calibrating()) return;
  if (imu.hasHeading()) {
    Serial.printf("[Watch] heading %3.0f (raw %3.0f)", imu.reading().heading, imu.rawHeading());
  } else {
    Serial.print("[Watch] no compass reading");
  }
  if (gps.hasFix(GPS_MAX_AGE_MS)) {
    Serial.printf(" | lat %.6f, lon %.6f, %.1f km/h", gps.lat(), gps.lon(), gps.speedMps() * 3.6f);
  } else {
    Serial.print(" | GPS: no fix");
  }
  if (gpsCourseInUse()) Serial.printf(" | moving: GPS course %.0f", gps.courseDeg());
  Serial.println();
}

void printHelp() {
  Serial.println("Commands (type one, then press Enter):");
  Serial.println("  cal      calibrate the compass: turn it slowly through full circles, flat, for 30 s");
  Serial.println("  north    the car (or the sensor's arrow) points north right now");
  Serial.println("  reverse  the heading goes down when turning right: flip its direction");
  Serial.println("  reset    forget the saved calibration and corrections");
  Serial.println("  watch    show the heading and position 5 times a second; press Enter to stop");
  Serial.println("  help     this list");
}

void runCommand(String cmd) {
  cmd.trim();
  cmd.toLowerCase();
  if (watching) {  // any Enter stops the live view
    watching = false;
    Serial.println("[Watch] Stopped");
    if (cmd.isEmpty() || cmd == "watch") return;
  }
  if (cmd.isEmpty()) return;

  if (cmd == "watch") {
    watching = true;
    Serial.println("[Watch] Turn the sensor; press Enter to stop");
  } else if (cmd == "cal") {
    if (imu.startCalibration()) {
      Serial.println("[Cal] Started. Keep the sensor flat and turn it slowly round, 2 or 3 full");
      Serial.println("[Cal] circles in 30 s, away from metal, laptops, phones and magnets");
    } else if (!imu.ok()) {
      Serial.println("[Cal] No heading sensor found; check the wiring first");
    } else {
      Serial.printf("[Cal] The %s calibrates itself; just move it around\n", imu.sensorName());
    }
  } else if (cmd == "north") {
    if (imu.setNorthHere()) {
      Serial.printf("[IMU] Saved: this direction is now north (raw %.0f)\n", imu.rawHeading());
      Serial.println("[IMU] Now turn right: the heading should go up. If it goes down, type reverse");
    } else {
      Serial.println("[IMU] No heading reading yet; check the wiring");
    }
  } else if (cmd == "reverse") {
    imu.toggleReversed();
    Serial.println("[IMU] Saved: heading direction flipped. Check again by turning right");
    imu.printCorrections();
  } else if (cmd == "reset") {
    imu.resetCorrections();
    Serial.println("[IMU] Saved corrections cleared");
    imu.printCorrections();
  } else if (cmd == "help") {
    printHelp();
  } else {
    Serial.printf("Unknown command: %s\n", cmd.c_str());
    printHelp();
  }
}

void readSerialCommands() {
  while (Serial.available() > 0) {
    char c = static_cast<char>(Serial.read());
    if (c == '\n' || c == '\r') {
      runCommand(commandLine);
      commandLine = "";
    } else if (c == '\b' || c == 127) {  // Backspace
      if (commandLine.length() > 0) commandLine.remove(commandLine.length() - 1);
    } else if (commandLine.length() < 32) {
      commandLine += c;
    }
  }
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

  {
    std::lock_guard<std::mutex> lock(outboxLock);
    outbox.hasTelemetry = true;
    outbox.telemetry = t;
  }
  lastSentHeading = t.heading;

  detector.onPosition(t.lat, t.lon, t.speed, t.hasBearing ? t.bearing : t.heading,
                      t.accuracy, millis());
}

void printStatus() {
  if (imu.calibrating()) return;  // keep the screen for the calibration messages
  Serial.printf("[Status] server: %s %s | GPS: %s, %u sats, %u chars | IMU: %s %s",
                server.stateName(), vehicleIdNow().c_str(),
                gps.hasFix(GPS_MAX_AGE_MS) ? "fix" : "no fix", gps.satellites(),
                gps.charsProcessed(), imu.sensorName(), imu.ok() ? "ok" : "missing");
  if (imu.hasHeading()) {
    Serial.printf(", heading %.0f (raw %.0f)", imu.reading().heading, imu.rawHeading());
  }
  if (gps.hasFix(GPS_MAX_AGE_MS)) {
    Serial.printf(" | lat %.6f, lon %.6f", gps.lat(), gps.lon());
  }
  if (gpsCourseInUse()) {
    Serial.printf(" | moving %.0f km/h: using GPS course %.0f", gps.speedMps() * 3.6f,
                  gps.courseDeg());
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
  // Core 0 is the Wi-Fi core; loop() runs on core 1
  xTaskCreatePinnedToCore(networkTask, "network", 12288, nullptr, 1, nullptr, 0);
  Serial.println("Type help for the heading calibration commands");
}

void loop() {
  uint32_t now = millis();

  readSerialCommands();
  gps.update();
  imu.update();
  camera.update(CAMERA_UDP_PORT);

  if (now - lastTelemetryMs >= TELEMETRY_INTERVAL_MS) {
    lastTelemetryMs = now;
    sendTelemetry();
  }

  // Between packets, a turn is sent straight away so the car on the map turns
  if (hasHeading() && server.ready() && now - lastHeadingMs >= HEADING_MIN_INTERVAL_MS &&
      angleDifference(currentHeading(), lastSentHeading) >= HEADING_MIN_CHANGE_DEG) {
    lastHeadingMs = now;
    lastSentHeading = currentHeading();
    std::lock_guard<std::mutex> lock(outboxLock);
    outbox.hasHeading = true;
    outbox.heading = lastSentHeading;
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
    std::lock_guard<std::mutex> lock(outboxLock);
    outbox.hasRoad = true;
    outbox.road = road;
#endif
  }

  if (watching) {
    if (now - lastWatchMs >= 200) {
      lastWatchMs = now;
      printWatch();
    }
  } else if (now - lastStatusMs >= 5000) {
    lastStatusMs = now;
    printStatus();
  }

  delay(2);
}
