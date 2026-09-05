#include "ReadingStatsAnalytics.h"

#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <ctime>

#include "util/TimeUtils.h"

namespace ReadingStatsAnalytics {
namespace {
constexpr uint64_t MIN_READING_DAY_BOOK_MS = 3ULL * 60ULL * 1000ULL;

int resolveYearFromTimestamp(const uint32_t timestamp) {
  if (!TimeUtils::isClockValid(timestamp)) {
    return 0;
  }

  tm localTime = {};
  if (!TimeUtils::getLocalDateTime(timestamp, localTime)) return 0;
  return localTime.tm_year + 1900;
}

}  // namespace

std::string formatDurationHm(const uint64_t totalMs) {
  const uint64_t totalMinutes = totalMs / 60000ULL;
  const uint64_t hours = totalMinutes / 60ULL;
  const uint64_t minutes = totalMinutes % 60ULL;
  if (hours == 0) {
    return std::to_string(minutes) + "m";
  }
  return std::to_string(hours) + "h " + std::to_string(minutes) + "m";
}

void formatDurationParts(const uint64_t totalMs, char* number, const size_t numberSize, bool& hours) {
  if (number == nullptr || numberSize == 0) {
    hours = false;
    return;
  }
  const uint64_t minutes = totalMs / 60000ULL;
  if (minutes < 60ULL) {
    hours = false;
    snprintf(number, numberSize, "%llu", static_cast<unsigned long long>(minutes));
    return;
  }
  hours = true;
  const unsigned tenths = static_cast<unsigned>((minutes * 10ULL + 3ULL) / 60ULL);
  snprintf(number, numberSize, "%u.%u", tenths / 10U, tenths % 10U);
}

void formatDurationLabel(const uint64_t totalMs, char* buffer, const size_t bufferSize) {
  if (buffer == nullptr || bufferSize == 0) {
    return;
  }
  const uint64_t minutes = totalMs / 60000ULL;
  if (minutes < 60ULL) {
    snprintf(buffer, bufferSize, "%llu%s", static_cast<unsigned long long>(minutes), tr(STR_MINUTES_UNIT));
    return;
  }
  const uint64_t hours = minutes / 60ULL;
  const uint64_t remainder = minutes % 60ULL;
  if (remainder == 0) {
    snprintf(buffer, bufferSize, "%llu%s", static_cast<unsigned long long>(hours), tr(STR_HOURS_UNIT));
    return;
  }
  snprintf(buffer, bufferSize, "%llu%s%llu%s", static_cast<unsigned long long>(hours), tr(STR_HOURS_UNIT),
           static_cast<unsigned long long>(remainder), tr(STR_MINUTES_UNIT));
}

std::string formatDayOrdinalLabel(const uint32_t dayOrdinal) {
  int year = 0;
  unsigned month = 0;
  unsigned day = 0;
  if (!TimeUtils::getDateFromDayOrdinal(dayOrdinal, year, month, day)) {
    return "";
  }

  return TimeUtils::formatDateParts(year, month, day);
}

std::string formatMonthLabel(const int year, const unsigned month) { return TimeUtils::formatMonthYear(year, month); }

int getReferenceYear() {
  const uint32_t timestamp = READING_STATS.getDisplayTimestamp();
  if (const int year = resolveYearFromTimestamp(timestamp); year != 0) {
    return year;
  }

  if (!READING_STATS.getReadingDays().empty()) {
    int year = 0;
    unsigned month = 0;
    unsigned day = 0;
    if (TimeUtils::getDateFromDayOrdinal(READING_STATS.getReadingDays().back().dayOrdinal, year, month, day)) {
      return year;
    }
  }

  return 2026;
}

std::vector<DayBookEntry> getBooksReadOnDay(const uint32_t dayOrdinal) {
  std::vector<DayBookEntry> entries;
  for (const auto& book : READING_STATS.getBooks()) {
    auto it = std::find_if(book.readingDays.begin(), book.readingDays.end(), [&](const ReadingDayStats& day) {
      return day.dayOrdinal == dayOrdinal && day.readingMs >= MIN_READING_DAY_BOOK_MS;
    });
    if (it == book.readingDays.end()) {
      continue;
    }

    entries.push_back(DayBookEntry{&book, it->readingMs});
  }

  std::sort(entries.begin(), entries.end(), [](const DayBookEntry& left, const DayBookEntry& right) {
    if (left.readingMs != right.readingMs) {
      return left.readingMs > right.readingMs;
    }
    if (!left.book || !right.book) {
      return left.book != nullptr;
    }
    return left.book->title < right.book->title;
  });
  return entries;
}

TimelineDayEntry buildTimelineDayEntry(const uint32_t dayOrdinal) {
  TimelineDayEntry entry;
  entry.dayOrdinal = dayOrdinal;
  for (const auto& day : READING_STATS.getReadingDays()) {
    // cppcheck-suppress useStlAlgorithm
    if (day.dayOrdinal == dayOrdinal) {
      entry.totalReadingMs = day.readingMs;
      break;
    }
  }

  const auto books = getBooksReadOnDay(dayOrdinal);
  entry.booksReadCount = static_cast<uint32_t>(books.size());
  if (!books.empty()) {
    entry.topBook = books.front().book;
    entry.topBookReadingMs = books.front().readingMs;
  }
  return entry;
}

std::vector<TimelineDayEntry> buildTimelineEntries(const size_t maxEntries) {
  std::vector<TimelineDayEntry> entries;
  const auto& readingDays = READING_STATS.getReadingDays();
  entries.reserve(readingDays.size());

  for (auto it = readingDays.rbegin(); it != readingDays.rend(); ++it) {
    if (it->readingMs == 0) {
      continue;
    }
    entries.push_back(buildTimelineDayEntry(it->dayOrdinal));
    if (maxEntries > 0 && entries.size() >= maxEntries) {
      break;
    }
  }
  return entries;
}

int hourToDaypart(const uint8_t hour) {
  if (hour > 23) {
    return -1;
  }
  if (hour >= 5 && hour < 11) {
    return 0;
  }
  if (hour >= 11 && hour < 17) {
    return 1;
  }
  if (hour >= 17 && hour < 21) {
    return 2;
  }
  return 3;
}

void getLifetimeDaypartMs(uint64_t out[DAYPART_COUNT]) {
  if (out == nullptr) {
    return;
  }
  for (int i = 0; i < DAYPART_COUNT; ++i) {
    out[i] = 0;
  }
  for (uint8_t hour = 0; hour < 24; ++hour) {
    const int part = hourToDaypart(hour);
    if (part >= 0) {
      out[part] += READING_STATS.getHourReadingMs(hour);
    }
  }
}

void getDayDaypartMs(const uint32_t dayOrdinal, uint64_t out[DAYPART_COUNT]) {
  if (out == nullptr) {
    return;
  }
  for (int i = 0; i < DAYPART_COUNT; ++i) {
    out[i] = 0;
  }
  if (dayOrdinal == 0) {
    return;
  }
  for (uint8_t hour = 0; hour < 24; ++hour) {
    const uint64_t ms = READING_STATS.getDayHourReadingMs(dayOrdinal, hour);
    const int part = hourToDaypart(hour);
    if (ms > 0 && part >= 0) {
      out[part] += ms;
    }
  }
  if (hasDaypartMs(out)) {
    return;
  }
  for (const auto& session : READING_STATS.getSessionLog()) {
    if (session.dayOrdinal != dayOrdinal) {
      continue;
    }
    const int part = hourToDaypart(session.hour);
    if (part >= 0) {
      out[part] += session.sessionMs;
    }
  }
}

bool hasDaypartMs(const uint64_t dayparts[DAYPART_COUNT]) {
  if (dayparts == nullptr) {
    return false;
  }
  for (int i = 0; i < DAYPART_COUNT; ++i) {
    if (dayparts[i] > 0) {
      return true;
    }
  }
  return false;
}

}  // namespace ReadingStatsAnalytics
