#pragma once

#if defined(SIMULATOR)
// Host build: there is no I2C sensor and the pinned simulator library predates
// HalEnvironment (it ships no shim, unlike HalDisplay/HalGPIO), so stub the
// class inline. present()/read() report "no sensor" and callers hide the data.
class HalEnvironment {
 public:
  void begin() {}
  bool present() const { return false; }
  bool read(float& tempC, float& humidityPct) {
    (void)tempC;
    (void)humidityPct;
    return false;
  }
};

inline HalEnvironment halEnvironment;
#else
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
#endif
