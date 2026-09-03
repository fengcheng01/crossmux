#pragma once

#include <BoardConfig.h>

#if defined(FREEINK_CAP_USB_MSC) && FREEINK_CAP_USB_MSC

#include <UsbMassStorage.h>

#include "activities/Activity.h"

// USB Mass Storage session (the "USB data cable" mode of the File Transfer
// menu): hands the whole SD card to the host PC over TinyUSB. The FAT volume
// is detached and the card is served as raw sectors, so the firmware itself
// must not touch the card while the session is live — that is what makes the
// PC's writes safe. Launched from the home flow only, so no reader activity
// with an open chapter-build handle can be on the stack.
//
// Exit contract (SDK UsbMassStorage.h): after the host releases the card the
// firmware must re-mount. The eject flow re-mounts in place and shows a
// return button — no reboot, no clean-off flashes; the SD font registry is
// marked dirty because the PC may have replaced font files. Only a failed
// re-mount reboots (the boot-time mount path retries the card).
class UsbTransferActivity final : public Activity {
 public:
  UsbTransferActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("UsbTransfer", renderer, mappedInput) {}
  ~UsbTransferActivity() override;
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  // Hold the fast loop and block sleep/gestures only while a session is live;
  // once ejected and re-mounted the screen idles like any other.
  bool skipLoopDelay() override { return began; }
  bool preventAutoSleep() override { return began; }
  bool handleHomeGesture() override { return began; }

 private:
  enum class UiState { WaitingForHost, Connected, Ejected, Error };

  bool beginSession();
  void settle();

  freeink::UsbMassStorage msc;
  UiState uiState = UiState::WaitingForHost;
  bool began = false;        // msc.begin() succeeded and msc.end() is owed
  bool hostSeen = false;     // a PC enumerated the device at least once
  bool remounted_ = true;    // failure path: the FAT volume came back
  bool ejectRemountOk = true;   // eject path: in-place re-mount succeeded
  unsigned long ejectRestartAt = 0;  // eject path, remount failed: reboot after the notice shows
  // Return-button hit rect, filled by render() from the drawn layout (the
  // text stack position); loop() taps against it. Width 0 = not drawn yet.
  // Plain ints: Rect is incomplete in this header's include chain.
  int ejectBtnX = 0, ejectBtnY = 0, ejectBtnW = 0, ejectBtnH = 0;
  freeink::UsbMassStorageState lastSdkState = freeink::UsbMassStorageState::Idle;
};

#elif defined(SIMULATOR) && defined(FREEINK_DEVICE_MURPHY_M4) && FREEINK_DEVICE_MURPHY_M4

#include "activities/Activity.h"

// Simulator stub for the M4 build: the menu entry exists so the File Transfer
// flow can be exercised without hardware, but a host build has no USB OTG
// controller and no SD block device to hand over — the screen says so and
// Back returns home. Only the real build above performs any card work.
class UsbTransferActivity final : public Activity {
 public:
  UsbTransferActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("UsbTransfer", renderer, mappedInput) {}
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return true; }
};

#endif  // FREEINK_CAP_USB_MSC
