#pragma once

#include <Arduino.h>

#include "heading_math.h"

// Heading (and, from a BNO085 or BNO055, motion) readings from the sensor
// chosen in config.h: IMU_COMPASS (GY-271), IMU_BNO085 or IMU_BNO055.
// Units match the Android app: m/s² for acceleration (including gravity),
// rad/s for rotation, degrees from north for heading. A compass alone
// measures no motion, so those stay 0.
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
  const char *sensorName() const;

  // The heading before the north and reverse corrections, for calibrating
  float rawHeading() const { return rawHeading_; }

  // ── Serial commands (see main.cpp). Changes are saved in flash. ──
  // "cal": turn the sensor through full circles for 30 seconds.
  // Only the compass needs it; the BNO chips calibrate themselves.
  bool startCalibration();
  bool calibrating() const { return calibratingUntilMs_ != 0; }
  // "north": the car is pointing north now
  bool setNorthHere();
  // "reverse": the heading went down when the car turned right
  void toggleReversed();
  // "reset": forget everything saved
  void resetCorrections();
  void printCorrections() const;

 private:
  void setRawHeading(float degrees);
  void loadCorrections();
  void saveCorrections();
  void updateCalibration(float x, float y, float z);

  bool ok_ = false;
  bool hasHeading_ = false;
  float rawHeading_ = 0;
  ImuReading reading_;
  uint32_t lastRetryMs_ = 0;
  int sda_ = -1, scl_ = -1;

  HeadingCorrection correction_;
  CompassCalibration compassCal_;
  bool compassCalibrated_ = false;
  CompassCalibrator calibrator_;
  uint32_t calibratingUntilMs_ = 0;
  uint32_t lastCalibrationPrintMs_ = 0;
  uint32_t lastCalibrationSampleMs_ = 0;
};
