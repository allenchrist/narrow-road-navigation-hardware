#pragma once

#include <Arduino.h>
#include <Wire.h>

// INA219 current and voltage sensor (the blue board with the R100 resistor).
// Put it in the battery's + wire: battery + to Vin+, Vin- on to the load.
// It measures the voltage at Vin- (up to 26 V) and the current through its
// 0.1 ohm resistor (up to 3.2 A), over I2C.
class Ina219 {
 public:
  bool begin(TwoWire &wire, uint8_t address, float shuntOhms);
  // volts: the battery side of the load. milliamps: positive when current
  // flows from Vin+ to Vin- (the battery discharging)
  bool read(float &volts, float &milliamps);
  bool ok() const { return ok_; }

 private:
  bool writeRegister(uint8_t reg, uint16_t value);
  bool readRegister(uint8_t reg, uint16_t &value);

  TwoWire *wire_ = nullptr;
  uint8_t address_ = 0x40;
  float shuntOhms_ = 0.1f;
  bool ok_ = false;
};
