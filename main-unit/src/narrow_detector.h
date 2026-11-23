#pragma once

// Turns camera road widths and GPS positions into narrow-road suggestions.
//
// The camera measures the road a few metres ahead, so each width is placed
// at the point that distance ahead of the car along its direction of
// travel. While the readings say the road is narrow, they're collected into
// a stretch. When the road has been wide again for a while, the stretch is
// checked with the same rules the server's findNarrowRoads.js tool uses,
// and if it passes, its shape is returned for sending as a suggestion.
//
// Plain C++ with no Arduino code, so it can be tested on a laptop.

#include <cstddef>
#include <cstdint>
#include <vector>

struct NarrowRules {
  float maxWidthM = 4.0f;
  float minLengthM = 25.0f;
  float minFraction = 0.70f;
  float endGapM = 15.0f;
  float minConfidence = 0.5f;
  float maxGpsErrorM = 15.0f;
  float minSpeedMps = 1.5f;      // below this, GPS direction isn't reliable
  float sampleSpacingM = 2.0f;   // one reading per 2 m of road
  float maxPairAgeMs = 1500.0f;  // a width and position this close in time
  float maxSilenceMs = 10000.0f; // a stretch ends after this long without readings
};

struct RoadSuggestion {
  std::vector<double> lats;  // polygon, closed implicitly
  std::vector<double> lons;
  float lengthM = 0;
  float medianWidthM = 0;
  float narrowFraction = 0;
};

class NarrowDetector {
 public:
  explicit NarrowDetector(const NarrowRules &rules = NarrowRules()) : rules_(rules) {}

  // Call with every GPS fix
  void onPosition(double lat, double lon, float speedMps, float bearingDeg,
                  float accuracyM, uint32_t nowMs);

  // Call with every camera reading. `wide` means the road was wider than the
  // camera could see, so it's certainly not narrow.
  void onWidth(float widthM, bool wide, float distanceAheadM, float confidence,
               uint32_t nowMs);

  // Call regularly. Returns true and fills `out` when a stretch has ended
  // and passed the rules.
  bool poll(uint32_t nowMs, RoadSuggestion &out);

  bool inStretch() const { return !stretch_.empty(); }
  size_t stretchSamples() const { return stretch_.size(); }

 private:
  struct Sample {
    double lat, lon;
    float width;
    bool narrow;
    uint32_t atMs;
  };

  void addSample(const Sample &s);
  bool finishStretch(RoadSuggestion &out);
  bool nearPreviousSuggestion(double lat, double lon) const;

  NarrowRules rules_;

  bool havePosition_ = false;
  double lat_ = 0, lon_ = 0;
  float speed_ = 0, bearing_ = 0, accuracy_ = 0;
  uint32_t positionAtMs_ = 0;

  std::vector<Sample> stretch_;
  float trailingWideM_ = 0;  // road length since the last narrow reading
  uint32_t lastSampleAtMs_ = 0;
  bool pendingFinish_ = false;

  std::vector<double> sentLats_, sentLons_;
};
