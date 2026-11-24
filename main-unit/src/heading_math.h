#pragma once

// Heading arithmetic with no Arduino code in it, so the host tests can run
// it on a laptop (main-unit/host-tests/test_heading_math.cpp).

// 0 to 360
float normaliseDegrees(float d);

// The smaller angle between two headings, 0 to 180
float angleDifference(float a, float b);

// Corrections the serial commands change, saved in flash
struct HeadingCorrection {
  float offsetDeg = 0;    // added after reversing: makes "north" read 0
  bool reversed = false;  // heading went down when turning right
};

// The raw sensor heading turned into degrees from north
float correctedHeading(float rawDeg, const HeadingCorrection &c);

// The offset that makes the current raw reading come out as 0 (north)
float offsetForNorth(float rawDeg, bool reversed);

// Compass calibration. While the sensor is turned through full circles,
// every reading goes into add(). The middle of each axis's range is the
// "hard iron" offset (magnets and steel nearby shift every reading the same
// way), and scaling each axis to the same range undoes a squashed circle.
struct CompassCalibration {
  float offsetX = 0, offsetY = 0, offsetZ = 0;
  float scaleX = 1, scaleY = 1, scaleZ = 1;
};

class CompassCalibrator {
 public:
  void start();
  void add(float x, float y, float z);
  int samples() const { return samples_; }

  // The X and Y ranges seen so far, in microtesla
  float rangeX() const { return samples_ ? maxX_ - minX_ : 0; }
  float rangeY() const { return samples_ ? maxY_ - minY_ : 0; }

  // False if the sensor wasn't turned enough to trust the result
  bool finish(CompassCalibration &out) const;

 private:
  // A sample of the readings, to check they went all the way round
  static constexpr int kKept = 400;
  float keptX_[kKept], keptY_[kKept];
  int kept_ = 0;

  int samples_ = 0;
  float minX_ = 0, maxX_ = 0, minY_ = 0, maxY_ = 0, minZ_ = 0, maxZ_ = 0;
};

void applyCalibration(const CompassCalibration &c, float &x, float &y, float &z);

// Heading of a flat-mounted compass from its calibrated X and Y, 0 to 360.
// The direction it counts in depends on how the board is mounted; the
// "reverse" serial command fixes that.
float compassHeading(float x, float y);
