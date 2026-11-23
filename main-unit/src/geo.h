#pragma once

// Small geometry helpers. Plain C++, so the host tests can use them too.

#include <cmath>

namespace geo {

constexpr double kPi = 3.14159265358979323846;
constexpr double kMetresPerDegreeLat = 110540.0;
constexpr double kMetresPerDegreeLonAtEquator = 111320.0;

inline double toRadians(double degrees) { return degrees * kPi / 180.0; }

// Metres east (x) and north (y) of an origin. Accurate to well under a metre
// over the few hundred metres a narrow road covers.
struct LocalFrame {
  double originLat = 0;
  double originLon = 0;
  double metresPerDegreeLon = kMetresPerDegreeLonAtEquator;

  LocalFrame() = default;
  LocalFrame(double lat, double lon)
      : originLat(lat),
        originLon(lon),
        metresPerDegreeLon(kMetresPerDegreeLonAtEquator * std::cos(toRadians(lat))) {}

  void toLocal(double lat, double lon, double &x, double &y) const {
    x = (lon - originLon) * metresPerDegreeLon;
    y = (lat - originLat) * kMetresPerDegreeLat;
  }

  void toLatLon(double x, double y, double &lat, double &lon) const {
    lat = originLat + y / kMetresPerDegreeLat;
    lon = originLon + x / metresPerDegreeLon;
  }
};

// The point `distance` metres from (lat, lon) in compass direction `bearing`
inline void project(double lat, double lon, double bearingDeg, double distance,
                    double &outLat, double &outLon) {
  LocalFrame frame(lat, lon);
  double b = toRadians(bearingDeg);
  frame.toLatLon(distance * std::sin(b), distance * std::cos(b), outLat, outLon);
}

inline double distanceMetres(double lat1, double lon1, double lat2, double lon2) {
  LocalFrame frame(lat1, lon1);
  double x, y;
  frame.toLocal(lat2, lon2, x, y);
  return std::sqrt(x * x + y * y);
}

}  // namespace geo
