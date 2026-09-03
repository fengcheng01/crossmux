#include "UsbTransferActivity.h"

#if defined(FREEINK_CAP_USB_MSC) && FREEINK_CAP_USB_MSC

#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>

#include "AchievementsStore.h"
#include "CrossPointState.h"
#include "MappedInputManager.h"
#include "ReadingStatsStore.h"
#include "SdCardFontSystem.h"
#include "SilentRestart.h"
#include "components/SubpageLayout.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "usb/UsbMassStorageControl.h"
#include "util/TaskWatchdog.h"

namespace {
// The SDK class holds the TinyUSB callbacks and latches state atomically; one
// session owner at a time, so a single instance shared across activity
// instances is safe and avoids a heap allocation per entry.
freeink::UsbMassStorage gMsc;

// Ejected-but-unmountable card: let the failure notice show before the
// fallback reboot (its mount path retries the card several times).
constexpr unsigned long SHOW_FAILURE_BEFORE_RESTART_MS = 2500;

// Return button geometry for the eject screen (render() also stores the final
// rect in ejectButtonRect_ for loop()'s tap test).
constexpr int EJECT_BTN_W = 240;
constexpr int EJECT_BTN_H = 64;
// Air between the detail text baseline band and the button — the button must
// read as a separate control, not crowd the notice text (device feedback
// 2026-09-03).
constexpr int EJECT_BTN_GAP = 64;
}  // namespace

UsbTransferActivity::~UsbTransferActivity() = default;

bool UsbTransferActivity::beginSession() {
  // Drop the SD fonts' resident glyph/kern caches before the volume detaches:
  // it releases their heap arenas and guarantees no lazy font read can start
  // against a detached card. (UI text renders from embedded fonts, and the
  // reader — the only SD-font consumer — is not on the activity stack here.)
  if (auto* fcm = renderer.getFontCacheManager()) {
    fcm->releaseSdFontCaches();
  }
  // Flush stores whose writers are gated on activity state; nothing may write
  // to the card once it is handed over.
  READING_STATS.saveToFile();
  ACHIEVEMENTS.saveToFile();
  APP_STATE.saveToFile();

  FsBlockDeviceInterface* dev = Storage.detachForRawUsbAccess();
  if (dev == nullptr || !gMsc.begin(dev)) {
    LOG_ERR("USBMSC", "Failed to start USB Mass Storage session");
    // Nothing was served to the host: re-mount so Back can return home. If
    // even the re-mount fails, onEnter reboots into the boot-time mount path
    // (which retries the card several times) instead of stranding the UI on a
    // dead volume.
    remounted_ = Storage.begin();
    return false;
  }
  began = true;
  usbMscActiveFlag() = true;
  lastSdkState = freeink::UsbMassStorageState::WaitingForHost;
  return true;
}

void UsbTransferActivity::settle() {
  if (began) {
    gMsc.end();
    began = false;
  }
  usbMscActiveFlag() = false;
}

void UsbTransferActivity::onEnter() {
  Activity::onEnter();
  if (!beginSession()) {
    uiState = UiState::Error;
    if (!remounted_) {
      // The card did not come back after the failed session: a fresh boot is
      // the only clean recovery (its mount path retries the card).
      silentRestart();
      return;
    }
  }
  requestUpdate();
}

void UsbTransferActivity::onExit() {
  Activity::onExit();

  // Normal exits already settled in loop() (eject/error) or cancelNoHost
  // handled remounting; this covers being replaced while a session is live.
  if (began) {
    gMsc.end();
    began = false;
    usbMscActiveFlag() = false;
    if (!hostSeen) {
      // Host never touched the card: restore the filesystem in place instead
      // of rebooting.
      Storage.begin();
    } else {
      // The PC may still hold the volume: rebooting is the SDK-documented
      // exit, and matches the web-server activity's teardown precedent.
      silentRestart();
    }
  }
}

void UsbTransferActivity::loop() {
  // Ejected: the session is settled and the volume re-mounted in place. Wait
  // for the user to leave; no reboot, no clean-off flashes (device feedback
  // 2026-09-03 — the restart flow flashed ~3 times and dumped the user on
  // Home instead of where they came from).
  if (uiState == UiState::Ejected) {
    if (!ejectRemountOk) {
      if (millis() >= ejectRestartAt) silentRestart();
      return;
    }
    if ((ejectBtnW > 0 && mappedInput.wasTapInRect(ejectBtnX, ejectBtnY, ejectBtnW, ejectBtnH)) ||
        mappedInput.wasReleased(MappedInputManager::Button::Back) ||
        mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      // The PC may have replaced font files on the card while it owned the
      // volume; re-discover on the next ensureLoaded().
      sdFontSystem.markRegistryDirty();
      onGoHome();
    }
    return;
  }

  if (!began) {
    // Begin failed and the volume was re-mounted (onEnter rebooted otherwise):
    // keep Back working so the error screen cannot trap the user.
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      onGoHome();
    }
    return;
  }

  const auto state = gMsc.state();
  if (state != lastSdkState) {
    lastSdkState = state;
    LOG_INF("USBMSC", "MSC state -> %d", static_cast<int>(state));
  }

  switch (state) {
    case freeink::UsbMassStorageState::Connected:
    case freeink::UsbMassStorageState::Accessed:
      hostSeen = true;
      if (uiState != UiState::Connected) {
        uiState = UiState::Connected;
        requestUpdate();
      }
      break;
    case freeink::UsbMassStorageState::Ejected:
    case freeink::UsbMassStorageState::Disconnected:
    case freeink::UsbMassStorageState::IoError:
      if (uiState != UiState::Ejected) {
        uiState = UiState::Ejected;
        // End the session and bring the FAT volume back now: the card is
        // ours again and nothing below needs the reboot anymore.
        settle();
        ejectRemountOk = Storage.begin();
        if (!ejectRemountOk) {
          LOG_ERR("USBMSC", "Re-mount after eject failed; restarting");
          ejectRestartAt = millis() + SHOW_FAILURE_BEFORE_RESTART_MS;
        }
        requestUpdate();
      }
      break;
    default:
      break;
  }

  // Back cancels only before a host has ever connected; once the PC owns the
  // volume, Back does nothing (the screen tells the user to eject first).
  if (!hostSeen && mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    settle();
    // Re-mount the FAT volume; font caches rebuild lazily. A failed re-mount
    // means the card is in a state only a fresh boot should touch.
    if (!Storage.begin()) {
      silentRestart();
      return;
    }
    onGoHome();
    return;
  }

  resetTaskWatchdogIfSubscribed();
}

void UsbTransferActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safeArea = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  GUI.drawHeader(renderer, Rect{safeArea.x, safeArea.y + metrics.topPadding, safeArea.width, metrics.headerHeight},
                 tr(STR_USB_TRANSFER), nullptr);

  const Rect body = SubpageLayout::contentRect(safeArea, metrics, true);
  const int lineHeight = renderer.getLineHeight(UI_12_FONT_ID);
  int y = SubpageLayout::centeredTop(body, lineHeight);

  const char* headline = nullptr;
  const char* detail = nullptr;
  bool showExitHint = false;
  switch (uiState) {
    case UiState::Connected:
      headline = tr(STR_USB_CARD_SHARED);
      detail = tr(STR_USB_EJECT_FIRST);
      break;
    case UiState::Ejected:
      headline = tr(STR_USB_EJECTED);
      // Remount ok: invite the user back (button below). Failed: the device
      // restarts itself once the notice has been readable for a moment.
      detail = ejectRemountOk ? tr(STR_USB_EJECT_DONE) : tr(STR_USB_RESTARTING);
      showExitHint = ejectRemountOk;
      break;
    case UiState::Error:
      // Session start failed; the volume was re-mounted (or the device
      // rebooted) — Back goes home, so offer the exit instead of a restart.
      headline = tr(STR_USB_ERROR);
      detail = tr(STR_USB_CANCEL_HINT);
      showExitHint = true;
      break;
    case UiState::WaitingForHost:
    default:
      headline = tr(STR_USB_CONNECT_CABLE);
      detail = tr(STR_USB_CANCEL_HINT);
      showExitHint = true;
      break;
  }

  renderer.drawCenteredText(UI_12_FONT_ID, y, headline, true, EpdFontFamily::BOLD);
  y += lineHeight + SubpageLayout::relatedGap(metrics);
  renderer.drawCenteredText(UI_10_FONT_ID, y, detail, true);

  ejectBtnW = 0;
  if (uiState == UiState::Ejected && ejectRemountOk) {
    // Anchor the button to the drawn detail line (not screen fractions — the
    // centered text stack moved around and the button crowded it): one band
    // of air below the text, clamped above the button-hint bar.
    const int detailH = renderer.getLineHeight(UI_10_FONT_ID);
    const int btnMaxTop = renderer.getScreenHeight() - metrics.buttonHintsHeight - 16 - EJECT_BTN_H;
    const int btnTop = std::min(y + detailH + EJECT_BTN_GAP, btnMaxTop);
    ejectBtnX = (renderer.getScreenWidth() - EJECT_BTN_W) / 2;
    ejectBtnY = btnTop;
    ejectBtnW = EJECT_BTN_W;
    ejectBtnH = EJECT_BTN_H;
    GUI.drawActionButton(renderer, Rect{ejectBtnX, ejectBtnY, ejectBtnW, ejectBtnH}, tr(STR_BACK));
  }

  const auto labels = mappedInput.mapLabels(showExitHint ? tr(STR_EXIT) : "", "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}

#elif defined(SIMULATOR) && defined(FREEINK_DEVICE_MURPHY_M4) && FREEINK_DEVICE_MURPHY_M4

#include <GfxRenderer.h>
#include <I18n.h>

#include "components/SubpageLayout.h"
#include "components/UITheme.h"
#include "fontIds.h"

void UsbTransferActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    onGoHome();
  }
}

void UsbTransferActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safeArea = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  GUI.drawHeader(renderer, Rect{safeArea.x, safeArea.y + metrics.topPadding, safeArea.width, metrics.headerHeight},
                 tr(STR_USB_TRANSFER), nullptr);

  const Rect body = SubpageLayout::contentRect(safeArea, metrics, true);
  const int lineHeight = renderer.getLineHeight(UI_12_FONT_ID);
  UITheme::drawCenteredText(renderer, body, UI_12_FONT_ID, SubpageLayout::centeredTop(body, lineHeight),
                            tr(STR_USB_SIMULATOR_ONLY));

  const auto labels = mappedInput.mapLabels(tr(STR_EXIT), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}

#endif  // FREEINK_CAP_USB_MSC / simulator stub
