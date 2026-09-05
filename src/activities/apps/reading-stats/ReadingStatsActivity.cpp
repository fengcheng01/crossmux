#include "ReadingStatsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <string>

#include "AppMetricCard.h"
#include "CrossPointSettings.h"
#include "InxItemLayout.h"
#include "ReadingDayDetailActivity.h"
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

bool pointInRect(const int x, const int y, const Rect& rect) {
  return rect.width > 0 && rect.height > 0 && x >= rect.x && y >= rect.y && x < rect.x + rect.width &&
         y < rect.y + rect.height;
}

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

int daypartCardHeight(const GfxRenderer& renderer) {
  const int pad = 12;
  const int lineH = renderer.getLineHeight(UI_10_FONT_ID);
  return pad + lineH + 6 + 2 * (lineH + 10) + pad;
}

void drawDaypartRows(const GfxRenderer& renderer, const Rect card, const uint64_t* dayparts, const char* title) {
  const int pad = 12;
  const int titleH = renderer.getLineHeight(UI_10_FONT_ID);
  renderer.drawText(UI_10_FONT_ID, card.x + pad, card.y + pad, title, true, EpdFontFamily::BOLD);

  if (!ReadingStatsAnalytics::hasDaypartMs(dayparts)) {
    renderer.drawText(UI_10_FONT_ID, card.x + pad, card.y + pad + titleH + 6, tr(STR_NO_DAYPART_STATS));
    return;
  }

  const int colW = std::max(1, (card.width - pad * 2) / 2);
  const int rowH = titleH + 10;
  for (int i = 0; i < ReadingStatsAnalytics::DAYPART_COUNT; ++i) {
    const int col = i % 2;
    const int row = i / 2;
    const int cellX = card.x + pad + col * colW;
    const int cellY = card.y + pad + titleH + 6 + row * rowH;
    char left[24] = {};
    snprintf(left, sizeof(left), "%s %s", daypartLabel(i), kDaypartHours[i]);
    char right[24] = {};
    ReadingStatsAnalytics::formatDurationLabel(dayparts[i], right, sizeof(right));
    const int valueW = renderer.getTextWidth(UI_10_FONT_ID, right, EpdFontFamily::BOLD);
    const int labelMax = std::max(8, colW - valueW - 12);
    const std::string shown = renderer.truncatedText(UI_10_FONT_ID, left, labelMax);
    renderer.drawText(UI_10_FONT_ID, cellX, cellY, shown.c_str());
    renderer.drawText(UI_10_FONT_ID, cellX + colW - 8 - valueW, cellY, right, true, EpdFontFamily::BOLD);
  }
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
  moreHitRect_ = Rect(0, 0, 0, 0);
  bookPreviewCount_ = 0;
  for (int i = 0; i < kInxDayBars; ++i) {
    dayBarHit_[i] = Rect(0, 0, 0, 0);
    dayBarOrdinal_[i] = 0;
  }
  for (int i = 0; i < kInxBookPreview; ++i) {
    bookPreviewHit_[i] = Rect(0, 0, 0, 0);
    bookPreviewIndex_[i] = 0;
  }
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
      handleInxTap(touchX, touchY);
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

void ReadingStatsActivity::openDayDetail(const uint32_t dayOrdinal) {
  if (dayOrdinal == 0) {
    return;
  }
  startActivityForResultWith<ReadingDayDetailActivity>(
      [this](const ActivityResult&) {
        guardBackReturn();
        requestUpdate();
      },
      dayOrdinal);
}

void ReadingStatsActivity::handleInxTap(const int x, const int y) {
  if (pointInRect(x, y, moreHitRect_)) {
    openSelectedEntry();
    return;
  }
  for (int i = 0; i < kInxDayBars; ++i) {
    if (pointInRect(x, y, dayBarHit_[i])) {
      openDayDetail(dayBarOrdinal_[i]);
      return;
    }
  }
  const auto& books = READING_STATS.getBooks();
  for (int i = 0; i < bookPreviewCount_; ++i) {
    if (!pointInRect(x, y, bookPreviewHit_[i])) {
      continue;
    }
    const int bookIndex = bookPreviewIndex_[i];
    if (bookIndex < 0 || bookIndex >= static_cast<int>(books.size())) {
      return;
    }
    startActivityForResultWith<ReadingStatsDetailActivity>(
        [this](const ActivityResult&) {
          guardBackReturn();
          requestUpdate();
        },
        books[bookIndex].path);
    return;
  }
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
  drawPageHeader(Rect{0, metrics.topPadding, screenWidth, metrics.headerHeight}, tr(STR_READING_STATS));

  moreHitRect_ = Rect(0, 0, 0, 0);
  bookPreviewCount_ = 0;
  for (int i = 0; i < kInxDayBars; ++i) {
    dayBarHit_[i] = Rect(0, 0, 0, 0);
    dayBarOrdinal_[i] = 0;
  }

  const Rect content = UITheme::getInstance().getMainTabContentRect(renderer);
  const int pad = 14;
  const int innerPad = 12;
  const int sectionGap = 8;
  const int smallH = renderer.getLineHeight(SMALL_FONT_ID);
  const int ui10H = renderer.getLineHeight(UI_10_FONT_ID);
  const int ui12H = renderer.getLineHeight(UI_12_FONT_ID);
  int y = content.y;

  const int statusH = smallH + 8;
  char timeBuf[16] = {};
  TimeUtils::formatCurrentTime(timeBuf, sizeof(timeBuf), SETTINGS.clockFormat == 1);
  if (timeBuf[0] != '\0') renderer.drawText(SMALL_FONT_ID, 16, y + 4, timeBuf);
  GUI.drawBatteryRight(renderer, Rect{screenWidth - 12 - 15, y + 4, 15, 12},
                       SETTINGS.hideBatteryPercentage != CrossPointSettings::HIDE_BATTERY_PERCENTAGE::HIDE_ALWAYS);

  const uint32_t streakDays = READING_STATS.getCurrentStreakDays();
  if (streakDays > 0) {
    char streakBuf[24] = {};
    snprintf(streakBuf, sizeof(streakBuf), tr(STR_STREAK_DAYS_FMT), static_cast<int>(streakDays));
    const int streakW = renderer.getTextWidth(UI_10_FONT_ID, streakBuf, EpdFontFamily::BOLD);
    renderer.drawText(UI_10_FONT_ID, (screenWidth - streakW) / 2, y + 4, streakBuf, true, EpdFontFamily::BOLD);
  }
  y += statusH;

  const int volH = innerPad + smallH + 6 + ui12H + innerPad;
  const Rect vol{pad, y, screenWidth - pad * 2, volH};
  InxInkCards::drawCard(renderer, vol);
  {
    const GfxRenderer::ClipScope clip(renderer, vol.x + 2, vol.y + 2, vol.width - 4, vol.height - 4);
    const int cellW = vol.width / 3;
    const char* volLabels[] = {tr(STR_TODAY_READING), tr(STR_LAST_7D), tr(STR_THIS_MONTH_READING)};
    const uint64_t volMs[] = {READING_STATS.getTodayReadingMs(), READING_STATS.getRecentReadingMs(7),
                              READING_STATS.getRecentReadingMs(30)};
    const int labelY = vol.y + innerPad;
    const int valueY = labelY + smallH + 6;
    for (int i = 0; i < 3; ++i) {
      const int cx = vol.x + cellW * i + cellW / 2;
      const int labW = renderer.getTextWidth(SMALL_FONT_ID, volLabels[i]);
      renderer.drawText(SMALL_FONT_ID, cx - labW / 2, labelY, volLabels[i]);
      char value[24] = {};
      ReadingStatsAnalytics::formatDurationLabel(volMs[i], value, sizeof(value));
      const int valueW = renderer.getTextWidth(UI_12_FONT_ID, value, EpdFontFamily::BOLD);
      renderer.drawText(UI_12_FONT_ID, cx - valueW / 2, valueY, value, true, EpdFontFamily::BOLD);
      if (i > 0) renderer.drawLine(vol.x + cellW * i, vol.y + 10, vol.x + cellW * i, vol.y + vol.height - 10);
    }
  }
  y += volH + sectionGap;

  const int bottom = content.y + content.height - 12;
  const int partH = daypartCardHeight(renderer);
  const int bookRowH = ui10H + 6;
  const int bookH = innerPad + ui10H + 6 + bookRowH + innerPad;
  const int minChart = innerPad + ui10H + 4 + ui10H + 8 + 36 + ui10H + innerPad;
  int chartH = bottom - y - partH - bookH - sectionGap * 2;
  bool showBooks = true;
  if (chartH < minChart) {
    showBooks = false;
    chartH = bottom - y - partH - sectionGap;
  }
  if (chartH < minChart) {
    chartH = std::max(72, bottom - y - sectionGap - std::min(partH, (bottom - y) / 2));
  }

  const Rect chart{pad, y, screenWidth - pad * 2, chartH};
  InxInkCards::drawCard(renderer, chart);
  const int headerY = chart.y + innerPad;
  char chartTitleWithUnit[32] = {};
  snprintf(chartTitleWithUnit, sizeof(chartTitleWithUnit), "%s (分钟)", tr(STR_LAST_7D));
  renderer.drawText(UI_10_FONT_ID, chart.x + innerPad, headerY, chartTitleWithUnit, true, EpdFontFamily::BOLD);
  const char* more = tr(STR_MORE);
  const int moreW = renderer.getTextWidth(UI_10_FONT_ID, more);
  const int moreX = chart.x + chart.width - innerPad - moreW;
  renderer.drawText(UI_10_FONT_ID, moreX, headerY, more);
  moreHitRect_ = Rect{moreX - 8, headerY - 4, moreW + 16, ui10H + 12};

  const auto& days = READING_STATS.getReadingDays();
  uint32_t refDay = TimeUtils::getLocalDayOrdinal(READING_STATS.getDisplayTimestamp());
  if (refDay == 0 && !days.empty()) refDay = days.back().dayOrdinal;
  uint64_t maxMs = 1;
  uint64_t dayMs[kInxDayBars] = {};
  char dayLabel[kInxDayBars][4] = {};
  char topLabel[kInxDayBars][24] = {};
  for (int i = 0; i < kInxDayBars; ++i) {
    const uint32_t ordinal = (refDay >= static_cast<uint32_t>(6 - i)) ? refDay - static_cast<uint32_t>(6 - i) : 0;
    dayBarOrdinal_[i] = ordinal;
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
        if (entry.readingMs > 0) {
          ReadingStatsAnalytics::formatDurationLabel(entry.readingMs, topLabel[i], sizeof(topLabel[i]));
        }
        break;
      }
    }
  }

  const int titleBottom = headerY + ui10H;
  const int plotTop = titleBottom + 8;
  const int plotBottom = chart.y + chart.height - innerPad - smallH - 4;
  const int plotH = std::max(16, plotBottom - plotTop - ui10H - 2);
  const int gap = 8;
  const int barW = std::max(8, (chart.width - innerPad * 2 - gap * 6) / 7);
  {
    const GfxRenderer::ClipScope clip(renderer, chart.x + 2, chart.y + 2, chart.width - 4, chart.height - 4);
    renderer.drawLine(chart.x + innerPad, plotBottom, chart.x + chart.width - innerPad, plotBottom);
    for (int i = 0; i < kInxDayBars; ++i) {
      const int barX = chart.x + innerPad + i * (barW + gap);
      dayBarHit_[i] = Rect{barX - gap / 2, plotTop, barW + gap, std::max(1, (chart.y + chart.height - 6) - plotTop)};
      if (dayMs[i] > 0) {
        int barH = static_cast<int>(dayMs[i] * static_cast<uint64_t>(plotH) / maxMs);
        if (barH < 4) barH = 4;
        if (barH > plotH) barH = plotH;
        renderer.fillRect(barX, plotBottom - barH, barW, barH);

        char minutesBuf[8] = {};
        const unsigned long long mins = static_cast<unsigned long long>((dayMs[i] + 30000ULL) / 60000ULL);
        snprintf(minutesBuf, sizeof(minutesBuf), "%llu", mins > 0 ? mins : 1ULL);
        const int tw = renderer.getTextWidth(UI_10_FONT_ID, minutesBuf, EpdFontFamily::BOLD);
        const int labelY = plotBottom - barH - ui10H - 2;
        renderer.drawText(UI_10_FONT_ID, barX + (barW - tw) / 2, labelY, minutesBuf, true, EpdFontFamily::BOLD);
      }
      if (dayLabel[i][0] != '\0') {
        const int tw = renderer.getTextWidth(SMALL_FONT_ID, dayLabel[i]);
        renderer.drawText(SMALL_FONT_ID, barX + (barW - tw) / 2, plotBottom + 4, dayLabel[i]);
      }
    }
  }
  y += chartH + sectionGap;

  const Rect parts{pad, y, screenWidth - pad * 2, partH};
  InxInkCards::drawCard(renderer, parts);
  uint64_t dayparts[ReadingStatsAnalytics::DAYPART_COUNT] = {};
  const uint32_t today = TimeUtils::getLocalDayOrdinal(READING_STATS.getDisplayTimestamp());
  ReadingStatsAnalytics::getDayDaypartMs(today, dayparts);
  {
    const GfxRenderer::ClipScope clip(renderer, parts.x + 2, parts.y + 2, parts.width - 4, parts.height - 4);
    drawDaypartRows(renderer, parts, dayparts, tr(STR_TODAY_READING_DAYPART));
  }
  y += partH + sectionGap;

  if (showBooks && y + 40 < bottom) {
    const Rect booksCard{pad, y, screenWidth - pad * 2, std::min(bookH, std::max(0, bottom - y))};
    InxInkCards::drawCard(renderer, booksCard);
    renderer.drawText(UI_10_FONT_ID, booksCard.x + innerPad, booksCard.y + innerPad, tr(STR_NOW_READING), true,
                      EpdFontFamily::BOLD);
    const auto& books = READING_STATS.getBooks();
    if (books.empty()) {
      renderer.drawText(UI_10_FONT_ID, booksCard.x + innerPad, booksCard.y + innerPad + ui10H + 6,
                        tr(STR_NO_READING_STATS));
    } else {
      const int rowTop = booksCard.y + innerPad + ui10H + 6;
      bookPreviewCount_ = 1;
      bookPreviewIndex_[0] = 0;
      bookPreviewHit_[0] = Rect{booksCard.x + 8, rowTop, booksCard.width - 16, bookRowH};
      char duration[24] = {};
      ReadingStatsAnalytics::formatDurationLabel(books[0].totalReadingMs, duration, sizeof(duration));
      const int durW = renderer.getTextWidth(UI_10_FONT_ID, duration);
      const std::string title = renderer.truncatedText(UI_10_FONT_ID, getBookTitle(books[0]).c_str(),
                                                       booksCard.width - innerPad * 2 - durW - 12, EpdFontFamily::BOLD);
      renderer.drawText(UI_10_FONT_ID, booksCard.x + innerPad, rowTop, title.c_str(), true, EpdFontFamily::BOLD);
      renderer.drawText(UI_10_FONT_ID, booksCard.x + booksCard.width - innerPad - durW, rowTop, duration);
    }
  }

  const auto labels = mainTabButtonLabels(tr(STR_BACK), tr(STR_MORE), false);
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
