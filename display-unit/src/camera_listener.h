#pragma once

#include <Arduino.h>
#include <WiFiUdp.h>

// Small JSON packets broadcast on the hotspot (UDP port 4210):
//   camera unit:  {"type":"width","width":3.2,"wide":false,"distance":6.0,"confidence":0.8}
//   main unit:    {"type":"position","fix":true,"lat":..,"lon":..,"speed":..,"heading":..}
//   health unit:  {"type":"fuel","ok":true,"calibrated":true,"percent":62.0,"litres":3.1,...}
//                 {"type":"battery","ok":true,"volts":15.2,"amps":0.42,"percent":71,...}
struct WidthReading {
  float width = 0;       // metres between the two sides of the road
  bool wide = false;     // wider than the camera could see
  float distance = 0;    // metres ahead of the car where it was measured
  float confidence = 0;  // 0 to 1
  uint32_t receivedMs = 0;
};

struct PositionReading {
  bool fix = false;
  double lat = 0, lon = 0;
  float speedMps = 0;
  bool hasHeading = false;
  float heading = 0;
  uint32_t receivedMs = 0;
};

struct FuelReading {
  bool ok = false;          // the sensor is getting echoes
  bool calibrated = false;  // empty and full have been set
  float percent = -1;
  float litres = -1;        // -1 if the capacity isn't set
  bool low = false;
  uint32_t receivedMs = 0;
};

struct BatteryReading {
  bool ok = false;  // a battery is connected and measured
  float volts = 0;
  float amps = 0;
  float watts = 0;
  float percent = -1;
  float minutesLeft = -1;
  bool low = false;
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

  // The other units' latest readings; receivedMs is 0 until one arrives
  const PositionReading &position() const { return position_; }
  const FuelReading &fuel() const { return fuel_; }
  const BatteryReading &battery() const { return battery_; }

 private:
  WiFiUDP udp_;
  bool listening_ = false;
  bool fresh_ = false;
  uint32_t lastMs_ = 0;
  WidthReading last_;
  PositionReading position_;
  FuelReading fuel_;
  BatteryReading battery_;
};
