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
// Keep sub-minute sessions visible instead of labelling an active day as zero.
void formatPaperMinutes(const uint64_t ms, char* output, const size_t size) {
  if (ms > 0 && ms < 60000) {
    snprintf(output, size, "<1");
    return;
  }
  snprintf(output, size, "%llu", static_cast<unsigned long long>((ms + 30000) / 60000));
}

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
  const int titleH = renderer.getLineHeight(UI_10_FONT_ID);
  const int smallH = renderer.getLineHeight(SMALL_FONT_ID);
  return pad + titleH + 8 + 16 + 3 + smallH + 8 + 2 * (titleH + 8) + pad;
}

void drawDaypartRows(const GfxRenderer& renderer, const Rect card, const uint64_t* dayparts, const char* title) {
  const int pad = 12;
  const int titleH = renderer.getLineHeight(UI_10_FONT_ID);
  const int smallH = renderer.getLineHeight(SMALL_FONT_ID);
  renderer.drawText(UI_10_FONT_ID, card.x + pad, card.y + pad, title, true, EpdFontFamily::BOLD);

  // 24-Hour Timeline Heat Strip
  const int timelineY = card.y + pad + titleH + 8;
  const int timelineH = 14;
  const int availableW = card.width - pad * 2;
  const int blockGap = 2;
  const int blockW = std::max(4, (availableW - blockGap * 23) / 24);
  const int stripStartX = card.x + pad + (availableW - (blockW * 24 + blockGap * 23)) / 2;

  const uint32_t today = TimeUtils::getLocalDayOrdinal(READING_STATS.getDisplayTimestamp());
  for (int h = 0; h < 24; ++h) {
    const int bx = stripStartX + h * (blockW + blockGap);
    const uint64_t hMs = READING_STATS.getDayHourReadingMs(today, static_cast<uint8_t>(h));
    if (hMs > 0) {
      renderer.fillRoundedRect(bx, timelineY, blockW, timelineH, 2, Color::Black);
    } else {
      renderer.drawRoundedRect(bx, timelineY, blockW, timelineH, 1, 2, true);
    }
  }

  // 5 Time labels underneath: 00:00, 06:00, 12:00, 18:00, 24:00
  const int labelsY = timelineY + timelineH + 3;
  static const char* const kHourTicks[5] = {"00:00", "06:00", "12:00", "18:00", "24:00"};
  static const int kTickHours[5] = {0, 6, 12, 18, 23};
  for (int t = 0; t < 5; ++t) {
    const int bx = stripStartX + kTickHours[t] * (blockW + blockGap);
    const int tw = renderer.getTextWidth(SMALL_FONT_ID, kHourTicks[t]);
    int tx = bx + blockW / 2 - tw / 2;
    if (t == 0) tx = stripStartX;
    if (t == 4) tx = stripStartX + availableW - tw;
    renderer.drawText(SMALL_FONT_ID, tx, labelsY, kHourTicks[t]);
  }

  // 4 Daypart summaries in 2 clean columns below
  const int summaryY = labelsY + smallH + 8;
  const int colW = std::max(1, (card.width - pad * 2) / 2);
  const int rowH = titleH + 8;
  for (int i = 0; i < ReadingStatsAnalytics::DAYPART_COUNT; ++i) {
    const int col = i % 2;
    const int row = i / 2;
    const int cellX = card.x + pad + col * colW;
    const int cellY = summaryY + row * rowH;
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
  if (GUI.usesPaperStyle()) {
    selectedIndex = edge == MainTabContentEdge::First ? 0 : kInxDayBars;
    return;
  }
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
    if (usesInxLayout() && GUI.usesPaperStyle() && selectedIndex > 0) {
      if (selectedIndex <= kInxDayBars) openDayDetail(dayBarOrdinal_[selectedIndex - 1]);
      return;
    }
    if (selectedIndex > 0 && mappedInput.getHeldTime() >= BOOK_LONG_PRESS_MS) {
      confirmRemoveSelectedBook();
      return;
    }

    openSelectedEntry();
    return;
  }

  if (usesInxLayout()) {
    if (GUI.usesPaperStyle()) {
      buttonNavigator.onNextRelease([this] {
        selectedIndex = ButtonNavigator::nextIndex(selectedIndex, kInxDayBars + 1);
        requestUpdate();
      });
      buttonNavigator.onPreviousRelease([this] {
        selectedIndex = ButtonNavigator::previousIndex(selectedIndex, kInxDayBars + 1);
        requestUpdate();
      });
    }
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
    if (GUI.usesPaperStyle()) selectedIndex = 0;
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
  if (GUI.usesPaperStyle()) {
    renderPaper();
    return;
  }
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
  const int pad = 20;
  const int contentW = screenWidth - pad * 2;
  const int smallH = renderer.getLineHeight(SMALL_FONT_ID);
  const int ui10H = renderer.getLineHeight(UI_10_FONT_ID);
  const int ui12H = renderer.getLineHeight(UI_12_FONT_ID);

  int y = content.y + 2;

  // 1. Top Section Header: "阅读统计" + "本月 5.2 小时" + 1px divider line
  renderer.drawText(UI_12_FONT_ID, pad, y, "阅读统计", true, EpdFontFamily::BOLD);

  const uint64_t thisMonthMs = READING_STATS.getRecentReadingMs(30);
  const unsigned long long monthMins = (thisMonthMs + 30000ULL) / 60000ULL;
  char goalBuf[32] = {};
  if (monthMins >= 60) {
    snprintf(goalBuf, sizeof(goalBuf), "本月 %.1f 小时", static_cast<double>(monthMins) / 60.0);
  } else {
    snprintf(goalBuf, sizeof(goalBuf), "本月 %llu 分钟", monthMins);
  }
  const int goalW = renderer.getTextWidth(SMALL_FONT_ID, goalBuf);
  renderer.drawText(SMALL_FONT_ID, pad + contentW - goalW, y + 2, goalBuf);

  y += ui12H + 6;
  renderer.drawLine(pad, y, pad + contentW, y, 1, true);
  y += 14;

  // 2. Stats Trio Summary: 3 rounded cards
  const int chipGap = 12;
  const int chipW = (contentW - chipGap * 2) / 3;
  const int chipH = 88;
  const uint64_t todayMs = READING_STATS.getTodayReadingMs();
  const uint64_t last7dMs = READING_STATS.getRecentReadingMs(7);
  const uint64_t monthMs = READING_STATS.getRecentReadingMs(30);

  // Today Card
  {
    const Rect r{pad, y, chipW, chipH};
    InxInkCards::drawCard(renderer, r, 6);
    const char* label = "今日";
    const int lw = renderer.getTextWidth(SMALL_FONT_ID, label);
    renderer.drawText(SMALL_FONT_ID, r.x + (r.width - lw) / 2, r.y + 12, label);

    char numBuf[16] = {};
    const unsigned long long todayMins = (todayMs + 30000ULL) / 60000ULL;
    snprintf(numBuf, sizeof(numBuf), "%llu", todayMins);
    const int nw = renderer.getTextWidth(NOTOSERIF_14_FONT_ID, numBuf, EpdFontFamily::BOLD);
    const int uw = renderer.getTextWidth(SMALL_FONT_ID, "分");
    const int totalW = nw + 2 + uw;
    const int startX = r.x + (r.width - totalW) / 2;
    const int valY = r.y + 36;
    renderer.drawText(NOTOSERIF_14_FONT_ID, startX, valY, numBuf, true, EpdFontFamily::BOLD);
    renderer.drawText(SMALL_FONT_ID, startX + nw + 2, valY + (renderer.getLineHeight(NOTOSERIF_14_FONT_ID) - smallH), "分");
  }

  // Last 7 Days Card
  {
    const Rect r{pad + chipW + chipGap, y, chipW, chipH};
    InxInkCards::drawCard(renderer, r, 6);
    const char* label = "近 7 天";
    const int lw = renderer.getTextWidth(SMALL_FONT_ID, label);
    renderer.drawText(SMALL_FONT_ID, r.x + (r.width - lw) / 2, r.y + 12, label);

    char numBuf[16] = {};
    const unsigned long long mins = (last7dMs + 30000ULL) / 60000ULL;
    snprintf(numBuf, sizeof(numBuf), "%llu", mins);
    const int nw = renderer.getTextWidth(NOTOSERIF_14_FONT_ID, numBuf, EpdFontFamily::BOLD);
    const int uw = renderer.getTextWidth(SMALL_FONT_ID, "分");
    const int totalW = nw + 2 + uw;
    const int startX = r.x + (r.width - totalW) / 2;
    const int valY = r.y + 36;
    renderer.drawText(NOTOSERIF_14_FONT_ID, startX, valY, numBuf, true, EpdFontFamily::BOLD);
    renderer.drawText(SMALL_FONT_ID, startX + nw + 2, valY + (renderer.getLineHeight(NOTOSERIF_14_FONT_ID) - smallH), "分");
  }

  // Month Card
  {
    const Rect r{pad + (chipW + chipGap) * 2, y, chipW, chipH};
    InxInkCards::drawCard(renderer, r, 6);
    const char* label = "本月";
    const int lw = renderer.getTextWidth(SMALL_FONT_ID, label);
    renderer.drawText(SMALL_FONT_ID, r.x + (r.width - lw) / 2, r.y + 12, label);

    char numBuf[16] = {};
    const unsigned long long mins = (monthMs + 30000ULL) / 60000ULL;
    const char* unit = "分";
    if (mins >= 60) {
      const double hrs = static_cast<double>(mins) / 60.0;
      snprintf(numBuf, sizeof(numBuf), "%.1f", hrs);
      unit = "时";
    } else {
      snprintf(numBuf, sizeof(numBuf), "%llu", mins);
    }
    const int nw = renderer.getTextWidth(NOTOSERIF_14_FONT_ID, numBuf, EpdFontFamily::BOLD);
    const int uw = renderer.getTextWidth(SMALL_FONT_ID, unit);
    const int totalW = nw + 2 + uw;
    const int startX = r.x + (r.width - totalW) / 2;
    const int valY = r.y + 36;
    renderer.drawText(NOTOSERIF_14_FONT_ID, startX, valY, numBuf, true, EpdFontFamily::BOLD);
    renderer.drawText(SMALL_FONT_ID, startX + nw + 2, valY + (renderer.getLineHeight(NOTOSERIF_14_FONT_ID) - smallH), unit);
  }

  y += chipH + 16;

  // 3. Bar Chart Box ("近 7 天（分钟）" + "日均 17.7 分钟")
  const int chartH = 240;
  const Rect chart{pad, y, contentW, chartH};
  InxInkCards::drawCard(renderer, chart, 8);

  const int innerPad = 16;
  const int headerY = chart.y + innerPad;
  renderer.drawText(UI_10_FONT_ID, chart.x + innerPad, headerY, "近 7 天（分钟）", true, EpdFontFamily::BOLD);

  const double avgMins = static_cast<double>((last7dMs + 30000ULL) / 60000ULL) / 7.0;
  char avgBuf[32] = {};
  snprintf(avgBuf, sizeof(avgBuf), "日均 %.1f 分钟", avgMins);
  const int avgW = renderer.getTextWidth(SMALL_FONT_ID, avgBuf);
  renderer.drawText(SMALL_FONT_ID, chart.x + chart.width - innerPad - avgW, headerY + 1, avgBuf);

  const auto& days = READING_STATS.getReadingDays();
  uint32_t refDay = TimeUtils::getLocalDayOrdinal(READING_STATS.getDisplayTimestamp());
  if (refDay == 0 && !days.empty()) refDay = days.back().dayOrdinal;
  uint64_t maxMs = 60000ULL * 10;
  uint64_t dayMs[kInxDayBars] = {};
  char dayLabel[kInxDayBars][8] = {};
  for (int i = 0; i < kInxDayBars; ++i) {
    const uint32_t ordinal = (refDay >= static_cast<uint32_t>(6 - i)) ? refDay - static_cast<uint32_t>(6 - i) : 0;
    dayBarOrdinal_[i] = ordinal;
    int year = 0;
    unsigned month = 0;
    unsigned day = 0;
    if (ordinal != 0 && TimeUtils::getDateFromDayOrdinal(ordinal, year, month, day)) {
      snprintf(dayLabel[i], sizeof(dayLabel[i]), "%u日", day);
    }
    for (const auto& entry : days) {
      if (entry.dayOrdinal == ordinal) {
        dayMs[i] = entry.readingMs;
        if (entry.readingMs > maxMs) maxMs = entry.readingMs;
        break;
      }
    }
  }

  const int plotTop = headerY + ui10H + 24;
  const int plotBottom = chart.y + chart.height - innerPad - smallH - 6;
  const int plotH = plotBottom - plotTop;
  const int barW = 22;
  const int chartInnerW = chart.width - innerPad * 2;
  const int barGap = (chartInnerW - barW * kInxDayBars) / (kInxDayBars - 1);
  const int barStartX = chart.x + innerPad;

  renderer.drawLine(chart.x + innerPad, plotBottom, chart.x + chart.width - innerPad, plotBottom, 1, true);

  for (int i = 0; i < kInxDayBars; ++i) {
    const int barX = barStartX + i * (barW + barGap);
    dayBarHit_[i] = Rect{barX - barGap / 2, plotTop - 16, barW + barGap, plotH + 32};
    const bool isToday = (i == kInxDayBars - 1);

    if (dayMs[i] > 0) {
      int barH = static_cast<int>(dayMs[i] * static_cast<uint64_t>(plotH) / maxMs);
      if (barH < 6) barH = 6;
      if (barH > plotH) barH = plotH;
      renderer.fillRect(barX, plotBottom - barH, barW, barH, true);
      if (isToday) {
        renderer.drawRect(barX - 2, plotBottom - barH - 2, barW + 4, barH + 2);
      }
      char valBuf[8] = {};
      const unsigned long long mins = (dayMs[i] + 30000ULL) / 60000ULL;
      snprintf(valBuf, sizeof(valBuf), "%llu", mins > 0 ? mins : 1ULL);
      const int tw = renderer.getTextWidth(UI_10_FONT_ID, valBuf, EpdFontFamily::BOLD);
      renderer.drawText(UI_10_FONT_ID, barX + (barW - tw) / 2, plotBottom - barH - ui10H - 2, valBuf, true, EpdFontFamily::BOLD);
    } else {
      const int dw = renderer.getTextWidth(SMALL_FONT_ID, "—");
      renderer.drawText(SMALL_FONT_ID, barX + (barW - dw) / 2, plotBottom - smallH - 2, "—");
    }

    const char* dateStr = isToday ? "今日" : dayLabel[i];
    const auto dateStyle = isToday ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
    const int dw = renderer.getTextWidth(SMALL_FONT_ID, dateStr, dateStyle);
    const int dateX = barX + (barW - dw) / 2;
    const int dateY = plotBottom + 6;
    renderer.drawText(SMALL_FONT_ID, dateX, dateY, dateStr, true, dateStyle);
    if (isToday) {
      renderer.drawLine(dateX, dateY + smallH, dateX + dw, dateY + smallH, 2, true);
    }
  }

  y += chartH + 16;

  // 4. Reading Timeline Box ("今日阅读时段" + "峰值 21–23 点")
  const int timelineBoxH = 110;
  const Rect timelineCard{pad, y, contentW, timelineBoxH};
  InxInkCards::drawCard(renderer, timelineCard, 8);

  const int timeHeaderY = timelineCard.y + 14;
  renderer.drawText(UI_10_FONT_ID, timelineCard.x + innerPad, timeHeaderY, "今日阅读时段", true, EpdFontFamily::BOLD);

  uint64_t maxHourMs = 0;
  uint8_t peakHour = 0;
  const uint32_t today = TimeUtils::getLocalDayOrdinal(READING_STATS.getDisplayTimestamp());
  for (uint8_t h = 0; h < 24; ++h) {
    const uint64_t hMs = READING_STATS.getDayHourReadingMs(today, h);
    if (hMs > maxHourMs) {
      maxHourMs = hMs;
      peakHour = h;
    }
  }
  if (maxHourMs > 0) {
    char peakBuf[32] = {};
    const uint8_t nextHour = (peakHour + 1 < 24) ? peakHour + 1 : 24;
    const uint64_t nextHourMs = (nextHour < 24) ? READING_STATS.getDayHourReadingMs(today, nextHour) : 0;
    if (nextHourMs >= maxHourMs / 2 && nextHourMs >= 60000ULL * 15) {
      const uint8_t endHour = (peakHour + 2 <= 24) ? peakHour + 2 : 24;
      snprintf(peakBuf, sizeof(peakBuf), "峰值 %02u:00–%02u:00", peakHour, endHour);
    } else {
      snprintf(peakBuf, sizeof(peakBuf), "峰值 %02u:00–%02u:00", peakHour, nextHour);
    }
    const int peakW = renderer.getTextWidth(SMALL_FONT_ID, peakBuf);
    renderer.drawText(SMALL_FONT_ID, timelineCard.x + timelineCard.width - innerPad - peakW, timeHeaderY + 1, peakBuf);
  }
  const int trackY = timeHeaderY + ui10H + 8;
  const int trackW = timelineCard.width - innerPad * 2;
  const int trackH = 16;
  const int trackX = timelineCard.x + innerPad;

  // Pure white track with 1px black outline (No muddy gray dithering on e-ink!)
  renderer.fillRoundedRect(trackX, trackY, trackW, trackH, 4, Color::White);
  renderer.drawRoundedRect(trackX, trackY, trackW, trackH, 1, 4, true);

  for (int h = 0; h < 24; ++h) {
    const int segX = trackX + h * trackW / 24;
    const int nextX = trackX + (h + 1) * trackW / 24;
    const int segW = std::max(1, nextX - segX);
    const uint64_t hMs = READING_STATS.getDayHourReadingMs(today, static_cast<uint8_t>(h));
    const unsigned mins = static_cast<unsigned>((hMs + 30000ULL) / 60000ULL);

    if (mins >= 30) {
      // Heavy reading (>= 30 mins): full solid black block
      renderer.fillRect(segX, trackY + 1, segW, trackH - 2, true);
    } else if (mins >= 10) {
      // Moderate reading (10..29 mins): 8px height central block
      const int barH = 8;
      renderer.fillRect(segX, trackY + (trackH - barH) / 2, segW, barH, true);
    } else if (mins > 0) {
      // Light reading (1..9 mins): subtle 4px central tick
      const int barH = 4;
      renderer.fillRect(segX, trackY + (trackH - barH) / 2, segW, barH, true);
    } else if (h > 0) {
      // Subtle 1px notch separating empty hours
      renderer.drawLine(segX, trackY + 3, segX, trackY + trackH - 4, 1, true);
    }
  }
  const int lblY = trackY + trackH + 6;
  static const char* const kLabels[5] = {"00:00", "06:00", "12:00", "18:00", "23:59"};
  static const int kHourFraction[5] = {0, 6, 12, 18, 24};
  for (int t = 0; t < 5; ++t) {
    const int lw = renderer.getTextWidth(SMALL_FONT_ID, kLabels[t]);
    int lx = trackX + kHourFraction[t] * trackW / 24 - lw / 2;
    if (t == 0) lx = trackX + 2;
    if (t == 4) lx = trackX + trackW - lw - 2;
    renderer.drawText(SMALL_FONT_ID, lx, lblY, kLabels[t]);
  }
  y += timelineBoxH + 14;

  // 5. Daypart Breakdown Card ("时段阅读明细" - 4-Quadrant clean summary)
  const int daypartCardH = 138;
  const Rect daypartCard{pad, y, contentW, daypartCardH};
  InxInkCards::drawCard(renderer, daypartCard, 8);

  const int dpHeaderY = daypartCard.y + 12;
  renderer.drawText(UI_10_FONT_ID, daypartCard.x + innerPad, dpHeaderY, "时段阅读明细", true, EpdFontFamily::BOLD);

  uint64_t dayparts[ReadingStatsAnalytics::DAYPART_COUNT] = {};
  ReadingStatsAnalytics::getDayDaypartMs(today, dayparts);

  const int gridTop = dpHeaderY + ui10H + 10;
  const int colW = (daypartCard.width - innerPad * 2) / 2;
  const int rowH = (daypartCard.y + daypartCard.height - innerPad - gridTop) / 2;

  // Subtle cross dividers inside the card
  renderer.drawLine(daypartCard.x + innerPad, gridTop + rowH, daypartCard.x + daypartCard.width - innerPad, gridTop + rowH, 1, false);
  renderer.drawLine(daypartCard.x + innerPad + colW, gridTop + 2, daypartCard.x + innerPad + colW, daypartCard.y + daypartCard.height - innerPad - 2, 1, false);

  for (int i = 0; i < ReadingStatsAnalytics::DAYPART_COUNT; ++i) {
    const int col = i % 2;
    const int row = i / 2;
    const int cellX = daypartCard.x + innerPad + col * colW + (col == 1 ? 12 : 0);
    const int cellY = gridTop + row * rowH + (rowH - ui10H) / 2;

    char durStr[24] = {};
    ReadingStatsAnalytics::formatDurationLabel(dayparts[i], durStr, sizeof(durStr));
    const int durW = renderer.getTextWidth(UI_10_FONT_ID, durStr, EpdFontFamily::BOLD);

    // Category label in clear, prominent UI_10_FONT_ID bold
    const char* catName = daypartLabel(i);
    renderer.drawText(UI_10_FONT_ID, cellX, cellY, catName, true, EpdFontFamily::BOLD);
    const int catW = renderer.getTextWidth(UI_10_FONT_ID, catName, EpdFontFamily::BOLD);

    // Time range in smaller font next to it
    char hourRange[16] = {};
    snprintf(hourRange, sizeof(hourRange), " %s", kDaypartHours[i]);
    renderer.drawText(SMALL_FONT_ID, cellX + catW + 2, cellY + (ui10H - smallH), hourRange);

    // Duration in UI_10_FONT_ID bold on the right
    renderer.drawText(UI_10_FONT_ID, cellX + colW - (col == 1 ? 12 : 8) - durW, cellY, durStr, true, EpdFontFamily::BOLD);
  }
  const auto labels = mainTabButtonLabels(tr(STR_BACK), tr(STR_MORE), false);
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

void ReadingStatsActivity::renderPaper() {
  renderer.clearScreen();
  const auto& m = GUI.paperMetrics();
  const auto& metrics = UITheme::getInstance().getMetrics();
  drawPageHeader(Rect{0, metrics.topPadding, renderer.getScreenWidth(), metrics.headerHeight}, tr(STR_PAPER_JOURNAL));
  const Rect content = UITheme::getInstance().getMainTabContentRect(renderer);
  const GfxRenderer::ClipScope clip(renderer, content.x, content.y, content.width, content.height);
  const int x = content.x + m.padding;
  const int width = content.width - m.padding * 2;
  const bool wide = content.width > content.height;
  const int textFont = wide ? m.smallFont : m.bookFont;
  const int textH = renderer.getLineHeight(textFont);
  const int numberH = renderer.getLineHeight(m.numberFont);
  const int focus = selectedIndex;
  const bool showFocus = showMainTabContentSelection();
  uint64_t dayMs[kInxDayBars] = {};
  uint64_t totalMs = 0;
  uint64_t maxMs = 60000;
  unsigned readDays = 0;
  int peak = 0;
  const auto& days = READING_STATS.getReadingDays();
  uint32_t reference = TimeUtils::getLocalDayOrdinal(READING_STATS.getDisplayTimestamp());
  if (reference == 0 && !days.empty()) reference = days.back().dayOrdinal;
  for (int i = 0; i < kInxDayBars; ++i) {
    const uint32_t ordinal = reference >= static_cast<uint32_t>(6 - i) ? reference - (6 - i) : 0;
    dayBarOrdinal_[i] = ordinal;
    dayBarHit_[i] = Rect{};
    for (const auto& entry : days) {
      if (entry.dayOrdinal == ordinal && ordinal != 0) {
        dayMs[i] = entry.readingMs;
        break;
      }
    }
    totalMs += dayMs[i];
    if (dayMs[i] > 0) ++readDays;
    if (dayMs[i] > maxMs) maxMs = dayMs[i];
    if (dayMs[i] > dayMs[peak]) peak = i;
  }
  bookPreviewCount_ = 0;
  char range[32] = {};
  if (reference >= 6) {
    int year;
    unsigned month, day, endMonth, endDay;
    TimeUtils::getDateFromDayOrdinal(reference - 6, year, month, day);
    TimeUtils::getDateFromDayOrdinal(reference, year, endMonth, endDay);
    snprintf(range, sizeof(range), "%02u.%02u - %02u.%02u", month, day, endMonth, endDay);
  }
  GUI.drawPaperStatus(renderer, Rect{content.x, content.y, content.width, m.statusHeight});
  const int top = GUI.drawPaperHeading(renderer, Rect{x, content.y + m.statusHeight, width, m.headingHeight},
                                       tr(STR_PAPER_JOURNAL), range) +
                  16;
  const int actionHeight = std::max(44, textH + 12);
  moreHitRect_ = Rect{x, content.y + content.height - actionHeight - 6, width, actionHeight};
  const int summaryWidth = (width - m.gap) * 3 / 5;
  GUI.drawPaperText(renderer, Rect{x, top, summaryWidth, textH}, textFont, tr(STR_PAPER_LAST_WEEK));
  char minutes[24] = {};
  formatPaperMinutes(totalMs, minutes, sizeof(minutes));
  const int valueY = top + textH + 6;
  const int valueW = std::min(summaryWidth, renderer.getTextWidth(m.numberFont, minutes, EpdFontFamily::BOLD));
  GUI.drawPaperText(renderer, Rect{x, valueY, valueW, numberH}, m.numberFont, minutes, true);
  if (valueW + 12 < summaryWidth) {
    GUI.drawPaperText(renderer, Rect{x + valueW + 10, valueY + numberH - textH, summaryWidth - valueW - 10, textH},
                      textFont, tr(STR_MINUTES_UNIT));
  }
  const int sideX = x + summaryWidth + m.gap;
  const int sideW = width - summaryWidth - m.gap;
  char summary[64] = {};
  char todayMinutes[24] = {};
  formatPaperMinutes(dayMs[6], todayMinutes, sizeof(todayMinutes));
  snprintf(summary, sizeof(summary), tr(STR_PAPER_TODAY_MIN_FMT), todayMinutes);
  const int todayLines = renderer.getTextWidth(textFont, summary) > sideW ? 2 : 1;
  const int sideHeight = (todayLines + 1) * textH + 12;
  const int summaryHeight = std::max(textH + 6 + numberH, sideHeight);
  const int sideY = top + (summaryHeight - sideHeight) / 2;
  GUI.drawPaperText(renderer, Rect{sideX, sideY, sideW, todayLines * textH}, textFont, summary, false, todayLines);
  snprintf(summary, sizeof(summary), tr(STR_PAPER_DAYS_SHORT_FMT), readDays);
  GUI.drawPaperText(renderer, Rect{sideX, sideY + todayLines * textH + 12, sideW, textH}, textFont, summary);
  const int summaryBottom = top + summaryHeight;
  const int chartX = x;
  const int chartW = width;
  const int chartY = summaryBottom + 18;
  const int lowerReserve = wide ? 8 : textH * 2 + 80 + (totalMs ? textH + 12 : 0);
  const int chartH = std::min(wide ? 140 : 260, std::max(100, moreHitRect_.y - chartY - lowerReserve));
  GUI.drawPaperText(renderer, Rect{chartX, chartY, chartW, textH}, textFont, tr(STR_PAPER_RHYTHM), true);
  const int plotTop = chartY + textH + textH + 16;
  const int plotBottom = chartY + chartH - textH - 8;
  const int plotH = std::max(1, plotBottom - plotTop);
  for (int i = 0; i < kInxDayBars; ++i) {
    const int left = chartX + chartW * i / kInxDayBars;
    const int right = chartX + chartW * (i + 1) / kInxDayBars;
    const int barW = std::min(34, (right - left) * 3 / 5);
    const int barX = left + (right - left - barW) / 2;
    const int barH = dayMs[i] ? std::max(2, static_cast<int>(dayMs[i] * plotH / maxMs)) : 0;
    if (barH > 0) GUI.drawPaperBar(renderer, Rect{barX, plotBottom - barH, barW, barH}, true);
    char value[16] = {};
    formatPaperMinutes(dayMs[i], value, sizeof(value));
    const int valueWidth = std::min(right - left - 4, renderer.getTextWidth(textFont, value));
    GUI.drawPaperText(renderer,
                      Rect{left + (right - left - valueWidth) / 2, plotBottom - barH - textH - 4, valueWidth, textH},
                      textFont, value);
    char date[12] = {};
    int year;
    unsigned month, day;
    if (dayBarOrdinal_[i]) {
      TimeUtils::getDateFromDayOrdinal(dayBarOrdinal_[i], year, month, day);
      snprintf(date, sizeof(date), "%u", day);
    } else {
      snprintf(date, sizeof(date), "-");
    }
    const char* label = i == 6 ? tr(STR_PAPER_TODAY) : date;
    const int labelWidth = std::min(right - left - 4, renderer.getTextWidth(textFont, label));
    GUI.drawPaperText(renderer, Rect{left + (right - left - labelWidth) / 2, plotBottom + 6, labelWidth, textH},
                      textFont, label, i == 6);
    if (i == 6)
      GUI.drawPaperRule(renderer, Rect{left + (right - left - labelWidth) / 2, plotBottom + textH + 7, labelWidth, 2},
                        2);
    dayBarHit_[i] = Rect{left, chartY + textH + 4, right - left, chartH - textH - 4};
    if (showFocus && focus == i + 1) GUI.drawPaperFocus(renderer, dayBarHit_[i]);
  }
  GUI.drawPaperRule(renderer, Rect{chartX, plotBottom, chartW, 1});
  int y = chartY + chartH + 12;
  if (totalMs && y + textH <= moreHitRect_.y - 8) {
    char peakText[96] = {};
    int year;
    unsigned month, day;
    TimeUtils::getDateFromDayOrdinal(dayBarOrdinal_[peak], year, month, day);
    char peakMinutes[24] = {};
    formatPaperMinutes(dayMs[peak], peakMinutes, sizeof(peakMinutes));
    snprintf(peakText, sizeof(peakText), tr(STR_PAPER_PEAK_DAY_FMT), month, day, peakMinutes);
    y = GUI.drawPaperText(renderer, Rect{x, y, width, textH}, textFont, peakText) + 12;
  }
  if (!wide && y + textH + textH + 58 <= moreHitRect_.y - 8) {
    GUI.drawPaperRule(renderer, Rect{x, y, width, 1});
    y += 10;
    y = GUI.drawPaperText(renderer, Rect{x, y, width, textH}, textFont, tr(STR_TODAY_READING_DAYPART), true) + 8;
    const int trackY = y;
    GUI.drawPaperRule(renderer, Rect{x, trackY + 10, width, 1});
    for (int h = 0; h < 24; ++h) {
      const int left = x + width * h / 24;
      const int right = x + width * (h + 1) / 24;
      const bool active = READING_STATS.getDayHourReadingMs(reference, h) > 0;
      GUI.drawPaperBar(renderer, Rect{left, trackY + 8, 1, 5}, true);
      if (active) GUI.drawPaperBar(renderer, Rect{left + 1, trackY, std::max(1, right - left - 2), 22}, true);
    }
    y += 28;
    static constexpr const char* ticks[] = {"00", "06", "12", "18", "24"};
    for (int i = 0; i < 5; ++i) {
      const int tickW = renderer.getTextWidth(textFont, ticks[i]);
      const int tickX = std::clamp(x + width * i / 4 - tickW / 2, x, x + width - tickW);
      GUI.drawPaperText(renderer, Rect{tickX, y, tickW, textH}, textFont, ticks[i]);
    }
    y += textH + 8;
  }
  GUI.drawPaperAction(renderer, moreHitRect_, tr(STR_MORE_DETAILS), showFocus && focus == 0);
  const auto labels = mainTabButtonLabels(tr(STR_BACK), tr(STR_SELECT), true);
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
