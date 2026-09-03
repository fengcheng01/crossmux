#pragma once

// Shared month-grid renderer for the Chinese calendar (standby face and the
// CALENDAR sleep screen). Monday-first 6x7 cells: solar day over lunar day,
// today inverted. Lunar text requires the CN almanac tables.

#include <GfxRenderer.h>
#include <Logging.h>

#include <cstddef>
#include <ctime>

#include "ChineseAlmanac.h"
#include "components/UITheme.h"
#include "components/themes/BaseTheme.h"  // Rect
#include "fontIds.h"
#include "util/TimeUtils.h"

// Festival names for the month-grid lunar line (CN-only renderer, so
// hardcoded Chinese matches the surrounding kWEEKDAYS/lunar tables).
// Lunar festivals key on (lunar month, lunar day); solar ones on the
// Gregorian date. Two-to-three-char names keep the cell layout intact.
inline const char* lunarFestivalFor(const unsigned lunarMonth, const unsigned lunarDay) {
  struct Entry {
    unsigned m, d;
    const char* name;
  };
  static constexpr Entry kFestivals[] = {
      {1, 1, "春节"},  {1, 15, "元宵"}, {2, 2, "龙抬头"}, {5, 5, "端午"},  {7, 7, "七夕"},
      {7, 15, "中元"}, {8, 15, "中秋"}, {9, 9, "重阳"},   {12, 8, "腊八"}, {12, 23, "小年"},
  };
  for (const auto& e : kFestivals) {
    if (e.m == lunarMonth && e.d == lunarDay) return e.name;
  }
  return nullptr;
}

inline const char* solarFestivalFor(const unsigned month, const unsigned day) {
  struct Entry {
    unsigned m, d;
    const char* name;
  };
  static constexpr Entry kFestivals[] = {
      {1, 1, "元旦"},   {3, 8, "妇女节"}, {3, 12, "植树节"}, {5, 1, "劳动节"}, {5, 4, "青年节"},
      {6, 1, "儿童节"}, {7, 1, "建党节"}, {8, 1, "建军节"},  {9, 10, "教师节"}, {10, 1, "国庆节"},
      {12, 25, "圣诞"},
  };
  for (const auto& e : kFestivals) {
    if (e.m == month && e.d == day) return e.name;
  }
  return nullptr;
}

inline void drawChineseMonthGrid(GfxRenderer& renderer, const Rect& viewport, const int year, const unsigned month,
                                 const bool reserveHeaderSpace = true) {
  const auto localToday = []() -> std::tm {
    struct tm out{};
    const uint32_t now = TimeUtils::getCurrentValidTimestamp();
    if (now != 0) TimeUtils::getLocalDateTime(now, out);
    return out;
  };
  struct tm base = localToday();
  const int64_t todayOrdinal = TimeUtils::getDayOrdinalForDate(base.tm_year + 1900, base.tm_mon + 1, base.tm_mday);
  // Delta from today (negative = past); resolves through the unsigned day-ordinal space.
  const auto dateFromOffset = [&](int64_t dayOffset, struct tm& out) -> bool {
    const int64_t ordinal = todayOrdinal + dayOffset;
    if (ordinal < 0) return false;
    int y = 0;
    unsigned m = 0;
    unsigned d = 0;
    if (!TimeUtils::getDateFromDayOrdinal(static_cast<uint32_t>(ordinal), y, m, d)) return false;
    out = {};
    out.tm_year = y - 1900;
    out.tm_mon = static_cast<int>(m) - 1;
    out.tm_mday = static_cast<int>(d);
    return true;
  };

  // Anchor on the 1st of the viewed month: its weekday sets the grid offset.
  const int64_t firstOrdinalFromToday =
      TimeUtils::getDayOrdinalForDate(year, month, 1) -
      TimeUtils::getDayOrdinalForDate(base.tm_year + 1900, base.tm_mon + 1, base.tm_mday);
  struct tm firstTm{};
  if (!dateFromOffset(firstOrdinalFromToday, firstTm)) {
    return;
  }
  AlmanacDay firstAlmanac{};
  if (!computeAlmanac(firstTm, firstAlmanac)) return;
  const int firstCol = (static_cast<int>(firstAlmanac.weekdayIdx) + 6) % 7;  // Monday-first
  const unsigned daysInMonth = TimeUtils::getDaysInMonth(year, month);
  const int gridRows = (firstCol + static_cast<int>(daysInMonth) + 6) / 7;

  const bool viewIsCurrentMonth = year == firstTm.tm_year + 1900 && month == static_cast<unsigned>(firstTm.tm_mon + 1);

  // In Normal mode the standby header (面名) overlays the top of the
  // viewport — start the grid below it so the year-month title never
  // collides with the face title. Callers without that header (the CALENDAR
  // lock screen) pass reserveHeaderSpace=false; the old unconditional inset
  // left a ~90px blank band under the lock header.
  const auto& themeMetrics = UITheme::getInstance().getMetrics();
  const int contentTop =
      reserveHeaderSpace ? viewport.y + themeMetrics.topPadding + themeMetrics.headerHeight + 4 : viewport.y + 6;

  char title[24] = {};
  std::snprintf(title, sizeof(title), "%d年%u月", year, month);
  const int titleW = renderer.getTextWidth(UI_12_FONT_ID, title, EpdFontFamily::BOLD);
  const int pad = 12;
  renderer.drawText(UI_12_FONT_ID, viewport.x + (viewport.width - titleW) / 2, contentTop, title, true,
                    EpdFontFamily::BOLD);

  static const char* const kWEEKDAYS[7] = {"一", "二", "三", "四", "五", "六", "日"};
  const int weekdayRowH = renderer.getLineHeight(SMALL_FONT_ID);
  const int gridTop = contentTop + renderer.getLineHeight(UI_12_FONT_ID) + 6;
  const int cellW = (viewport.width - pad * 2) / 7;
  // Cells start a full weekday-row below the weekday labels — the old
  // gridTop+4 put the day digits inside the label band (overlap).
  const int gridBodyTop = gridTop + weekdayRowH + 6;
  const int cellH = std::max(36, (viewport.y + viewport.height - gridBodyTop - 8) / gridRows);

  for (int col = 0; col < 7; ++col) {
    const int w = renderer.getTextWidth(SMALL_FONT_ID, kWEEKDAYS[col], EpdFontFamily::BOLD);
    renderer.drawText(SMALL_FONT_ID, viewport.x + pad + col * cellW + (cellW - w) / 2, gridTop, kWEEKDAYS[col], true,
                      EpdFontFamily::BOLD);
  }

  LOG_INF("CAL", "month grid %d-%u: todayOrd=%lld firstOff=%lld days=%u firstCol=%d rows=%d", year, month,
          static_cast<long long>(todayOrdinal), static_cast<long long>(firstOrdinalFromToday), daysInMonth, firstCol,
          gridRows);
  int cellsDrawn = 0;
  for (int day = 1; day <= static_cast<int>(daysInMonth); ++day) {
    const int index = firstCol + day - 1;
    const int row = index / 7;
    const int col = index % 7;
    const bool isToday = viewIsCurrentMonth && static_cast<unsigned>(day) == static_cast<unsigned>(base.tm_mday);

    struct tm cellTm{};
    // Day N sits N-1 days after the 1st; the 1st's offset from today is
    // (firstOrdinal - todayOrdinal) — usually negative within the month.
    if (!dateFromOffset(static_cast<int64_t>(day - 1) + firstOrdinalFromToday, cellTm)) {
      continue;
    }
    AlmanacDay almanac{};
    const bool hasLunar = computeAlmanac(cellTm, almanac);

    const Rect cell{viewport.x + pad + col * cellW, gridBodyTop + row * cellH, cellW, cellH};
    const bool inverted = isToday;
    if (inverted) {
      renderer.fillRect(cell.x + 2, cell.y + 2, cell.width - 4, cell.height - 4);
    }

    char dayBuf[4] = {};
    std::snprintf(dayBuf, sizeof(dayBuf), "%d", day);
    const int dayW = renderer.getTextWidth(UI_10_FONT_ID, dayBuf, EpdFontFamily::BOLD);
    // Center the two text lines in the cell instead of pinning to the top.
    const int dayLineH = renderer.getLineHeight(UI_10_FONT_ID);
    const int lunarLineH = renderer.getLineHeight(SMALL_FONT_ID);
    const int textTop = cell.y + std::max(4, (cellH - dayLineH - lunarLineH) / 2);
    renderer.drawText(UI_10_FONT_ID, cell.x + (cellW - dayW) / 2, textTop, dayBuf, !inverted, EpdFontFamily::BOLD);

    if (hasLunar) {
      // Festival first (lunar, then solar), lunar month name on the first day
      // of a lunar month, lunar day otherwise.
      char lunarBuf[16] = {};
      const char* festival = lunarFestivalFor(almanac.lunarMonth, almanac.lunarDay);
      if (!festival) festival = solarFestivalFor(static_cast<unsigned>(cellTm.tm_mon + 1), static_cast<unsigned>(cellTm.tm_mday));
      if (festival) {
        std::snprintf(lunarBuf, sizeof(lunarBuf), "%s", festival);
      } else if (almanac.lunarDay == 1) {
        std::snprintf(lunarBuf, sizeof(lunarBuf), "%s%s", almanac.lunarLeap ? "闰" : "",
                      chinese_almanac::kLunarMonthNames[(almanac.lunarMonth - 1) % 12]);
      } else {
        std::snprintf(lunarBuf, sizeof(lunarBuf), "%s", chinese_almanac::kLunarDayNames[(almanac.lunarDay - 1) % 30]);
      }
      const int lunarW = renderer.getTextWidth(SMALL_FONT_ID, lunarBuf);
      renderer.drawText(SMALL_FONT_ID, cell.x + (cellW - lunarW) / 2, textTop + dayLineH, lunarBuf, !inverted);
    }
  }
  LOG_INF("CAL", "month grid: cells drawn %d", cellsDrawn);
}
