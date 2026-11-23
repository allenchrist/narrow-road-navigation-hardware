// Tests for NarrowDetector, run on a laptop (no ESP32 needed):
//   g++ -std=c++17 -I../src test_narrow_detector.cpp ../src/narrow_detector.cpp -o t && ./t

#include <cmath>
#include <cstdio>
#include <functional>

#include "../src/geo.h"
#include "../src/narrow_detector.h"

static int failures = 0;

#define CHECK(cond, msg)                         \
  do {                                           \
    if (!(cond)) {                               \
      std::printf("  FAIL: %s\n", msg);          \
      failures++;                                \
    }                                            \
  } while (0)

// Drives north at `speed` m/s from a point in Coimbatore, with a GPS fix and
// a camera reading five times a second, as the camera sends them.
// widthAt(metresTravelled) gives the true width of the road at that point.
struct Drive {
  double startLat = 11.0168, startLon = 76.9558;
  float speed = 4.0f;  // about 15 km/h, typical in a narrow lane
  float cameraAhead = 6.0f;
  float accuracy = 5.0f;
  float confidence = 0.9f;

  int run(float totalM, const std::function<float(float)> &widthAt,
          std::vector<RoadSuggestion> &found, NarrowDetector &d) {
    uint32_t t = 0;
    const uint32_t stepMs = 200;
    float travelled = 0;
    while (travelled <= totalM) {
      double lat, lon;
      geo::project(startLat, startLon, 0.0, travelled, lat, lon);
      d.onPosition(lat, lon, speed, 0.0f, accuracy, t);
      float w = widthAt(travelled + cameraAhead);
      bool wide = w >= 9.0f;  // wider than the camera sees
      d.onWidth(wide ? 9.0f : w, wide, cameraAhead, confidence, t);
      RoadSuggestion s;
      if (d.poll(t, s)) found.push_back(s);
      t += stepMs;
      travelled += speed * stepMs / 1000.0f;
    }
    // Stop sending; the stretch ends after the silence timeout
    for (int i = 0; i < 80; i++) {
      t += stepMs;
      RoadSuggestion s;
      if (d.poll(t, s)) found.push_back(s);
    }
    return static_cast<int>(found.size());
  }
};

static void testFindsNarrowStretch() {
  std::printf("A 40 m lane 3 m wide between wide roads is suggested\n");
  NarrowDetector d;
  Drive drive;
  std::vector<RoadSuggestion> found;
  drive.run(140, [](float m) { return (m >= 50 && m < 90) ? 3.0f : 12.0f; }, found, d);
  CHECK(found.size() == 1, "exactly one suggestion");
  if (found.size() == 1) {
    const RoadSuggestion &s = found[0];
    std::printf("  length %.1f m, width %.2f m, %zu points, %.0f%% narrow\n", s.lengthM,
                s.medianWidthM, s.lats.size(), s.narrowFraction * 100);
    CHECK(std::fabs(s.lengthM - 40) < 4, "length about 40 m");
    CHECK(std::fabs(s.medianWidthM - 3.0f) < 0.01f, "width 3 m");
    CHECK(s.lats.size() >= 6 && s.lats.size() <= 500, "polygon size accepted by server");
    // The shape starts near where the lane starts: 50 m north of the start
    double startNorth = (s.lats[0] - 11.0168) * 110540.0;
    CHECK(std::fabs(startNorth - 50) < 4, "shape begins where the lane begins");
    // Shape is about 3 m wide: first left point to last (first right) point
    double w = geo::distanceMetres(s.lats[0], s.lons[0], s.lats.back(), s.lons.back());
    CHECK(std::fabs(w - 3.0) < 0.2, "shape is as wide as the lane");
  }
}

static void testIgnoresShortStretch() {
  std::printf("A 15 m narrow gap is too short\n");
  NarrowDetector d;
  Drive drive;
  std::vector<RoadSuggestion> found;
  drive.run(120, [](float m) { return (m >= 50 && m < 65) ? 3.0f : 12.0f; }, found, d);
  CHECK(found.empty(), "no suggestion");
}

static void testToleratesWideSpots() {
  std::printf("Short wide spots inside a narrow lane are allowed\n");
  NarrowDetector d;
  Drive drive;
  std::vector<RoadSuggestion> found;
  // 60 m lane with a 6 m wide gap between houses every 20 m
  drive.run(160, [](float m) {
    if (m < 40 || m >= 100) return 12.0f;
    float into = std::fmod(m - 40, 20.0f);
    return into < 6 ? 5.5f : 3.4f;
  }, found, d);
  CHECK(found.size() == 1, "one suggestion");
  if (!found.empty()) {
    std::printf("  length %.1f m, %.0f%% narrow\n", found[0].lengthM,
                found[0].narrowFraction * 100);
    CHECK(found[0].lengthM > 50, "covers the whole lane");
  }
}

static void testIgnoresMostlyWide() {
  std::printf("A road that's narrow less than 70%% of the way isn't suggested\n");
  NarrowDetector d;
  Drive drive;
  std::vector<RoadSuggestion> found;
  drive.run(160, [](float m) {
    if (m < 40 || m >= 100) return 12.0f;
    return std::fmod(m - 40, 10.0f) < 5 ? 3.0f : 6.0f;
  }, found, d);
  CHECK(found.empty(), "no suggestion");
}

static void testNeedsMovement() {
  std::printf("Readings while stopped, or with poor GPS, are ignored\n");
  NarrowDetector d;
  Drive drive;
  std::vector<RoadSuggestion> found;
  drive.speed = 0.5f;
  drive.run(30, [](float) { return 3.0f; }, found, d);
  CHECK(found.empty() && !d.inStretch(), "stopped: nothing collected");
  NarrowDetector d2;
  Drive bad;
  bad.accuracy = 30;
  bad.run(140, [](float m) { return (m >= 50 && m < 90) ? 3.0f : 12.0f; }, found, d2);
  CHECK(found.empty(), "poor GPS: nothing suggested");
}

static void testSameRoadOnce() {
  std::printf("Driving the same lane twice suggests it once\n");
  NarrowDetector d;
  std::vector<RoadSuggestion> found;
  Drive a;
  a.run(140, [](float m) { return (m >= 50 && m < 90) ? 3.0f : 12.0f; }, found, d);
  Drive b;
  b.run(140, [](float m) { return (m >= 50 && m < 90) ? 3.0f : 12.0f; }, found, d);
  CHECK(found.size() == 1, "one suggestion in total");
}

int main() {
  testFindsNarrowStretch();
  testIgnoresShortStretch();
  testToleratesWideSpots();
  testIgnoresMostlyWide();
  testNeedsMovement();
  testSameRoadOnce();
  std::printf(failures ? "\n%d check(s) failed\n" : "\nAll checks passed\n", failures);
  return failures ? 1 : 0;
}
