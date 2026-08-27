#pragma once

#include <cstdint>

// True when a long-running transfer should push another UI frame.
// E-paper cannot absorb a paint per chunk; 10% or 500 ms is the floor that
// keeps the SSD1677 BUSY line from stacking refreshes until it hangs.
inline constexpr bool shouldRepaintProgress(unsigned lastPercent, unsigned percent, uint32_t lastMs, uint32_t nowMs,
                                            unsigned minDeltaPercent = 10, uint32_t minIntervalMs = 500) {
  return percent == 100 || percent >= lastPercent + minDeltaPercent || (nowMs - lastMs) >= minIntervalMs;
}
