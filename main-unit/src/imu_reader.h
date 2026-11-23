#pragma once

#include <Arduino.h>

// Heading and motion readings from a BNO085 or BNO055 (chosen in config.h).
// Units match the Android app: m/s² for acceleration (including gravity),
// rad/s for rotation, degrees from north for heading.
struct ImuReading {
  float heading = 0;  // 0 = north, 90 = east
  float accelX = 0, accelY = 0, accelZ = 0;
  float gyroX = 0, gyroY = 0, gyroZ = 0;
};

class ImuReader {
 public:
  bool begin(int sdaPin, int sclPin);

  // Call often
  void update();

  bool ok() const { return ok_; }
  bool hasHeading() const { return hasHeading_; }
  const ImuReading &reading() const { return reading_; }

  // The heading before HEADING_OFFSET_DEG and HEADING_REVERSED, for calibrating
  float rawHeading() const { return rawHeading_; }

 private:
  void setRawHeading(float degrees);

  bool ok_ = false;
  bool hasHeading_ = false;
  float rawHeading_ = 0;
  ImuReading reading_;
  uint32_t lastRetryMs_ = 0;
  int sda_ = -1, scl_ = -1;
};
