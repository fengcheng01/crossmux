#include <cstdio>

#include "lut/Ssd1677Luts.h"

// Verify electrical scheduling only; these tests cannot predict pigment motion.
template <typename Lut>
constexpr unsigned frames(const Lut& lut) {
  unsigned total = 0;
  for (unsigned group = 0; group < 10; ++group) {
    for (unsigned phase = 0; phase < 4; ++phase) {
      total += lut[50 + group * 5 + phase] * (lut[54 + group * 5] + 1);
    }
  }
  return total;
}

template <typename Lut>
constexpr unsigned dose(const Lut& lut, unsigned target, unsigned source) {
  unsigned total = 0;
  for (unsigned group = 0; group < 10; ++group) {
    for (unsigned phase = 0; phase < 4; ++phase) {
      if (((lut[target * 10 + group] >> (6 - 2 * phase)) & 3) == source) {
        total += lut[50 + group * 5 + phase] * (lut[54 + group * 5] + 1);
      }
    }
  }
  return total;
}

template <typename Lut>
constexpr bool validPulse(const Lut& lut, unsigned lightInkFrames) {
  if (lut.size() != sizeof(freeink::lut_m4_from_white)) return false;
  for (unsigned i = 100; i < lut.size(); ++i) {
    if (lut[i] != freeink::lut_m4_from_white[i]) return false;
  }
  return dose(lut, 0, 2) == frames(lut) && dose(lut, 1, 1) == lightInkFrames && dose(lut, 2, 1) == 12 &&
         dose(lut, 3, 1) == 24 && dose(lut, 3, 2) == 0;
}

// All nonwhite targets must finish their ink dose at the end of Direct.
// This catches the v129 schedule (black finished at 24, edges at 33/36).
template <typename Lut>
constexpr unsigned inkEnd(const Lut& lut, unsigned target) {
  unsigned time = 0;
  unsigned end = 0;
  for (unsigned group = 0; group < 10; ++group) {
    for (unsigned repeat = 0; repeat <= lut[54 + group * 5]; ++repeat) {
      for (unsigned phase = 0; phase < 4; ++phase) {
        const auto duration = lut[50 + group * 5 + phase];
        time += duration;
        if (duration && ((lut[target * 10 + group] >> (6 - 2 * phase)) & 3) == 1) end = time;
      }
    }
  }
  return end;
}
static_assert(inkEnd(freeink::lut_m4_direct_pulse, 1) == 36);
static_assert(inkEnd(freeink::lut_m4_direct_pulse, 2) == 36);
static_assert(inkEnd(freeink::lut_m4_direct_pulse, 3) == 36);

static_assert(frames(freeink::lut_m4_from_white) == 60);
static_assert(frames(freeink::lut_m4_white_pulse) == 24);
static_assert(frames(freeink::lut_m4_direct_pulse) == 36);
static_assert(validPulse(freeink::lut_m4_white_pulse, 9));
static_assert(validPulse(freeink::lut_m4_direct_pulse, 8));
static_assert(dose(freeink::lut_m4_white_pulse, 1, 2) == 0);
static_assert(dose(freeink::lut_m4_white_pulse, 2, 2) == 0);
static_assert(dose(freeink::lut_m4_direct_pulse, 1, 2) == 24);
static_assert(dose(freeink::lut_m4_direct_pulse, 2, 2) == 24);
static_assert(dose(freeink::lut_m4_direct_pulse, 1, 0) == 4);

// Compare each frame with the accepted 9-frame light-edge baseline. Timing
// groups are global: changing them must not alter the other targets' drives.
constexpr unsigned sourceAt(const auto& lut, unsigned target, unsigned frame) {
  for (unsigned group = 0; group < 10; ++group) {
    for (unsigned repeat = 0; repeat <= lut[54 + group * 5]; ++repeat) {
      for (unsigned phase = 0; phase < 4; ++phase) {
        const auto duration = lut[50 + group * 5 + phase];
        if (frame < duration) return (lut[target * 10 + group] >> (6 - 2 * phase)) & 3;
        frame -= duration;
      }
    }
  }
  return 0;
}

constexpr bool onlyLightEdgeChanges() {
  auto previous = freeink::lut_m4_direct_pulse;
  previous[52] = 3;
  previous[53] = 9;
  for (unsigned target = 0; target < 4; ++target) {
    for (unsigned frame = 0; frame < 36; ++frame) {
      const auto before = sourceAt(previous, target, frame);
      const auto after = sourceAt(freeink::lut_m4_direct_pulse, target, frame);
      if (target == 1 && frame == 27) {
        if (before != 1 || after != 0) return false;
      } else if (before != after) {
        return false;
      }
    }
  }
  return true;
}
static_assert(onlyLightEdgeChanges());

int main() { std::puts("PASS: pulse LUT layout, target doses, frame totals, unchanged rate/voltage registers"); }
