#pragma once

#include <Arduino.h>
#include <WiFiUdp.h>

// Road-width readings broadcast by the camera unit (ESP32-CAM, or the
// Python program with a USB webcam or phone camera) as small JSON packets:
//   {"type":"width","width":3.2,"wide":false,"distance":6.0,"confidence":0.8}
struct WidthReading {
  float width = 0;       // metres between the two sides of the road
  bool wide = false;     // wider than the camera could see
  float distance = 0;    // metres ahead of the car where it was measured
  float confidence = 0;  // 0 to 1
  uint32_t receivedMs = 0;
};

class CameraListener {
 public:
  // Starts listening once Wi-Fi is up; safe to call every loop
  void update(uint16_t port);

  // True once per new reading
  bool take(WidthReading &out);

  bool everHeard() const { return lastMs_ != 0; }
  const WidthReading &last() const { return last_; }

 private:
  WiFiUDP udp_;
  bool listening_ = false;
  bool fresh_ = false;
  uint32_t lastMs_ = 0;
  WidthReading last_;
};
