#include "ultrasonic.h"

#include "health_math.h"

namespace {

constexpr uint32_t kPingIntervalMs = 60;
constexpr uint32_t kEchoTimeoutUs = 25000;  // about 4 m: beyond that, no echo
constexpr float kMinCm = 2.0f;              // closer than this it can't measure
constexpr float kMaxCm = 400.0f;
constexpr uint32_t kStaleMs = 2000;

}  // namespace

void Ultrasonic::begin(int trigPin, int echoPin, float airTempC) {
  trig_ = trigPin;
  echo_ = echoPin;
  airTempC_ = airTempC;
  pinMode(trig_, OUTPUT);
  digitalWrite(trig_, LOW);
  pinMode(echo_, INPUT);
}

void Ultrasonic::update() {
  uint32_t now = millis();
  if (now - lastPingMs_ < kPingIntervalMs) return;
  lastPingMs_ = now;

  // A 10 microsecond pulse on Trig sends the sound; Echo then stays high
  // for as long as the sound took to come back
  digitalWrite(trig_, LOW);
  delayMicroseconds(2);
  digitalWrite(trig_, HIGH);
  delayMicroseconds(10);
  digitalWrite(trig_, LOW);
  uint32_t us = pulseIn(echo_, HIGH, kEchoTimeoutUs);
  if (us == 0) return;  // no echo

  float cm = echoToCm(us, airTempC_);
  if (cm < kMinCm || cm > kMaxCm) return;

  readings_[next_] = cm;
  next_ = (next_ + 1) % kKeep;
  if (count_ < kKeep) count_++;
  lastGoodMs_ = millis();
  goodReadings_++;
}

bool Ultrasonic::ok() const { return count_ > 0 && millis() - lastGoodMs_ < kStaleMs; }

float Ultrasonic::distanceCm() const { return medianOf(readings_, count_); }
