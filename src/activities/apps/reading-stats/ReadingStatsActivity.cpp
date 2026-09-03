#include "ReadingStatsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <string>

#include "AppMetricCard.h"
#include "CrossPointSettings.h"
#include "InxItemLayout.h"
#include "ReadingStatsDetailActivity.h"
#include "ReadingStatsExtendedActivity.h"
#include "ReadingStatsStore.h"
#include "SdCardFontSystem.h"
#include "activities/util/ConfirmationActivity.h"
#include "components/UITheme.h"
#include "components/themes/inx/InxInkCards.h"
#include "fontIds.h"
#include "util/HeaderDateUtils.h"
#include "util/ReadingStatsAnalytics.h"
#include "util/TimeUtils.h"

namespace {
constexpr unsigned long BOOK_LONG_PRESS_MS = 1000;
constexpr int SUMMARY_CARD_HEIGHT = 70;
constexpr int SUMMARY_GAP = 10;
constexpr int DETAILS_BUTTON_HEIGHT = 58;
constexpr int LIST_HEADER_HEIGHT = 34;
constexpr int LIST_HEADER_BOTTOM_GAP = 10;
constexpr int BOOK_ROW_HEIGHT = 80;
constexpr int BOOK_ROW_GAP = 10;
constexpr int BOOKS_PER_PAGE = 3;

std::string getBookTitle(const ReadingBookStats& book) { return book.title.empty() ? book.path : book.title; }

int titleFontId() {
  // Card titles stay at 12pt so a CJK title can wrap instead of becoming 重生之…
  // when the reader size is 14/16/18. UI_12 is the full common-character subset
  // on CN builds. See BookTitleFont.h.
  return UI_12_FONT_ID;
}

std::string getBookSubtitle(const ReadingBookStats& book) {
  if (!book.author.empty()) {
    return book.author;
  }
  return book.completed ? std::string(tr(STR_DONE)) : std::string(tr(STR_IN_PROGRESS));
}

void drawMetricCard(const GfxRenderer& renderer, const Rect& rect, const char* label, const std::string& value,
                    const bool showCheck = false) {
  AppMetricCard::Options options;
  options.showCheck = showCheck;
  AppMetricCard::draw(renderer, rect, label, value, options);
}

void drawMoreDetailsButton(const GfxRenderer& renderer, const Rect& rect, const bool selected) {
  if (selected) {
    renderer.fillRectDither(rect.x, rect.y, rect.width, rect.height, Color::LightGray);
  }
  renderer.drawRect(rect.x, rect.y, rect.width, rect.height);

  const char* label = tr(STR_MORE_DETAILS);
  const int textWidth = renderer.getTextWidth(UI_12_FONT_ID, label, EpdFontFamily::BOLD);
  const int textX = rect.x + (rect.width - textWidth) / 2;
  const int textY = rect.y + (rect.height - renderer.getLineHeight(UI_12_FONT_ID)) / 2 + 2;
  renderer.drawText(UI_12_FONT_ID, textX, textY, label, true, EpdFontFamily::BOLD);
}

void drawDurationStack(const GfxRenderer& renderer, const int cx, const int y, const uint64_t ms) {
  char number[8];
  bool hours = false;
  ReadingStatsAnalytics::formatDurationParts(ms, number, sizeof(number), hours);
  const char* unit = hours ? tr(STR_HOURS_UNIT) : tr(STR_MINUTES_UNIT);
  const int numW = renderer.getTextWidth(UI_12_FONT_ID, number, EpdFontFamily::BOLD);
  renderer.drawText(UI_12_FONT_ID, cx - numW / 2, y, number, true, EpdFontFamily::BOLD);
  const int unitW = renderer.getTextWidth(SMALL_FONT_ID, unit);
  renderer.drawText(SMALL_FONT_ID, cx - unitW / 2, y + renderer.getLineHeight(UI_12_FONT_ID) + 4, unit);
}

void drawMiniProgressBar(const GfxRenderer& renderer, const Rect& rect, const uint8_t percent) {
  renderer.drawRect(rect.x, rect.y, rect.width, rect.height);
  const int innerWidth = std::max(0, rect.width - 4);
  const int fillWidth = innerWidth * std::min<int>(percent, 100) / 100;
  if (fillWidth > 0) {
    renderer.fillRect(rect.x + 2, rect.y + 2, fillWidth, std::max(0, rect.height - 4));
  }
}

void drawBookRow(const GfxRenderer& renderer, const Rect& rect, const ReadingBookStats& book, const bool selected) {
  if (selected) {
    renderer.fillRectDither(rect.x, rect.y, rect.width, rect.height, Color::LightGray);
    renderer.drawRect(rect.x, rect.y, rect.width, rect.height);
  } else {
    renderer.drawLine(rect.x, rect.y + rect.height, rect.x + rect.width, rect.y + rect.height);
  }

  const int sidePadding = 12;
  const int topPadding = 9;
  const int metaWidth = 88;
  const int innerX = rect.x + sidePadding;
  const int innerY = rect.y + topPadding;
  const int textWidth = rect.width - sidePadding * 2 - metaWidth;
  const int titleY = innerY;
  const int subtitleY = innerY + 26;
  const int progressBarY = rect.y + rect.height - 14;

  const int rowTitleFont = titleFontId();
  const std::string title =
      renderer.truncatedText(rowTitleFont, getBookTitle(book).c_str(), textWidth - 4, EpdFontFamily::BOLD);
  renderer.drawText(rowTitleFont, innerX, titleY, title.c_str(), true, EpdFontFamily::BOLD);

  const std::string subtitle =
      renderer.truncatedText(UI_10_FONT_ID, getBookSubtitle(book).c_str(), textWidth - 4, EpdFontFamily::REGULAR);
  renderer.drawText(UI_10_FONT_ID, innerX, subtitleY, subtitle.c_str());

  const std::string progressText = std::to_string(book.lastProgressPercent) + "%";
  const std::string totalTimeText = ReadingStatsAnalytics::formatDurationHm(book.totalReadingMs);
  const int progressWidth = renderer.getTextWidth(UI_12_FONT_ID, progressText.c_str(), EpdFontFamily::BOLD);
  const int timeWidth = renderer.getTextWidth(UI_10_FONT_ID, totalTimeText.c_str());
  const int progressX = rect.x + rect.width - sidePadding - progressWidth;
  const int timeX = rect.x + rect.width - sidePadding - timeWidth;

  renderer.drawText(UI_12_FONT_ID, progressX, titleY, progressText.c_str(), true, EpdFontFamily::BOLD);
  renderer.drawText(UI_10_FONT_ID, timeX, subtitleY, totalTimeText.c_str());

  drawMiniProgressBar(renderer, Rect{innerX, progressBarY, rect.width - sidePadding * 2, 9}, book.lastProgressPercent);
}
}  // namespace

void ReadingStatsActivity::selectMainTabContentEdge(const MainTabContentEdge edge) {
  (void)edge;
  // This screen draws no per-entry state, so a silently retargeted
  // selectedIndex would point taps and Confirm at a different detail screen
  // with nothing visible having changed. Stats content is fixed here; books
  // are reachable through the extended list.
  selectedIndex = 0;
}

void ReadingStatsActivity::onEnter() {
  Activity::onEnter();
  sdFontSystem.ensureLoaded(renderer);
  // Enter on the default FAST exactly like the other tabs. The M4's HALF
  // sequence (0xD4) black-flashes hard and leaves the page washed out
  // (faint tab bar and metric text) — strictly worse on this panel.
  selectedIndex = usesInxLayout() ? 0 : (READING_STATS.getBooks().empty() ? 0 : 1);
  waitForConfirmRelease = mappedInput.isPressed(MappedInputManager::Button::Confirm);
  waitForBackRelease = false;
  requestUpdate();
}

void ReadingStatsActivity::onExit() {
  // No refresh override here: returning to the Reading Stats menu stays on FAST_REFRESH (no flash).
  Activity::onExit();
}

void ReadingStatsActivity::loop() {
  const int bookCount = static_cast<int>(READING_STATS.getBooks().size());
  const int selectableCount = bookCount + 1;
  const int pageItems = BOOKS_PER_PAGE;

  if (waitForBackRelease) {
    if (!mappedInput.isPressed(MappedInputManager::Button::Back) &&
        !mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      waitForBackRelease = false;
    }
    return;
  }

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

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (selectedIndex > 0 && mappedInput.getHeldTime() >= BOOK_LONG_PRESS_MS) {
      confirmRemoveSelectedBook();
      return;
    }

    openSelectedEntry();
    return;
  }

  if (usesInxLayout()) {
    int touchX = 0;
    int touchY = 0;
    if (mappedInput.wasScreenTapped(touchX, touchY)) {
      openSelectedEntry();
      return;
    }
    return;
  }

  buttonNavigator.onNextRelease([this, selectableCount] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, selectableCount);
    requestUpdate();
  });

  buttonNavigator.onPreviousRelease([this, selectableCount] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, selectableCount);
    requestUpdate();
  });

  buttonNavigator.onNextContinuous([this, selectableCount, pageItems] {
    if (selectableCount <= 1) {
      return;
    }

    if (selectedIndex == 0) {
      selectedIndex = 1;
    } else {
      const int bookIndex = selectedIndex - 1;
      selectedIndex = ButtonNavigator::nextPageIndex(bookIndex, selectableCount - 1, pageItems) + 1;
    }
    requestUpdate();
  });

  buttonNavigator.onPreviousContinuous([this, selectableCount, pageItems] {
    if (selectableCount <= 1) {
      return;
    }

    if (selectedIndex == 0) {
      selectedIndex = ((selectableCount - 2) / pageItems) * pageItems + 1;
    } else {
      const int bookIndex = selectedIndex - 1;
      selectedIndex = ButtonNavigator::previousPageIndex(bookIndex, selectableCount - 1, pageItems) + 1;
    }
    requestUpdate();
  });
}

void ReadingStatsActivity::openSelectedEntry() {
  const auto& books = READING_STATS.getBooks();
  if (selectedIndex == 0) {
    startActivityForResultWith<ReadingStatsExtendedActivity>([this](const ActivityResult&) {
      guardBackReturn();
      requestUpdate();
    });
    return;
  }
  const int bookIndex = selectedIndex - 1;
  if (bookIndex < 0 || bookIndex >= static_cast<int>(books.size())) {
    return;
  }

  startActivityForResultWith<ReadingStatsDetailActivity>(
      [this](const ActivityResult&) {
        guardBackReturn();
        requestUpdate();
      },
      books[bookIndex].path);
}

void ReadingStatsActivity::confirmRemoveSelectedBook() {
  const auto& books = READING_STATS.getBooks();
  const int bookIndex = selectedIndex - 1;
  if (bookIndex < 0 || bookIndex >= static_cast<int>(books.size())) {
    return;
  }

  const ReadingBookStats selectedBook = books[bookIndex];
  const int currentSelection = selectedIndex;
  startActivityForResultWith<ConfirmationActivity>(
      [this, selectedBook, currentSelection](const ActivityResult& result) {
        if (!result.isCancelled && READING_STATS.removeBook(selectedBook.path)) {
          const int bookCount = static_cast<int>(READING_STATS.getBooks().size());
          selectedIndex = InxStatisticsGeometry::clampView(currentSelection, bookCount);
        }

        guardBackReturn();
        requestUpdate(true);
      },
      tr(STR_DELETE_STATS_ENTRY), getBookTitle(selectedBook));
}

void ReadingStatsActivity::guardBackReturn() { waitForBackRelease = true; }

bool ReadingStatsActivity::usesInxLayout() const { return UITheme::getInstance().hasMainTabs(); }

void ReadingStatsActivity::render(RenderLock&&) {
  if (usesInxLayout()) {
    renderInx();
    return;
  }

  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int sidePadding = metrics.contentSidePadding;
  const int cardWidth = (pageWidth - sidePadding * 2 - SUMMARY_GAP) / 2;
  const int summaryTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int detailsTop = summaryTop + SUMMARY_CARD_HEIGHT * 3 + SUMMARY_GAP * 2 + metrics.verticalSpacing;
  const uint64_t todayReadingMs = READING_STATS.getTodayReadingMs();
  const std::string dailyGoalValue = ReadingStatsAnalytics::formatDurationHm(todayReadingMs) + " / " +
                                     ReadingStatsAnalytics::formatDurationHm(getDailyReadingGoalMs());

  if (usesMainTabBar()) {
    drawPageHeader(Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_READING_STATS));
  } else {
    HeaderDateUtils::drawHeaderWithDate(renderer, tr(STR_READING_STATS));
  }

  drawMetricCard(renderer, Rect{sidePadding, summaryTop, cardWidth, SUMMARY_CARD_HEIGHT}, tr(STR_STREAK),
                 std::to_string(READING_STATS.getCurrentStreakDays()));
  drawMetricCard(renderer, Rect{sidePadding + cardWidth + SUMMARY_GAP, summaryTop, cardWidth, SUMMARY_CARD_HEIGHT},
                 tr(STR_MAX_STREAK), std::to_string(READING_STATS.getMaxStreakDays()));
  drawMetricCard(renderer,
                 Rect{sidePadding, summaryTop + SUMMARY_CARD_HEIGHT + SUMMARY_GAP, cardWidth, SUMMARY_CARD_HEIGHT},
                 tr(STR_DAILY_GOAL), dailyGoalValue, todayReadingMs >= getDailyReadingGoalMs());
  drawMetricCard(renderer,
                 Rect{sidePadding + cardWidth + SUMMARY_GAP, summaryTop + SUMMARY_CARD_HEIGHT + SUMMARY_GAP, cardWidth,
                      SUMMARY_CARD_HEIGHT},
                 tr(STR_READING_TIME), ReadingStatsAnalytics::formatDurationHm(READING_STATS.getTotalReadingMs()));
  drawMetricCard(
      renderer, Rect{sidePadding, summaryTop + (SUMMARY_CARD_HEIGHT + SUMMARY_GAP) * 2, cardWidth, SUMMARY_CARD_HEIGHT},
      tr(STR_BOOKS_FINISHED), std::to_string(READING_STATS.getBooksFinishedCount()));
  drawMetricCard(renderer,
                 Rect{sidePadding + cardWidth + SUMMARY_GAP, summaryTop + (SUMMARY_CARD_HEIGHT + SUMMARY_GAP) * 2,
                      cardWidth, SUMMARY_CARD_HEIGHT},
                 tr(STR_BOOKS_STARTED), std::to_string(READING_STATS.getBooksStartedCount()));

  drawMoreDetailsButton(renderer, Rect{sidePadding, detailsTop, pageWidth - sidePadding * 2, DETAILS_BUTTON_HEIGHT},
                        selectedIndex == 0);

  const int listHeaderTop = detailsTop + DETAILS_BUTTON_HEIGHT + metrics.verticalSpacing;
  const auto& books = READING_STATS.getBooks();
  const int totalPages = std::max(1, static_cast<int>((books.size() + BOOKS_PER_PAGE - 1) / BOOKS_PER_PAGE));
  const int currentPage = books.empty() || selectedIndex == 0 ? 1 : ((selectedIndex - 1) / BOOKS_PER_PAGE) + 1;
  const std::string bookCountLabel = std::to_string(currentPage) + "/" + std::to_string(totalPages);
  const std::string startedBooksLabel =
      std::string(tr(STR_STARTED_BOOKS)) + " (" + std::to_string(READING_STATS.getBooksStartedCount()) + ")";
  GUI.drawSubHeader(renderer, Rect{0, listHeaderTop, pageWidth, LIST_HEADER_HEIGHT}, startedBooksLabel.c_str(),
                    bookCountLabel.c_str());

  const int contentTop = listHeaderTop + LIST_HEADER_HEIGHT + LIST_HEADER_BOTTOM_GAP;

  if (books.empty()) {
    renderer.drawText(UI_10_FONT_ID, sidePadding, contentTop + 20, tr(STR_NO_READING_STATS));
  } else {
    const int selectedBookIndex = std::max(0, selectedIndex - 1);
    const int pageStartIndex = (selectedBookIndex / BOOKS_PER_PAGE) * BOOKS_PER_PAGE;
    const int pageEndIndex = std::min(static_cast<int>(books.size()), pageStartIndex + BOOKS_PER_PAGE);
    for (int index = pageStartIndex; index < pageEndIndex; ++index) {
      const int rowIndex = index - pageStartIndex;
      const int rowY = contentTop + rowIndex * (BOOK_ROW_HEIGHT + BOOK_ROW_GAP);
      drawBookRow(renderer, Rect{sidePadding, rowY, pageWidth - sidePadding * 2, BOOK_ROW_HEIGHT}, books[index],
                  selectedIndex == index + 1);
    }
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}

void ReadingStatsActivity::renderInx() {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int screenWidth = renderer.getScreenWidth();
  const int screenHeight = renderer.getScreenHeight();
  drawPageHeader(Rect{0, metrics.topPadding, screenWidth, metrics.headerHeight}, tr(STR_READING_STATS));

  const int hintH = GUI.buttonHintsVisible() ? metrics.buttonHintsHeight : 0;
  const int contentTop = metrics.topPadding + metrics.headerHeight;
  const int contentBottom = screenHeight - hintH;

  char timeBuf[16] = {};
  TimeUtils::formatCurrentTime(timeBuf, sizeof(timeBuf), SETTINGS.clockFormat == 1);
  if (timeBuf[0] != '\0') renderer.drawText(SMALL_FONT_ID, 16, contentTop + 6, timeBuf);
  GUI.drawBatteryRight(renderer, Rect{screenWidth - 12 - 15, contentTop + 4, 15, 12},
                       SETTINGS.hideBatteryPercentage != CrossPointSettings::HIDE_BATTERY_PERCENTAGE::HIDE_ALWAYS);

  const int pad = 14;
  const int volTop = contentTop + renderer.getLineHeight(SMALL_FONT_ID) + 14;
  const int volH = 110;
  const Rect vol{pad, volTop, screenWidth - pad * 2, volH};
  InxInkCards::drawCard(renderer, vol);
  const int cellW = vol.width / 3;
  const char* volLabels[] = {tr(STR_TODAY_READING), tr(STR_LAST_7_DAYS), tr(STR_THIS_MONTH_READING)};
  const uint64_t volMs[] = {READING_STATS.getTodayReadingMs(), READING_STATS.getRecentReadingMs(7),
                            READING_STATS.getRecentReadingMs(30)};
  // Reading-streak badge, centered in the header band between clock and
  // battery: the one metric this tab otherwise hides behind "更多". Centered
  // rather than trailing the clock — the left-side run-on read as cluttered
  // (device feedback 2026-09-03).
  const uint32_t streakDays = READING_STATS.getCurrentStreakDays();
  if (streakDays > 0) {
    char streakBuf[24] = {};
    snprintf(streakBuf, sizeof(streakBuf), tr(STR_STREAK_DAYS_FMT), static_cast<int>(streakDays));
    const int streakW = renderer.getTextWidth(UI_10_FONT_ID, streakBuf, EpdFontFamily::BOLD);
    renderer.drawText(UI_10_FONT_ID, (screenWidth - streakW) / 2, contentTop + 4, streakBuf, true,
                      EpdFontFamily::BOLD);
  }

  const int labelY = vol.y + 12;
  const int numY = labelY + renderer.getLineHeight(SMALL_FONT_ID) + 10;
  for (int i = 0; i < 3; ++i) {
    const int cx = vol.x + cellW * i + cellW / 2;
    const int labW = renderer.getTextWidth(SMALL_FONT_ID, volLabels[i]);
    renderer.drawText(SMALL_FONT_ID, cx - labW / 2, labelY, volLabels[i]);
    drawDurationStack(renderer, cx, numY, volMs[i]);
    if (i > 0) renderer.drawLine(vol.x + cellW * i, vol.y + 10, vol.x + cellW * i, vol.y + vol.height - 10);
  }

  const Rect chart{pad, vol.y + vol.height + 12, screenWidth - pad * 2,
                   std::max(120, contentBottom - 16 - (vol.y + vol.height + 12))};
  InxInkCards::drawCard(renderer, chart);
  const int headerY = chart.y + 10;
  const int titleH = renderer.getLineHeight(UI_12_FONT_ID);
  const int moreH = renderer.getLineHeight(UI_10_FONT_ID);
  const int valueH = renderer.getLineHeight(SMALL_FONT_ID);
  renderer.drawText(UI_12_FONT_ID, chart.x + 14, headerY, tr(STR_LAST_7D), true, EpdFontFamily::BOLD);
  const char* more = tr(STR_MORE);
  renderer.drawText(UI_10_FONT_ID, chart.x + chart.width - 16 - renderer.getTextWidth(UI_10_FONT_ID, more), headerY,
                    more);

  const auto& days = READING_STATS.getReadingDays();
  uint32_t refDay = TimeUtils::getLocalDayOrdinal(READING_STATS.getDisplayTimestamp());
  if (refDay == 0 && !days.empty()) refDay = days.back().dayOrdinal;
  uint64_t maxMs = 1;
  uint64_t dayMs[7] = {};
  char dayLabel[7][4] = {};
  char topLabel[7][8] = {};
  for (int i = 0; i < 7; ++i) {
    const uint32_t ordinal = (refDay >= static_cast<uint32_t>(6 - i)) ? refDay - static_cast<uint32_t>(6 - i) : 0;
    int year = 0;
    unsigned month = 0;
    unsigned day = 0;
    if (ordinal != 0 && TimeUtils::getDateFromDayOrdinal(ordinal, year, month, day)) {
      snprintf(dayLabel[i], sizeof(dayLabel[i]), "%u", day);
    }
    for (const auto& entry : days) {
      if (entry.dayOrdinal == ordinal) {
        dayMs[i] = entry.readingMs;
        if (entry.readingMs > maxMs) maxMs = entry.readingMs;
        const uint64_t minutes = entry.readingMs / 60000ULL;
        if (minutes > 0) snprintf(topLabel[i], sizeof(topLabel[i]), "%llu", static_cast<unsigned long long>(minutes));
        break;
      }
    }
  }

  const int headerBottom = headerY + std::max(titleH, moreH);
  const int plotTop = headerBottom + valueH + 6;
  const int plotBottom = chart.y + chart.height - 34;
  const int plotH = std::max(20, plotBottom - plotTop);
  const int gap = 8;
  const int barW = std::max(8, (chart.width - 28 - gap * 6) / 7);
  // Minute labels only on today and the peak bar: seven stacked numbers read
  // as noise on the 1-bit panel.
  int peakIndex = -1;
  for (int i = 0; i < 7; ++i) {
    if (dayMs[i] > 0 && dayMs[i] == maxMs) peakIndex = i;
  }
  for (int i = 0; i < 7; ++i) {
    const int x = chart.x + 14 + i * (barW + gap);
    int barH = static_cast<int>(dayMs[i] * static_cast<uint64_t>(plotH) / maxMs);
    if (dayMs[i] > 0 && barH < 6) barH = 6;
    if (barH < 3) barH = 3;
    if (barH > plotH) barH = plotH;
    renderer.fillRect(x, plotBottom - barH, barW, barH);
    const bool showMinutes = topLabel[i][0] != '\0' && (i == 6 || i == peakIndex);
    if (showMinutes) {
      const int tw = renderer.getTextWidth(SMALL_FONT_ID, topLabel[i], EpdFontFamily::BOLD);
      const int labelY = plotBottom - barH - valueH;
      renderer.drawText(SMALL_FONT_ID, x + (barW - tw) / 2, std::max(headerBottom + 2, labelY), topLabel[i], true,
                        EpdFontFamily::BOLD);
    }
    if (dayLabel[i][0] != '\0') {
      const int tw = renderer.getTextWidth(SMALL_FONT_ID, dayLabel[i]);
      renderer.drawText(SMALL_FONT_ID, x + (barW - tw) / 2, plotBottom + 4, dayLabel[i]);
    }
  }

  const auto labels = mainTabButtonLabels(tr(STR_BACK), tr(STR_MORE), false);
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
