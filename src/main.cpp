#include <Arduino.h>
#include <BoardConfig.h>
#include <Epub.h>
#include <FontCacheManager.h>
#include <FontDecompressor.h>
#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalDisplay.h>
#include <HalEnvironment.h>
#include <HalFrontlight.h>
#include <HalGPIO.h>
#include <HalOtaSlot.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <HalSystem.h>
#include <HalTiltSensor.h>
#include <I18n.h>
#include <Logging.h>
#include <SPI.h>
#include <WiFi.h>
#if FREEINK_DEVICE_X4PRO
#include <XteinkDetect.h>
#endif
#include <builtinFonts/all.h>

#include <cstring>

#include "AchievementsStore.h"
#include "CountdownStore.h"
#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "KOReaderCredentialStore.h"
#include "MappedInputManager.h"
#include "OpdsServerStore.h"
#include "ReadingStatsStore.h"
#include "RecentBooksStore.h"
#include "SdCardFontSystem.h"
#include "activities/Activity.h"
#include "activities/ActivityManager.h"
#include "activities/boot_sleep/SleepActivity.h"
#include "activities/reader/ReaderUtils.h"
#include "activities/settings/SdFirmwareUpdateActivity.h"
#include "activities/usb/UsbTransferActivity.h"
#include "activities/util/PinEntryActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "images/LoadingIcon.h"
#include "usb/UsbMassStorageControl.h"
#include "util/ButtonNavigator.h"
#include "util/ScreenshotUtil.h"
#include "util/TimeUtils.h"

GfxRenderer renderer(display);
MappedInputManager mappedInputManager(gpio, renderer);
ActivityManager activityManager(renderer, mappedInputManager);
FontDecompressor fontDecompressor;
SdCardFontSystem sdFontSystem;
FontCacheManager fontCacheManager(renderer.getFontMap(), renderer.getSdCardFonts());
static unsigned long allowSleepAt = 0;
constexpr unsigned long READING_STATS_CHECKPOINT_IDLE_MS = 15UL * 1000UL;

// Fonts
#ifdef ENABLE_CHINESE_VERSION
// Chinese build: each Latin EpdFont global aliases the matching-size CJK
// header (notosans_cjk_{8,10,12,14,16,18}, raw 2-bit bitmaps). Bold /
// italic / bolditalic variants share the single Regular OTF — bold and
// italic styling are not available for built-in CJK glyphs in this build.
// SD-card fonts continue to provide style variants when loaded.
//
// CJK character coverage is non-uniform across sizes (see
// build-cn-builtin-fonts.sh):
//   - 8/10/12pt: full subset (top-3500 现代汉语常用字表 by Zipf, ∪
//     i18n require-from chars). Sized for reader SMALL and all UI.
//   - 14/16/18pt: i18n-only subset (747 chars from chinese.yaml + feature
//     requirements). UI strings still render, while Chinese EPUB text relies
//     on an SD-card font for characters outside the subset.
EpdFont notoserif14RegularFont(&notosans_cjk_14);
EpdFont notoserif14BoldFont(&notosans_cjk_14);
EpdFont notoserif14ItalicFont(&notosans_cjk_14);
EpdFont notoserif14BoldItalicFont(&notosans_cjk_14);
EpdFontFamily notoserif14FontFamily(&notoserif14RegularFont, &notoserif14BoldFont, &notoserif14ItalicFont,
                                    &notoserif14BoldItalicFont);
#ifndef OMIT_FONTS
EpdFont notoserif12RegularFont(&notosans_cjk_12);
EpdFont notoserif12BoldFont(&notosans_cjk_12);
EpdFont notoserif12ItalicFont(&notosans_cjk_12);
EpdFont notoserif12BoldItalicFont(&notosans_cjk_12);
EpdFontFamily notoserif12FontFamily(&notoserif12RegularFont, &notoserif12BoldFont, &notoserif12ItalicFont,
                                    &notoserif12BoldItalicFont);
EpdFont notoserif16RegularFont(&notosans_cjk_16);
EpdFont notoserif16BoldFont(&notosans_cjk_16);
EpdFont notoserif16ItalicFont(&notosans_cjk_16);
EpdFont notoserif16BoldItalicFont(&notosans_cjk_16);
EpdFontFamily notoserif16FontFamily(&notoserif16RegularFont, &notoserif16BoldFont, &notoserif16ItalicFont,
                                    &notoserif16BoldItalicFont);
EpdFont notoserif18RegularFont(&notosans_cjk_18);
EpdFont notoserif18BoldFont(&notosans_cjk_18);
EpdFont notoserif18ItalicFont(&notosans_cjk_18);
EpdFont notoserif18BoldItalicFont(&notosans_cjk_18);
EpdFontFamily notoserif18FontFamily(&notoserif18RegularFont, &notoserif18BoldFont, &notoserif18ItalicFont,
                                    &notoserif18BoldItalicFont);

EpdFont notosans12RegularFont(&notosans_cjk_12);
EpdFont notosans12BoldFont(&notosans_cjk_12);
EpdFont notosans12ItalicFont(&notosans_cjk_12);
EpdFont notosans12BoldItalicFont(&notosans_cjk_12);
EpdFontFamily notosans12FontFamily(&notosans12RegularFont, &notosans12BoldFont, &notosans12ItalicFont,
                                   &notosans12BoldItalicFont);
EpdFont notosans14RegularFont(&notosans_cjk_14);
EpdFont notosans14BoldFont(&notosans_cjk_14);
EpdFont notosans14ItalicFont(&notosans_cjk_14);
EpdFont notosans14BoldItalicFont(&notosans_cjk_14);
EpdFontFamily notosans14FontFamily(&notosans14RegularFont, &notosans14BoldFont, &notosans14ItalicFont,
                                   &notosans14BoldItalicFont);
EpdFont notosans16RegularFont(&notosans_cjk_16);
EpdFont notosans16BoldFont(&notosans_cjk_16);
EpdFont notosans16ItalicFont(&notosans_cjk_16);
EpdFont notosans16BoldItalicFont(&notosans_cjk_16);
EpdFontFamily notosans16FontFamily(&notosans16RegularFont, &notosans16BoldFont, &notosans16ItalicFont,
                                   &notosans16BoldItalicFont);
EpdFont notosans18RegularFont(&notosans_cjk_18);
EpdFont notosans18BoldFont(&notosans_cjk_18);
EpdFont notosans18ItalicFont(&notosans_cjk_18);
EpdFont notosans18BoldItalicFont(&notosans_cjk_18);
EpdFontFamily notosans18FontFamily(&notosans18RegularFont, &notosans18BoldFont, &notosans18ItalicFont,
                                   &notosans18BoldItalicFont);

// OpenDyslexic 8/10/12/14pt → matching CJK headers.
EpdFont opendyslexic8RegularFont(&notosans_cjk_8);
EpdFont opendyslexic8BoldFont(&notosans_cjk_8);
EpdFont opendyslexic8ItalicFont(&notosans_cjk_8);
EpdFont opendyslexic8BoldItalicFont(&notosans_cjk_8);
EpdFontFamily opendyslexic8FontFamily(&opendyslexic8RegularFont, &opendyslexic8BoldFont, &opendyslexic8ItalicFont,
                                      &opendyslexic8BoldItalicFont);
EpdFont opendyslexic10RegularFont(&notosans_cjk_10);
EpdFont opendyslexic10BoldFont(&notosans_cjk_10);
EpdFont opendyslexic10ItalicFont(&notosans_cjk_10);
EpdFont opendyslexic10BoldItalicFont(&notosans_cjk_10);
EpdFontFamily opendyslexic10FontFamily(&opendyslexic10RegularFont, &opendyslexic10BoldFont, &opendyslexic10ItalicFont,
                                       &opendyslexic10BoldItalicFont);
EpdFont opendyslexic12RegularFont(&notosans_cjk_12);
EpdFont opendyslexic12BoldFont(&notosans_cjk_12);
EpdFont opendyslexic12ItalicFont(&notosans_cjk_12);
EpdFont opendyslexic12BoldItalicFont(&notosans_cjk_12);
EpdFontFamily opendyslexic12FontFamily(&opendyslexic12RegularFont, &opendyslexic12BoldFont, &opendyslexic12ItalicFont,
                                       &opendyslexic12BoldItalicFont);
EpdFont opendyslexic14RegularFont(&notosans_cjk_14);
EpdFont opendyslexic14BoldFont(&notosans_cjk_14);
EpdFont opendyslexic14ItalicFont(&notosans_cjk_14);
EpdFont opendyslexic14BoldItalicFont(&notosans_cjk_14);
EpdFontFamily opendyslexic14FontFamily(&opendyslexic14RegularFont, &opendyslexic14BoldFont, &opendyslexic14ItalicFont,
                                       &opendyslexic14BoldItalicFont);
#endif  // OMIT_FONTS

// smallFont (8pt status text) → 8pt CJK header.
EpdFont smallFont(&notosans_cjk_8);
EpdFontFamily smallFontFamily(&smallFont);

// Keep status digits and symbols pixel-identical to the international build.
EpdFont statusNumericFont(&notosans_8_regular);
EpdFontFamily statusNumericFontFamily(&statusNumericFont);

// UI fonts: 10pt status bar uses the 10pt CJK header so glyphs match the
// surrounding chrome size; 12pt menu uses the 12pt CJK header.
EpdFont ui10RegularFont(&notosans_cjk_10);
EpdFont ui10BoldFont(&notosans_cjk_10);
EpdFontFamily ui10FontFamily(&ui10RegularFont, &ui10BoldFont);

EpdFont ui12RegularFont(&notosans_cjk_12);
EpdFont ui12BoldFont(&notosans_cjk_12);
EpdFontFamily ui12FontFamily(&ui12RegularFont, &ui12BoldFont);
#else  // ENABLE_CHINESE_VERSION
EpdFont notoserif14RegularFont(&notoserif_14_regular);
EpdFont notoserif14BoldFont(&notoserif_14_bold);
EpdFont notoserif14ItalicFont(&notoserif_14_italic);
EpdFont notoserif14BoldItalicFont(&notoserif_14_bolditalic);
EpdFontFamily notoserif14FontFamily(&notoserif14RegularFont, &notoserif14BoldFont, &notoserif14ItalicFont,
                                    &notoserif14BoldItalicFont);
#ifndef OMIT_FONTS
EpdFont notoserif12RegularFont(&notoserif_12_regular);
EpdFont notoserif12BoldFont(&notoserif_12_bold);
EpdFont notoserif12ItalicFont(&notoserif_12_italic);
EpdFont notoserif12BoldItalicFont(&notoserif_12_bolditalic);
EpdFontFamily notoserif12FontFamily(&notoserif12RegularFont, &notoserif12BoldFont, &notoserif12ItalicFont,
                                    &notoserif12BoldItalicFont);
EpdFont notoserif16RegularFont(&notoserif_16_regular);
EpdFont notoserif16BoldFont(&notoserif_16_bold);
EpdFont notoserif16ItalicFont(&notoserif_16_italic);
EpdFont notoserif16BoldItalicFont(&notoserif_16_bolditalic);
EpdFontFamily notoserif16FontFamily(&notoserif16RegularFont, &notoserif16BoldFont, &notoserif16ItalicFont,
                                    &notoserif16BoldItalicFont);
EpdFont notoserif18RegularFont(&notoserif_18_regular);
EpdFont notoserif18BoldFont(&notoserif_18_bold);
EpdFont notoserif18ItalicFont(&notoserif_18_italic);
EpdFont notoserif18BoldItalicFont(&notoserif_18_bolditalic);
EpdFontFamily notoserif18FontFamily(&notoserif18RegularFont, &notoserif18BoldFont, &notoserif18ItalicFont,
                                    &notoserif18BoldItalicFont);

EpdFont notosans12RegularFont(&notosans_12_regular);
EpdFont notosans12BoldFont(&notosans_12_bold);
EpdFont notosans12ItalicFont(&notosans_12_italic);
EpdFont notosans12BoldItalicFont(&notosans_12_bolditalic);
EpdFontFamily notosans12FontFamily(&notosans12RegularFont, &notosans12BoldFont, &notosans12ItalicFont,
                                   &notosans12BoldItalicFont);
EpdFont notosans14RegularFont(&notosans_14_regular);
EpdFont notosans14BoldFont(&notosans_14_bold);
EpdFont notosans14ItalicFont(&notosans_14_italic);
EpdFont notosans14BoldItalicFont(&notosans_14_bolditalic);
EpdFontFamily notosans14FontFamily(&notosans14RegularFont, &notosans14BoldFont, &notosans14ItalicFont,
                                   &notosans14BoldItalicFont);
EpdFont notosans16RegularFont(&notosans_16_regular);
EpdFont notosans16BoldFont(&notosans_16_bold);
EpdFont notosans16ItalicFont(&notosans_16_italic);
EpdFont notosans16BoldItalicFont(&notosans_16_bolditalic);
EpdFontFamily notosans16FontFamily(&notosans16RegularFont, &notosans16BoldFont, &notosans16ItalicFont,
                                   &notosans16BoldItalicFont);
EpdFont notosans18RegularFont(&notosans_18_regular);
EpdFont notosans18BoldFont(&notosans_18_bold);
EpdFont notosans18ItalicFont(&notosans_18_italic);
EpdFont notosans18BoldItalicFont(&notosans_18_bolditalic);
EpdFontFamily notosans18FontFamily(&notosans18RegularFont, &notosans18BoldFont, &notosans18ItalicFont,
                                   &notosans18BoldItalicFont);

#endif  // OMIT_FONTS

EpdFont smallFont(&notosans_8_regular);
EpdFontFamily smallFontFamily(&smallFont);

EpdFont ui10RegularFont(&ubuntu_10_regular);
EpdFont ui10BoldFont(&ubuntu_10_bold);
EpdFontFamily ui10FontFamily(&ui10RegularFont, &ui10BoldFont);

EpdFont ui12RegularFont(&ubuntu_12_regular);
EpdFont ui12BoldFont(&ubuntu_12_bold);
EpdFontFamily ui12FontFamily(&ui12RegularFont, &ui12BoldFont);
#endif  // ENABLE_CHINESE_VERSION

#ifdef ENABLE_CHINESE_VERSION
// Chinese chess piece glyphs (subset CJK font, 14 characters at 16pt).
EpdFont chineseChessPieceFont(&chinese_chess_16);
EpdFontFamily chineseChessPieceFontFamily(&chineseChessPieceFont);
#endif

// measurement of power button press duration calibration value
unsigned long t1 = 0;
unsigned long t2 = 0;

// Definitions for SilentRestart.h. RTC_NOINIT survives ESP.restart() but not power loss.
RTC_NOINIT_ATTR uint32_t silentRebootMagic;
RTC_NOINIT_ATTR uint32_t silentRebootTarget;
constexpr uint32_t SILENT_REBOOT_MAGIC = 0xC1EAB007;
constexpr uint32_t SILENT_REBOOT_TARGET_HOME = 0;
constexpr uint32_t SILENT_REBOOT_TARGET_READER = 1;

// How the device is coming back to life, resolved once at boot. Both resume
// flows suppress the splash and leave the panel holding its pre-boot frame; a
// plain boot shows the splash. See setup() for the resolution.
enum class BootResume : uint8_t {
  Splash,       // cold boot, flash, panic, or plain reboot
  Silent,       // heap-defrag ESP.restart() (RTC flag; lost on power loss)
  QuickResume,  // wake from a quick-resume deep sleep (SD flag; survives power loss)
  ClockUnlock,  // power-wake from the CLOCK sleep screen
};

// Latched true once enterDeepSleep() commits to sleeping, before it tears down
// the current activity. WiFi activities call silentRestart() in onExit() to
// clear heap fragmentation on the way out, but deep sleep is a full chip reset
// on wake and already clears the heap, so rebooting here would just power the
// device back up against the user's sleep gesture. Never cleared:
// startDeepSleep() does not return, so a set latch only ends at the wakeup reset.
static bool deepSleepInProgress = false;

constexpr char SLEEP_FRAME_FILE[] = "/.crosspoint/sleep_frame.bin";

static void saveSleepFrameBuffer() {
  HalFile file;
  if (!Storage.openFileForWrite("SLP", SLEEP_FRAME_FILE, file)) return;
  file.write(renderer.getFrameBuffer(), renderer.getBufferSize());
  file.close();
}

static bool loadSleepFrameBuffer(const bool remove = true) {
  HalFile file;
  if (!Storage.openFileForRead("SLP", SLEEP_FRAME_FILE, file)) return false;
  const size_t bufferSize = display.getBufferSize();
  const size_t bytesRead = file.read(display.getFrameBuffer(), bufferSize);
  file.close();
  if (bytesRead != bufferSize) {
    Storage.remove(SLEEP_FRAME_FILE);
    return false;
  }
  if (remove) Storage.remove(SLEEP_FRAME_FILE);
  return true;
}

void silentRestart() {
  if (deepSleepInProgress) return;  // sleeping supersedes the heap-defrag reboot
  silentRebootTarget = SILENT_REBOOT_TARGET_HOME;
  silentRebootMagic = SILENT_REBOOT_MAGIC;
  LOG_DBG("MAIN", "Silent restart (target=home)");
  // E-ink retains the previous frame until Home's first paint lands (~2-3s).
  // Without an overlay, users don't see the reboot and fire input through to
  // Home. Select on the default selectorIndex=0 then opens the most-recent
  // book, looking like a trampoline back to the reader they just exited.
  GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
  // Seed the post-reboot differential baseline AFTER the popup: drawPopup()
  // refreshes the panel from the framebuffer, so the frame file must be saved
  // once it holds the popup too, or the panel and the saved baseline diverge —
  // Silent-resume's FAST diff then never drives the popup ink away and the
  // eject/loading text ghosts (photo-confirmed 2026-09-03, again on v40safe).
  saveSleepFrameBuffer();
  delay(50);
  ESP.restart();
}

void silentRestartToReader() {
  if (deepSleepInProgress) return;  // sleeping supersedes the heap-defrag reboot
  silentRebootTarget = SILENT_REBOOT_TARGET_READER;
  silentRebootMagic = SILENT_REBOOT_MAGIC;
  LOG_DBG("MAIN", "Silent restart (target=reader)");
  GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
  saveSleepFrameBuffer();  // after the popup — see silentRestart()
  delay(50);
  ESP.restart();
}

void waitForPowerRelease() {
  gpio.update();
  while (gpio.isPressed(HalGPIO::BTN_POWER)) {
    delay(50);
    gpio.update();
  }
}

// The lock-screen password gates wake-from-sleep entry into the system (the
// clock-family unlock and quick-resume boots). Cold boots (Splash), panic
// recovery, and in-session silent restarts stay ungated — the lock is an
// anti-mistouch barrier, not device encryption.
bool lockGateNeeded(const BootResume resume) {
  return SETTINGS.lockScreenPasswordEnabled && SETTINGS.lockScreenPinIsSet != 0 &&
         (resume == BootResume::ClockUnlock || resume == BootResume::QuickResume);
}


// Enter deep sleep mode
uint64_t clockSleepTimerUs() { return static_cast<uint64_t>(TimeUtils::secondsUntilNextLocalMinute()) * 1000000ULL; }

// A clock-less device would wake every minute just to paint a blank face;
// fall back to an hourly re-check until the clock becomes valid.
uint64_t clockSleepWakeUs() { return TimeUtils::isClockValid() ? clockSleepTimerUs() : 60ULL * 60 * 1000000ULL; }

// CALENDAR sleep screen: the grid only changes at midnight, so the tick wake
// is daily instead of per-minute.
uint64_t calendarSleepWakeUs() {
  return TimeUtils::isClockValid() ? static_cast<uint64_t>(TimeUtils::secondsUntilNextLocalMidnight()) * 1000000ULL
                                   : 60ULL * 60 * 1000000ULL;
}

// The desktop shim models deep sleep without the M4 clock-timer argument.
void startDeepSleepWithTimer(HalPowerManager& pm, HalGPIO& gpio, const uint64_t timerUs) {
#if defined(SIMULATOR)
  (void)timerUs;
  pm.startDeepSleep(gpio);
#else
  pm.startDeepSleep(gpio, timerUs);
#endif
}

void enterDeepSleep(bool fromTimeout = false) {
#if defined(FREEINK_CAP_USB_MSC) && FREEINK_CAP_USB_MSC
  // A USB Mass Storage session owns the SD card with the FAT volume detached:
  // every write below would land on a dead filesystem. The activity blocks
  // auto-sleep and the power long-press for the same reason. Sleep after the
  // session ends (eject) — the activity reboots instead of returning here.
  if (usbMscActive()) {
    LOG_INF("SLP", "Deep sleep deferred: USB Mass Storage session is active");
    return;
  }
#endif
  HalPowerManager::Lock powerLock;  // Ensure we are at normal CPU frequency for sleep preparation
  APP_STATE.lastSleepFromReader = activityManager.isReaderActivity();

  const bool isQuickResumeSleep =
      SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::QUICK_RESUME ||
      (fromTimeout &&
       SETTINGS.quickResumeSleepScreen == CrossPointSettings::QUICK_RESUME_SLEEP_SCREEN::QUICK_RESUME_AFTER_TIMEOUT);
  const bool clockSleep = SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::CLOCK;
  const bool calendarSleep = SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::CALENDAR;
  const bool countdownSleep = SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::COUNTDOWN;
  APP_STATE.showBootScreen = !isQuickResumeSleep;
  APP_STATE.clockSleepActive = clockSleep || calendarSleep || countdownSleep;

  APP_STATE.saveToFile();

  // Commit to sleeping before goToSleep() runs the outgoing activity's onExit():
  // a WiFi activity would otherwise silentRestart() here and reboot instead.
  deepSleepInProgress = true;
  activityManager.goToSleep(fromTimeout);

  if (!READING_STATS.saveToFile()) {
    LOG_ERR("RST", "Failed to save reading stats before deep sleep");
  }
  if (!ACHIEVEMENTS.saveToFile()) {
    LOG_ERR("ACH", "Failed to save achievements before deep sleep");
  }

  // Save the unlock frame for every clock-family lock (clock/calendar/
  // countdown): BootResume::ClockUnlock seeds RED from this file so the
  // first FAST home paint drives the lock's ink away. Calendar/countdown
  // were missing here — their unlock diffed against unseeded RAM and the
  // lock screen stayed on as a ghost.
  if (isQuickResumeSleep || APP_STATE.clockSleepActive) {
    saveSleepFrameBuffer();
  }

  // Tear down WiFi so the modem power domain isn't held alive across deep sleep.
  // Wake from deep sleep is effectively a chip reset, so no state needs to survive.
  if (WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
  }

  halTiltSensor.deepSleep();
  Frontlight.setOn(false);
  display.deepSleep();
  LOG_DBG("MAIN", "Entering deep sleep");

  startDeepSleepWithTimer(
      powerManager, gpio,
      calendarSleep || countdownSleep ? calendarSleepWakeUs() : (clockSleep ? clockSleepWakeUs() : 0));
}

bool setupDisplayAndFonts(bool seamless = false, bool skipSdFonts = false, bool deferSdFamilyLoad = false) {
#if FREEINK_DEVICE_X4PRO
  // X4 Pro batches use SSD1677 or UC81xx. Resolve the controller before
  // display.begin(); C3 X3/X4 already do this once in HalGPIO::begin().
  static bool controllerResolved = false;
  if (!controllerResolved) {
    controllerResolved = true;
    freeink::applyXteinkDisplayController();
  }
#endif

  display.begin(seamless);
#if FREEINK_DEVICE_MURPHY_M4 && !defined(SIMULATOR)
  // The FT6336U shares the panel's reset line; re-validate it after the
  // display begin toggled it. The simulator shim has no touch controller.
  if (!gpio.restoreTouchAfterDisplayReset()) {
    LOG_ERR("MAIN", "Failed to restore Murphy M4 touch after display reset");
  }
#endif
  renderer.begin();
  activityManager.begin();
  LOG_DBG("MAIN", "Display initialized");

  // Initialize font decompressor for compressed reader fonts
  const bool fontDecompressorReady = fontDecompressor.init();
  if (!fontDecompressorReady) {
    LOG_ERR("MAIN", "Font decompressor init failed");
  }
  fontCacheManager.setFontDecompressor(&fontDecompressor);
  renderer.setFontCacheManager(&fontCacheManager);
  renderer.insertFont(NOTOSERIF_14_FONT_ID, notoserif14FontFamily);
#ifndef OMIT_FONTS
  renderer.insertFont(NOTOSERIF_12_FONT_ID, notoserif12FontFamily);
  renderer.insertFont(NOTOSERIF_16_FONT_ID, notoserif16FontFamily);
  renderer.insertFont(NOTOSERIF_18_FONT_ID, notoserif18FontFamily);

  renderer.insertFont(NOTOSANS_12_FONT_ID, notosans12FontFamily);
  renderer.insertFont(NOTOSANS_14_FONT_ID, notosans14FontFamily);
  renderer.insertFont(NOTOSANS_16_FONT_ID, notosans16FontFamily);
  renderer.insertFont(NOTOSANS_18_FONT_ID, notosans18FontFamily);
#endif  // OMIT_FONTS
  renderer.insertFont(UI_10_FONT_ID, ui10FontFamily);
  renderer.insertFont(UI_12_FONT_ID, ui12FontFamily);
  renderer.insertFont(SMALL_FONT_ID, smallFontFamily);
#ifdef ENABLE_CHINESE_VERSION
  // One boot-time map node (<100 bytes including font objects); glyph data stays in flash.
  renderer.insertFont(BaseTheme::STATUS_NUMERIC_FONT_ID, statusNumericFontFamily);
  renderer.insertFont(CHINESE_CHESS_FONT_ID, chineseChessPieceFontFamily);
#endif

  if (!skipSdFonts) sdFontSystem.begin(renderer, deferSdFamilyLoad);

  LOG_DBG("MAIN", "Fonts setup");
  return fontDecompressorReady;
}

void setup() {
  BoardConfig::holdPowerRails();

  t1 = millis();

#ifdef ENABLE_SERIAL_LOG
  // Earliest possible Serial setup. The 250 ms stall before begin() lets the
  // USB Serial/JTAG peripheral finish power-on and lets the host complete USB
  // enumeration before we touch the CDC state — otherwise cold boot races
  // and the host has to be physically replugged for logs to flow. Warm reboot
  // worked without the delay because USB was already enumerated. A deep-sleep
  // wake is a warm reboot, and this stall sits directly on the
  // power-key-to-screen path, so skip it there (wake-boot live serial may be
  // flaky until replug; the on-device log ring is unaffected).
#if !defined(SIMULATOR)
  if (esp_reset_reason() != ESP_RST_DEEPSLEEP) delay(250);
#else
  delay(250);
#endif
  Serial.begin(115200);
#if LOG_SERIAL_HAS_TX_TIMEOUT
  logSerial.setTxTimeoutMs(1);  // This is a load-bearing 1. Do not modify.
#endif
#endif

  HalSystem::begin();
  const bool otaPendingAtBoot = HalOtaSlot::runningImageState() == HalOtaSlot::RunningImageState::PendingVerify;

  // Read-and-clear so a panic later in setup() doesn't loop into silent reboot.
  // Bound the target range too — RTC_NOINIT memory is uninitialized on cold boot.
  const bool isSilentReboot = (silentRebootMagic == SILENT_REBOOT_MAGIC);
  const uint32_t snapshotTarget =
      (isSilentReboot && silentRebootTarget <= SILENT_REBOOT_TARGET_READER) ? silentRebootTarget : 0;
  silentRebootMagic = 0;
  silentRebootTarget = 0;

  gpio.begin();
  powerManager.begin();
  halTiltSensor.begin();
  halClock.begin();
  halEnvironment.begin();

  LOG_INF("MAIN", "Hardware detect: %s", BoardConfig::ACTIVE.name);

  // SD Card Initialization
  // We need 6 open files concurrently when parsing a new chapter
  if (!Storage.begin()) {
    LOG_ERR("MAIN", "SD card initialization failed");
    const bool fontsReady = setupDisplayAndFonts(isSilentReboot);
    activityManager.goToFullScreenMessage("SD card error", EpdFontFamily::BOLD);
    activityManager.requestUpdateAndWait();
    if (otaPendingAtBoot && fontsReady) {
      if (HalOtaSlot::confirmRunningImage()) {
        LOG_INF("OTA", "Running image confirmed after SD error display");
      } else {
        LOG_ERR("OTA", "Running image confirmation failed after SD error display");
      }
    }
    return;
  }

  HalSystem::checkPanic();
  LOG_DBG("MAIN", "Boot timing: SD ready");

  SETTINGS.loadFromFile();
  APP_STATE.loadFromFile();
  LOG_DBG("MAIN", "Boot timing: settings loaded");
  const auto wakeupReason = gpio.getWakeupReason();
  // A clock tick repaints and goes straight back to sleep; keep the frontlight
  // off for it so it does not flash once during the repaint boot.
  const bool clockTickWake =
#if !defined(SIMULATOR)
      wakeupReason == HalGPIO::WakeupReason::Timer && APP_STATE.clockSleepActive &&
      (SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::CLOCK ||
       SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::CALENDAR);
#else
      false;
#endif
  Frontlight.begin(SETTINGS.frontlightBrightness, SETTINGS.frontlightWarmth,
                   SETTINGS.frontlightOn != 0 && !clockTickWake);
  halClock.setAutoSyncEnabled(SETTINGS.clockAutoSync != 0);
  RECENT_BOOKS.loadFromFile();
  READING_STATS.loadFromFile();
  ACHIEVEMENTS.loadFromFile();
  I18N.setLanguage(static_cast<Language>(SETTINGS.language));
  KOREADER_STORE.loadFromFile();
  OPDS_STORE.loadFromFile();
  COUNTDOWN_STORE.loadFromFile();
  UITheme::getInstance().reload();
  LOG_DBG("MAIN", "Boot timing: stores loaded");
  ButtonNavigator::setMappedInputManager(mappedInputManager);

  bool clockUnlock = false;
  switch (wakeupReason) {
    case HalGPIO::WakeupReason::PowerButton:
      LOG_DBG("MAIN", "Verifying power button press duration");
      if (!gpio.verifyPowerButtonWakeup(SETTINGS.getPowerButtonDuration(),
                                        SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::SLEEP)) {
        startDeepSleepWithTimer(
            powerManager, gpio,
            APP_STATE.clockSleepActive
                ? (SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::CALENDAR ? calendarSleepWakeUs()
                                                                                           : clockSleepWakeUs())
                : 0);
      }
      if (APP_STATE.clockSleepActive) {
        APP_STATE.clockSleepActive = false;
        APP_STATE.saveToFile();
        clockUnlock = true;
      }
      break;
    case HalGPIO::WakeupReason::AfterUSBPower:
#if FREEINK_DEVICE_MURPHY_M4
      // M4's USB-Serial/JTAG cable is the flash/debug link, not a charger that
      // should dump the device back to sleep with a frozen panel.
      LOG_DBG("MAIN", "Wakeup reason: After USB Power (M4 continues boot)");
      break;
#else
      // If USB power caused a cold boot, go back to sleep
      LOG_DBG("MAIN", "Wakeup reason: After USB Power");
      startDeepSleepWithTimer(
          powerManager, gpio,
          APP_STATE.clockSleepActive
              ? (SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::CALENDAR ? calendarSleepWakeUs()
                                                                                         : clockSleepWakeUs())
              : 0);
      break;
#endif
#if !defined(SIMULATOR)
    case HalGPIO::WakeupReason::Timer: {
      const bool calendarTick = SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::CALENDAR;
      if (APP_STATE.clockSleepActive &&
          (SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::CLOCK || calendarTick ||
           SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::COUNTDOWN)) {
        const char* tickName = calendarTick ? "Calendar sleep tick"
                                            : (SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::COUNTDOWN
                                                   ? "Countdown sleep tick"
                                                   : "Clock sleep tick");
        LOG_DBG("MAIN", "%s", tickName);
        setupDisplayAndFonts(true, true);
        if (APP_STATE.lastSleepFromReader) {
          ReaderUtils::applyOrientation(renderer, SETTINGS.orientation);
        }
        const bool countdownTick = SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::COUNTDOWN;
        if (calendarTick || countdownTick) {
          // Daily modes fully redraw — no saved-frame base needed.
          if (countdownTick) {
            SleepActivity::paintCountdownSleep(renderer, /*dailyTick=*/true);
          } else {
            SleepActivity::paintCalendarSleep(renderer, /*dailyTick=*/true);
          }
          // Keep the unlock frame in sync with the panel: BootResume::ClockUnlock
          // seeds RED from this file, and a stale frame leaves the repainted
          // day's ink as a ghost under the first FAST home/reader paint.
          saveSleepFrameBuffer();
        } else {
          // Step logs: a reset anywhere in this silent stretch reboots before
          // anything else can be logged — without these the crash report ends
          // at "Fonts setup" and the failing step is unidentifiable (device
          // crash 2026-09-03, empty panic reason = CPU-fault path).
          LOG_DBG("MAIN", "Clock tick: loading sleep frame");
          if (loadSleepFrameBuffer(false)) {
            LOG_DBG("MAIN", "Clock tick: seeding planes");
            renderer.cleanupGrayscaleWithFrameBuffer();
          }
          LOG_DBG("MAIN", "Clock tick: painting clock");
          SleepActivity::paintClock(renderer, true);
          LOG_DBG("MAIN", "Clock tick: saving sleep frame");
          saveSleepFrameBuffer();
          LOG_DBG("MAIN", "Clock tick: entering deep sleep");
        }
        halTiltSensor.deepSleep();
        Frontlight.setOn(false);
        display.deepSleep();
        startDeepSleepWithTimer(powerManager, gpio, countdownTick ? calendarSleepWakeUs() : clockSleepWakeUs());
      }
      break;
    }
#endif
    case HalGPIO::WakeupReason::AfterFlash:
      // After flashing, just proceed to boot
    case HalGPIO::WakeupReason::Other:
    default:
      break;
  }

  // Recovery firmware mode: hold a side button together with power at boot. X4 Pro uses
  // BTN_DOWN/GPIO7 because BTN_UP/GPIO0 is an ESP32-S3 boot strap; other boards keep BTN_UP.
  bool recoveryFirmwareMode = false;
  if (wakeupReason == HalGPIO::WakeupReason::PowerButton) {
    // Refresh the cached button state a few times — isPressed() needs ~half a second to settle
    // after boot per the HalGPIO contract. Use a millis-based deadline so we always wait the full
    // settle window even if the loop body takes longer than expected on slow boots.
    const unsigned long settleStart = millis();
    while (millis() - settleStart < 500) {
      gpio.update();
      delay(10);
    }
    const uint8_t recoveryButton = BoardConfig::isX4Pro() ? HalGPIO::BTN_DOWN : HalGPIO::BTN_UP;
    if (gpio.isPressed(recoveryButton)) {
      recoveryFirmwareMode = true;
      LOG_INF("MAIN", "Recovery firmware mode (%s + POWER held at boot)", BoardConfig::isX4Pro() ? "DOWN" : "UP");
    }
  }

  // First serial output only here to avoid timing inconsistencies for power button press duration verification.
  // The build timestamp lets a log trace prove which binary produced it — a
  // rebuilt program on disk does not replace an already-running process.
  LOG_DBG("MAIN", "Starting CrossPoint version " CROSSPOINT_VERSION " (built " __DATE__ " " __TIME__ ")");

  // Resolve the single boot-presentation decision. Skipping the splash also
  // skips the panel-clearing pass and the X3 initial-full-sync arming (see
  // HalDisplay::begin), so the first paint is FAST_REFRESH (~500ms) over the
  // retained frame and input dispatches against a visible UI.
  const BootResume resume = isSilentReboot              ? BootResume::Silent
                            : clockUnlock               ? BootResume::ClockUnlock
                            : !APP_STATE.showBootScreen ? BootResume::QuickResume
                                                        : BootResume::Splash;
  bool allowFastInitialReaderRefresh = false;

  const bool fontsReady = setupDisplayAndFonts(resume != BootResume::Splash, false,
                                               /*deferSdFamilyLoad=*/lockGateNeeded(resume));
  LOG_DBG("MAIN", "Boot timing: display+fonts ready");
  const bool postOtaBoot = otaPendingAtBoot && fontsReady && activityManager.goToPostOtaBoot(!recoveryFirmwareMode);

  if (!postOtaBoot) {
    switch (resume) {
      case BootResume::Silent:
        // Splash skipped: the routing block below picks the target activity; the
        // panel keeps showing the pre-reboot frame until that first paint lands.
        // Seed RED from the frame saved at the restart so that first FAST
        // paint drives the old screen's ink away instead of ghosting it.
        if (loadSleepFrameBuffer()) {
          renderer.cleanupGrayscaleWithFrameBuffer();
        }
        // The pre-reboot screen (USB transfer, WiFi teardown) sat displayed
        // for minutes and the loading popup was painted right before the
        // reset — both leave physical residue that the single-pass HALF
        // first paint does not fully bleach (eject ghost persisted through
        // the v38/v43 baseline fixes; device photos 2026-09-03). Force the
        // first HOME paint through the multi-inversion FULL clean. Reader
        // targets keep their own AA/refresh cadence and skip the override.
        if (snapshotTarget == SILENT_REBOOT_TARGET_HOME) {
          renderer.requestNextFullRefresh();
        }
        break;
      case BootResume::QuickResume:
        // One-shot flag: re-arm the splash for the next non-quick-resume boot. Save
        // before any painting so a hang in the blocking paint path can't strand
        // us in a quick-resume-with-no-frame loop on the next boot.
        APP_STATE.showBootScreen = true;
        APP_STATE.saveToFile();
        if (loadSleepFrameBuffer()) {
          const bool useDifferentialRefresh = gpio.deviceIsX3();
          if (useDifferentialRefresh) {
            // begin() clears the X3 controller RAM, so restore the saved frame as
            // the baseline before replacing the moon with the loading icon.
            renderer.cleanupGrayscaleWithFrameBuffer();
          }

          const auto pageHeight = renderer.getScreenHeight();
          renderer.drawImage(LoadingIcon, 0, pageHeight - LOADINGICON_HEIGHT, LOADINGICON_WIDTH, LOADINGICON_HEIGHT);
          if (useDifferentialRefresh) {
            renderer.displayGrayscaleBase(HalDisplay::FAST_REFRESH);
            allowFastInitialReaderRefresh = true;
          } else {
            renderer.displayBuffer(HalDisplay::HALF_REFRESH);
          }
        } else {
          activityManager.goToBoot();  // frame file missing, fall back to the splash
        }
        break;
      case BootResume::ClockUnlock:
        // Seed RED from the saved clock so the first FAST home/reader paint
        // drives the old digits to white. Without this, FAST diffs against
        // empty RAM and the time stays as a ghost.
        if (loadSleepFrameBuffer()) {
#if FREEINK_DEVICE_MURPHY_M4
          // The saved frame is exactly what the panel physically shows (an
          // absolute lock-entry refresh painted it), so seed BOTH planes and
          // let the first paint — the PIN pad or Home — be a fast
          // differential. The default path would promote it to a ~1s HALF
          // absolute, which is most of the perceived power-key-to-screen
          // latency (device feedback 2026-09-03).
          renderer.seedBaselineFromFrameBuffer();
#else
          renderer.cleanupGrayscaleWithFrameBuffer();
#endif
        }
        break;
      case BootResume::Splash:
        activityManager.goToBoot();
        break;
    }
  }

  if (recoveryFirmwareMode) {
    // Skip normal home/reader routing: jump straight into the SD firmware picker.
    activityManager.replaceActivityWith<SdFirmwareUpdateActivity>(/*recoveryMode=*/true);
  } else if (HalSystem::isRebootFromPanic()) {
    // If we rebooted from a panic, go to crash report screen to show the panic info
    activityManager.goToCrashReport();
  } else if (postOtaBoot) {
    activityManager.goHome();
  } else if (lockGateNeeded(resume)) {
    // Wake-from-sleep with the lock armed: park on the PIN pad and defer the
    // home/reader decision main.cpp would have taken. The pad runs that
    // navigation itself once the code checks out.
    PinEntryActivity::WakeTarget target;
    target.toReader = !(APP_STATE.openEpubPath.empty() || !APP_STATE.lastSleepFromReader ||
                        mappedInputManager.isPressed(MappedInputManager::Button::Back) ||
                        APP_STATE.readerActivityLoadCount > 0);
    target.allowFastRefresh = allowFastInitialReaderRefresh;
    if (!activityManager.replaceActivityWith<PinEntryActivity>(PinEntryActivity::Mode::Unlock, std::move(target))) {
      activityManager.goHome();  // OOM fallback: never strand the user at a blank screen
    }
  } else if (resume == BootResume::Silent && snapshotTarget == SILENT_REBOOT_TARGET_READER &&
             !APP_STATE.openEpubPath.empty()) {
    activityManager.goToReader(APP_STATE.openEpubPath);
  } else if (resume == BootResume::Silent) {
    // target == home (or reader with no open book): land on home — don't fall
    // through to the sleep-wake "resume reader" logic, which fires on stale
    // openEpubPath + lastSleepFromReader from a prior session.
    activityManager.goHome();
  } else if (APP_STATE.openEpubPath.empty() || !APP_STATE.lastSleepFromReader ||
             mappedInputManager.isPressed(MappedInputManager::Button::Back) || APP_STATE.readerActivityLoadCount > 0) {
    // Boot to home screen if no book is open, last sleep was not from reader, back button is held, or reader activity
    // crashed (indicated by readerActivityLoadCount > 0)
    activityManager.goHome();
  } else {
    // Clear app state to avoid getting into a boot loop if the epub doesn't load
    const auto path = APP_STATE.openEpubPath;
    APP_STATE.openEpubPath = "";
    APP_STATE.readerActivityLoadCount++;
    APP_STATE.saveToFile();
    activityManager.goToReader(path, allowFastInitialReaderRefresh);
  }

  if (resume == BootResume::Silent || resume == BootResume::ClockUnlock) {
    if (postOtaBoot) {
      // Apply the queued Home replacement before waiting for its first physical paint.
      activityManager.loop();
    }
    // Block until the first paint physically completes. refreshDisplay()
    // waits on the panel BUSY pin so when this returns the user can see the
    // new activity. Without the wait, an edge captured by gpio.update()
    // during boot dispatches against an invisible Home and the default
    // selectorIndex=0 opens the most-recent book.
    activityManager.requestUpdateAndWait();
    // Absorb any button held at this point into currentState as a non-edge:
    // two gpio.update() calls separated by > InputManager's 5ms debounce
    // transition the held bit through lastDebounceTime into currentState
    // without setting pressedEvents, so the first loop()'s own gpio.update()
    // sees state == currentState and emits nothing.
    gpio.update();
    delay(10);
    gpio.update();
  }

  // Ensure we're not still holding the power button before leaving setup
  waitForPowerRelease();
  allowSleepAt = millis() + 2000;
}

void loop() {
  static unsigned long maxLoopDuration = 0;
  const unsigned long loopStartTime = millis();
  static unsigned long lastMemPrint = 0;

  gpio.setSharedConfirmPowerShortPressEmitsPower(SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::SLEEP);
  gpio.update();
  halTiltSensor.update(SETTINGS.tiltPageTurn, SETTINGS.orientation, activityManager.isReaderActivity());
  halClock.update();

  renderer.setFadingFix(SETTINGS.fadingFix);

  if (Serial && millis() - lastMemPrint >= 10000) {
    LOG_INF("MEM", "Free: %d bytes, Total: %d bytes, Min Free: %d bytes, MaxAlloc: %d bytes", ESP.getFreeHeap(),
            ESP.getHeapSize(), ESP.getMinFreeHeap(), ESP.getMaxAllocHeap());
    lastMemPrint = millis();
  }

  // Handle incoming serial commands,
  // nb: we use logSerial from logging to avoid deprecation warnings
  if (logSerial.available() > 0) {
    String line = logSerial.readStringUntil('\n');
    if (line.startsWith("CMD:")) {
      String cmd = line.substring(4);
      cmd.trim();
      if (cmd == "SCREENSHOT") {
        const uint32_t bufferSize = display.getBufferSize();
        logSerial.printf("SCREENSHOT_START:%d\n", bufferSize);
        uint8_t* buf = display.getFrameBuffer();
        logSerial.write(buf, bufferSize);
        logSerial.printf("SCREENSHOT_END\n");
      }
    }
  }

  // Check for any real user activity (button, touch, or tilt).
  static unsigned long lastActivityTime = millis();
  if (gpio.wasAnyPressed() || gpio.wasAnyReleased() || gpio.wasTouchActivity() || halTiltSensor.hadActivity()) {
    lastActivityTime = millis();         // Reset inactivity timer
    powerManager.setPowerSaving(false);  // Restore normal CPU frequency on user activity
  }
  // preventAutoSleep() is intentionally NOT folded into the activity check above:
  // it only short-circuits the deep-sleep timer below, not the inactivity clock
  // that drives auto-downclock. Standby (a clock face) wants deep sleep blocked
  // but still benefits from the framework dropping CPU to LOW_POWER_FREQ.

  static bool screenshotButtonsReleased = true;
  static bool screenshotComboActive = false;
  if (gpio.isPressed(HalGPIO::BTN_POWER) && gpio.isPressed(HalGPIO::BTN_DOWN)) {
#if defined(FREEINK_CAP_USB_MSC) && FREEINK_CAP_USB_MSC
    // A screenshot writes the BMP to the SD card — forbidden while the USB
    // host owns the volume (would race the host's raw sector writes).
    if (usbMscActive()) {
      return;
    }
#endif
    screenshotComboActive = true;
    if (screenshotButtonsReleased) {
      screenshotButtonsReleased = false;
      {
        RenderLock lock;
        ScreenshotUtil::takeScreenshot(renderer);
      }
    }
    return;
  }
  if (screenshotComboActive) {
    if (gpio.isPressed(HalGPIO::BTN_POWER)) return;
    if (gpio.wasReleased(HalGPIO::BTN_POWER)) {
      screenshotButtonsReleased = true;
      screenshotComboActive = false;
      return;
    }
    screenshotButtonsReleased = true;
    screenshotComboActive = false;
  }

  const unsigned long sleepTimeoutMs = SETTINGS.getSleepTimeoutMs();
  if (sleepTimeoutMs > 0 && !activityManager.preventAutoSleep() && millis() - lastActivityTime >= sleepTimeoutMs) {
    LOG_DBG("SLP", "Auto-sleep triggered after %lu ms of inactivity", sleepTimeoutMs);
    enterDeepSleep(true);
    // This should never be hit as `enterDeepSleep` calls esp_deep_sleep_start
    return;
  }

  if (millis() >= allowSleepAt && gpio.isPressed(HalGPIO::BTN_POWER) &&
      gpio.getPowerButtonHeldTime() > SETTINGS.getPowerButtonDuration()) {
    // If the screenshot combination is potentially being pressed, don't sleep
    if (gpio.isPressed(HalGPIO::BTN_DOWN)) {
      return;
    }
#if defined(FREEINK_CAP_USB_MSC) && FREEINK_CAP_USB_MSC
    // The USB host owns the SD card in MSC mode; this path bypasses
    // preventAutoSleep(), so it needs the explicit guard (see enterDeepSleep).
    if (usbMscActive()) {
      return;
    }
#endif
    enterDeepSleep();
    // This should never be hit as `enterDeepSleep` calls esp_deep_sleep_start
    return;
  }

  // Refresh screen when power button is short-pressed with FORCE_REFRESH setting.
  if (SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::FORCE_REFRESH &&
      mappedInputManager.wasReleased(MappedInputManager::Button::Power)) {
    LOG_DBG("MAIN", "Manual screen refresh triggered");
    if (!activityManager.handleForcedRefresh()) {
      RenderLock lock;
      renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    }
  }

  // Refresh the battery icon when USB is plugged or unplugged.
  // Placed after sleep guards so we never queue a render that won't be processed.
  if (gpio.wasUsbStateChanged()) {
    activityManager.requestUpdate();
  }
#ifndef CROSSPOINT_EMULATED
  if (gpio.wasInputModalityChanged() && gpio.hasTouch() && UITheme::getInstance().hasMainTabs() &&
      !activityManager.isReaderActivity()) {
    activityManager.requestUpdate();
  }
#endif

  const unsigned long activityStartTime = millis();
  const bool readerWasActive = activityManager.isReaderActivity();
  activityManager.loop();
  const bool readerIsActive = activityManager.isReaderActivity();
  const unsigned long activityDuration = millis() - activityStartTime;

  if (readerWasActive && !readerIsActive) {
    if (!READING_STATS.saveToFile()) {
      LOG_ERR("RST", "Failed to save reading stats after reader exit");
    }
    if (!ACHIEVEMENTS.saveToFile()) {
      LOG_ERR("ACH", "Failed to save achievements after reader exit");
    }
  } else if (readerIsActive && (millis() - lastActivityTime) >= READING_STATS_CHECKPOINT_IDLE_MS &&
             !activityManager.skipLoopDelay() && !activityManager.preventAutoSleep() &&
             READING_STATS.shouldSaveCheckpoint()) {
    RenderLock lock;
    if (!READING_STATS.saveToFile()) {
      LOG_ERR("RST", "Failed to save idle reading checkpoint");
    }
  }

  const unsigned long loopDuration = millis() - loopStartTime;
  if (loopDuration > maxLoopDuration) {
    maxLoopDuration = loopDuration;
    if (maxLoopDuration > 50) {
      LOG_DBG("LOOP", "New max loop duration: %lu ms (activity: %lu ms)", maxLoopDuration, activityDuration);
    }
  }

  // Add delay at the end of the loop to prevent tight spinning
  // When an activity requests skip loop delay (e.g., webserver running), use yield() for faster response
  // Otherwise, use longer delay to save power
  if (activityManager.skipLoopDelay()) {
    powerManager.setPowerSaving(false);  // Make sure we're at full performance when skipLoopDelay is requested
    yield();                             // Give FreeRTOS a chance to run tasks, but return immediately
  } else {
    if (millis() - lastActivityTime >= HalPowerManager::IDLE_POWER_SAVING_MS) {
      // If we've been inactive for a while, increase the delay to save power
      powerManager.setPowerSaving(true);  // Lower CPU frequency after extended inactivity
      delay(50);
    } else {
      // Short delay to prevent tight loop while still being responsive
      delay(10);
    }
  }
}
