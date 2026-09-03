#include "CountdownStore.h"

#include <Logging.h>

void CountdownStore::toJson(JsonDocument& doc) const {
  JsonArray arr = doc["countdowns"].to<JsonArray>();
  for (const auto& entry : entries) {
    JsonObject obj = arr.add<JsonObject>();
    obj["ordinal"] = entry.targetDayOrdinal;
    obj["label"] = entry.label;
  }
}

bool CountdownStore::fromJson(JsonVariantConst doc) {
  entries.clear();

  // Current format: "countdowns": [{ordinal, label}, ...]
  JsonArrayConst arr = doc["countdowns"].as<JsonArrayConst>();
  for (JsonObjectConst obj : arr) {
    if (entries.size() >= MAX_COUNTDOWNS) break;
    Entry entry;
    entry.targetDayOrdinal = obj["ordinal"] | static_cast<uint32_t>(0);
    entry.label = obj["label"] | "";
    if (entry.targetDayOrdinal != 0) {
      entries.push_back(std::move(entry));
    }
  }

  // Legacy single-event format from the first 倒数日 build (unreleased, but a
  // user store may exist): migrate it into one entry.
  if (entries.empty()) {
    const uint32_t legacyOrdinal = doc["targetDayOrdinal"] | static_cast<uint32_t>(0);
    if (legacyOrdinal != 0) {
      Entry entry;
      entry.targetDayOrdinal = legacyOrdinal;
      entry.label = doc["label"] | "";
      entries.push_back(std::move(entry));
      requestResave();
    }
  }

  return true;
}

bool CountdownStore::add(const uint32_t dayOrdinal, const std::string& label) {
  if (full() || dayOrdinal == 0) {
    LOG_ERR("CNT", "Cannot add countdown (full=%d, ordinal=%u)", full() ? 1 : 0, dayOrdinal);
    return false;
  }
  Entry entry;
  entry.targetDayOrdinal = dayOrdinal;
  entry.label = label;
  entries.push_back(std::move(entry));
  LOG_DBG("CNT", "Countdown added: ordinal=%u label='%s' (%zu total)", dayOrdinal, label.c_str(), entries.size());
  return true;
}

bool CountdownStore::update(const size_t index, const uint32_t dayOrdinal, const std::string& label) {
  if (index >= entries.size()) return false;
  entries[index].targetDayOrdinal = dayOrdinal;
  entries[index].label = label;
  LOG_DBG("CNT", "Countdown %zu updated: ordinal=%u label='%s'", index, dayOrdinal, label.c_str());
  return true;
}

bool CountdownStore::remove(const size_t index) {
  if (index >= entries.size()) return false;
  entries.erase(entries.begin() + static_cast<long>(index));
  LOG_DBG("CNT", "Countdown %zu removed (%zu left)", index, entries.size());
  return true;
}
