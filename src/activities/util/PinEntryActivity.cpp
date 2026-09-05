#include "PinEntryActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "activities/ActivityManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

PinEntryActivity::PinEntryActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const Mode mode,
                                   const WakeTarget target)
    : Activity("PinEntry", renderer, mappedInput), mode(mode), target(target) {}

void PinEntryActivity::onEnter() {
  Activity::onEnter();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();

  // Resolve the layout once so loop()'s touch hit-testing and render() agree.
  titleTop = metrics.topPadding + 8;
  messageTop = titleTop + renderer.getLineHeight(UI_12_FONT_ID) + 10;
  boxSide = std::clamp(pageWidth / 12, 44, 84);
  boxTop = messageTop + renderer.getLineHeight(UI_10_FONT_ID) + 20;
  keypadY = boxTop + boxSide + 24;
  const int bottom = pageHeight - metrics.buttonHintsHeight - 14;
  keyH = std::clamp((bottom - keypadY - keyGap * 3) / 4, 34, 96);
  keyW = std::min((pageWidth - metrics.contentSidePadding * 2 - keyGap * 2) / 3, keyH * 8 / 5);
  keypadX = (pageWidth - (keyW * 3 + keyGap * 2)) / 2;

  // Nudge the block down into the leftover band so the pad is not glued to
  // the top bezel (device feedback: portrait M4 left a large empty footer).
  const int keypadBottom = keypadY + keyH * 4 + keyGap * 3;
  const int shift = std::clamp(std::max(0, bottom - keypadBottom) / 3, 16, 48);
  titleTop += shift;
  messageTop += shift;
  boxTop += shift;
  keypadY += shift;

  requestUpdate();
}

Rect PinEntryActivity::keyRect(const int index) const {
  const int row = index / 3;
  const int col = index % 3;
  return Rect{keypadX + col * (keyW + keyGap), keypadY + row * (keyH + keyGap), keyW, keyH};
}

StrId PinEntryActivity::promptId() const {
  if (mode == Mode::Unlock) return StrId::STR_ENTER_LOCK_PIN;
  return awaitingConfirm ? StrId::STR_CONFIRM_LOCK_PIN : StrId::STR_NEW_LOCK_PIN;
}

void PinEntryActivity::showMessage(const StrId id) {
  message = id;
  hasMessage = true;
  requestUpdate();
}

void PinEntryActivity::pressKey(const int index) {
  if (index < 0 || index >= KEY_COUNT || index == 9) return;  // 9 = blank cell
  if (index == 11) {
    backspace();
    return;
  }
  enterDigit(index == 10 ? 0 : index + 1);
}

void PinEntryActivity::enterDigit(const int digit) {
  if (digitCount >= PIN_LENGTH) return;
  digits[digitCount++] = static_cast<uint8_t>(digit);
  hasMessage = false;
  if (digitCount == PIN_LENGTH) {
    // Paint the fourth box before verify/navigation so the pad does not look
    // stuck on three dots while home/reader is loading.
    requestUpdateAndWait();
    submitEntry();
  } else {
    requestUpdate();
  }
}

void PinEntryActivity::backspace() {
  if (digitCount > 0) {
    digitCount--;
    hasMessage = false;
    requestUpdate();
  }
}

void PinEntryActivity::moveCursor(const int delta) {
  int next = cursorKey;
  if (next < 0) {
    next = delta >= 0 ? 0 : 11;
  } else {
    next = (next + delta + KEY_COUNT) % KEY_COUNT;
  }
  while (next == 9) {  // the blank cell can never hold the cursor
    next = (next + (delta >= 0 ? 1 : -1) + KEY_COUNT) % KEY_COUNT;
  }
  if (next != cursorKey) {
    cursorKey = next;
    requestUpdate();
  }
}

void PinEntryActivity::submitEntry() {
  const uint16_t value =
      static_cast<uint16_t>(static_cast<int>(digits[0]) * 1000 + digits[1] * 100 + digits[2] * 10 + digits[3]);

  if (mode == Mode::Unlock) {
    if (SETTINGS.lockScreenPinIsSet && value == SETTINGS.lockScreenPin) {
      applyUnlockSuccess();
      return;
    }
    digitCount = 0;
    showMessage(StrId::STR_PIN_WRONG);
    return;
  }

  if (!awaitingConfirm) {
    firstEntry = value;
    awaitingConfirm = true;
    digitCount = 0;
    requestUpdate();
    return;
  }
  if (value == firstEntry) {
    SETTINGS.lockScreenPin = firstEntry;
    SETTINGS.lockScreenPinIsSet = 1;
    SETTINGS.saveToFile();
    finished = true;
    setResult(ActivityResult{PinResult{true}});
    finish();
    return;
  }
  awaitingConfirm = false;
  digitCount = 0;
  showMessage(StrId::STR_PIN_MISMATCH);
}

void PinEntryActivity::applyUnlockSuccess() {
  finished = true;
  // PIN already painted with FAST; keep the home/reader frame on FAST so the
  // unlock path does not pay a FULL (black-flashing, slow) first paint.
  renderer.requestNextRefresh(HalDisplay::FAST_REFRESH);
  if (target.toReader) {
    // Same boot-loop guard the direct wake path runs before goToReader().
    const std::string path = APP_STATE.openEpubPath;
    APP_STATE.openEpubPath.clear();
    APP_STATE.readerActivityLoadCount++;
    APP_STATE.saveToFile();
    activityManager.goToReader(path, target.allowFastRefresh);
  } else {
    activityManager.goHome();
  }
}

void PinEntryActivity::loop() {
  if (finished) return;

  // Set mode is a child screen: Back cancels back to Settings. Unlock mode is
  // the root lock — Back is just backspace; the only way past it is the PIN.
  if (mode == Mode::Set && mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
    return;
  }

  for (int i = 0; i < KEY_COUNT; i++) {
    if (i == 9) continue;
    const Rect r = keyRect(i);
    if (mappedInput.wasTapInRect(r.x, r.y, r.width, r.height)) {
      cursorKey = i;
      pressKey(i);
      return;
    }
  }

  if (mode == Mode::Unlock && mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    backspace();
    return;
  }
  if (cursorKey >= 0 && mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    pressKey(cursorKey);
    return;
  }
  buttonNavigator.onRelease({MappedInputManager::Button::Down}, [this] { moveCursor(3); });
  buttonNavigator.onRelease({MappedInputManager::Button::Up}, [this] { moveCursor(-3); });
  buttonNavigator.onRelease({MappedInputManager::Button::Right}, [this] { moveCursor(1); });
  buttonNavigator.onRelease({MappedInputManager::Button::Left}, [this] { moveCursor(-1); });
}

void PinEntryActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int radius = metrics.optionPopupSelectionRadius + 2;

  renderer.drawCenteredText(UI_12_FONT_ID, titleTop, I18N.get(promptId()), true, EpdFontFamily::BOLD);
  if (hasMessage) {
    renderer.drawCenteredText(UI_10_FONT_ID, messageTop, I18N.get(message), true);
  }

  const int boxGap = boxSide / 3;
  const int boxesW = PIN_LENGTH * boxSide + (PIN_LENGTH - 1) * boxGap;
  int x = (renderer.getScreenWidth() - boxesW) / 2;
  for (int i = 0; i < PIN_LENGTH; i++) {
    renderer.drawRoundedRect(x, boxTop, boxSide, boxSide, 2, radius, true);
    if (i < digitCount) {
      const int dot = std::max(10, boxSide / 4);
      renderer.fillRoundedRect(x + (boxSide - dot) / 2, boxTop + (boxSide - dot) / 2, dot, dot, dot / 2, Color::Black);
    }
    x += boxSide + boxGap;
  }

  for (int i = 0; i < KEY_COUNT; i++) {
    if (i == 9) continue;
    const Rect r = keyRect(i);
    const char label[2] = {static_cast<char>(i < 9 ? '1' + i : (i == 10 ? '0' : '<')), '\0'};
    const bool active = cursorKey >= 0 && i == cursorKey;
    if (active) {
      renderer.fillRoundedRect(r.x, r.y, r.width, r.height, radius, Color::Black);
    } else {
      renderer.fillRoundedRect(r.x, r.y, r.width, r.height, radius, Color::White);
      renderer.drawRoundedRect(r.x, r.y, r.width, r.height, 1, radius, true);
    }
    const int textW = renderer.getTextWidth(UI_12_FONT_ID, label, EpdFontFamily::BOLD);
    const int textH = renderer.getLineHeight(UI_12_FONT_ID);
    renderer.drawText(UI_12_FONT_ID, r.x + (r.width - textW) / 2, r.y + (r.height - textH) / 2, label, !active,
                      EpdFontFamily::BOLD);
  }

  const char* backLabel = mode == Mode::Unlock ? tr(STR_DELETE) : tr(STR_BACK);
  const auto labels = mappedInput.mapLabels(backLabel, tr(STR_CONFIRM), tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
