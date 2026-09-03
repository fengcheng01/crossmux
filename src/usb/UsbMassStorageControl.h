#pragma once

#include <atomic>

// Set while a USB Mass Storage session owns the SD card (UsbTransferActivity
// entered and the FAT volume detached). The main loop's power-button sleep
// path bypasses preventAutoSleep(), so it consults this flag instead: deep
// sleep with the volume detached would write APP_STATE and the sleep frame
// onto a dead filesystem, and the USB session would end uncleanly.
inline std::atomic<bool>& usbMscActiveFlag() {
  static std::atomic<bool> active{false};
  return active;
}

inline bool usbMscActive() { return usbMscActiveFlag().load(std::memory_order_relaxed); }
