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
struct Paint { std::array<uint8_t, 4> xRange; std::array<uint8_t, 4> yRange; uint8_t sequence; Frame bw; Frame red; Lut lut; uint8_t control1; uint8_t temperature; };
Lut loadedLut{};
std::array<uint8_t, 4> xRange{}, yRange{};
unsigned resets = 0;
bool delayedActivationBusy = false;
Frame bw{}, red{};
std::vector<Paint> paints;
std::vector<uint8_t> activations;
bool analogOn = false;
bool analogAfterActivation = false;
unsigned sleepCommands = 0;
unsigned lutWrites = 0;
uint8_t control1 = 0, temperature = 0;
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
  if (c == 0x32) ++lutWrites;
  if (c == 0x12) { assert(!analogOn); ++resets; }
  if (c == 0x20) {
    busy = true;
    activations.push_back(sequence);
    if (sequence & 0x40) analogOn = true;
    analogAfterActivation = (sequence & 0x02) == 0 && analogOn;
    if (sequence & 0x04) paints.push_back({xRange, yRange, sequence, bw, red, loadedLut, control1, temperature});
  }
  if (c == 0x10) {
    assert(!analogOn);
    ++sleepCommands;
  }
}
void EpdBus::data(uint8_t d) {
  assert(!busy);
  if (command == 0x44 && offset < 4) xRange[offset++] = d;
  if (command == 0x45 && offset < 4) yRange[offset++] = d;
  if (command == 0x22) sequence = d;
  if (command == 0x21 && offset++ == 0) control1 = d;
  if (command == 0x1A && offset++ == 0) temperature = d;
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
void EpdBus::reset(uint16_t) { busy = false; analogOn = false; analogAfterActivation = false; }
void EpdBus::waitBusy(const char*) {
  // A level-only poll can miss a delayed assertion. Keep the activation
  // outstanding so subsequent RAM writes/power-off commands fail the test.
  if (delayedActivationBusy && (sequence == 0xCC || sequence == 0xCF || sequence == 0xC0 ||
                                sequence == 0xD7 || sequence == 0x83)) return;
  busy = false;
  analogOn = analogAfterActivation;
}
void EpdBus::waitRefreshComplete(const char*) {
  busy = false;
  analogOn = analogAfterActivation;
}
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
    assert(paints.size() == 1);
    assert(paints[0].sequence == 0xD7 && paints[0].bw == menu && paints[0].red == menu);
    assert(!busy && !analogOn);
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
  assert(paints.size() == 1 && paints[0].sequence == 0xFF);  // Gray-exit clean is one-shot.

  gray();
  paints.clear();
  panel.displayWindow(bus, menu.data(), oldPage.data(), 0, 0, 8, 1, false);
  checkExit();  // Partial menu requests clean the whole composed frame.

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
    assert(paints.size() == 1 && paints[0].sequence == 0xD7);
    assert(paints[0].control1 == 0x40);  // OTP clean bypasses the previous gray RAM class.
    assert(paints[0].temperature == 0x50);  // M4 batch-2 HALF setting.
    assert(lutWrites == 0);  // No custom gray table or second gray paint on clock entry.
    assert(paints[0].bw == menu && paints[0].red == menu);
    assert(bw == menu && red == menu);  // Binary baseline for minute tick and unlock.
    assert(!busy && !analogOn);  // Also required with turnOff=false (reading fading fix off).
    assert(resets == 1);  // Previous custom voltage/register state cannot survive.
    assert(activations.back() == 0xD7);
    assert(activations.size() <= 2);
    if (activations.size() == 2) assert(activations.front() == 0x83);
    // Shutdown is part of the paint, before BUSY completes and RAM is restored.
    const auto count = activations.size();
    const auto sleeps = sleepCommands;
    panel.deepSleep(bus);
    assert(activations.size() == count && sleepCommands == sleeps + 1);
    activations.clear();
  };
  auto requestClock = [&] {
    lutWrites = 0;
    resets = 0;
    assert(panel.requestSleepClean());
  };

  gray();
  paints.clear();
  activations.clear();
  delayedActivationBusy = true;
  requestClock();
  panel.display(bus, menu.data(), oldPage.data(), RefreshMode::Fast, false);
  checkClock();
  paints.clear();
  requestClock();
  panel.display(bus, menu.data(), oldPage.data(), RefreshMode::Fast, false);
  checkClock();  // Also from B/W home, without a preceding gray page.

  panel.begin(bus);
  paints.clear();
  activations.clear();
  requestClock();
  // Emulate the facade's inversion-dirty HALF promotion. The explicit clean
  // must own one HALF waveform, avoiding stacked gray-exit/FULL activations.
  panel.display(bus, menu.data(), oldPage.data(), RefreshMode::Half, false);
  checkClock();

  paints.clear();
  requestClock();
  assert(!panel.displayStart(bus, menu.data(), oldPage.data(), RefreshMode::Fast, true));
  checkClock();  // Completed inline and powered down; no false pending refresh.
  paints.clear();
  panel.display(bus, menu.data(), menu.data(), RefreshMode::Fast, false);
  assert(paints.size() == 1 && paints[0].sequence == 0xFF);  // No stale gray-exit request.

  panel.deepSleep(bus);
  panel.begin(bus);
  panel.skipInitialResync();  // Timer wake restores the saved binary frame before a minute update.
  paints.clear();
  activations.clear();
  panel.displayWindow(bus, menu.data(), menu.data(), 0, 0, 16, 2, false);
  assert(paints.size() == 1 && paints[0].sequence == 0xFF);
  panel.deepSleep(bus);
  assert(!busy && !analogOn && activations.back() == 0xFF);

  // Direct facade requests turnOff=true even when the reading fading fix is off.
  panel.begin(bus);
  panel.copyGrayscaleLsb(bus, oldPage.data());
  panel.copyGrayscaleMsb(bus, oldPage.data());
  paints.clear();
  activations.clear();
  panel.displayGray(bus, oldPage.data(), true, lut_m4_direct_pulse.data(), true, true);
  assert(paints.size() == 1 && paints[0].sequence == 0xCF);
  assert(paints[0].lut == lut_m4_direct_pulse && paints[0].bw == oldPage && paints[0].red == oldPage);
  assert(!busy && !analogOn);  // Drive must already be off BEFORE B/W cleanup.
  assert(activations == std::vector<uint8_t>({0xC0, 0xCF}));
  panel.cleanupGrayscaleBuffers(bus, menu.data());
  assert(!analogOn && red == menu);
  panel.deepSleep(bus);
  assert(activations == std::vector<uint8_t>({0xC0, 0xCF}));  // No second activation on sleep.

  // The last 1-row strip/window must never delimit the following gray page.
  panel.writeGrayscalePlaneStrip(bus, GrayPlane::Lsb, oldPage.data(), 1, 1);
  panel.writeGrayscalePlaneStrip(bus, GrayPlane::Msb, oldPage.data(), 1, 1);
  paints.clear();
  panel.displayGray(bus, oldPage.data(), true, lut_m4_direct_pulse.data(), true, true);
  assert(paints.size() == 1);
  assert((paints[0].xRange == std::array<uint8_t, 4>{0, 0, 15, 0}));
  assert((paints[0].yRange == std::array<uint8_t, 4>{1, 0, 0, 0}));

  // Reading white flash with no power-off request still uses its accepted CC waveform.
  panel.begin(bus);
  paints.clear();
  panel.displayGray(bus, oldPage.data(), false, lut_m4_white_pulse.data(), true, true);
  assert(paints.size() == 1 && paints[0].sequence == 0xCC && paints[0].lut == lut_m4_white_pulse);
  delayedActivationBusy = false;

  panel.deepSleep(bus);

  auto otherConfig = ssd1677DefaultConfig();
  Ssd1677Driver other(otherConfig);
  assert(!other.requestSleepClean());
  other.displayGray(bus, oldPage.data(), false, lut_m4_direct_pulse.data(), true, true);
  other.cleanupGrayscaleBuffers(bus, oldPage.data());
  paints.clear();
  other.display(bus, menu.data(), oldPage.data(), RefreshMode::Fast, false);
  assert(paints.size() == 1);  // Other SSD1677 boards keep their existing behavior.
  paints.clear();
  activations.clear();
  other.displayGray(bus, oldPage.data(), true, lut_m4_direct_pulse.data(), true, true);
  assert(paints.size() == 1 && paints[0].sequence == 0xCC);
  assert(activations == std::vector<uint8_t>({0xCC, 0x03}) && !analogOn);
  // Overlay CC keeps its analog rails on; software must not skip sleep shutdown.
  activations.clear();
  other.displayGray(bus, oldPage.data(), false, nullptr, false, true);
  assert(analogOn);
  other.deepSleep(bus);
  assert(!analogOn && activations.back() == 0x03);

  std::puts("PASS: clean reset, one D7 paint, BW power-off, full gray window, Direct CF and other-board isolation");
}
