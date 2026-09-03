#pragma once

#include <BoardConfig.h>

#include "activities/UiListActivity.h"

// Real M4 builds run the USB Mass Storage session; the M4 simulator shows a
// stub screen (no USB OTG or SD block device on the host) so the menu flow
// stays exercisable. Other boards and simulators hide the entry.
#if (defined(FREEINK_CAP_USB_MSC) && FREEINK_CAP_USB_MSC) || \
    (defined(SIMULATOR) && defined(FREEINK_DEVICE_MURPHY_M4) && FREEINK_DEVICE_MURPHY_M4)
#define NETWORK_MODE_HAS_USB 1
#else
#define NETWORK_MODE_HAS_USB 0
#endif

enum class NetworkMode {
  JOIN_NETWORK,
  CONNECT_CALIBRE,
  CREATE_HOTSPOT
#if NETWORK_MODE_HAS_USB
  ,
  USB_TRANSFER
#endif
};

/**
 * NetworkModeSelectionActivity presents the user with a choice:
 * - "Join a Network" - Connect to an existing WiFi network (STA mode)
 * - "Connect to Calibre" - Use Calibre wireless device transfers
 * - "Create Hotspot" - Create an Access Point that others can connect to (AP mode)
 * - "USB Transfer" - Hand the SD card to the PC over USB (USB-MSC boards only)
 *
 * The onModeSelected callback is called with the user's choice.
 * The onCancel callback is called if the user presses back.
 *
 * The header stays on GUI.drawHeader for the battery indicator.
 */
class NetworkModeSelectionActivity final : public UiListActivity {
 public:
  explicit NetworkModeSelectionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  static constexpr int MENU_ITEM_COUNT = 3 + NETWORK_MODE_HAS_USB;

  void onModeSelected(NetworkMode mode);
  void onCancel();

 private:
  int listCount() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onBackButton() override { onCancel(); }
  const char* headerTitle() const override;

  // Row storage: entirely static (label/subtitle/icon never change), so it's
  // built once in the constructor instead of every buildScreen() call, into
  // fixed-capacity storage that avoids any heap allocation for the row list.
  freeink::ui::ListItem rowItems_[MENU_ITEM_COUNT]{};
};
