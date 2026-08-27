#include "ReaderTapZonesActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <iterator>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr StrId kActionIds[] = {StrId::STR_NONE_OPT, StrId::STR_PREV_PAGE, StrId::STR_NEXT_PAGE, StrId::STR_READER_MENU};
static_assert(std::size(kActionIds) == CrossPointSettings::TAP_ACTION_COUNT);
}  // namespace

const char* ReaderTapZonesActivity::actionLabel(const uint8_t action) {
  return action < std::size(kActionIds) ? I18N.get(kActionIds[action]) : I18N.get(StrId::STR_NONE_OPT);
}

void ReaderTapZonesActivity::openPicker() {
  optionPopup_.show(StrId::STR_READER_TAP_ZONES, kActionIds, static_cast<int>(std::size(kActionIds)),
                    SETTINGS.readerTapActionAt(static_cast<uint8_t>(selected_)), [this](int idx) {
                      SETTINGS.setReaderTapActionAt(static_cast<uint8_t>(selected_), static_cast<uint8_t>(idx));
                      SETTINGS.saveToFile();
                    });
  requestUpdate();
}

void ReaderTapZonesActivity::loop() {
  if (optionPopup_.handleInput(mappedInput, [this] { requestUpdate(); })) return;

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    openPicker();
    return;
  }

  const int col = selected_ % 3;
  const int row = selected_ / 3;
  int nextCol = col;
  int nextRow = row;
  if (mappedInput.wasPressed(MappedInputManager::Button::Left)) nextCol = col > 0 ? col - 1 : 2;
  if (mappedInput.wasPressed(MappedInputManager::Button::Right)) nextCol = col < 2 ? col + 1 : 0;
  if (mappedInput.wasPressed(MappedInputManager::Button::Up) ||
      mappedInput.wasPressed(MappedInputManager::Button::PageBack)) {
    nextRow = row > 0 ? row - 1 : 2;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Down) ||
      mappedInput.wasPressed(MappedInputManager::Button::PageForward)) {
    nextRow = row < 2 ? row + 1 : 0;
  }
  const int next = nextRow * 3 + nextCol;
  if (next != selected_) {
    selected_ = next;
    requestUpdate();
    return;
  }

  int x = 0;
  int y = 0;
  if (mappedInput.wasScreenTapped(x, y)) {
    const auto& metrics = UITheme::getInstance().getMetrics();
    const int top = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
    const int bottom = renderer.getScreenHeight() - metrics.buttonHintsHeight - metrics.verticalSpacing;
    if (y >= top && y < bottom) {
      const int cell = CrossPointSettings::readerTapZoneIndex(x, y - top, renderer.getScreenWidth(), bottom - top);
      if (cell == selected_) {
        openPicker();
      } else {
        selected_ = cell;
        requestUpdate();
      }
    }
  }
}

void ReaderTapZonesActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, renderer.getScreenWidth(), metrics.headerHeight},
                 tr(STR_READER_TAP_ZONES));

  const int gridTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int gridBottom = renderer.getScreenHeight() - metrics.buttonHintsHeight - metrics.verticalSpacing;
  const int gridH = gridBottom - gridTop;
  const int gridW = renderer.getScreenWidth();

  for (int i = 0; i < CrossPointSettings::READER_TAP_ZONE_COUNT; ++i) {
    const int col = i % 3;
    const int row = i / 3;
    const int x = col * gridW / 3;
    const int y = gridTop + row * gridH / 3;
    const int cellW = (col + 1) * gridW / 3 - x;
    const int cellH = gridTop + (row + 1) * gridH / 3 - y;
    const bool selected = i == selected_;
    const char* label = actionLabel(SETTINGS.readerTapActionAt(static_cast<uint8_t>(i)));
    const int textH = renderer.getTextHeight(UI_10_FONT_ID);
    const int textW = renderer.getTextWidth(UI_10_FONT_ID, label);
    const int tx = x + (cellW - textW) / 2;
    const int ty = y + (cellH - textH) / 2;
    if (selected) {
      renderer.fillRect(x + 2, y + 2, cellW - 4, cellH - 4, true);
      renderer.drawText(UI_10_FONT_ID, tx, ty, label, false);
    } else {
      renderer.drawRect(x + 2, y + 2, cellW - 4, cellH - 4, true);
      renderer.drawText(UI_10_FONT_ID, tx, ty, label, true);
    }
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  if (optionPopup_.processRender(renderer, mappedInput)) return;
  renderer.displayBuffer();
}
