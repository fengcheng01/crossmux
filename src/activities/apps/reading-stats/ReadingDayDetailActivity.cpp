#include "ReadingDayDetailActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <string>

#include "AppMetricCard.h"
#include "ReadingStatsDetailActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/HeaderDateUtils.h"

namespace {
constexpr int SUMMARY_CARD_HEIGHT = 70;
constexpr int SUMMARY_GAP = 8;
constexpr int DAYPART_HEIGHT = 88;

const char* daypartLabel(const int part) {
  switch (part) {
    case 0:
      return tr(STR_DAYPART_MORNING);
    case 1:
      return tr(STR_DAYPART_NOON);
    case 2:
      return tr(STR_DAYPART_EVENING);
    default:
      return tr(STR_DAYPART_NIGHT);
  }
}

constexpr const char* kDaypartHours[ReadingStatsAnalytics::DAYPART_COUNT] = {"05-11", "11-17", "17-21", "21-05"};

std::string getBookTitle(const ReadingBookStats& book) { return book.title.empty() ? book.path : book.title; }

void drawMetricCard(const GfxRenderer& renderer, const Rect& rect, const char* label, const std::string& value) {
  AppMetricCard::draw(renderer, rect, label, value);
}
}  // namespace

void ReadingDayDetailActivity::refreshEntries() {
  entries = ReadingStatsAnalytics::getBooksReadOnDay(dayOrdinal);
  if (selectedIndex >= static_cast<int>(entries.size())) {
    selectedIndex = std::max(0, static_cast<int>(entries.size()) - 1);
  }
}

void ReadingDayDetailActivity::openSelectedBook() {
  if (selectedIndex < 0 || selectedIndex >= static_cast<int>(entries.size()) ||
      entries[selectedIndex].book == nullptr) {
    return;
  }

  startActivityForResultWith<ReadingStatsDetailActivity>(
      [this](const ActivityResult&) {
        refreshEntries();
        requestUpdate();
      },
      entries[selectedIndex].book->path);
}

void ReadingDayDetailActivity::onEnter() {
  Activity::onEnter();
  refreshEntries();
  waitForConfirmRelease = mappedInput.isPressed(MappedInputManager::Button::Confirm);
  requestUpdate();
}

void ReadingDayDetailActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  if (waitForConfirmRelease) {
    if (!mappedInput.isPressed(MappedInputManager::Button::Confirm)) {
      waitForConfirmRelease = false;
    }
    return;
  }

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int listTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing + SUMMARY_CARD_HEIGHT +
                      metrics.verticalSpacing + DAYPART_HEIGHT + metrics.verticalSpacing + 34 + 10;
  const int listHeight = renderer.getScreenHeight() - listTop - metrics.buttonHintsHeight - metrics.verticalSpacing;
  if (handleListTouch(selectedIndex, static_cast<int>(entries.size()), listTop, listHeight, false) ==
      ListTouchResult::Activated) {
    openSelectedBook();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    openSelectedBook();
    return;
  }

  buttonNavigator.onNextRelease([this] {
    if (entries.empty()) {
      return;
    }
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, static_cast<int>(entries.size()));
    requestUpdate();
  });

  buttonNavigator.onPreviousRelease([this] {
    if (entries.empty()) {
      return;
    }
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, static_cast<int>(entries.size()));
    requestUpdate();
  });
}

void ReadingDayDetailActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int sidePadding = metrics.contentSidePadding;
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int cardWidth = (pageWidth - sidePadding * 2 - SUMMARY_GAP) / 2;
  const std::string dateLabel = ReadingStatsAnalytics::formatDayOrdinalLabel(dayOrdinal);
  const uint64_t totalReadingMs =
      !entries.empty() ? ReadingStatsAnalytics::buildTimelineDayEntry(dayOrdinal).totalReadingMs : 0;

  HeaderDateUtils::drawHeaderWithDate(renderer, tr(STR_READING_DAY), dateLabel.c_str());

  drawMetricCard(renderer, Rect{sidePadding, contentTop, cardWidth, SUMMARY_CARD_HEIGHT}, tr(STR_TOTAL_TIME),
                 ReadingStatsAnalytics::formatDurationHm(totalReadingMs));
  drawMetricCard(renderer, Rect{sidePadding + cardWidth + SUMMARY_GAP, contentTop, cardWidth, SUMMARY_CARD_HEIGHT},
                 tr(STR_BOOKS_READ), std::to_string(entries.size()));

  const int daypartTop = contentTop + SUMMARY_CARD_HEIGHT + metrics.verticalSpacing;
  const Rect daypartRect{sidePadding, daypartTop, pageWidth - sidePadding * 2, DAYPART_HEIGHT};
  renderer.drawRect(daypartRect.x, daypartRect.y, daypartRect.width, daypartRect.height);
  uint64_t dayparts[ReadingStatsAnalytics::DAYPART_COUNT] = {};
  ReadingStatsAnalytics::getDayDaypartMs(dayOrdinal, dayparts);
  const int partPad = 10;
  const int titleH = renderer.getLineHeight(UI_10_FONT_ID);
  renderer.drawText(UI_10_FONT_ID, daypartRect.x + partPad, daypartRect.y + 6, tr(STR_READING_DAYPART), true,
                    EpdFontFamily::BOLD);
  if (!ReadingStatsAnalytics::hasDaypartMs(dayparts)) {
    renderer.drawText(UI_10_FONT_ID, daypartRect.x + partPad, daypartRect.y + 6 + titleH + 4, tr(STR_NO_DAYPART_STATS));
  } else {
    const int colW = std::max(1, (daypartRect.width - partPad * 2) / 2);
    const int rowH = titleH + 10;
    for (int i = 0; i < ReadingStatsAnalytics::DAYPART_COUNT; ++i) {
      const int col = i % 2;
      const int row = i / 2;
      const int cellX = daypartRect.x + partPad + col * colW;
      const int cellY = daypartRect.y + 6 + titleH + 6 + row * rowH;
      char left[24] = {};
      snprintf(left, sizeof(left), "%s %s", daypartLabel(i), kDaypartHours[i]);
      char value[24] = {};
      ReadingStatsAnalytics::formatDurationLabel(dayparts[i], value, sizeof(value));
      const int valueW = renderer.getTextWidth(UI_10_FONT_ID, value, EpdFontFamily::BOLD);
      const int labelMax = std::max(8, colW - valueW - 12);
      const std::string shown = renderer.truncatedText(UI_10_FONT_ID, left, labelMax);
      renderer.drawText(UI_10_FONT_ID, cellX, cellY, shown.c_str());
      renderer.drawText(UI_10_FONT_ID, cellX + colW - 8 - valueW, cellY, value, true, EpdFontFamily::BOLD);
    }
  }

  const char* topBookLabel = tr(STR_TOP_BOOK);
  const std::string topBookTitle = !entries.empty() && entries.front().book != nullptr
                                       ? getBookTitle(*entries.front().book)
                                       : std::string(tr(STR_NOT_SET));
  const int listTop = daypartTop + DAYPART_HEIGHT + metrics.verticalSpacing;
  GUI.drawSubHeader(renderer, Rect{0, listTop, pageWidth, 34}, topBookLabel, topBookTitle.c_str());

  const int listContentTop = listTop + 34 + 10;
  const int listHeight = pageHeight - listContentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;
  if (entries.empty()) {
    renderer.drawText(UI_10_FONT_ID, sidePadding, listContentTop + 20, tr(STR_NO_READING_DAY));
  } else {
    GUI.drawList(
        renderer, Rect{0, listContentTop, pageWidth, listHeight}, static_cast<int>(entries.size()), selectedIndex,
        [this](const int index) {
          return entries[index].book ? getBookTitle(*entries[index].book) : std::string(tr(STR_NOT_SET));
        },
        [this](const int index) {
          if (!entries[index].book) {
            return std::string(tr(STR_NOT_SET));
          }
          return entries[index].book->author.empty() ? std::string(tr(STR_IN_PROGRESS)) : entries[index].book->author;
        },
        [](const int) { return UIIcon::Book; },
        [this](const int index) { return ReadingStatsAnalytics::formatDurationHm(entries[index].readingMs); });
  }

  const auto labels =
      mappedInput.mapLabels(tr(STR_BACK), entries.empty() ? "" : tr(STR_OPEN), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
