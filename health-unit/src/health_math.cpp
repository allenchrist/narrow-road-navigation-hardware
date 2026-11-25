#include "health_math.h"

#include <math.h>

float echoToCm(uint32_t echoUs, float airTempC) {
  float metresPerSecond = 331.3f + 0.606f * airTempC;
  // us x (m/s) = micrometres; / 10000 = cm; / 2 for there and back
  return echoUs * metresPerSecond / 10000.0f / 2.0f;
}

float medianOf(const float *values, int n) {
  if (n <= 0) return 0;
  if (n > 15) n = 15;
  float v[15];
  for (int i = 0; i < n; i++) v[i] = values[i];
  // Insertion sort: n is tiny
  for (int i = 1; i < n; i++) {
    float x = v[i];
    int j = i - 1;
    while (j >= 0 && v[j] > x) {
      v[j + 1] = v[j];
      j--;
    }
    v[j + 1] = x;
  }
  return n % 2 ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2.0f;
}

float fuelPercent(const TankCalibration &tank, float distanceCm) {
  if (!tank.valid()) return -1;
  float p = (tank.emptyCm - distanceCm) / (tank.emptyCm - tank.fullCm) * 100.0f;
  if (p < 0) p = 0;
  if (p > 100) p = 100;
  return p;
}

float fuelLitres(const TankCalibration &tank, float percent) {
  if (tank.litres <= 0 || percent < 0) return -1;
  return tank.litres * percent / 100.0f;
}

namespace {

// A typical 18650 at rest: volts, percent
struct Point {
  float volts, percent;
};
const Point kCurve[] = {
    {3.00f, 0},  {3.30f, 2},  {3.50f, 5},  {3.61f, 10}, {3.67f, 20}, {3.71f, 30}, {3.75f, 40},
    {3.79f, 50}, {3.85f, 60}, {3.92f, 70}, {4.00f, 80}, {4.10f, 90}, {4.20f, 100},
};
constexpr int kPoints = sizeof(kCurve) / sizeof(kCurve[0]);

}  // namespace

float cellPercentFromVoltage(float v) {
  if (v <= kCurve[0].volts) return 0;
  if (v >= kCurve[kPoints - 1].volts) return 100;
  for (int i = 1; i < kPoints; i++) {
    if (v <= kCurve[i].volts) {
      const Point &a = kCurve[i - 1], &b = kCurve[i];
      return a.percent + (v - a.volts) / (b.volts - a.volts) * (b.percent - a.percent);
    }
  }
  return 100;
}

void BatteryGauge::configure(int cellsInSeries, float capacityMah) {
  cells_ = cellsInSeries < 1 ? 1 : cellsInSeries;
  capacityMah_ = capacityMah < 100 ? 100 : capacityMah;
  started_ = false;
}

void BatteryGauge::update(float volts, float milliamps, uint32_t dtMs) {
  float fromVoltage = cellPercentFromVoltage(volts / cells_);
  if (!started_) {
    percent_ = fromVoltage;
    started_ = true;
    return;
  }

  // Count the charge that went out (or in, if milliamps is negative)
  float hours = dtMs / 3600000.0f;
  percent_ -= milliamps * hours / capacityMah_ * 100.0f;

  // Under a light load the voltage is close to the resting voltage, so
  // lean towards it slowly (over about 5 minutes)
  if (fabsf(milliamps) < capacityMah_ / 10.0f) {
    float k = dtMs / 300000.0f;
    if (k > 1) k = 1;
    percent_ += (fromVoltage - percent_) * k;
  }

  if (percent_ < 0) percent_ = 0;
  if (percent_ > 100) percent_ = 100;
}

float BatteryGauge::minutesLeft(float milliamps) const {
  if (!started_ || milliamps < 20) return -1;
  float remainingMah = percent_ / 100.0f * capacityMah_;
  return remainingMah / milliamps * 60.0f;
}
