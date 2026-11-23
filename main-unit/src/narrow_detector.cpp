#include "narrow_detector.h"

#include <algorithm>
#include <cmath>

#include "geo.h"

namespace {

// Suggestions closer than this to one already sent are the same road
constexpr double kSameRoadM = 40.0;

// Points along the road shape, and the most kept. The server accepts 500.
constexpr double kShapeSpacingM = 3.0;
constexpr size_t kMaxShapePoints = 80;

// The shape's half-width, whatever the camera measured. The server's own
// road shapes are 2.5 to 4 m wide.
constexpr double kMinHalfWidthM = 1.25;
constexpr double kMaxHalfWidthM = 2.0;

constexpr size_t kMaxRemembered = 50;

}  // namespace

void NarrowDetector::onPosition(double lat, double lon, float speedMps,
                                float bearingDeg, float accuracyM, uint32_t nowMs) {
  havePosition_ = true;
  lat_ = lat;
  lon_ = lon;
  speed_ = speedMps;
  bearing_ = bearingDeg;
  accuracy_ = accuracyM;
  positionAtMs_ = nowMs;
}

void NarrowDetector::onWidth(float widthM, bool wide, float distanceAheadM,
                             float confidence, uint32_t nowMs) {
  if (!havePosition_ || nowMs - positionAtMs_ > rules_.maxPairAgeMs) return;
  if (accuracy_ > rules_.maxGpsErrorM) return;
  if (speed_ < rules_.minSpeedMps) return;  // can't tell which way "ahead" is
  if (!wide && confidence < rules_.minConfidence) return;

  Sample s;
  geo::project(lat_, lon_, bearing_, distanceAheadM, s.lat, s.lon);
  s.width = widthM;
  s.narrow = !wide && widthM > 0 && widthM <= rules_.maxWidthM;
  s.atMs = nowMs;

  addSample(s);
}

void NarrowDetector::addSample(const Sample &s) {
  if (stretch_.empty()) {
    if (s.narrow) {
      stretch_.push_back(s);
      trailingWideM_ = 0;
      lastSampleAtMs_ = s.atMs;
    }
    return;
  }

  const Sample &last = stretch_.back();
  double step = geo::distanceMetres(last.lat, last.lon, s.lat, s.lon);

  if (step < rules_.sampleSpacingM) return;

  stretch_.push_back(s);
  lastSampleAtMs_ = s.atMs;

  if (s.narrow) {
    trailingWideM_ = 0;
  } else {
    trailingWideM_ += step;
    if (trailingWideM_ >= rules_.endGapM) pendingFinish_ = true;
  }
}

bool NarrowDetector::poll(uint32_t nowMs, RoadSuggestion &out) {
  if (stretch_.empty()) return false;

  bool silent = nowMs - lastSampleAtMs_ > rules_.maxSilenceMs;

  if (!pendingFinish_ && !silent) return false;

  return finishStretch(out);
}

bool NarrowDetector::nearPreviousSuggestion(double lat, double lon) const {
  for (size_t i = 0; i < sentLats_.size(); i++) {
    if (geo::distanceMetres(lat, lon, sentLats_[i], sentLons_[i]) < kSameRoadM) {
      return true;
    }
  }
  return false;
}

bool NarrowDetector::finishStretch(RoadSuggestion &out) {
  std::vector<Sample> samples;
  samples.swap(stretch_);
  pendingFinish_ = false;
  trailingWideM_ = 0;

  // The wide readings after the narrow part aren't part of the road
  while (!samples.empty() && !samples.back().narrow) samples.pop_back();

  if (samples.size() < 2) return false;

  double length = 0;
  for (size_t i = 1; i < samples.size(); i++) {
    length += geo::distanceMetres(samples[i - 1].lat, samples[i - 1].lon,
                                  samples[i].lat, samples[i].lon);
  }

  std::vector<float> widths;
  for (const Sample &s : samples) {
    if (s.narrow) widths.push_back(s.width);
  }

  float fraction = static_cast<float>(widths.size()) / samples.size();

  if (length < rules_.minLengthM || fraction < rules_.minFraction) return false;

  std::sort(widths.begin(), widths.end());
  float median = widths[widths.size() / 2];

  // Middle of the stretch, to recognise a road already suggested
  const Sample &middle = samples[samples.size() / 2];
  if (nearPreviousSuggestion(middle.lat, middle.lon)) return false;

  // The centre line, a point every few metres
  geo::LocalFrame frame(samples.front().lat, samples.front().lon);
  std::vector<double> xs, ys;
  double sinceLast = 0;

  for (size_t i = 0; i < samples.size(); i++) {
    double x, y;
    frame.toLocal(samples[i].lat, samples[i].lon, x, y);

    if (i > 0) {
      double dx = x - xs.back(), dy = y - ys.back();
      sinceLast = std::sqrt(dx * dx + dy * dy);
    }

    bool lastPoint = i == samples.size() - 1;

    if (i == 0 || sinceLast >= kShapeSpacingM || lastPoint) {
      xs.push_back(x);
      ys.push_back(y);
    }
  }

  // Too many points: keep every nth, always keeping the ends
  if (xs.size() > kMaxShapePoints) {
    std::vector<double> kx, ky;
    double stride = static_cast<double>(xs.size() - 1) / (kMaxShapePoints - 1);
    for (size_t k = 0; k < kMaxShapePoints; k++) {
      size_t i = static_cast<size_t>(std::lround(k * stride));
      kx.push_back(xs[i]);
      ky.push_back(ys[i]);
    }
    xs.swap(kx);
    ys.swap(ky);
  }

  double half = std::min(kMaxHalfWidthM, std::max(kMinHalfWidthM, median / 2.0));
  size_t n = xs.size();
  std::vector<double> leftX(n), leftY(n), rightX(n), rightY(n);

  for (size_t i = 0; i < n; i++) {
    size_t a = i == 0 ? 0 : i - 1;
    size_t b = i == n - 1 ? n - 1 : i + 1;
    double dx = xs[b] - xs[a], dy = ys[b] - ys[a];
    double len = std::sqrt(dx * dx + dy * dy);
    if (len < 1e-6) {
      dx = 0;
      dy = 1;
      len = 1;
    }
    // Perpendicular, to the left of the direction of travel
    double nx = -dy / len, ny = dx / len;
    leftX[i] = xs[i] + nx * half;
    leftY[i] = ys[i] + ny * half;
    rightX[i] = xs[i] - nx * half;
    rightY[i] = ys[i] - ny * half;
  }

  out = RoadSuggestion();
  auto addPoint = [&](double x, double y) {
    double lat, lon;
    frame.toLatLon(x, y, lat, lon);
    out.lats.push_back(lat);
    out.lons.push_back(lon);
  };

  for (size_t i = 0; i < n; i++) addPoint(leftX[i], leftY[i]);
  for (size_t i = n; i-- > 0;) addPoint(rightX[i], rightY[i]);

  out.lengthM = static_cast<float>(length);
  out.medianWidthM = median;
  out.narrowFraction = fraction;

  sentLats_.push_back(middle.lat);
  sentLons_.push_back(middle.lon);
  if (sentLats_.size() > kMaxRemembered) {
    sentLats_.erase(sentLats_.begin());
    sentLons_.erase(sentLons_.begin());
  }

  return true;
}
