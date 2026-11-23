#pragma once

// Measures the road's width in a grayscale camera picture.
//
// Calibration gives a homography H: a 3x3 matrix that turns a pixel (u, v)
// into a point on the flat ground, x metres to the right of the camera and
// y metres ahead. Its inverse turns a ground point into a pixel.
//
// For each measuring distance (5, 6 and 7 m ahead by default):
//   1. find the pixel row showing the ground at that distance, straight ahead
//   2. average a few rows there, and smooth along the row
//   3. take the brightness of the road just ahead of the car as the reference
//   4. scan left and right until the brightness differs from the road for
//      several pixels in a row: that's the wall, kerb or edge of the road
//   5. turn both edge pixels into ground points; the distance between them
//      is the width
// The answer is the median over the distances. If the scan reaches the
// picture's edge without finding a side, the road is wider than the camera
// sees there, and the result says "wide".
//
// Plain C++ with no Arduino code: the same file is tested on a laptop, and
// camera-unit/python/widthlib.py does exactly the same steps.

#include <cstdint>

struct Homography {
  double m[9];  // row-major
};

bool invertHomography(const Homography &h, Homography &inverse);
bool applyHomography(const Homography &h, double u, double v, double &x, double &y);

struct WidthSettings {
  float distances[5] = {5.0f, 6.0f, 7.0f};
  int distanceCount = 3;
  int rowBand = 2;        // average rows v-2..v+2
  int smooth = 2;         // box filter half-width along the row
  int referenceHalf = 8;  // road reference: pixels either side of straight ahead
  int minRun = 4;         // a side needs this many differing pixels in a row
  float minContrast = 22.0f;
  float madFactor = 3.0f;  // or this many times the road's own variation
  float narrowMaxM = 4.0f;  // "wide" is only reported above this
};

struct RowResult {
  bool used = false;
  int v = 0, centre = 0, left = 0, right = 0;
  bool openLeft = false, openRight = false;
  float width = 0;
};

struct WidthResult {
  bool valid = false;
  bool wide = false;      // wider than the camera can see
  float width = 0;        // metres (for wide: at least this)
  float confidence = 0;   // share of rows that agreed
  float distance = 0;     // metres ahead, the middle measuring distance
  RowResult rows[5];
};

WidthResult measureWidth(const uint8_t *gray, int width, int height, const Homography &toGround,
                         const Homography &toImage, const WidthSettings &settings);

// Median of the last few results, so one bad frame doesn't jump the reading
class WidthSmoother {
 public:
  WidthResult add(const WidthResult &r);

 private:
  static constexpr int kSize = 5;
  WidthResult history_[kSize];
  int count_ = 0, next_ = 0;
};
