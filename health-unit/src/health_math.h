#pragma once

// The health unit's arithmetic, in plain C++ so it can be tested on a laptop
// (host-tests/test_health_math.cpp). No Arduino code in here.

#include <stdint.h>

// ── Fuel level ──────────────────────────────────────────────────────────────

// Microseconds of ultrasonic echo to centimetres. Sound travels there and
// back, so the distance is half. Speed of sound: 331.3 + 0.606 x temp m/s.
float echoToCm(uint32_t echoUs, float airTempC = 25.0f);

// The middle value of n readings (n at most 15): one bad echo can't move it
float medianOf(const float *values, int n);

// What the sensor measures with the tank empty and with it full. Set by the
// "empty" and "full" commands, so the tank never has to be measured by hand.
struct TankCalibration {
  float emptyCm = 0;  // sensor to the bottom (empty tank)
  float fullCm = 0;   // sensor to the surface when full
  float litres = 0;   // capacity when full; 0 if not set

  bool valid() const { return emptyCm - fullCm >= 2.0f; }
};

// 0 to 100. Straight-sided tanks (a box, a bottle standing up) are linear.
// Returns -1 if the tank hasn't been calibrated.
float fuelPercent(const TankCalibration &tank, float distanceCm);

// Litres left, or -1 if the capacity isn't set
float fuelLitres(const TankCalibration &tank, float percent);

// ── Battery ─────────────────────────────────────────────────────────────────

// Charge of one 18650 cell from its voltage at rest, 0 to 100
float cellPercentFromVoltage(float cellVolts);

// Keeps the battery's charge: starts from the voltage, then counts the
// current going out (the way phones do), and drifts back towards the
// voltage estimate while the load is light, so it doesn't wander.
class BatteryGauge {
 public:
  void configure(int cellsInSeries, float capacityMah);
  // volts: the whole pack. milliamps: positive while discharging.
  void update(float volts, float milliamps, uint32_t dtMs);

  bool started() const { return started_; }
  float percent() const { return percent_; }
  // Minutes until empty at this current, or -1 when not discharging
  float minutesLeft(float milliamps) const;
  float cellVolts(float volts) const { return volts / cells_; }
  int cells() const { return cells_; }
  float capacityMah() const { return capacityMah_; }

 private:
  int cells_ = 4;
  float capacityMah_ = 3000;
  bool started_ = false;
  float percent_ = 0;
};
