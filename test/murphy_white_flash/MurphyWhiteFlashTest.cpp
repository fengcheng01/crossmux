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
constexpr bool validPulse(const Lut& lut) {
  if (lut.size() != sizeof(freeink::lut_m4_from_white)) return false;
  for (unsigned i = 100; i < lut.size(); ++i) {
    if (lut[i] != freeink::lut_m4_from_white[i]) return false;
  }
  return dose(lut, 0, 2) == frames(lut) && dose(lut, 1, 1) == 9 && dose(lut, 2, 1) == 12 &&
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
static_assert(validPulse(freeink::lut_m4_white_pulse));
static_assert(validPulse(freeink::lut_m4_direct_pulse));
static_assert(dose(freeink::lut_m4_white_pulse, 1, 2) == 0);
static_assert(dose(freeink::lut_m4_white_pulse, 2, 2) == 0);
static_assert(dose(freeink::lut_m4_direct_pulse, 1, 2) == 24);
static_assert(dose(freeink::lut_m4_direct_pulse, 2, 2) == 24);

int main() { std::puts("PASS: pulse LUT layout, target doses, frame totals, unchanged rate/voltage registers"); }
