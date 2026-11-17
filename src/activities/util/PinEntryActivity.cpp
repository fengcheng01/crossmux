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
  waitForPowerRelease = mappedInput.isPressed(MappedInputManager::Button::Power);
  frameValid = false;

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
  const int slack = std::max(0, bottom - keypadBottom);
  const int shift = slack <= 0 ? 0 : std::min(slack, std::clamp(slack / 3, 16, 48));
  titleTop += shift;
  messageTop += shift;
  boxTop += shift;
  keypadY += shift;

  requestUpdate();
}

void PinEntryActivity::requestUpdate(const bool immediate) {
  const RenderState state{promptId(), message, digitCount, cursorKey, hasMessage};
  taskENTER_CRITICAL(&renderStateMux);
  pendingRenderState = state;
  taskEXIT_CRITICAL(&renderStateMux);
  Activity::requestUpdate(immediate);
}

PinEntryActivity::RenderState PinEntryActivity::snapshotRenderState() {
  taskENTER_CRITICAL(&renderStateMux);
  const RenderState state = pendingRenderState;
  taskEXIT_CRITICAL(&renderStateMux);
  return state;
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
    // Verification and navigation should not wait for a full e-paper repaint.
    // A wrong PIN requests its own message repaint below.
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
  if (waitForPowerRelease) {
    if (!mappedInput.isPressed(MappedInputManager::Button::Power)) waitForPowerRelease = false;
    return;  // Consume the wake gesture's release, including its Confirm alias.
  }

  // Set mode is a child screen: Back cancels back to Settings. Unlock mode is
  // the root lock — Back is just backspace; the only way past it is the PIN.
  if (mode == Mode::Set && mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
    return;
  }

  int touchX = 0;
  int touchY = 0;
  const bool touchHeld = mappedInput.isScreenTouchHeld(touchX, touchY);
  if (touchKeyHeld) {
    if (!touchHeld || mappedInput.wasScreenTouchReleased()) touchKeyHeld = false;
    return;  // Consume this contact's release as well as its hold.
  }

  // Keypad presses need no list-selection hold delay; suppress their later tap.
  const bool keyDown = touchHeld && mappedInput.wasScreenTouchPressed(touchX, touchY);
  const bool keyTap = !touchHeld && mappedInput.wasScreenTapped(touchX, touchY);
  if (keyDown || keyTap) {
    for (int i = 0; i < KEY_COUNT; i++) {
      if (i == 9) continue;
      const Rect r = keyRect(i);
      if (touchX >= r.x && touchX < r.x + r.width && touchY >= r.y && touchY < r.y + r.height) {
        touchKeyHeld = keyDown;
        if (keyDown) mappedInput.suppressTouchContact();
        cursorKey = i;
        pressKey(i);
        return;
      }
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
  if (mappedInput.wasReleased(MappedInputManager::Button::Down)) moveCursor(3);
  if (mappedInput.wasReleased(MappedInputManager::Button::Up)) moveCursor(-3);
  if (mappedInput.wasReleased(MappedInputManager::Button::Right)) moveCursor(1);
  if (mappedInput.wasReleased(MappedInputManager::Button::Left)) moveCursor(-1);
}

void PinEntryActivity::render(RenderLock&&) {
  if (finished) return;
  const RenderState state = snapshotRenderState();
  const bool fullFrame = !frameValid;
  if (fullFrame) renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int radius = metrics.optionPopupSelectionRadius + 2;

  if (fullFrame || state.prompt != frameState.prompt) {
    renderer.fillRect(0, titleTop, renderer.getScreenWidth(), renderer.getLineHeight(UI_12_FONT_ID), false);
    renderer.drawCenteredText(UI_12_FONT_ID, titleTop, I18N.get(state.prompt), true, EpdFontFamily::BOLD);
  }
  if (fullFrame || state.hasMessage != frameState.hasMessage || state.message != frameState.message) {
    renderer.fillRect(0, messageTop, renderer.getScreenWidth(), renderer.getLineHeight(UI_10_FONT_ID), false);
    if (state.hasMessage) renderer.drawCenteredText(UI_10_FONT_ID, messageTop, I18N.get(state.message), true);
  }

  const int boxGap = boxSide / 3;
  const int boxesW = PIN_LENGTH * boxSide + (PIN_LENGTH - 1) * boxGap;
  int x = (renderer.getScreenWidth() - boxesW) / 2;
  for (int i = 0; i < PIN_LENGTH; i++) {
    if (!fullFrame && (i < state.digitCount) == (i < frameState.digitCount)) {
      x += boxSide + boxGap;
      continue;
    }
    renderer.fillRect(x, boxTop, boxSide, boxSide, false);
    renderer.drawRoundedRect(x, boxTop, boxSide, boxSide, 2, radius, true);
    if (i < state.digitCount) {
      const int dot = std::max(10, boxSide / 4);
      renderer.fillRoundedRect(x + (boxSide - dot) / 2, boxTop + (boxSide - dot) / 2, dot, dot, dot / 2, Color::Black);
    }
    x += boxSide + boxGap;
  }

  for (int i = 0; i < KEY_COUNT; i++) {
    if (i == 9) continue;
    if (!fullFrame && (state.cursorKey == frameState.cursorKey || (i != state.cursorKey && i != frameState.cursorKey)))
      continue;
    const Rect r = keyRect(i);
    const char label[2] = {static_cast<char>(i < 9 ? '1' + i : (i == 10 ? '0' : '<')), '\0'};
    const bool active = state.cursorKey >= 0 && i == state.cursorKey;
    renderer.fillRect(r.x, r.y, r.width, r.height, false);
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

  if (fullFrame) {
    const char* backLabel = mode == Mode::Unlock ? tr(STR_DELETE) : tr(STR_BACK);
    const auto labels = mappedInput.mapLabels(backLabel, tr(STR_CONFIRM), tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }
  // Retain the framebuffer even if a newer input makes this panel update obsolete.
  frameState = state;
  frameValid = true;
  if (finished) return;
  if (!(snapshotRenderState() == state)) {
    Activity::requestUpdate(true);
    return;
  }
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
