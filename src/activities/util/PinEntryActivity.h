#pragma once

#include <I18n.h>

#include <cstdint>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

struct Rect;

// 4-digit lock-screen PIN pad with two entry points:
// - Mode::Set, launched from SettingsActivity for result: the new code is
//   entered twice; on success the caller receives PinResult{verified=true}
//   and owns saving the enable toggle.
// - Mode::Unlock, installed by the boot wake gate in main.cpp via
//   replaceActivityWith(): verifies against the stored PIN and then runs the
//   deferred wake navigation (home or reader) itself — at that point the lock
//   IS the root activity, so there is no parent to return a result to.
class PinEntryActivity : public Activity {
 public:
  enum class Mode : uint8_t { Unlock, Set };

  // Where the unlock should land once the PIN checks out; mirrors the wake
  // routing decision that main.cpp would have taken without the gate.
  struct WakeTarget {
    bool toReader = false;
    bool allowFastRefresh = false;
  };

  // Default-argument form of WakeTarget trips a GCC default-arg/nested-class
  // quirk (bug 88165), so the no-target case is a delegating overload.
  PinEntryActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, Mode mode, WakeTarget target);
  PinEntryActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, Mode mode)
      : PinEntryActivity(renderer, mappedInput, mode, WakeTarget()) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&& lock) override;
  // The lock screen must not be escaped by system gestures: a swipe-up that
  // goHome()s from the PIN pad bypasses the lock entirely.
  bool blocksSystemGestures() const override { return true; }

 private:
  static constexpr int PIN_LENGTH = 4;
  static constexpr int KEY_COUNT = 12;  // 3x4 grid, cell 9 (bottom-left) is blank

  Rect keyRect(int index) const;
  void pressKey(int index);
  void enterDigit(int digit);
  void backspace();
  void moveCursor(int delta);
  void submitEntry();
  void applyUnlockSuccess();
  StrId promptId() const;
  void showMessage(StrId id);

  Mode mode;
  WakeTarget target;
  // Set-mode double entry: first pass captures the code, second confirms it.
  bool awaitingConfirm = false;
  uint16_t firstEntry = 0;
  uint8_t digits[PIN_LENGTH] = {};
  uint8_t digitCount = 0;
  int cursorKey = 0;  // button-nav cursor on the keypad
  bool hasMessage = false;
  StrId message = StrId::STR_CROSSPOINT;  // only read while hasMessage
  bool finished = false;                  // navigation/result already dispatched
  ButtonNavigator buttonNavigator;

  // Layout, resolved once in onEnter() so loop() hit-testing matches render().
  int titleTop = 0;
  int messageTop = 0;
  int boxTop = 0;
  int boxSide = 0;
  int keypadX = 0;
  int keypadY = 0;
  int keyW = 0;
  int keyH = 0;
  int keyGap = 10;
};
