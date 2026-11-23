#pragma once

#include <Arduino.h>
#include <TinyGPSPlus.h>

// Reads NMEA sentences from any standard GPS module (u-blox NEO-6M, M8N,
// M9N, ATGM336H...). On u-blox modules it also turns on the GST sentence,
// which gives a real accuracy estimate in metres.
class GpsReader {
 public:
  void begin(int rxPin, int txPin, uint32_t baud);

  // Call often: reads whatever bytes have arrived
  void update();

  // A position newer than maxAgeMs
  bool hasFix(uint32_t maxAgeMs) const;

  double lat() { return gps_.location.lat(); }
  double lon() { return gps_.location.lng(); }
  float speedMps() { return gps_.speed.isValid() ? gps_.speed.mps() : 0.0f; }

  // Direction of travel. Only meaningful once the car is moving.
  bool hasCourse() { return gps_.course.isValid() && gps_.course.age() < 3000; }
  float courseDeg() { return gps_.course.deg(); }

  // Estimated horizontal error in metres
  float accuracyM();

  uint32_t satellites() { return gps_.satellites.isValid() ? gps_.satellites.value() : 0; }
  uint32_t charsProcessed() const { return gps_.charsProcessed(); }

 private:
  void sendUbloxCommand(const char *body);

  HardwareSerial serial_{1};
  TinyGPSPlus gps_;
  // GST fields 6 and 7: standard deviation of latitude and longitude error
  TinyGPSCustom gpLatErr_{gps_, "GPGST", 6};
  TinyGPSCustom gpLonErr_{gps_, "GPGST", 7};
  TinyGPSCustom gnLatErr_{gps_, "GNGST", 6};
  TinyGPSCustom gnLonErr_{gps_, "GNGST", 7};
};
