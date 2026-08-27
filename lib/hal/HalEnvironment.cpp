#include "HalEnvironment.h"

#include <Logging.h>

HalEnvironment halEnvironment;

void HalEnvironment::begin() {
  begun_ = sensor_.begin();
  if (begun_) {
    LOG_INF("ENV", "Environment sensor ready");
  } else {
    LOG_DBG("ENV", "No environment sensor");
  }
}

bool HalEnvironment::read(float& tempC, float& humidityPct) {
  if (!begun_) return false;
  return sensor_.read(tempC, humidityPct);
}
