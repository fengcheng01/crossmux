#pragma once

#include <EnvironmentSensor.h>

class HalEnvironment;
extern HalEnvironment halEnvironment;

class HalEnvironment {
  EnvironmentSensor sensor_;
  bool begun_ = false;

 public:
  // Call after BoardConfig has selected the active device.
  void begin();

  bool present() const { return begun_; }

  // Single-shot ambient read. Returns false if the board has no sensor or the
  // transfer failed. tempC is °C; humidityPct is 0-100 %RH.
  bool read(float& tempC, float& humidityPct);
};
