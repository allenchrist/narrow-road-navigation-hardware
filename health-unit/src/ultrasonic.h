#pragma once

#include <Arduino.h>

// HC-SR04 ultrasonic distance sensor, mounted at the top of the tank
// pointing straight down at the liquid. Takes one reading every 60 ms (the
// echo needs time to die away) and keeps the last 7; distanceCm() is their
// middle value, so a stray echo doesn't make the level jump.
class Ultrasonic {
 public:
  void begin(int trigPin, int echoPin, float airTempC);
  void update();  // call every loop

  // False until there are readings, or when the echo has stopped coming back
  bool ok() const;
  float distanceCm() const;
  uint32_t goodReadings() const { return goodReadings_; }

 private:
  static constexpr int kKeep = 7;

  int trig_ = -1, echo_ = -1;
  float airTempC_ = 25;
  uint32_t lastPingMs_ = 0;
  uint32_t lastGoodMs_ = 0;
  uint32_t goodReadings_ = 0;
  float readings_[kKeep] = {};
  int count_ = 0, next_ = 0;
};
