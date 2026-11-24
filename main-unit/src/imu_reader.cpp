#include "imu_reader.h"

#include <Preferences.h>
#include <Wire.h>
#include <math.h>

#include "config.h"

namespace {

constexpr uint32_t kRetryMs = 5000;
constexpr uint32_t kCalibrationMs = 30000;
const char *kPrefsName = "heading";

}  // namespace

// ── Corrections shared by every sensor ──────────────────────────────────────

void ImuReader::setRawHeading(float degrees) {
  rawHeading_ = normaliseDegrees(degrees);
  reading_.heading = correctedHeading(rawHeading_, correction_);
  hasHeading_ = true;
}

void ImuReader::loadCorrections() {
  // Until a serial command saves something, use the values in config.h
  correction_.offsetDeg = HEADING_OFFSET_DEG;
  correction_.reversed = HEADING_REVERSED;
  compassCalibrated_ = false;

  Preferences prefs;
  if (!prefs.begin(kPrefsName, true)) return;  // nothing saved yet
  if (prefs.isKey("offset")) correction_.offsetDeg = prefs.getFloat("offset");
  if (prefs.isKey("rev")) correction_.reversed = prefs.getBool("rev");
  if (prefs.isKey("cal") && prefs.getBytesLength("cal") == sizeof(compassCal_)) {
    prefs.getBytes("cal", &compassCal_, sizeof(compassCal_));
    compassCalibrated_ = true;
  }
  prefs.end();
}

void ImuReader::saveCorrections() {
  Preferences prefs;
  if (!prefs.begin(kPrefsName, false)) {
    Serial.println("[IMU] Couldn't save to flash");
    return;
  }
  prefs.putFloat("offset", correction_.offsetDeg);
  prefs.putBool("rev", correction_.reversed);
  if (compassCalibrated_) {
    prefs.putBytes("cal", &compassCal_, sizeof(compassCal_));
  } else {
    prefs.remove("cal");
  }
  prefs.end();
}

bool ImuReader::setNorthHere() {
  if (!hasHeading_) return false;
  correction_.offsetDeg = offsetForNorth(rawHeading_, correction_.reversed);
  saveCorrections();
  setRawHeading(rawHeading_);
  return true;
}

void ImuReader::toggleReversed() {
  correction_.reversed = !correction_.reversed;
  // Keep the current direction reading the same, so only turns change
  if (hasHeading_) {
    float current = reading_.heading;
    float h = correction_.reversed ? -rawHeading_ : rawHeading_;
    correction_.offsetDeg = normaliseDegrees(current - h);
  }
  saveCorrections();
  if (hasHeading_) setRawHeading(rawHeading_);
}

void ImuReader::resetCorrections() {
  Preferences prefs;
  if (prefs.begin(kPrefsName, false)) {
    prefs.clear();
    prefs.end();
  }
  loadCorrections();
  if (hasHeading_) setRawHeading(rawHeading_);
}

void ImuReader::printCorrections() const {
  Serial.printf("[IMU] %s | north offset %.1f, reversed %s", sensorName(),
                correction_.offsetDeg, correction_.reversed ? "yes" : "no");
#if defined(IMU_COMPASS)
  if (compassCalibrated_) {
    Serial.printf(" | calibrated: offsets %.1f %.1f %.1f uT, scales %.2f %.2f",
                  compassCal_.offsetX, compassCal_.offsetY, compassCal_.offsetZ,
                  compassCal_.scaleX, compassCal_.scaleY);
  } else {
    Serial.print(" | not calibrated: type cal");
  }
#endif
  Serial.println();
}

#if defined(IMU_COMPASS)

// ── GY-271 compass ──────────────────────────────────────────────────────────

#include "compass.h"

namespace {

Compass compass;
constexpr uint32_t kReadIntervalMs = 20;
uint32_t lastReadMs = 0;
uint32_t failedReads = 0;  // in a row

}  // namespace

const char *ImuReader::sensorName() const { return ok_ ? compass.chipName() : "compass"; }

bool ImuReader::begin(int sdaPin, int sclPin) {
  bool first = sda_ < 0;
  sda_ = sdaPin;
  scl_ = sclPin;
  if (first) {
    Wire.begin(sdaPin, sclPin);
    loadCorrections();
  }
  ok_ = compass.begin(Wire);
  lastRetryMs_ = millis();
  if (ok_) {
    Serial.printf("[IMU] Compass %s ready\n", compass.chipName());
    if (first) printCorrections();
  } else if (first) {
    Serial.println("[IMU] Compass not found at 0x0D, 0x2C or 0x1E. Trying again every 5 s");
    compass.printI2cScan();
  }
  return ok_;
}

bool ImuReader::startCalibration() {
  if (!ok_) return false;
  calibrator_.start();
  calibratingUntilMs_ = millis() + kCalibrationMs;
  lastCalibrationPrintMs_ = millis();
  lastCalibrationSampleMs_ = millis();
  return true;
}

void ImuReader::updateCalibration(float x, float y, float z) {
  calibrator_.add(x, y, z);
  uint32_t now = millis();

  // If the board was held up for a moment, don't count that time:
  // the 30 s is 30 s of readings
  uint32_t gap = now - lastCalibrationSampleMs_;
  lastCalibrationSampleMs_ = now;
  if (gap > 1000) {
    calibratingUntilMs_ += gap;
    lastCalibrationPrintMs_ += gap;
  }

  if (now - lastCalibrationPrintMs_ >= 3000) {
    lastCalibrationPrintMs_ = now;
    Serial.printf("[Cal] %lu s left | X range %.0f uT, Y range %.0f uT (each should reach about 60 to 90)\n",
                  (calibratingUntilMs_ - now) / 1000, calibrator_.rangeX(), calibrator_.rangeY());
  }

  if (static_cast<int32_t>(now - calibratingUntilMs_) < 0) return;
  calibratingUntilMs_ = 0;

  CompassCalibration result;
  if (calibrator_.finish(result)) {
    compassCal_ = result;
    compassCalibrated_ = true;
    saveCorrections();
    Serial.println("[Cal] Done and saved. Now point the car north and type north");
    printCorrections();
  } else {
    Serial.println("[Cal] Not saved: the sensor wasn't turned through full circles, keeping it flat.");
    Serial.println("[Cal] Type cal to try again");
  }
}

void ImuReader::update() {
  if (!ok_) {
    if (millis() - lastRetryMs_ > kRetryMs) begin(sda_, scl_);
    return;
  }

  if (millis() - lastReadMs < kReadIntervalMs) return;
  lastReadMs = millis();

  float x, y, z;
  if (!compass.read(x, y, z)) {
    // A wire that loses contact for a moment can leave the I2C bus stuck:
    // after 1 s of failed readings, restart the bus and the compass
    if (++failedReads >= 50) {
      failedReads = 0;
      Serial.println("[IMU] Compass not answering (loose wire?); restarting it");
      Wire.end();
      Wire.begin(sda_, scl_);
      ok_ = compass.begin(Wire);
      lastRetryMs_ = millis();
      if (!ok_) Serial.println("[IMU] Compass missing; trying again every 5 s");
    }
    if (calibrating() && (!ok_ || millis() - lastCalibrationSampleMs_ > 5000)) {
      calibratingUntilMs_ = 0;
      Serial.println("[Cal] Stopped: the compass stopped answering. Push its 4 wires in");
      Serial.println("[Cal] firmly (SDA to D21, SCL to D22), then type cal again");
    }
    return;
  }
  failedReads = 0;

  if (calibrating()) updateCalibration(x, y, z);
  if (compassCalibrated_) applyCalibration(compassCal_, x, y, z);

  // Smooth the heading a little: the raw reading jitters by a degree or two
  float h = compassHeading(x, y);
  if (hasHeading_) {
    float diff = fmodf(h - rawHeading_ + 540.0f, 360.0f) - 180.0f;
    h = rawHeading_ + 0.3f * diff;
  }
  setRawHeading(h);
}

#elif defined(IMU_BNO085)

// ── BNO085 ──────────────────────────────────────────────────────────────────

#include <Adafruit_BNO08x.h>

namespace {

Adafruit_BNO08x bno;
sh2_SensorValue_t event;

bool enableReports() {
  // 20 ms between readings is plenty for a car
  return bno.enableReport(SH2_ROTATION_VECTOR, 20000) &&
         bno.enableReport(SH2_ACCELEROMETER, 20000) &&
         bno.enableReport(SH2_GYROSCOPE_CALIBRATED, 20000);
}

}  // namespace

const char *ImuReader::sensorName() const { return "BNO085"; }

bool ImuReader::startCalibration() { return false; }

bool ImuReader::begin(int sdaPin, int sclPin) {
  bool first = sda_ < 0;
  sda_ = sdaPin;
  scl_ = sclPin;
  if (first) {
    Wire.begin(sdaPin, sclPin);
    loadCorrections();
  }
  ok_ = bno.begin_I2C(BNO085_I2C_ADDRESS, &Wire) && enableReports();
  lastRetryMs_ = millis();
  Serial.printf("[IMU] BNO085 %s\n", ok_ ? "ready" : "not found, check wiring and address");
  return ok_;
}

void ImuReader::update() {
  if (!ok_) {
    if (millis() - lastRetryMs_ > kRetryMs) begin(sda_, scl_);
    return;
  }

  if (bno.wasReset()) enableReports();

  while (bno.getSensorEvent(&event)) {
    switch (event.sensorId) {
      case SH2_ROTATION_VECTOR: {
        float r = event.un.rotationVector.real;
        float i = event.un.rotationVector.i;
        float j = event.un.rotationVector.j;
        float k = event.un.rotationVector.k;
        // Rotation about the vertical axis, from the quaternion
        float yaw = atan2f(2.0f * (i * j + k * r), r * r + i * i - j * j - k * k);
        // The BNO085 measures yaw anticlockwise; a compass goes clockwise
        setRawHeading(-yaw * 180.0f / PI);
        break;
      }
      case SH2_ACCELEROMETER:
        reading_.accelX = event.un.accelerometer.x;
        reading_.accelY = event.un.accelerometer.y;
        reading_.accelZ = event.un.accelerometer.z;
        break;
      case SH2_GYROSCOPE_CALIBRATED:
        reading_.gyroX = event.un.gyroscope.x;
        reading_.gyroY = event.un.gyroscope.y;
        reading_.gyroZ = event.un.gyroscope.z;
        break;
    }
  }
}

#elif defined(IMU_BNO055)

// ── BNO055 ──────────────────────────────────────────────────────────────────

#include <Adafruit_BNO055.h>
#include <Adafruit_Sensor.h>

namespace {

Adafruit_BNO055 bno(55, BNO055_I2C_ADDRESS, &Wire);
constexpr uint32_t kReadIntervalMs = 20;
uint32_t lastReadMs = 0;

}  // namespace

const char *ImuReader::sensorName() const { return "BNO055"; }

bool ImuReader::startCalibration() { return false; }

bool ImuReader::begin(int sdaPin, int sclPin) {
  bool first = sda_ < 0;
  sda_ = sdaPin;
  scl_ = sclPin;
  if (first) {
    Wire.begin(sdaPin, sclPin);
    loadCorrections();
  }
  ok_ = bno.begin();
  if (ok_) bno.setExtCrystalUse(true);
  lastRetryMs_ = millis();
  Serial.printf("[IMU] BNO055 %s\n", ok_ ? "ready" : "not found, check wiring and address");
  return ok_;
}

void ImuReader::update() {
  if (!ok_) {
    if (millis() - lastRetryMs_ > kRetryMs) begin(sda_, scl_);
    return;
  }

  if (millis() - lastReadMs < kReadIntervalMs) return;
  lastReadMs = millis();

  // The BNO055's Euler heading already goes clockwise from north
  imu::Vector<3> euler = bno.getVector(Adafruit_BNO055::VECTOR_EULER);
  setRawHeading(euler.x());

  imu::Vector<3> a = bno.getVector(Adafruit_BNO055::VECTOR_ACCELEROMETER);
  reading_.accelX = a.x();
  reading_.accelY = a.y();
  reading_.accelZ = a.z();

  // The library gives degrees a second; the app sends radians a second
  imu::Vector<3> g = bno.getVector(Adafruit_BNO055::VECTOR_GYROSCOPE);
  reading_.gyroX = g.x() * DEG_TO_RAD;
  reading_.gyroY = g.y() * DEG_TO_RAD;
  reading_.gyroZ = g.z() * DEG_TO_RAD;
}

#else
#error "Choose IMU_COMPASS, IMU_BNO085 or IMU_BNO055 in config.h"
#endif
