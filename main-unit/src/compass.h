#pragma once

#include <Arduino.h>
#include <Wire.h>

// The GY-271 compass board. Shops sell it as "QMC5883L", but three different
// chips turn up on it, so begin() looks for each:
//   QMC5883L at I2C address 0x0D (most GY-271 boards today)
//   QMC5883P at 0x2C (some newer boards)
//   HMC5883L at 0x1E (the original GY-271)
class Compass {
 public:
  bool begin(TwoWire &wire);

  // A new reading of the magnetic field in microtesla. False if none is
  // ready yet, or a magnet nearby overloaded the sensor.
  bool read(float &x, float &y, float &z);

  const char *chipName() const;

  // Prints every address that answers on the I2C wires, to help with wiring
  void printI2cScan();

 private:
  enum class Chip { None, QMC5883L, QMC5883P, HMC5883L };

  bool present(uint8_t address);
  bool writeRegister(uint8_t reg, uint8_t value);
  bool readRegisters(uint8_t reg, uint8_t *buf, size_t len);

  TwoWire *wire_ = nullptr;
  Chip chip_ = Chip::None;
  uint8_t address_ = 0;
};
