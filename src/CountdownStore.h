#pragma once
#include <ArduinoJson.h>
#include <PersistableStore.h>

#include <cstdint>
#include <string>
#include <vector>

/**
 * Singleton store for the 倒数日 (countdown) events: up to MAX_COUNTDOWNS
 * entries, each a label plus a target date day ordinal
 * (TimeUtils::getDayOrdinalForDate).
 */
class CountdownStore : public PersistableStore<CountdownStore> {
 public:
  static constexpr size_t MAX_COUNTDOWNS = 5;

  struct Entry {
    uint32_t targetDayOrdinal = 0;  // 0 = invalid
    std::string label;
  };

 private:
  std::vector<Entry> entries;

  CountdownStore() = default;
  ~CountdownStore() = default;

  friend class PersistableStore<CountdownStore>;

 public:
  static const char* getFilePath() { return "/.crosspoint/countdown.json"; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);

  size_t count() const { return entries.size(); }
  bool full() const { return entries.size() >= MAX_COUNTDOWNS; }
  const Entry& at(size_t index) const { return entries.at(index); }

  // Appends a new event. Returns false when the list is full or the ordinal is 0.
  bool add(uint32_t dayOrdinal, const std::string& label);
  // Updates an existing event by index. Returns false on a bad index.
  bool update(size_t index, uint32_t dayOrdinal, const std::string& label);
  bool remove(size_t index);
};

#define COUNTDOWN_STORE CountdownStore::getInstance()
