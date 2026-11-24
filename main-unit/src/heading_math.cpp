#include "heading_math.h"

#include <math.h>

namespace {
constexpr float kPi = 3.14159265358979f;
}

float normaliseDegrees(float d) {
  d = fmodf(d, 360.0f);
  if (d < 0) d += 360.0f;
  if (d >= 360.0f) d = 0;  // -0.00001 + 360 rounds to 360 in a float
  return d;
}

float angleDifference(float a, float b) {
  return fabsf(fmodf(a - b + 540.0f, 360.0f) - 180.0f);
}

float correctedHeading(float rawDeg, const HeadingCorrection &c) {
  float h = c.reversed ? -rawDeg : rawDeg;
  return normaliseDegrees(h + c.offsetDeg);
}

float offsetForNorth(float rawDeg, bool reversed) {
  float h = reversed ? -rawDeg : rawDeg;
  return normaliseDegrees(-h);
}

void CompassCalibrator::start() { samples_ = kept_ = 0; }

void CompassCalibrator::add(float x, float y, float z) {
  if (samples_ == 0) {
    minX_ = maxX_ = x;
    minY_ = maxY_ = y;
    minZ_ = maxZ_ = z;
  } else {
    minX_ = fminf(minX_, x), maxX_ = fmaxf(maxX_, x);
    minY_ = fminf(minY_, y), maxY_ = fmaxf(maxY_, y);
    minZ_ = fminf(minZ_, z), maxZ_ = fmaxf(maxZ_, z);
  }
  // Keep every 4th reading (30 s at 50 a second is 1500), overwriting the
  // oldest once full
  if (samples_ % 4 == 0) {
    keptX_[kept_ % kKept] = x;
    keptY_[kept_ % kKept] = y;
    kept_++;
  }
  samples_++;
}

bool CompassCalibrator::finish(CompassCalibration &out) const {
  float rx = rangeX(), ry = rangeY();
  // Turned flat through a full circle, X and Y swing about equally. If one
  // barely moved, the sensor wasn't turned all the way round.
  if (samples_ < 50 || rx <= 0 || ry <= 0) return false;
  // The earth's field is about 40 µT here, so a full turn swings an axis
  // by about 80 µT. Under 20 µT, it was hardly turned at all.
  if (fmaxf(rx, ry) < 20.0f) return false;
  if (fminf(rx, ry) < 0.6f * fmaxf(rx, ry)) return false;

  out.offsetX = (maxX_ + minX_) / 2;
  out.offsetY = (maxY_ + minY_) / 2;

  // Seen from the centre, the readings must cover at least 10 of the 12
  // 30-degree slices of the circle. A quarter turn also swings X and Y
  // equally, but only covers 3 or 4.
  bool seen[12] = {};
  int n = kept_ < kKept ? kept_ : kKept;
  for (int i = 0; i < n; i++) {
    float a = normaliseDegrees(atan2f(keptY_[i] - out.offsetY, keptX_[i] - out.offsetX) * 180.0f / kPi);
    seen[static_cast<int>(a / 30.0f) % 12] = true;
  }
  int slices = 0;
  for (bool s : seen) slices += s;
  if (slices < 10) return false;

  out.offsetZ = (maxZ_ + minZ_) / 2;
  float avg = (rx + ry) / 2;
  out.scaleX = avg / rx;
  out.scaleY = avg / ry;
  // Z hardly changes when turning flat, so it isn't scaled
  out.scaleZ = 1;
  return true;
}

void applyCalibration(const CompassCalibration &c, float &x, float &y, float &z) {
  x = (x - c.offsetX) * c.scaleX;
  y = (y - c.offsetY) * c.scaleY;
  z = (z - c.offsetZ) * c.scaleZ;
}

float compassHeading(float x, float y) {
  return normaliseDegrees(atan2f(y, x) * 180.0f / kPi);
}
