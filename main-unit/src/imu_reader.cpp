#include "imu_reader.h"

#include <Wire.h>
#include <math.h>

#include "config.h"

namespace {

constexpr uint32_t kRetryMs = 5000;

float normaliseDegrees(float d) {
  d = fmodf(d, 360.0f);
  return d < 0 ? d + 360.0f : d;
}

}  // namespace

void ImuReader::setRawHeading(float degrees) {
  rawHeading_ = normaliseDegrees(degrees);
  float h = HEADING_REVERSED ? -rawHeading_ : rawHeading_;
  reading_.heading = normaliseDegrees(h + HEADING_OFFSET_DEG);
  hasHeading_ = true;
}

#if defined(IMU_BNO085)

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

bool ImuReader::begin(int sdaPin, int sclPin) {
  sda_ = sdaPin;
  scl_ = sclPin;
  Wire.begin(sdaPin, sclPin);
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

#include <Adafruit_BNO055.h>
#include <Adafruit_Sensor.h>

namespace {

Adafruit_BNO055 bno(55, BNO055_I2C_ADDRESS, &Wire);
constexpr uint32_t kReadIntervalMs = 20;
uint32_t lastReadMs = 0;

}  // namespace

bool ImuReader::begin(int sdaPin, int sclPin) {
  sda_ = sdaPin;
  scl_ = sclPin;
  Wire.begin(sdaPin, sclPin);
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
#error "Choose IMU_BNO085 or IMU_BNO055 in config.h"
#endif
