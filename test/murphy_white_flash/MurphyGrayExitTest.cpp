#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <vector>

#include "driver/Ssd1677Driver.h"
#include "lut/Ssd1677Luts.h"

namespace {
using Frame = std::array<uint8_t, 4>;
using Lut = std::array<uint8_t, 112>;
struct Paint { uint8_t sequence; Frame bw; Frame red; Lut lut; };
Lut loadedLut{};
bool delayedActivationBusy = false;
Frame bw{}, red{};
std::vector<Paint> paints;
std::vector<uint8_t> activations;
bool analogOn = false;
unsigned sleepCommands = 0;
uint8_t command = 0, sequence = 0;
unsigned offset = 0;
bool busy = false;
const Frame white{0xFF, 0xFF, 0xFF, 0xFF};
const Frame black{};
const Frame oldPage{0x55, 0xAA, 0x55, 0xAA};
const Frame menu{0xF0, 0x0F, 0x33, 0xCC};
}

// Link the real driver to a recording bus, without SPI/GPIO hardware. Reject
// RAM writes during an activation and inspect the actual target/previous planes.
namespace freeink {
void EpdBus::cmd(uint8_t c) {
  assert(!busy);
  command = c;
  offset = 0;
  if (c == 0x20) {
    busy = true;
    activations.push_back(sequence);
    analogOn = sequence != 0x03;
    if (sequence != 0xC0 && sequence != 0x03) paints.push_back({sequence, bw, red, loadedLut});
  }
  if (c == 0x10) {
    assert(!analogOn);
    ++sleepCommands;
  }
}
void EpdBus::data(uint8_t d) {
  assert(!busy);
  if (command == 0x22) sequence = d;
  if (command == 0x32) { assert(offset < 105); loadedLut[offset++] = d; }
  if (command == 0x03) loadedLut[105] = d;
  if (command == 0x04) { assert(offset < 3); loadedLut[106 + offset++] = d; }
  if (command == 0x2C) loadedLut[109] = d;
  if (command == 0x24 || command == 0x26) {
    assert(offset < bw.size());
    (command == 0x24 ? bw : red)[offset++] = d;
  }
}
void EpdBus::data(const uint8_t* d, uint16_t len) { for (unsigned i = 0; i < len; ++i) data(d[i]); }
void EpdBus::fillPlane(uint8_t c, uint8_t value, uint16_t h, uint16_t wb) {
  cmd(c);
  for (unsigned i = 0; i < h * wb; ++i) data(value);
}
void EpdBus::reset(uint16_t) { busy = false; }
void EpdBus::waitBusy(const char*) {
  // A level-only poll can miss a delayed assertion. Keep the activation
  // outstanding so subsequent RAM writes/power-off commands fail the test.
  if (delayedActivationBusy && (sequence == 0xCC || sequence == 0xC0)) return;
  busy = false;
}
void EpdBus::waitRefreshComplete(const char*) { busy = false; }
}

int main() {
  using namespace freeink;
  EpdBus bus;
  auto& panel = ssd1677Driver();  // Exercises the actual M4 config selection.
  auto gray = [&] {
    panel.copyGrayscaleLsb(bus, oldPage.data());
    panel.copyGrayscaleMsb(bus, oldPage.data());
    panel.displayGray(bus, oldPage.data(), false, lut_m4_direct_pulse.data(), true, true);
    panel.cleanupGrayscaleBuffers(bus, oldPage.data());
  };
  auto checkExit = [&] {
    assert(paints.size() == 2);
    assert(paints[0].sequence == 0xFC && paints[0].bw == white && paints[0].red == black);
    assert(paints[1].sequence == 0xFC && paints[1].bw == menu && paints[1].red == white);
    assert(!busy);
  };

  gray();
  paints.clear();
  gray();
  assert(paints.size() == 1 && paints[0].sequence == 0xCC);  // No extra white on gray page turns.
  paints.clear();
  panel.display(bus, menu.data(), oldPage.data(), RefreshMode::Fast, false);
  checkExit();  // RAM cleanup must not lose physical-gray state or retain old shadow.
  paints.clear();
  panel.display(bus, menu.data(), menu.data(), RefreshMode::Fast, false);
  assert(paints.size() == 1 && paints[0].sequence == 0xFC);  // White clear is one-shot.

  gray();
  paints.clear();
  panel.displayWindow(bus, menu.data(), oldPage.data(), 0, 0, 8, 1, false);
  checkExit();  // Partial menu requests repaint the whole composed frame after white.

  gray();
  paints.clear();
  assert(panel.displayStart(bus, menu.data(), oldPage.data(), RefreshMode::Fast, false));
  assert(busy);
  panel.displayFinish(bus, menu.data());
  checkExit();

  gray();
  panel.flashToWhite(bus);
  bus.waitRefreshComplete();
  paints.clear();
  panel.display(bus, menu.data(), nullptr, RefreshMode::Half, false);
  assert(paints.size() == 1);  // Explicit whitening consumed the pending gray exit.

  auto checkClock = [&] {
    assert(paints.size() == 1 && paints[0].sequence == 0xCC);
    assert(std::equal(paints[0].lut.begin(), paints[0].lut.end(), lut_m4_aa_direct));
    unsigned frames = 0;
    for (unsigned group = 0; group < 10; ++group) {
      for (unsigned phase = 0; phase < 4; ++phase) {
        frames += paints[0].lut[50 + group * 5 + phase] * (paints[0].lut[54 + group * 5] + 1);
      }
    }
    assert(frames == 60);  // Retain the complete clean/paint tail, not the 24-frame reading pulse.
    for (unsigned i = 0; i < menu.size(); ++i) {
      assert(paints[0].bw[i] == static_cast<uint8_t>(~menu[i]));
      assert(paints[0].red[i] == static_cast<uint8_t>(~menu[i]));
    }
    assert(bw == menu && red == menu);  // Binary baseline for minute tick and unlock.
    assert(!busy && !analogOn);  // Also required with turnOff=false (reading fading fix off).
    assert(activations.size() >= 2 && activations.size() <= 3);
    if (activations.size() == 3) assert(activations[0] == 0xC0);
    assert(activations[activations.size() - 2] == 0xCC);
    assert(activations.back() == 0x03);  // No pre-white, second paint or duplicate shutdown.
    const auto count = activations.size();
    const auto sleeps = sleepCommands;
    panel.deepSleep(bus);
    assert(activations.size() == count && sleepCommands == sleeps + 1);
    activations.clear();
  };

  gray();
  paints.clear();
  activations.clear();
  delayedActivationBusy = true;
  assert(panel.requestSleepClean());
  panel.display(bus, menu.data(), oldPage.data(), RefreshMode::Fast, false);
  checkClock();
  paints.clear();
  assert(panel.requestSleepClean());
  panel.display(bus, menu.data(), oldPage.data(), RefreshMode::Fast, false);
  checkClock();  // Also from B/W home, without a preceding gray page.

  panel.begin(bus);
  paints.clear();
  activations.clear();
  assert(panel.requestSleepClean());
  // Emulate the facade's inversion-dirty HALF promotion. The explicit clean
  // must own the waveform, avoiding HALF/FULL even after initialization.
  panel.display(bus, menu.data(), oldPage.data(), RefreshMode::Half, false);
  checkClock();

  paints.clear();
  assert(panel.requestSleepClean());
  assert(!panel.displayStart(bus, menu.data(), oldPage.data(), RefreshMode::Fast, true));
  checkClock();  // Completed inline and powered down; no false pending refresh.
  paints.clear();
  panel.display(bus, menu.data(), menu.data(), RefreshMode::Fast, false);
  assert(paints.size() == 1 && paints[0].sequence == 0xFC);  // No stale gray-exit request.

  panel.deepSleep(bus);
  panel.begin(bus);
  panel.skipInitialResync();  // Timer wake restores the saved binary frame before a minute update.
  paints.clear();
  activations.clear();
  panel.displayWindow(bus, menu.data(), menu.data(), 0, 0, 16, 2, false);
  assert(paints.size() == 1 && paints[0].sequence == 0xFC);
  panel.deepSleep(bus);
  assert(!busy && !analogOn && activations.back() == 0x03);
  delayedActivationBusy = false;

  auto otherConfig = ssd1677DefaultConfig();
  Ssd1677Driver other(otherConfig);
  assert(!other.requestSleepClean());
  other.displayGray(bus, oldPage.data(), false, lut_m4_direct_pulse.data(), true, true);
  other.cleanupGrayscaleBuffers(bus, oldPage.data());
  paints.clear();
  other.display(bus, menu.data(), oldPage.data(), RefreshMode::Fast, false);
  assert(paints.size() == 1);  // Other SSD1677 boards keep their existing behavior.
  std::puts("PASS: gray-to-UI cleanup; sleep full-E endpoints, completion, power-off, no repeated paint; board isolation");
}
