#pragma once

// FreeInk temperature + humidity sensor.
//
// Reads ambient temperature (°C) and relative humidity (%) from the I2C sensor
// described by BoardConfig::ACTIVE.sensors (tempHumidityAddr / sensor bus):
//   0x44 = Sensirion SHT40 (Sticky)
//   0x38 = Aosong AHT20 (Murphy M4)
// Boards without the sensor (FREEINK_CAP_TEMP_HUMIDITY off, or
// tempHumidityAddr == 0) link stub bodies and present() returns false.

#include <Arduino.h>

#include <cstdint>

namespace freeink {

class EnvironmentSensor {
 public:
  bool begin();
  bool present() const { return begun_; }

  // High-precision single-shot read. Returns false on I2C error or invalid
  // payload. tempC is degrees Celsius; humidityPct is 0-100 %RH (clamped).
  bool read(float& tempC, float& humidityPct);

 private:
  bool begun_ = false;
};

}  // namespace freeink

using EnvironmentSensor = freeink::EnvironmentSensor;
