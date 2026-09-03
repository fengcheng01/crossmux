#include "CountdownActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <SloppyDigits.h>

#include <algorithm>
#include <limits>
#include <memory>

#include "CountdownStore.h"
#include "MappedInputManager.h"
#include "activities/apps/reading-stats/ReadingDateSelectionActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/OptionPopup.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/TimeUtils.h"

namespace {

constexpr size_t kAddRowSentinel = std::numeric_limits<size_t>::max();

// Statutory holidays first, then common personal events. Custom is last and
// opens the Latin keyboard. Count stays at OptionPopup::MAX_OPTIONS (16).
constexpr StrId kLabelPresets[] = {
    StrId::STR_COUNTDOWN_LABEL_NEW_YEAR,     StrId::STR_COUNTDOWN_LABEL_SPRING_FESTIVAL,
    StrId::STR_COUNTDOWN_LABEL_QINGMING,     StrId::STR_COUNTDOWN_LABEL_LABOR_DAY,
    StrId::STR_COUNTDOWN_LABEL_DRAGON_BOAT,  StrId::STR_COUNTDOWN_LABEL_MID_AUTUMN,
    StrId::STR_COUNTDOWN_LABEL_NATIONAL_DAY, StrId::STR_COUNTDOWN_LABEL_BIRTHDAY,
    StrId::STR_COUNTDOWN_LABEL_ANNIVERSARY,  StrId::STR_COUNTDOWN_LABEL_WEDDING,
    StrId::STR_COUNTDOWN_LABEL_EXAM,         StrId::STR_COUNTDOWN_LABEL_GAOKAO,
    StrId::STR_COUNTDOWN_LABEL_KAOYAN,       StrId::STR_COUNTDOWN_LABEL_GRADUATION,
    StrId::STR_COUNTDOWN_LABEL_TRIP,         StrId::STR_COUNTDOWN_LABEL_CUSTOM,
};
constexpr int kLabelPresetCount = static_cast<int>(sizeof(kLabelPresets) / sizeof(kLabelPresets[0]));
constexpr int kLabelCustomIndex = kLabelPresetCount - 1;
static_assert(kLabelPresetCount <= 16, "OptionPopup has no scroll on non-INX themes");

int64_t todayOrdinal() {
  const uint32_t now = TimeUtils::getCurrentValidTimestamp();
  std::tm civil{};
  if (now == 0 || !TimeUtils::getLocalDateTime(now, civil)) return 0;
  return static_cast<int64_t>(TimeUtils::getDayOrdinalForDate(civil.tm_year + 1900, civil.tm_mon + 1, civil.tm_mday));
}

}  // namespace

void CountdownActivity::onEnter() {
  Activity::onEnter();
  // Fixed seed: rows must render identically on every repaint (e-ink).
  heroStyle_ = makeUniqueNoThrow<sloppy::Style>();
  heroSeeds_ = makeUniqueNoThrow<sloppy::Seeds>();
  selectedRow_ = 0;
  // Without this the first paint never happens: the screen keeps showing the
  // apps menu (activity switched underneath an unchanged framebuffer), and
  // hitRow's geometry members stay at their defaults so taps die.
  requestUpdate();
}

void CountdownActivity::refreshRows() { requestUpdate(); }

void CountdownActivity::showActionPopup(const size_t entryIndex) {
  const char* eventActions[3] = {tr(STR_COUNTDOWN_SET_DATE), tr(STR_COUNTDOWN_SET_LABEL), tr(STR_COUNTDOWN_CLEAR)};
  popupEntry_ = entryIndex;
  popupTarget_ = PopupTarget::Event;
  actionChoice_ = -1;
  // Title = the event's own label; the store caps labels at 32 chars.
  actionPopup_.show(COUNTDOWN_STORE.at(entryIndex).label.c_str(), eventActions, 3, 0,
                    [this](int index) { actionChoice_ = index; });
  requestUpdate();
}

void CountdownActivity::showAddFlow() {
  // Add flow: date first (defaults to today), then the preset-label picker.
  openDateSelection(kAddRowSentinel);
}

void CountdownActivity::runEventAction(const size_t entryIndex, const int action) {
  switch (action) {
    case 0:
      openDateSelection(entryIndex);
      break;
    case 1:
      openLabelPicker(entryIndex);
      break;
    case 2:
      COUNTDOWN_STORE.remove(entryIndex);
      COUNTDOWN_STORE.saveToFile();
      selectedRow_ = 0;
      refreshRows();
      break;
    default:
      break;
  }
}

void CountdownActivity::openDateSelection(const size_t entryIndex) {
  // entryIndex == kAddRowSentinel: the add flow — the date result creates the
  // entry, then the preset-label picker chains off it.
  const bool isNew = entryIndex == kAddRowSentinel;
  const uint32_t initial =
      isNew ? static_cast<uint32_t>(todayOrdinal()) : COUNTDOWN_STORE.at(entryIndex).targetDayOrdinal;
  startActivityForResultWith<ReadingDateSelectionActivity>(
      [this, entryIndex, isNew](const ActivityResult& result) {
        if (result.isCancelled) return;
        if (const auto* page = std::get_if<PageResult>(&result.data)) {
          size_t targetIndex = entryIndex;
          if (isNew) {
            COUNTDOWN_STORE.add(page->page, "");
            targetIndex = COUNTDOWN_STORE.count() - 1;
          } else {
            COUNTDOWN_STORE.update(entryIndex, page->page, COUNTDOWN_STORE.at(entryIndex).label);
          }
          COUNTDOWN_STORE.saveToFile();
          openLabelPicker(targetIndex);
          return;
        }
      },
      initial);
}

void CountdownActivity::openLabelPicker(const size_t entryIndex) {
  popupEntry_ = entryIndex;
  popupTarget_ = PopupTarget::Label;
  actionChoice_ = -1;
  actionPopup_.show(StrId::STR_COUNTDOWN_SET_LABEL, kLabelPresets, kLabelPresetCount, 0,
                    [this](int index) { actionChoice_ = index; });
  requestUpdate();
}

void CountdownActivity::applyLabelChoice(const size_t entryIndex, const int choice) {
  if (choice < 0 || entryIndex >= COUNTDOWN_STORE.count()) return;
  if (choice == kLabelCustomIndex) {
    openLabelEditor(entryIndex);
    return;
  }
  COUNTDOWN_STORE.update(entryIndex, COUNTDOWN_STORE.at(entryIndex).targetDayOrdinal, I18N.get(kLabelPresets[choice]));
  COUNTDOWN_STORE.saveToFile();
  refreshRows();
}

void CountdownActivity::openLabelEditor(const size_t entryIndex) {
  startActivityForResultWith<KeyboardEntryActivity>(
      [this, entryIndex](const ActivityResult& result) {
        if (result.isCancelled) return;
        const auto& kb = std::get<KeyboardResult>(result.data);
        if (entryIndex < COUNTDOWN_STORE.count()) {
          COUNTDOWN_STORE.update(entryIndex, COUNTDOWN_STORE.at(entryIndex).targetDayOrdinal, kb.text);
          COUNTDOWN_STORE.saveToFile();
        }
        refreshRows();
      },
      tr(STR_COUNTDOWN_SET_LABEL), COUNTDOWN_STORE.at(entryIndex).label, 32, InputType::Text);
}

int CountdownActivity::hitRow(const int y) const {
  if (y < listTop_) return -1;
  for (int i = 0; i < rowsDrawn_; ++i) {
    const int top = listTop_ + i * ROW_H;
    if (y >= top && y < top + ROW_H) return i;
  }
  if (hasAddRow_) {
    const int top = listTop_ + rowsDrawn_ * ROW_H;
    if (y >= top && y < top + ADD_ROW_H) return rowsDrawn_;  // add row encoded as index == rowsDrawn_
  }
  return -1;
}

void CountdownActivity::loop() {
  if (actionPopup_.isActive()) {
    actionPopup_.handleInput(mappedInput, [this] { requestUpdate(); });
    if (actionPopup_.isActive()) return;
    const int choice = actionChoice_;
    actionChoice_ = -1;
    if (choice >= 0 && popupTarget_ == PopupTarget::Event) runEventAction(popupEntry_, choice);
    if (choice >= 0 && popupTarget_ == PopupTarget::Label) applyLabelChoice(popupEntry_, choice);
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    activityManager.goToApps();
    return;
  }

  const size_t eventCount = COUNTDOWN_STORE.count();
  const int selectableRows = static_cast<int>(eventCount) + (COUNTDOWN_STORE.full() ? 0 : 1);

  int touchX = 0;
  int touchY = 0;
  const bool tapped = mappedInput.wasScreenTapped(touchX, touchY);
  const bool confirmed = mappedInput.wasReleased(MappedInputManager::Button::Confirm);
  const bool up = mappedInput.wasReleased(MappedInputManager::Button::Up);
  const bool down = mappedInput.wasReleased(MappedInputManager::Button::Down);
  if (up || down) {
    selectedRow_ = std::clamp(selectedRow_ + (down ? 1 : -1), 0, std::max(0, selectableRows - 1));
    requestUpdate();
    return;
  }

  if (tapped || confirmed) {
    const int row = tapped ? hitRow(touchY) : std::min(selectedRow_, selectableRows - 1);
    if (row < 0) return;
    if (row < static_cast<int>(eventCount)) {
      showActionPopup(static_cast<size_t>(row));
    } else if (row == static_cast<int>(eventCount) && !COUNTDOWN_STORE.full()) {
      showAddFlow();
    }
    return;
  }
}

void CountdownActivity::render(RenderLock&&) {
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const Rect safeArea = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  GUI.drawHeader(renderer, Rect{safeArea.x, safeArea.y + metrics.topPadding, safeArea.width, metrics.headerHeight},
                 tr(STR_COUNTDOWN_TITLE));
  const int contentTop = metrics.topPadding + metrics.headerHeight + 8;

  const int64_t today = todayOrdinal();
  const auto& store = COUNTDOWN_STORE;

  int y = contentTop;
  // hitRow() must anchor on the same top the rows are painted from; leaving it
  // at 0 shifted the whole touch map down by the header height.
  listTop_ = contentTop;
  int drawn = 0;
  for (size_t i = 0; i < store.count(); ++i) {
    const auto& entry = store.at(i);
    const int64_t remaining = static_cast<int64_t>(entry.targetDayOrdinal) - today;
    const bool expired = remaining < 0;
    const int days = std::max(0, static_cast<int>(remaining));
    const bool selected = static_cast<int>(i) == selectedRow_;

    if (selected) renderer.fillRect(12, y + 2, pageWidth - 24, ROW_H - 4);

    const std::string label = entry.label.empty() ? std::string(tr(STR_COUNTDOWN_TITLE)) : entry.label;
    const bool labelOnInk = selected;
    renderer.drawText(UI_12_FONT_ID, 20, y + 12, label.c_str(), !labelOnInk, EpdFontFamily::BOLD);

    char daysBuf[32] = {};
    if (expired) {
      std::snprintf(daysBuf, sizeof(daysBuf), "%s", tr(STR_COUNTDOWN_PAST));
    } else {
      std::snprintf(daysBuf, sizeof(daysBuf), tr(STR_COUNTDOWN_DAYS_FMT), days);
    }
    const int daysW = renderer.getTextWidth(UI_12_FONT_ID, daysBuf, EpdFontFamily::BOLD);
    renderer.drawText(UI_12_FONT_ID, pageWidth - 20 - daysW, y + 12, daysBuf, !labelOnInk, EpdFontFamily::BOLD);

    char dateBuf[32] = {};
    int y2 = 0;
    unsigned m2 = 0;
    unsigned d2 = 0;
    if (TimeUtils::getDateFromDayOrdinal(entry.targetDayOrdinal, y2, m2, d2)) {
      std::snprintf(dateBuf, sizeof(dateBuf), tr(STR_COUNTDOWN_TARGET_FMT), y2, m2, d2);
      renderer.drawText(SMALL_FONT_ID, 20, y + 52, dateBuf, !labelOnInk);
    }

    renderer.drawLine(20, y + ROW_H - 8, pageWidth - 20, y + ROW_H - 8, !labelOnInk);
    y += ROW_H;
    ++drawn;
  }

  rowsDrawn_ = drawn;
  hasAddRow_ = !store.full();
  if (hasAddRow_) {
    const char* add = tr(STR_COUNTDOWN_ADD);
    const int addW = renderer.getTextWidth(UI_12_FONT_ID, add, EpdFontFamily::BOLD);
    const int textY = y + (ADD_ROW_H - renderer.getLineHeight(UI_12_FONT_ID)) / 2;
    renderer.drawText(UI_12_FONT_ID, (pageWidth - addW) / 2, textY, add, true, EpdFontFamily::BOLD);
  }

  if (store.count() == 0) {
    const char* hint = tr(STR_COUNTDOWN_EMPTY);
    const int hintW = renderer.getTextWidth(UI_12_FONT_ID, hint);
    renderer.drawText(UI_12_FONT_ID, (pageWidth - hintW) / 2, contentTop + 60, hint, true, EpdFontFamily::BOLD);
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  // The actions popup must be drawn over the list — an undrawn popup still
  // consumes every tap, which reads as a frozen page.
  if (actionPopup_.isActive()) {
    actionPopup_.processRender(renderer, mappedInput);
  }

  renderer.displayBuffer();
}
