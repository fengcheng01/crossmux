#include "EnvironmentSensor.h"

#include <BoardConfig.h>

#if FREEINK_CAP_TEMP_HUMIDITY

#include <Wire.h>
#include <soc/soc_caps.h>

#if FREEINK_DEVICE_MURPHY_M4
#include <MurphyM4I2c.h>
#endif

namespace freeink {
namespace {

constexpr uint8_t ADDR_AHT20 = 0x38;

// SHT40 commands (datasheet). 0xFD = measure T+RH, high precision.
constexpr uint8_t SHT40_CMD_MEASURE_HIGH_PRECISION = 0xFD;
constexpr uint8_t SHT40_CMD_SOFT_RESET = 0x94;
constexpr uint32_t SHT40_MEASURE_DELAY_MS = 10;  // high-precision conversion (~8.3 ms max)

// AHT20: init 0xBE 0x08 0x00, trigger 0xAC 0x33 0x00, 20-bit T/RH in 6-byte frame.
constexpr uint8_t AHT20_CMD_SOFT_RESET = 0xBA;
constexpr uint8_t AHT20_INIT[] = {0xBE, 0x08, 0x00};
constexpr uint8_t AHT20_TRIGGER[] = {0xAC, 0x33, 0x00};
constexpr uint32_t AHT20_RESET_DELAY_MS = 20;
constexpr uint32_t AHT20_INIT_DELAY_MS = 10;
constexpr uint32_t AHT20_MEASURE_DELAY_MS = 80;

bool g_wireReady[2] = {false, false};

bool isAht20(const uint8_t addr) { return addr == ADDR_AHT20; }

#if FREEINK_DEVICE_MURPHY_M4
bool usesM4NativeEnv() {
  return BoardConfig::ACTIVE.board == BoardConfig::Board::MurphyM4;
}

i2c_master_dev_handle_t m4EnvDevice() {
  const auto& s = BoardConfig::ACTIVE.sensors;
  return freeink::murphy_m4_i2c::envDevice(s.i2cSda, s.i2cScl, s.tempHumidityAddr);
}
#endif

TwoWire& sensorWire() {
  const auto& s = BoardConfig::ACTIVE.sensors;
#if SOC_I2C_NUM > 1
  return s.i2cBus == 1 ? Wire1 : Wire;
#else
  return Wire;
#endif
}

void ensureWire() {
#if FREEINK_DEVICE_MURPHY_M4
  if (usesM4NativeEnv()) return;
#endif
  const auto& s = BoardConfig::ACTIVE.sensors;
  const uint8_t bus =
#if SOC_I2C_NUM > 1
      s.i2cBus == 1 ? 1 : 0;
#else
      0;
#endif
  if (g_wireReady[bus]) return;
  auto& wire = sensorWire();
  wire.begin(s.i2cSda, s.i2cScl, s.i2cHz);
  g_wireReady[bus] = true;
}

bool sendBytes(uint8_t addr, const uint8_t* data, uint8_t len) {
#if FREEINK_DEVICE_MURPHY_M4
  if (usesM4NativeEnv()) {
    return freeink::murphy_m4_i2c::write(m4EnvDevice(), data, len);
  }
#endif
  ensureWire();
  auto& wire = sensorWire();
  wire.beginTransmission(addr);
  wire.write(data, len);
  return wire.endTransmission() == 0;
}

bool sendCommand(uint8_t addr, uint8_t cmd) { return sendBytes(addr, &cmd, 1); }

bool readBytes(uint8_t addr, uint8_t* dst, uint8_t len) {
#if FREEINK_DEVICE_MURPHY_M4
  if (usesM4NativeEnv()) {
    return freeink::murphy_m4_i2c::receive(m4EnvDevice(), dst, len);
  }
#endif
  auto& wire = sensorWire();
  if (wire.requestFrom(addr, len, static_cast<uint8_t>(true)) < len) return false;
  for (uint8_t i = 0; i < len; ++i) dst[i] = wire.read();
  return true;
}

// Sensirion CRC-8: polynomial 0x31, init 0xFF, over the two data bytes.
uint8_t crc8(uint8_t msb, uint8_t lsb) {
  uint8_t crc = 0xFF;
  const uint8_t data[2] = {msb, lsb};
  for (uint8_t b = 0; b < 2; ++b) {
    crc ^= data[b];
    for (uint8_t i = 0; i < 8; ++i) crc = (crc & 0x80) ? static_cast<uint8_t>((crc << 1) ^ 0x31) : static_cast<uint8_t>(crc << 1);
  }
  return crc;
}

bool decodeAht20(const uint8_t b[6], float& tempC, float& humidityPct) {
  if ((b[0] & 0x80) != 0) return false;  // still busy
  const uint32_t hum = (static_cast<uint32_t>(b[1]) << 12) | (static_cast<uint32_t>(b[2]) << 4) |
                       (static_cast<uint32_t>(b[3]) >> 4);
  const uint32_t tmp = ((static_cast<uint32_t>(b[3]) & 0x0Fu) << 16) | (static_cast<uint32_t>(b[4]) << 8) | b[5];
  float rh = static_cast<float>(hum) * 100.0f / 1048576.0f;
  if (rh < 0.0f) rh = 0.0f;
  if (rh > 100.0f) rh = 100.0f;
  humidityPct = rh;
  tempC = static_cast<float>(tmp) * 200.0f / 1048576.0f - 50.0f;
  return true;
}

bool beginAht20(uint8_t addr) {
  (void)sendCommand(addr, AHT20_CMD_SOFT_RESET);
  delay(AHT20_RESET_DELAY_MS);
  if (!sendBytes(addr, AHT20_INIT, sizeof(AHT20_INIT))) return false;
  delay(AHT20_INIT_DELAY_MS);
  return true;
}

bool readAht20(uint8_t addr, float& tempC, float& humidityPct) {
  if (!sendBytes(addr, AHT20_TRIGGER, sizeof(AHT20_TRIGGER))) return false;
  delay(AHT20_MEASURE_DELAY_MS);
  uint8_t b[6];
  if (!readBytes(addr, b, 6)) return false;
  if ((b[0] & 0x80) != 0) {
    delay(20);
    if (!readBytes(addr, b, 6)) return false;
  }
  return decodeAht20(b, tempC, humidityPct);
}

bool beginSht40(uint8_t addr) {
  if (!sendCommand(addr, SHT40_CMD_SOFT_RESET)) return false;
  delay(2);
  return true;
}

bool readSht40(uint8_t addr, float& tempC, float& humidityPct) {
  if (!sendCommand(addr, SHT40_CMD_MEASURE_HIGH_PRECISION)) return false;
  delay(SHT40_MEASURE_DELAY_MS);
  uint8_t b[6];
  if (!readBytes(addr, b, 6)) return false;
  if (crc8(b[0], b[1]) != b[2] || crc8(b[3], b[4]) != b[5]) return false;
  const uint16_t tRaw = static_cast<uint16_t>(b[0] << 8 | b[1]);
  const uint16_t rhRaw = static_cast<uint16_t>(b[3] << 8 | b[4]);
  tempC = -45.0f + 175.0f * (static_cast<float>(tRaw) / 65535.0f);
  float rh = -6.0f + 125.0f * (static_cast<float>(rhRaw) / 65535.0f);
  if (rh < 0.0f) rh = 0.0f;
  if (rh > 100.0f) rh = 100.0f;
  humidityPct = rh;
  return true;
}

}  // namespace

bool EnvironmentSensor::begin() {
  const uint8_t addr = BoardConfig::ACTIVE.sensors.tempHumidityAddr;
  if (addr == 0) return false;
  const auto& s = BoardConfig::ACTIVE.sensors;
  if (s.i2cSda < 0 || s.i2cScl < 0 || s.i2cHz == 0) return false;
#if FREEINK_DEVICE_MURPHY_M4
  if (usesM4NativeEnv()) {
    if (m4EnvDevice() == nullptr) return false;
  } else
#endif
  {
    ensureWire();
  }
  begun_ = isAht20(addr) ? beginAht20(addr) : beginSht40(addr);
  return begun_;
}

bool EnvironmentSensor::read(float& tempC, float& humidityPct) {
  const uint8_t addr = BoardConfig::ACTIVE.sensors.tempHumidityAddr;
  if (!begun_ || addr == 0) return false;
  return isAht20(addr) ? readAht20(addr, tempC, humidityPct) : readSht40(addr, tempC, humidityPct);
}

}  // namespace freeink

#else  // FREEINK_CAP_TEMP_HUMIDITY — sensor absent.

namespace freeink {
bool EnvironmentSensor::begin() { return false; }
bool EnvironmentSensor::read(float&, float&) { return false; }
}  // namespace freeink

#endif  // FREEINK_CAP_TEMP_HUMIDITY
