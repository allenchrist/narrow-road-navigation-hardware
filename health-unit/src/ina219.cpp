#include "ina219.h"

namespace {

constexpr uint8_t kConfig = 0x00;
constexpr uint8_t kShuntVoltage = 0x01;
constexpr uint8_t kBusVoltage = 0x02;

// 32 V range, +-320 mV across the resistor, 12-bit readings averaged over
// 8 samples (steadier numbers), measuring both all the time
constexpr uint16_t kConfigValue = 0x3DDF;

}  // namespace

bool Ina219::begin(TwoWire &wire, uint8_t address, float shuntOhms) {
  wire_ = &wire;
  address_ = address;
  shuntOhms_ = shuntOhms;

  uint16_t check = 0;
  ok_ = writeRegister(kConfig, kConfigValue) && readRegister(kConfig, check) &&
        check == kConfigValue;
  return ok_;
}

bool Ina219::read(float &volts, float &milliamps) {
  uint16_t shuntRaw, busRaw;
  if (!readRegister(kShuntVoltage, shuntRaw) || !readRegister(kBusVoltage, busRaw)) {
    ok_ = false;
    return false;
  }
  ok_ = true;

  // Shunt: signed, 10 microvolts per step
  float shuntMv = static_cast<int16_t>(shuntRaw) * 0.01f;
  milliamps = shuntMv / shuntOhms_;

  // Bus: top 13 bits, 4 millivolts per step
  volts = (busRaw >> 3) * 0.004f;
  return true;
}

bool Ina219::writeRegister(uint8_t reg, uint16_t value) {
  wire_->beginTransmission(address_);
  wire_->write(reg);
  wire_->write(static_cast<uint8_t>(value >> 8));
  wire_->write(static_cast<uint8_t>(value & 0xFF));
  return wire_->endTransmission() == 0;
}

bool Ina219::readRegister(uint8_t reg, uint16_t &value) {
  wire_->beginTransmission(address_);
  wire_->write(reg);
  if (wire_->endTransmission(false) != 0) return false;
  if (wire_->requestFrom(address_, static_cast<uint8_t>(2)) != 2) return false;
  value = static_cast<uint16_t>(wire_->read()) << 8;
  value |= wire_->read();
  return true;
}
