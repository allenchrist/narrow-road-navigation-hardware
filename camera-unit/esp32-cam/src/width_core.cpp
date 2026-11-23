#include "width_core.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace {

constexpr int kMaxWidth = 1024;

float medianOf(float *values, int n) {
  std::sort(values, values + n);
  return n % 2 ? values[n / 2] : 0.5f * (values[n / 2 - 1] + values[n / 2]);
}

}  // namespace

bool invertHomography(const Homography &h, Homography &out) {
  const double *a = h.m;
  double det = a[0] * (a[4] * a[8] - a[5] * a[7]) - a[1] * (a[3] * a[8] - a[5] * a[6]) +
               a[2] * (a[3] * a[7] - a[4] * a[6]);
  if (std::fabs(det) < 1e-12) return false;
  double *b = out.m;
  b[0] = (a[4] * a[8] - a[5] * a[7]) / det;
  b[1] = (a[2] * a[7] - a[1] * a[8]) / det;
  b[2] = (a[1] * a[5] - a[2] * a[4]) / det;
  b[3] = (a[5] * a[6] - a[3] * a[8]) / det;
  b[4] = (a[0] * a[8] - a[2] * a[6]) / det;
  b[5] = (a[2] * a[3] - a[0] * a[5]) / det;
  b[6] = (a[3] * a[7] - a[4] * a[6]) / det;
  b[7] = (a[1] * a[6] - a[0] * a[7]) / det;
  b[8] = (a[0] * a[4] - a[1] * a[3]) / det;
  return true;
}

bool applyHomography(const Homography &h, double u, double v, double &x, double &y) {
  const double *m = h.m;
  double w = m[6] * u + m[7] * v + m[8];
  if (std::fabs(w) < 1e-12) return false;
  x = (m[0] * u + m[1] * v + m[2]) / w;
  y = (m[3] * u + m[4] * v + m[5]) / w;
  return true;
}

WidthResult measureWidth(const uint8_t *gray, int width, int height, const Homography &toGround,
                         const Homography &toImage, const WidthSettings &s) {
  WidthResult result;
  if (width > kMaxWidth) return result;

  float profile[kMaxWidth], smoothed[kMaxWidth];
  float closedWidths[5], openWidths[5];
  int closed = 0, open = 0, tried = 0;

  for (int d = 0; d < s.distanceCount && d < 5; d++) {
    RowResult &row = result.rows[d];
    double uc, vc;
    if (!applyHomography(toImage, 0.0, s.distances[d], uc, vc)) continue;
    int v = static_cast<int>(std::lround(vc));
    int centre = static_cast<int>(std::lround(uc));
    if (v - s.rowBand < 0 || v + s.rowBand >= height) continue;
    if (centre - s.referenceHalf < 1 || centre + s.referenceHalf >= width - 1) continue;
    tried++;

    // 1-2: average a band of rows, then smooth along the row
    for (int u = 0; u < width; u++) {
      int sum = 0;
      for (int r = v - s.rowBand; r <= v + s.rowBand; r++) sum += gray[r * width + u];
      profile[u] = static_cast<float>(sum) / (2 * s.rowBand + 1);
    }
    for (int u = 0; u < width; u++) {
      int a = std::max(0, u - s.smooth), b = std::min(width - 1, u + s.smooth);
      float sum = 0;
      for (int k = a; k <= b; k++) sum += profile[k];
      smoothed[u] = sum / (b - a + 1);
    }

    // 3: the road just ahead, and how much it varies by itself
    float window[64];
    int n = 0;
    for (int u = centre - s.referenceHalf; u <= centre + s.referenceHalf && n < 64; u++) {
      window[n++] = smoothed[u];
    }
    float reference = medianOf(window, n);
    for (int i = 0; i < n; i++) window[i] = std::fabs(window[i] - reference);
    float mad = medianOf(window, n);
    float threshold = std::max(s.minContrast, s.madFactor * mad);

    // 4: scan outwards for the sides
    auto scan = [&](int step, int &edge, bool &opened) {
      int run = 0;
      int u = centre + step * (s.referenceHalf + 1);
      for (; u >= 0 && u < width; u += step) {
        if (std::fabs(smoothed[u] - reference) > threshold) {
          if (++run >= s.minRun) {
            // The differing pixel nearest the road, moved out by half the
            // smoothing, which spreads the side inwards
            edge = u - step * (s.minRun - 1) + step * (s.smooth / 2);
            edge = std::max(0, std::min(width - 1, edge));
            opened = false;
            return;
          }
        } else {
          run = 0;
        }
      }
      edge = step < 0 ? 0 : width - 1;
      opened = true;
    };
    scan(-1, row.left, row.openLeft);
    scan(+1, row.right, row.openRight);

    // 5: both edges onto the ground
    double xl, yl, xr, yr;
    if (!applyHomography(toGround, row.left, v, xl, yl) ||
        !applyHomography(toGround, row.right, v, xr, yr)) {
      continue;
    }
    row.used = true;
    row.v = v;
    row.centre = centre;
    row.width = static_cast<float>(std::hypot(xr - xl, yr - yl));

    if (row.openLeft || row.openRight) openWidths[open++] = row.width;
    else closedWidths[closed++] = row.width;
  }

  if (tried == 0) return result;
  int needed = (tried + 1) / 2;
  result.distance = s.distances[s.distanceCount / 2];

  if (closed >= needed) {
    result.valid = true;
    result.width = medianOf(closedWidths, closed);
    result.confidence = static_cast<float>(closed) / tried;
  } else if (open >= needed) {
    // Only useful if the camera sees more than a narrow road's width
    float atLeast = medianOf(openWidths, open);
    if (atLeast > s.narrowMaxM) {
      result.valid = true;
      result.wide = true;
      result.width = atLeast;
      result.confidence = static_cast<float>(open) / tried;
    }
  }
  return result;
}

WidthResult WidthSmoother::add(const WidthResult &r) {
  if (!r.valid) return r;
  history_[next_] = r;
  next_ = (next_ + 1) % kSize;
  if (count_ < kSize) count_++;

  int wide = 0;
  float widths[kSize];
  int n = 0;
  float confidence = 0;
  for (int i = 0; i < count_; i++) {
    if (history_[i].wide) wide++;
    else widths[n++] = history_[i].width;
    confidence += history_[i].confidence;
  }

  WidthResult out = r;
  out.confidence = confidence / count_;
  if (wide * 2 > count_ || n == 0) {
    out.wide = true;
  } else {
    out.wide = false;
    out.width = medianOf(widths, n);
  }
  return out;
}
