#include "ReadingStatsExtendedActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <string>
#include <vector>

#include "AppMetricCard.h"
#include "ReadingStatsDetailActivity.h"
#include "ReadingStatsStore.h"
#include "components/UITheme.h"
#include "components/themes/inx/InxInkCards.h"
#include "fontIds.h"
#include "util/HeaderDateUtils.h"
#include "util/ReadingStatsAnalytics.h"
#include "util/TimeUtils.h"

namespace {
constexpr int SUMMARY_CARD_HEIGHT = 76;
constexpr int SUMMARY_GAP = 10;
constexpr int RECENT_CARD_HEIGHT = SUMMARY_CARD_HEIGHT;
constexpr int CHART_HEADER_HEIGHT = 42;  // taller so the subheader text clears its underline
constexpr int CHART_HEIGHT = 180;
constexpr int CHART_TOP_GAP = 10;
constexpr int CHART_BOTTOM_GAP = 10;
constexpr int CHART_SECTION_GAP = 16;
constexpr int CHART_SCROLL_STEP = 110;
constexpr int INX_PAD = 14;
constexpr int INX_GOAL_H = 108;
constexpr int INX_BOOK_ROW_H = 110;
constexpr int INX_LIST_GAP = 12;

const char* bookTitleOf(const ReadingBookStats& book) {
  return book.title.empty() ? book.path.c_str() : book.title.c_str();
}

void drawDurationColumn(const GfxRenderer& renderer, const int rightX, const int y, const uint64_t ms) {
  char number[8];
  bool hours = false;
  ReadingStatsAnalytics::formatDurationParts(ms, number, sizeof(number), hours);
  const char* unit = hours ? tr(STR_HOURS_UNIT) : tr(STR_MINUTES_UNIT);
  const int numW = renderer.getTextWidth(UI_12_FONT_ID, number, EpdFontFamily::BOLD);
  const int unitW = renderer.getTextWidth(SMALL_FONT_ID, unit);
  renderer.drawText(UI_12_FONT_ID, rightX - numW, y, number, true, EpdFontFamily::BOLD);
  renderer.drawText(SMALL_FONT_ID, rightX - unitW, y + renderer.getLineHeight(UI_12_FONT_ID) + 2, unit);
}

int inxListInnerHeight(const GfxRenderer& renderer) {
  const Rect content = UITheme::getInstance().getMainTabContentRect(renderer);
  const int contentTop = content.y + 8;
  const int contentBottom = content.y + content.height - 8;
  const int availListH = contentBottom - (contentTop + INX_GOAL_H + INX_LIST_GAP);
  const int innerAvailH = std::max(1, availListH - 16);
  const int visibleFullRows = std::max(1, innerAvailH / INX_BOOK_ROW_H);
  return visibleFullRows * INX_BOOK_ROW_H;
}

int inxMaxBookScroll(const GfxRenderer& renderer) {
  const int contentH = static_cast<int>(READING_STATS.getBooks().size()) * INX_BOOK_ROW_H;
  return std::max(0, contentH - inxListInnerHeight(renderer));
}

struct ChartBar {
  std::string bottomLabel;
  std::string topLabel;
  uint64_t readingMs = 0;
};

void drawMetricCard(const GfxRenderer& renderer, const Rect& rect, const char* label, const std::string& value,
                    const bool showCheck = false) {
  AppMetricCard::Options options;
  options.showCheck = showCheck;
  AppMetricCard::draw(renderer, rect, label, value, options);
}

void drawRecentWindowCard(const GfxRenderer& renderer, const Rect& rect, const char* periodLabel,
                          const std::string& value) {
  drawMetricCard(renderer, rect, periodLabel, value);
}

void civilFromDays(int z, int& year, unsigned& month, unsigned& day) {
  z += 719468;
  const int era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = static_cast<unsigned>(z - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  year = static_cast<int>(yoe) + era * 400;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  day = doy - (153 * mp + 2) / 5 + 1;
  month = mp + (mp < 10 ? 3 : -9);
  year += (month <= 2);
}

std::string formatMinutesLabel(const uint64_t readingMs) {
  const uint64_t totalMinutes = readingMs / 60000ULL;
  if (totalMinutes == 0) {
    return "";
  }
  return std::to_string(totalMinutes) + "m";
}

std::string formatRoundedDurationLabel(const uint64_t readingMs) {
  if (readingMs == 0) {
    return "";
  }

  const uint64_t totalMinutes = readingMs / 60000ULL;
  if (totalMinutes < 60ULL) {
    return std::to_string(std::max<uint64_t>(1, totalMinutes)) + "m";
  }

  const uint64_t totalHours = (readingMs + (30ULL * 60ULL * 1000ULL)) / (60ULL * 60ULL * 1000ULL);
  if (totalHours < 24ULL) {
    return std::to_string(std::max<uint64_t>(1, totalHours)) + "h";
  }

  const uint64_t totalDays = (readingMs + (12ULL * 60ULL * 60ULL * 1000ULL)) / (24ULL * 60ULL * 60ULL * 1000ULL);
  return std::to_string(std::max<uint64_t>(1, totalDays)) + "d";
}

std::string formatDayLabel(const uint32_t dayOrdinal) {
  if (dayOrdinal == 0) {
    return "";
  }

  int year = 0;
  unsigned month = 0;
  unsigned day = 0;
  civilFromDays(static_cast<int>(dayOrdinal), year, month, day);

  char buffer[16];
  snprintf(buffer, sizeof(buffer), "%02u/%02u", day, month);
  return buffer;
}

std::string formatMonthLabel(const unsigned month) {
  char buffer[8];
  snprintf(buffer, sizeof(buffer), "%02u", month);
  return buffer;
}

uint32_t getDisplayReferenceDayOrdinal() {
  const uint32_t displayTimestamp = READING_STATS.getDisplayTimestamp();
  if (!TimeUtils::isClockValid(displayTimestamp)) {
    return 0;
  }
  return TimeUtils::getLocalDayOrdinal(displayTimestamp);
}

int resolveReferenceYear(const std::vector<ReadingDayStats>& readingDays) {
  uint32_t referenceDayOrdinal = getDisplayReferenceDayOrdinal();
  if (referenceDayOrdinal == 0 && !readingDays.empty()) {
    referenceDayOrdinal = readingDays.back().dayOrdinal;
  }

  if (referenceDayOrdinal == 0) {
    return 0;
  }

  int year = 0;
  unsigned month = 0;
  unsigned day = 0;
  civilFromDays(static_cast<int>(referenceDayOrdinal), year, month, day);
  return year;
}

std::vector<ChartBar> getRecentDailyReadingBars() {
  std::vector<ChartBar> bars(7);
  const auto& readingDays = READING_STATS.getReadingDays();
  if (readingDays.empty()) {
    return bars;
  }

  uint32_t referenceDayOrdinal = getDisplayReferenceDayOrdinal();
  if (referenceDayOrdinal == 0) {
    referenceDayOrdinal = readingDays.back().dayOrdinal;
  }

  for (int index = 0; index < 7; ++index) {
    const uint32_t dayOrdinal = referenceDayOrdinal >= static_cast<uint32_t>(6 - index)
                                    ? referenceDayOrdinal - static_cast<uint32_t>(6 - index)
                                    : 0;
    bars[index].bottomLabel = formatDayLabel(dayOrdinal);
    for (const auto& day : readingDays) {
      // cppcheck-suppress useStlAlgorithm
      if (day.dayOrdinal == dayOrdinal) {
        bars[index].readingMs = day.readingMs;
        bars[index].topLabel = formatMinutesLabel(day.readingMs);
        break;
      }
    }
  }

  return bars;
}

std::vector<ChartBar> getAnnualReadingBars(int& year) {
  std::vector<ChartBar> bars(12);
  for (unsigned month = 1; month <= 12; ++month) {
    bars[month - 1].bottomLabel = formatMonthLabel(month);
  }

  const auto& readingDays = READING_STATS.getReadingDays();
  year = resolveReferenceYear(readingDays);
  if (year == 0) {
    return bars;
  }

  for (const auto& day : readingDays) {
    int dayYear = 0;
    unsigned dayMonth = 0;
    unsigned dayNumber = 0;
    civilFromDays(static_cast<int>(day.dayOrdinal), dayYear, dayMonth, dayNumber);
    if (dayYear != year || dayMonth == 0 || dayMonth > 12) {
      continue;
    }
    bars[dayMonth - 1].readingMs += day.readingMs;
  }

  for (auto& bar : bars) {
    bar.topLabel = formatRoundedDurationLabel(bar.readingMs);
  }

  return bars;
}

std::string formatAnnualReadingTitle(const int year) {
  if (year <= 0) {
    return tr(STR_ANNUAL_READING);
  }
  return std::string(tr(STR_ANNUAL_READING)) + " (" + std::to_string(year) + ")";
}

int getScrollableContentBottom(const GfxRenderer&, const ThemeMetrics&) {
  return CHART_HEADER_HEIGHT + CHART_TOP_GAP + CHART_HEIGHT + CHART_SECTION_GAP + CHART_HEADER_HEIGHT + CHART_TOP_GAP +
         CHART_HEIGHT;
}

int getMaxScrollOffset(const GfxRenderer& renderer, const ThemeMetrics& metrics) {
  const int summaryTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int recentTop = summaryTop + SUMMARY_CARD_HEIGHT * 3 + SUMMARY_GAP * 2 + metrics.verticalSpacing;
  const int chartViewportTop = recentTop + RECENT_CARD_HEIGHT + metrics.verticalSpacing;
  const int visibleHeight =
      renderer.getScreenHeight() - metrics.buttonHintsHeight - CHART_BOTTOM_GAP - chartViewportTop;
  return std::max(0, getScrollableContentBottom(renderer, metrics) - visibleHeight);
}

void drawReadingChart(const GfxRenderer& renderer, const Rect& rect, const std::vector<ChartBar>& bars,
                      const bool rotateBottomLabels) {
  if (bars.empty()) {
    return;
  }

  const int innerLeft = rect.x + 14;
  const int innerRight = rect.x + rect.width - 14;
  const int topLabelY = rect.y + 2;
  const int chartTop = rect.y + 30;
  const int bottomGap = rotateBottomLabels ? 12 : 10;
  // Non-rotated (annual) labels get extra reserved height so they lift clear of the
  // viewport bottom / button hints instead of sitting flush against it.
  const int bottomLabelAreaHeight = rotateBottomLabels ? 40 : 28;
  const int baselineY = rect.y + rect.height - bottomLabelAreaHeight - bottomGap - 2;
  const int bottomLabelY = baselineY + bottomGap;
  const int chartHeight = std::max(1, baselineY - chartTop);

  const int barCount = static_cast<int>(bars.size());
  const int barGap = barCount <= 7 ? 7 : 4;
  const int minBarWidth = barCount <= 7 ? 12 : 8;
  const int barWidth = std::max(minBarWidth, (innerRight - innerLeft - barGap * (barCount - 1)) / barCount);
  const int usedWidth = barWidth * barCount + barGap * (barCount - 1);
  const int chartLeft = rect.x + (rect.width - usedWidth) / 2;
  uint64_t maxValue = 1;
  for (const auto& bar : bars) {
    // cppcheck-suppress useStlAlgorithm
    maxValue = std::max(maxValue, bar.readingMs);
  }

  renderer.drawLine(innerLeft - 2, baselineY, innerRight + 2, baselineY, 2, true);

  for (int index = 0; index < barCount; ++index) {
    const int barX = chartLeft + index * (barWidth + barGap);
    const uint64_t readingMs = bars[index].readingMs;
    if (!bars[index].topLabel.empty()) {
      const int labelWidth = renderer.getTextWidth(SMALL_FONT_ID, bars[index].topLabel.c_str(), EpdFontFamily::REGULAR);
      renderer.drawText(SMALL_FONT_ID, barX + (barWidth - labelWidth) / 2, topLabelY, bars[index].topLabel.c_str());
    }

    int barHeight = static_cast<int>((readingMs * chartHeight) / maxValue);
    if (readingMs > 0 && barHeight < 6) {
      barHeight = 6;
    }

    const int barY = baselineY - barHeight;
    if (barHeight > 0) {
      renderer.fillRectDither(barX + 1, barY + 1, std::max(0, barWidth - 2), std::max(0, barHeight - 2),
                              Color::LightGray);
      renderer.drawRect(barX, barY, barWidth, barHeight);
    } else {
      renderer.drawLine(barX, baselineY - 1, barX + barWidth, baselineY - 1);
    }

    if (bars[index].bottomLabel.empty()) {
      continue;
    }

    if (rotateBottomLabels) {
      const int labelWidth =
          renderer.getTextWidth(SMALL_FONT_ID, bars[index].bottomLabel.c_str(), EpdFontFamily::REGULAR);
      const int rotatedX = barX + (barWidth - renderer.getTextHeight(SMALL_FONT_ID)) / 2;
      const int rotatedY = bottomLabelY + (bottomLabelAreaHeight + labelWidth) / 2;
      renderer.drawTextRotated90CW(SMALL_FONT_ID, rotatedX, rotatedY, bars[index].bottomLabel.c_str());
    } else {
      const int labelWidth =
          renderer.getTextWidth(SMALL_FONT_ID, bars[index].bottomLabel.c_str(), EpdFontFamily::REGULAR);
      renderer.drawText(SMALL_FONT_ID, barX + (barWidth - labelWidth) / 2, bottomLabelY + 2,
                        bars[index].bottomLabel.c_str());
    }
  }
}
}  // namespace

void ReadingStatsExtendedActivity::onEnter() {
  Activity::onEnter();
  scrollOffset = 0;
  selectedIndex = 0;
  waitForConfirmRelease = mappedInput.isPressed(MappedInputManager::Button::Confirm);
  waitForBackRelease = false;
  requestUpdate();
}

bool ReadingStatsExtendedActivity::usesInxLayout() const { return UITheme::getInstance().hasMainTabs(); }

void ReadingStatsExtendedActivity::openSelectedBook() {
  const auto& books = READING_STATS.getBooks();
  if (selectedIndex < 0 || selectedIndex >= static_cast<int>(books.size())) return;
  startActivityForResultWith<ReadingStatsDetailActivity>(
      [this](const ActivityResult&) {
        waitForBackRelease = true;
        requestUpdate();
      },
      books[selectedIndex].path);
}

void ReadingStatsExtendedActivity::loopInx() {
  const int maxScrollOffset = inxMaxBookScroll(renderer);
  scrollOffset = std::clamp(scrollOffset, 0, maxScrollOffset);
  const auto scrollBy = [this, maxScrollOffset](const int delta) {
    const int nextOffset = std::clamp(scrollOffset + delta, 0, maxScrollOffset);
    if (nextOffset != scrollOffset) {
      scrollOffset = nextOffset;
      requestUpdate();
    }
  };

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    openSelectedBook();
    return;
  }

  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Up) {
    scrollBy(INX_BOOK_ROW_H);
    return;
  }
  if (swipe == MappedInputManager::SwipeDir::Down) {
    scrollBy(-INX_BOOK_ROW_H);
    return;
  }

  int touchX = 0;
  int touchY = 0;
  if (mappedInput.wasScreenTapped(touchX, touchY)) {
    if (touchY < 50 && touchX < 140) {
      finish();
      return;
    }
    if (touchX >= bookListRect_.x && touchY >= bookListInnerTop_ && touchX < bookListRect_.x + bookListRect_.width &&
        touchY < bookListRect_.y + bookListRect_.height) {
      const int localY = touchY - bookListInnerTop_ + scrollOffset;
      if (bookRowHeight_ > 0 && localY >= 0) {
        const int index = localY / bookRowHeight_;
        if (index >= 0 && index < static_cast<int>(READING_STATS.getBooks().size())) {
          selectedIndex = index;
          openSelectedBook();
        }
      }
    }
    return;
  }

  buttonNavigator.onPreviousRelease([&]() { scrollBy(-INX_BOOK_ROW_H); });
  buttonNavigator.onNextRelease([&]() { scrollBy(INX_BOOK_ROW_H); });
}

void ReadingStatsExtendedActivity::loop() {
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

  if (usesInxLayout()) {
    loopInx();
    return;
  }

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int maxScrollOffset = getMaxScrollOffset(renderer, metrics);
  const auto scrollBy = [this, maxScrollOffset](const int delta) {
    const int nextOffset = std::clamp(scrollOffset + delta, 0, maxScrollOffset);
    if (nextOffset != scrollOffset) {
      scrollOffset = nextOffset;
      requestUpdate();
    }
  };

  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Up) {
    scrollBy(CHART_SCROLL_STEP);
    return;
  }
  if (swipe == MappedInputManager::SwipeDir::Down) {
    scrollBy(-CHART_SCROLL_STEP);
    return;
  }

  buttonNavigator.onPreviousRelease([&]() { scrollBy(-CHART_SCROLL_STEP); });

  buttonNavigator.onNextRelease([&]() { scrollBy(CHART_SCROLL_STEP); });
}

void ReadingStatsExtendedActivity::render(RenderLock&&) {
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
  const int recentTop = summaryTop + SUMMARY_CARD_HEIGHT * 3 + SUMMARY_GAP * 2 + metrics.verticalSpacing;
  const int chartViewportTop = recentTop + RECENT_CARD_HEIGHT + metrics.verticalSpacing;
  const int chartViewportBottom = renderer.getScreenHeight() - metrics.buttonHintsHeight - CHART_BOTTOM_GAP;
  const int maxScrollOffset = getMaxScrollOffset(renderer, metrics);
  scrollOffset = std::clamp(scrollOffset, 0, maxScrollOffset);
  const int dailyChartHeaderTop = chartViewportTop - scrollOffset;
  const int dailyChartTop = dailyChartHeaderTop + CHART_HEADER_HEIGHT + CHART_TOP_GAP;
  const int annualChartHeaderTop = dailyChartTop + CHART_HEIGHT + CHART_SECTION_GAP;
  const int annualChartTop = annualChartHeaderTop + CHART_HEADER_HEIGHT + CHART_TOP_GAP;

  const std::string last7DaysValue = ReadingStatsAnalytics::formatDurationHm(READING_STATS.getRecentReadingMs(7));
  const std::string last30DaysValue = ReadingStatsAnalytics::formatDurationHm(READING_STATS.getRecentReadingMs(30));
  const uint64_t todayReadingMs = READING_STATS.getTodayReadingMs();
  const std::string dailyGoalValue = ReadingStatsAnalytics::formatDurationHm(todayReadingMs) + " / " +
                                     ReadingStatsAnalytics::formatDurationHm(getDailyReadingGoalMs());
  int annualReadingYear = 0;
  const auto annualReadingBars = getAnnualReadingBars(annualReadingYear);

  {
    // Clip the scrolling charts to their viewport so headers/bars/baselines that
    // scroll past the screen edges are dropped silently instead of spamming
    // "Outside range". Masks below stay outside the scope (clip already cleared).
    const GfxRenderer::ClipScope clip(renderer, 0, chartViewportTop, pageWidth, chartViewportBottom - chartViewportTop);

    GUI.drawSubHeader(renderer, Rect{0, dailyChartHeaderTop, pageWidth, CHART_HEADER_HEIGHT}, tr(STR_DAILY_READING),
                      nullptr);
    drawReadingChart(renderer, Rect{sidePadding, dailyChartTop, pageWidth - sidePadding * 2, CHART_HEIGHT},
                     getRecentDailyReadingBars(), true);

    const std::string annualReadingTitle = formatAnnualReadingTitle(annualReadingYear);
    GUI.drawSubHeader(renderer, Rect{0, annualChartHeaderTop, pageWidth, CHART_HEADER_HEIGHT},
                      annualReadingTitle.c_str(), nullptr);
    drawReadingChart(renderer, Rect{sidePadding, annualChartTop, pageWidth - sidePadding * 2, CHART_HEIGHT},
                     annualReadingBars, false);
  }

  renderer.fillRect(0, 0, pageWidth, chartViewportTop, false);
  if (chartViewportBottom < renderer.getScreenHeight()) {
    renderer.fillRect(0, chartViewportBottom, pageWidth, renderer.getScreenHeight() - chartViewportBottom, false);
  }

  HeaderDateUtils::drawHeaderWithDate(renderer, tr(STR_READING_STATS), tr(STR_MORE_DETAILS));

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

  drawRecentWindowCard(renderer, Rect{sidePadding, recentTop, cardWidth, RECENT_CARD_HEIGHT}, "7D", last7DaysValue);
  drawRecentWindowCard(renderer, Rect{sidePadding + cardWidth + SUMMARY_GAP, recentTop, cardWidth, RECENT_CARD_HEIGHT},
                       "30D", last30DaysValue);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", scrollOffset > 0 ? tr(STR_DIR_UP) : "",
                                            scrollOffset < maxScrollOffset ? tr(STR_DIR_DOWN) : "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

void ReadingStatsExtendedActivity::renderInx() {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int screenWidth = renderer.getScreenWidth();
  const int pad = metrics.contentSidePadding > 0 ? metrics.contentSidePadding : 16;

  const auto& allBooks = READING_STATS.getBooks();
  unsigned validBookCount = 0;
  for (const auto& b : allBooks) {
    if (b.path.find("crash_report") == std::string::npos && b.path.find(".log") == std::string::npos) {
      ++validBookCount;
    }
  }

  // 1. Top Header Bar (Cleanly centered, ASCII < button)
  constexpr int kHeaderY = 16;
  renderer.drawText(UI_10_FONT_ID, pad, kHeaderY + 2, "< 返回");

  const char* pageTitle = "阅读档案与目标";
  const int titleW = renderer.getTextWidth(UI_12_FONT_ID, pageTitle, EpdFontFamily::BOLD);
  renderer.drawText(UI_12_FONT_ID, (screenWidth - titleW) / 2, kHeaderY, pageTitle, true, EpdFontFamily::BOLD);

  char cntBuf[32] = {};
  snprintf(cntBuf, sizeof(cntBuf), "共 %u 本在读", validBookCount);
  const int cntW = renderer.getTextWidth(UI_10_FONT_ID, cntBuf);
  renderer.drawText(UI_10_FONT_ID, screenWidth - pad - cntW, kHeaderY + 2, cntBuf);

  // Header bottom divider line
  renderer.drawLine(pad, kHeaderY + 34, screenWidth - pad, kHeaderY + 34);

  const int tH12 = renderer.getLineHeight(UI_12_FONT_ID);
  const int tHSmall = renderer.getLineHeight(SMALL_FONT_ID);

  // 2. Goal Card (每日目标)
  constexpr int kGoalTop = 60;
  constexpr int kGoalCardH = 104;
  const Rect goal{pad, kGoalTop, screenWidth - pad * 2, kGoalCardH};
  renderer.fillRect(goal.x, goal.y, goal.width, goal.height, false);
  renderer.drawRect(goal.x, goal.y, goal.width, goal.height);

  const uint64_t todayMs = READING_STATS.getTodayReadingMs();
  const uint64_t goalMs = getDailyReadingGoalMs();
  char goalBuf[64] = {};
  snprintf(goalBuf, sizeof(goalBuf), "已完成 %s / %s",
           ReadingStatsAnalytics::formatDurationHm(todayMs).c_str(),
           ReadingStatsAnalytics::formatDurationHm(goalMs).c_str());
  if (todayMs >= goalMs && goalMs > 0) {
    strncat(goalBuf, "  [今日已达标]", sizeof(goalBuf) - strlen(goalBuf) - 1);
  }

  // Row 1: "每日目标" (bold) + completion status
  const int row1Y = goal.y + 12;
  renderer.drawText(UI_12_FONT_ID, goal.x + 14, row1Y, tr(STR_DAILY_GOAL), true, EpdFontFamily::BOLD);
  const int goalBufW = renderer.getTextWidth(SMALL_FONT_ID, goalBuf);
  renderer.drawText(SMALL_FONT_ID, goal.x + goal.width - 14 - goalBufW, row1Y + (tH12 - tHSmall) / 2, goalBuf);

  // Row 2: Progress bar (strictly below Row 1 text with 10px clearance)
  const uint8_t goalPercent = goalMs == 0 ? 0 : static_cast<uint8_t>(std::min<uint64_t>(100, todayMs * 100ULL / goalMs));
  const int barY = row1Y + tH12 + 10;
  InxInkCards::drawHairProgress(renderer, Rect{goal.x + 14, barY, goal.width - 28, 6}, goalPercent);

  // Row 3: Streak stats (strictly below progress bar with 10px clearance)
  char streakLine[32];
  snprintf(streakLine, sizeof(streakLine), tr(STR_STREAK_DAYS_FMT), static_cast<int>(READING_STATS.getCurrentStreakDays()));
  char maxStreakLine[32];
  snprintf(maxStreakLine, sizeof(maxStreakLine), tr(STR_MAX_STREAK_DAYS_FMT), static_cast<int>(READING_STATS.getMaxStreakDays()));
  const int streakY = barY + 6 + 10;
  renderer.drawText(SMALL_FONT_ID, goal.x + 14, streakY, streakLine);
  const int maxW = renderer.getTextWidth(SMALL_FONT_ID, maxStreakLine);
  renderer.drawText(SMALL_FONT_ID, goal.x + goal.width - 14 - maxW, streakY, maxStreakLine);

  // 3. Book Records Card (累计阅读书目)
  const int listTop = goal.y + goal.height + 12;
  const int bottomReserve = metrics.buttonHintsHeight + 8;
  const int listH = renderer.getScreenHeight() - listTop - bottomReserve;
  const Rect list{pad, listTop, screenWidth - pad * 2, std::max(120, listH)};
  renderer.fillRect(list.x, list.y, list.width, list.height, false);
  renderer.drawRect(list.x, list.y, list.width, list.height);

  // Header row inside list card
  const int listHeadY = list.y + 10;
  renderer.drawText(SMALL_FONT_ID, list.x + 14, listHeadY, "累计阅读书目 (已过滤系统日志)");
  const int sortW = renderer.getTextWidth(SMALL_FONT_ID, "按时长排序");
  renderer.drawText(SMALL_FONT_ID, list.x + list.width - 14 - sortW, listHeadY, "按时长排序");

  // Divider line strictly below header text (8px gap)
  const int headLineY = listHeadY + tHSmall + 8;
  renderer.drawLine(list.x, headLineY, list.x + list.width - 1, headLineY);

  bookListRect_ = list;
  constexpr int kRowH = 88;
  bookRowHeight_ = kRowH;
  bookListInnerTop_ = headLineY + 1;
  const int innerH = list.height - (bookListInnerTop_ - list.y) - 4;

  const auto& books = READING_STATS.getBooks();
  const int maxScrollOffset = std::max(0, static_cast<int>(books.size()) * kRowH - innerH);
  scrollOffset = std::clamp(scrollOffset, 0, maxScrollOffset);

  if (books.empty()) {
    renderer.drawText(UI_10_FONT_ID, list.x + 14, bookListInnerTop_ + 14, tr(STR_NO_READING_STATS));
  } else {
    const GfxRenderer::ClipScope clip(renderer, list.x + 2, bookListInnerTop_, list.width - 4, innerH);
    for (int index = 0; index < static_cast<int>(books.size()); ++index) {
      const int rowY = bookListInnerTop_ - scrollOffset + index * kRowH;
      if (rowY + kRowH <= bookListInnerTop_ || rowY > bookListInnerTop_ + innerH) continue;
      if (index > 0 && rowY > bookListInnerTop_) {
        renderer.drawLine(list.x + 8, rowY, list.x + list.width - 8, rowY);
      }

      const ReadingBookStats& book = books[index];
      if (book.path.find("crash_report") != std::string::npos || book.path.find(".log") != std::string::npos) {
        continue;
      }

      const int textX = list.x + 14;
      const char* bTitle = bookTitleOf(book);

      // Line 1: Title + Duration (bold)
      const int line1Y = rowY + 10;
      const std::string durFull = ReadingStatsAnalytics::formatDurationHm(book.totalReadingMs);
      const int durW = renderer.getTextWidth(UI_12_FONT_ID, durFull.c_str(), EpdFontFamily::BOLD);
      const int titleMaxW = list.width - 28 - durW - 12;

      const std::string cutTitle = renderer.truncatedText(UI_12_FONT_ID, bTitle, titleMaxW, EpdFontFamily::BOLD);
      renderer.drawText(UI_12_FONT_ID, textX, line1Y, cutTitle.c_str(), true, EpdFontFamily::BOLD);
      renderer.drawText(UI_12_FONT_ID, list.x + list.width - 14 - durW, line1Y, durFull.c_str(), true, EpdFontFamily::BOLD);

      // Line 2: Author · Chapter · Sessions + Progress % (strictly below line1Y + tH12)
      const int line2Y = line1Y + tH12 + 4;
      char subLine[96] = {};
      const char* auth = book.author.empty() ? "" : book.author.c_str();
      const char* ch = book.chapterTitle.empty() ? "" : book.chapterTitle.c_str();
      if (*auth && *ch) {
        snprintf(subLine, sizeof(subLine), "%s · %s · %u次", auth, ch, book.sessions);
      } else if (*ch) {
        snprintf(subLine, sizeof(subLine), "%s · %u次", ch, book.sessions);
      } else {
        snprintf(subLine, sizeof(subLine), "%u次翻阅", book.sessions);
      }

      char pctBuf[16] = {};
      snprintf(pctBuf, sizeof(pctBuf), "全书 %u%%", static_cast<unsigned>(book.lastProgressPercent));
      const int pctW = renderer.getTextWidth(SMALL_FONT_ID, pctBuf);
      const int subMaxW = list.width - 28 - pctW - 8;
      const std::string cutSub = renderer.truncatedText(SMALL_FONT_ID, subLine, subMaxW);

      renderer.drawText(SMALL_FONT_ID, textX, line2Y, cutSub.c_str());
      renderer.drawText(SMALL_FONT_ID, list.x + list.width - 14 - pctW, line2Y, pctBuf);

      // Line 3: Progress bar (strictly below line2Y + tHSmall)
      const int line3Y = line2Y + tHSmall + 6;
      const int barW = list.width - 28;
      GUI.drawPaperProgress(renderer, Rect{textX, line3Y, barW, 4}, book.lastProgressPercent);
    }
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), books.empty() ? "" : tr(STR_SELECT),
                                            scrollOffset > 0 ? tr(STR_DIR_UP) : "",
                                            scrollOffset < maxScrollOffset ? tr(STR_DIR_DOWN) : "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
