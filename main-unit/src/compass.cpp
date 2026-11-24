#include "compass.h"

namespace {

constexpr uint8_t kQmc5883lAddress = 0x0D;
constexpr uint8_t kQmc5883pAddress = 0x2C;
constexpr uint8_t kHmc5883lAddress = 0x1E;

// Counts per gauss at the range each chip is set to below (1 gauss = 100 µT)
constexpr float kQmc5883lPerGauss = 3000.0f;  // ±8 gauss range
constexpr float kQmc5883pPerGauss = 3750.0f;  // ±8 gauss range
constexpr float kHmc5883lPerGauss = 1090.0f;  // ±1.3 gauss range

int16_t littleEndian(const uint8_t *b) { return static_cast<int16_t>(b[0] | (b[1] << 8)); }
int16_t bigEndian(const uint8_t *b) { return static_cast<int16_t>((b[0] << 8) | b[1]); }

}  // namespace

bool Compass::present(uint8_t address) {
  wire_->beginTransmission(address);
  return wire_->endTransmission() == 0;
}

bool Compass::writeRegister(uint8_t reg, uint8_t value) {
  wire_->beginTransmission(address_);
  wire_->write(reg);
  wire_->write(value);
  return wire_->endTransmission() == 0;
}

bool Compass::readRegisters(uint8_t reg, uint8_t *buf, size_t len) {
  wire_->beginTransmission(address_);
  wire_->write(reg);
  if (wire_->endTransmission(false) != 0) return false;
  if (wire_->requestFrom(address_, static_cast<uint8_t>(len)) != len) return false;
  for (size_t i = 0; i < len; i++) buf[i] = wire_->read();
  return true;
}

bool Compass::begin(TwoWire &wire) {
  wire_ = &wire;
  chip_ = Chip::None;

  if (present(kQmc5883lAddress)) {
    address_ = kQmc5883lAddress;
    // Settings from the QMC5883L datasheet: soft reset, then the set/reset
    // period it recommends, then continuous readings at 200 a second,
    // ±8 gauss range, 512× oversampling (the least noisy)
    bool ok = writeRegister(0x0A, 0x80);
    delay(10);
    ok = ok && writeRegister(0x0B, 0x01) && writeRegister(0x09, 0x1D);
    if (ok) chip_ = Chip::QMC5883L;
  } else if (present(kQmc5883pAddress)) {
    address_ = kQmc5883pAddress;
    // The QMC5883P datasheet's own start-up example: axis signs, then
    // set/reset on with ±8 gauss range, then continuous readings
    bool ok = writeRegister(0x29, 0x06) && writeRegister(0x0B, 0x08) &&
              writeRegister(0x0A, 0xCD);
    if (ok) chip_ = Chip::QMC5883P;
  } else if (present(kHmc5883lAddress)) {
    address_ = kHmc5883lAddress;
    // 8 readings averaged at 15 a second, ±1.3 gauss, continuous
    bool ok = writeRegister(0x00, 0x70) && writeRegister(0x01, 0x20) &&
              writeRegister(0x02, 0x00);
    if (ok) chip_ = Chip::HMC5883L;
  }

  return chip_ != Chip::None;
}

bool Compass::read(float &x, float &y, float &z) {
  uint8_t b[6];
  uint8_t status = 0;

  switch (chip_) {
    case Chip::QMC5883L:
      // Status: bit 0 new reading ready, bit 1 overloaded
      if (!readRegisters(0x06, &status, 1) || !(status & 0x01)) return false;
      if (!readRegisters(0x00, b, 6)) return false;
      if (status & 0x02) return false;
      x = littleEndian(b) / kQmc5883lPerGauss * 100.0f;
      y = littleEndian(b + 2) / kQmc5883lPerGauss * 100.0f;
      z = littleEndian(b + 4) / kQmc5883lPerGauss * 100.0f;
      return true;

    case Chip::QMC5883P:
      if (!readRegisters(0x09, &status, 1) || !(status & 0x01)) return false;
      if (!readRegisters(0x01, b, 6)) return false;
      if (status & 0x02) return false;
      x = littleEndian(b) / kQmc5883pPerGauss * 100.0f;
      y = littleEndian(b + 2) / kQmc5883pPerGauss * 100.0f;
      z = littleEndian(b + 4) / kQmc5883pPerGauss * 100.0f;
      return true;

    case Chip::HMC5883L: {
      if (!readRegisters(0x09, &status, 1) || !(status & 0x01)) return false;
      // The HMC5883L sends X, then Z, then Y, high byte first
      if (!readRegisters(0x03, b, 6)) return false;
      int16_t rx = bigEndian(b), rz = bigEndian(b + 2), ry = bigEndian(b + 4);
      if (rx == -4096 || ry == -4096 || rz == -4096) return false;  // overloaded
      x = rx / kHmc5883lPerGauss * 100.0f;
      y = ry / kHmc5883lPerGauss * 100.0f;
      z = rz / kHmc5883lPerGauss * 100.0f;
      return true;
    }

    case Chip::None:
      break;
  }
  return false;
}

const char *Compass::chipName() const {
  switch (chip_) {
    case Chip::QMC5883L: return "QMC5883L";
    case Chip::QMC5883P: return "QMC5883P";
    case Chip::HMC5883L: return "HMC5883L";
    case Chip::None: break;
  }
  return "none";
}

void Compass::printI2cScan() {
  Serial.print("[IMU] Devices answering on I2C:");
  int found = 0;
  for (uint8_t a = 1; a < 127; a++) {
    if (present(a)) {
      Serial.printf(" 0x%02X", a);
      found++;
    }
  }
  Serial.println(found ? "" : " none. Check SDA, SCL, VCC and GND");
}
