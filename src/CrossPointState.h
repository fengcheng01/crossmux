#pragma once
#include <ArduinoJson.h>
#include <PersistableStore.h>

#include <cstdint>
#include <string>

class CrossPointState : public PersistableStore<CrossPointState> {
  CrossPointState() = default;

  friend class PersistableStore<CrossPointState>;

 public:
  static constexpr uint8_t SLEEP_RECENT_COUNT = 16;

  std::string openEpubPath;
  uint16_t recentSleepImages[SLEEP_RECENT_COUNT] = {};  // circular buffer of recent wallpaper indices
  uint8_t recentSleepPos = 0;                           // next write slot
  uint8_t recentSleepFill = 0;                          // valid entries (0..SLEEP_RECENT_COUNT)
  uint16_t recentOverlaySleepImages[SLEEP_RECENT_COUNT] = {};
  uint8_t recentOverlaySleepPos = 0;
  uint8_t recentOverlaySleepFill = 0;
  uint8_t readerActivityLoadCount = 0;
  bool lastSleepFromReader = false;
  bool showBootScreen = true;
  // True while the CLOCK sleep screen is showing; timer wake redraws it, power
  // wake clears this and resumes the previous activity.
  bool clockSleepActive = false;
  // Most recent epoch timestamp the device trusted as valid. Used by the
  // Reading Analytics suite to bucket reading time into days when the live
  // clock is temporarily unavailable (RTC-less boots before NTP sync).
  uint32_t lastKnownValidTimestamp = 0;
  // Standby 传统日历 face: 1 = month-grid view was last shown (persisted so
  // the face reopens the way the user left it).
  uint8_t standbyCalendarMonthView = 0;

  static const char* getFilePath() { return "/.crosspoint/state.json"; }
  bool loadFromFile();
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);

  // Returns true if idx was shown within the last checkCount picks.
  // Walks backwards from the most recently written slot.
  bool isRecentSleep(uint16_t idx, uint8_t checkCount) const;
  bool isRecentOverlaySleep(uint16_t idx, uint8_t checkCount) const;

  void pushRecentSleep(uint16_t idx);
  void pushRecentOverlaySleep(uint16_t idx);

 private:
  bool loadFromBinaryFile();
};

// Helper macro to access state
#define APP_STATE CrossPointState::getInstance()
