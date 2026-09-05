#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ReadingStatsStore.h"

namespace ReadingStatsAnalytics {

struct DayBookEntry {
  const ReadingBookStats* book = nullptr;
  uint64_t readingMs = 0;
};

struct TimelineDayEntry {
  uint32_t dayOrdinal = 0;
  uint64_t totalReadingMs = 0;
  uint32_t booksReadCount = 0;
  const ReadingBookStats* topBook = nullptr;
  uint64_t topBookReadingMs = 0;
};

std::string formatDurationHm(uint64_t totalMs);
// Number + whether it is hours (true) or minutes (false). Stack buffer for
// render paths; avoids std::string on the hot UI frame.
void formatDurationParts(uint64_t totalMs, char* number, size_t numberSize, bool& hours);
// Localized "33分钟" / "1小时10分钟" for chart labels. Stack buffer only.
void formatDurationLabel(uint64_t totalMs, char* buffer, size_t bufferSize);
std::string formatDayOrdinalLabel(uint32_t dayOrdinal);
std::string formatMonthLabel(int year, unsigned month);
int getReferenceYear();
std::vector<DayBookEntry> getBooksReadOnDay(uint32_t dayOrdinal);
TimelineDayEntry buildTimelineDayEntry(uint32_t dayOrdinal);
std::vector<TimelineDayEntry> buildTimelineEntries(size_t maxEntries = 0);

constexpr int DAYPART_COUNT = 4;
// 清晨 05–11 / 午间 11–17 / 晚间 17–21 / 夜间 21–05. Returns -1 when hour is unknown.
int hourToDaypart(uint8_t hour);
void getLifetimeDaypartMs(uint64_t out[DAYPART_COUNT]);
void getDayDaypartMs(uint32_t dayOrdinal, uint64_t out[DAYPART_COUNT]);
bool hasDaypartMs(const uint64_t dayparts[DAYPART_COUNT]);

}  // namespace ReadingStatsAnalytics
