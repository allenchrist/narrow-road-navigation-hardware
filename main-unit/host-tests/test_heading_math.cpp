// Tests for the heading arithmetic, on a laptop:
//   g++ -std=c++17 -Wall -Imain-unit/src main-unit/host-tests/test_heading_math.cpp main-unit/src/heading_math.cpp -o heading && ./heading

#include <math.h>
#include <stdio.h>

#include <initializer_list>

#include "heading_math.h"

static int failures = 0;

static void check(bool ok, const char *what) {
  printf("%s  %s\n", ok ? "pass" : "FAIL", what);
  if (!ok) failures++;
}

static bool near(float a, float b, float tol = 0.5f) { return angleDifference(a, b) <= tol; }

int main() {
  const float pi = 3.14159265f;

  check(near(normaliseDegrees(-10), 350) && near(normaliseDegrees(725), 5), "normalise wraps");
  check(normaliseDegrees(-0.00001f) < 360.0f, "normalise never gives 360");
  check(near(angleDifference(350, 10), 20, 0.01f), "difference across north");

  // north: whatever the raw reading, it becomes 0
  for (float raw : {0.0f, 37.0f, 200.0f, 359.0f}) {
    for (bool rev : {false, true}) {
      HeadingCorrection c;
      c.reversed = rev;
      c.offsetDeg = offsetForNorth(raw, rev);
      if (!near(correctedHeading(raw, c), 0)) check(false, "north gives 0");
    }
  }
  check(true, "north gives 0 for any raw reading, reversed or not");

  // After north, turning right 90° (raw +90 or, reversed, raw -90) reads 90
  {
    HeadingCorrection c;
    c.offsetDeg = offsetForNorth(37, false);
    check(near(correctedHeading(127, c), 90), "turn right 90 after north");
    c.reversed = true;
    c.offsetDeg = offsetForNorth(37, true);
    check(near(correctedHeading(-53, c), 90), "reversed: raw going down reads as right turn");
  }

  // Calibration: a circle of radius 40 µT, shifted by a nearby magnet and
  // squashed in Y, comes back as a centred round circle
  {
    CompassCalibrator cal;
    cal.start();
    for (int i = 0; i < 360; i++) {
      float a = i * pi / 180;
      cal.add(12 + 40 * cosf(a), -7 + 28 * sinf(a), 30);
    }
    CompassCalibration out;
    bool ok = cal.finish(out);
    check(ok, "full circle accepted");
    check(fabsf(out.offsetX - 12) < 0.1f && fabsf(out.offsetY + 7) < 0.1f, "hard-iron offset found");

    float worst = 0;
    for (int i = 0; i < 360; i += 15) {
      float a = i * pi / 180;
      float x = 12 + 40 * cosf(a), y = -7 + 28 * sinf(a), z = 30;
      applyCalibration(out, x, y, z);
      worst = fmaxf(worst, angleDifference(compassHeading(x, y), (float)i));
    }
    printf("      worst heading error after calibration: %.2f degrees\n", worst);
    check(worst < 1.0f, "calibrated heading within 1 degree all the way round");
  }

  // Not turned enough: a quarter circle, half a circle, or hardly at all
  {
    CompassCalibration out;
    CompassCalibrator quarter;
    quarter.start();
    for (int i = 0; i < 300; i++) {  // north to east, slowly
      float a = (i * 0.3f) * pi / 180;
      quarter.add(40 * cosf(a), 40 * sinf(a), 30);
    }
    check(!quarter.finish(out), "quarter circle: rejected");

    CompassCalibrator still;
    still.start();
    for (int i = 0; i < 200; i++) still.add(20 + (i % 3) * 0.5f, 30 + (i % 2) * 0.5f, 30);
    check(!still.finish(out), "sensor not turned: rejected");

    CompassCalibrator half;
    half.start();
    for (int i = 0; i < 180; i++) {  // turned only from east to west through north
      float a = i * pi / 180;
      half.add(40 * cosf(a), 40 * sinf(a), 30);
    }
    check(!half.finish(out), "half circle: rejected");
  }

  printf(failures ? "\n%d FAILED\n" : "\nAll heading tests passed\n", failures);
  return failures ? 1 : 0;
}
