#include "HalSystem.h"

#include <BoardConfig.h>

#include <string>

#include "Arduino.h"
#include "HalStorage.h"
#include "Logging.h"
#include "driver/temperature_sensor.h"
#include "esp_debug_helpers.h"
#include "esp_mac.h"
#include "esp_private/esp_cpu_internal.h"
#include "esp_private/esp_system_attr.h"
#include "esp_private/panic_internal.h"
#include "esp_timer.h"

#if defined(__XTENSA__)
#include "xtensa_context.h"
#endif

#define MAX_PANIC_STACK_DEPTH 32
#define PANIC_CAPTURE_MAGIC 0x50414E49u

RTC_NOINIT_ATTR char panicMessage[256];
RTC_NOINIT_ATTR HalSystem::StackFrame panicStack[MAX_PANIC_STACK_DEPTH];
// Exception registers from the faulting frame (RISC-V mepc/mcause/mtval, or
// Xtensa pc/exccause/excvaddr stored in the same slots). panicMessage stays
// empty on the CPU-fault path (print_backtrace without panic_abort).
RTC_NOINIT_ATTR uint32_t panicMepc;
RTC_NOINIT_ATTR uint32_t panicMcause;
RTC_NOINIT_ATTR uint32_t panicMtval;
RTC_NOINIT_ATTR uint32_t panicRegsMarker;
// RTC_NOINIT is uninitialized on cold boot, so only this exact marker proves a
// panic diagnostic was captured before the reset.
RTC_NOINIT_ATTR volatile uint32_t panicCaptureMarker;

extern "C" {

void __real_panic_abort(const char* message);
void __real_panic_print_backtrace(const void* frame, int core);

static DRAM_ATTR const char PANIC_REASON_UNKNOWN[] = "(unknown panic reason)";
void IRAM_ATTR __wrap_panic_abort(const char* message) {
  if (!message) message = PANIC_REASON_UNKNOWN;
  // IRAM-safe bounded copy (strncpy is not IRAM-safe in panic context)
  int i = 0;
  for (; i < (int)sizeof(panicMessage) - 1 && message[i]; i++) {
    panicMessage[i] = message[i];
  }
  panicMessage[i] = '\0';
  panicCaptureMarker = PANIC_CAPTURE_MAGIC;

  __real_panic_abort(message);
}

static void IRAM_ATTR captureStackFromSp(uint32_t sp) {
  for (size_t i = 0; i < MAX_PANIC_STACK_DEPTH; i++) {
    panicStack[i].sp = 0;
  }
  const int per_line = 8;
  int depth = 0;
  for (int x = 0; x < 1024; x += per_line * sizeof(uint32_t)) {
    uint32_t* spp = reinterpret_cast<uint32_t*>(sp + x);
    panicStack[depth].sp = sp + x;
    for (int y = 0; y < per_line; y++) {
      panicStack[depth].spp[y] = spp[y];
    }
    depth++;
    if (depth >= MAX_PANIC_STACK_DEPTH) {
      break;
    }
  }
}

void IRAM_ATTR __wrap_panic_print_backtrace(const void* frame, int core) {
  if (!frame) {
    __real_panic_print_backtrace(frame, core);
    return;
  }

  // Register capture must happen before the marker: a reset between the two
  // would otherwise report garbage registers as real.
#if defined(__XTENSA__)
  // M4 is ESP32-S3 (Xtensa). The previous wrapper only decoded RISC-V frames,
  // so S3 CPU faults landed as empty "no abort message" reports with no PC.
  const XtExcFrame* exc = static_cast<const XtExcFrame*>(frame);
  panicMepc = static_cast<uint32_t>(exc->pc);
  panicMcause = static_cast<uint32_t>(exc->exccause);
  panicMtval = static_cast<uint32_t>(exc->excvaddr);
  panicRegsMarker = PANIC_CAPTURE_MAGIC;
  captureStackFromSp(static_cast<uint32_t>(exc->a1));
  panicCaptureMarker = PANIC_CAPTURE_MAGIC;
#elif defined(__riscv)
  const RvExcFrame* exc = static_cast<const RvExcFrame*>(frame);
  panicMepc = exc->mepc;
  panicMcause = exc->mcause;
  panicMtval = exc->mtval;
  panicRegsMarker = PANIC_CAPTURE_MAGIC;
  captureStackFromSp(exc->sp);
  panicCaptureMarker = PANIC_CAPTURE_MAGIC;
#endif

  __real_panic_print_backtrace(frame, core);
}
}

namespace HalSystem {

void begin() {
  // On a panic reboot, preserve diagnostics until checkPanic() has tried to write them to the SD card.
  // Ordinary boots clear any stale retained diagnostics.
  if (!isRebootFromPanic()) {
    clearPanic();
  } else {
    // Panic reboot: preserve logs and panic info, but clamp logHead in case the
    // panic occurred before begin() ever ran (e.g. in a static constructor).
    // If logHead was out of range, logMessages is also garbage — clear it so
    // getLastLogs() does not dump corrupt data into the crash report.
    if (sanitizeLogHead()) {
      clearLastLogs();
    }
  }
}

void checkPanic() {
  if (isRebootFromPanic()) {
    auto panicInfo = getPanicInfo(true);
    auto file = Storage.open("/crash_report.txt", O_WRITE | O_CREAT | O_TRUNC);
    if (file) {
      const size_t written = file.write(panicInfo.c_str(), panicInfo.size());
      file.close();
      if (written == panicInfo.size()) {
        // Keep the crash data for CrashActivity, but mark it consumed so a
        // later watchdog reset cannot be mistaken for this panic.
        panicCaptureMarker = 0;
        LOG_INF("SYS", "Dumped panic info to SD card");
      } else {
        LOG_ERR("SYS", "Failed to write complete crash report (%zu of %zu bytes)", written, panicInfo.size());
      }
    } else {
      LOG_ERR("SYS", "Failed to open crash_report.txt for writing");
    }
  }
}

void clearPanic() {
  panicCaptureMarker = 0;
  panicRegsMarker = 0;
  panicMessage[0] = '\0';
  for (size_t i = 0; i < MAX_PANIC_STACK_DEPTH; i++) {
    panicStack[i].sp = 0;
  }
  clearLastLogs();
}

const char* getDeviceModel() { return BoardConfig::ACTIVE.name; }

bool getDeviceId(DeviceId& out) {
  out.fill(0);
  if (esp_efuse_mac_get_default(out.data()) != ESP_OK) {
    LOG_ERR("SYS", "Failed to read eFuse device ID");
    return false;
  }
  return true;
}

bool getWifiStationMac(DeviceId& out) {
  out.fill(0);
  if (esp_read_mac(out.data(), ESP_MAC_WIFI_STA) != ESP_OK) {
    LOG_ERR("SYS", "Failed to read Wi-Fi station MAC address");
    return false;
  }
  return true;
}

bool getChipTemperatureCelsius(float& out) {
  out = 0.0f;
  temperature_sensor_handle_t sensor = nullptr;
  const temperature_sensor_config_t config = TEMPERATURE_SENSOR_CONFIG_DEFAULT(-10, 80);

  // ESP-IDF owns two short-lived allocations here (about 112 bytes of payload);
  // its opaque handle has no supported stack/static alternative.
  if (temperature_sensor_install(&config, &sensor) != ESP_OK) {
    LOG_ERR("SYS", "Failed to install chip temperature sensor");
    return false;
  }
  if (temperature_sensor_enable(sensor) != ESP_OK) {
    LOG_ERR("SYS", "Failed to enable chip temperature sensor");
    if (temperature_sensor_uninstall(sensor) != ESP_OK) {
      LOG_ERR("SYS", "Failed to uninstall chip temperature sensor after enable failure");
    }
    return false;
  }

  const bool readOk = temperature_sensor_get_celsius(sensor, &out) == ESP_OK;
  if (!readOk) LOG_ERR("SYS", "Failed to read chip temperature");

  const bool disableOk = temperature_sensor_disable(sensor) == ESP_OK;
  if (!disableOk) LOG_ERR("SYS", "Failed to disable chip temperature sensor");

  const bool uninstallOk = temperature_sensor_uninstall(sensor) == ESP_OK;
  if (!uninstallOk) LOG_ERR("SYS", "Failed to uninstall chip temperature sensor");

  return readOk && disableOk && uninstallOk;
}

uint64_t getUptimeSeconds() { return static_cast<uint64_t>(esp_timer_get_time()) / 1000000ULL; }

HeapInfo getHeapInfo() {
  return {static_cast<uint32_t>(ESP.getFreeHeap()), static_cast<uint32_t>(ESP.getHeapSize()),
          static_cast<uint32_t>(ESP.getMaxAllocHeap())};
}

std::string getPanicInfo(bool full) {
  if (!full) {
    return panicMessage;
  } else {
    // Faults that route through the panic handler's print_backtrace path set
    // the capture marker without ever calling panic_abort, leaving
    // panicMessage empty and the report's reason line blank. Name the
    // hardware reset reason so those reports still say what kind of reset it
    // was (empty-reason crash reported 2026-09-03).
    const char* resetReasonLabel = "other";
    switch (esp_reset_reason()) {
      case ESP_RST_PANIC:
        resetReasonLabel = "panic";
        break;
      case ESP_RST_CPU_LOCKUP:
        resetReasonLabel = "cpu lockup";
        break;
      case ESP_RST_INT_WDT:
        resetReasonLabel = "interrupt watchdog";
        break;
      case ESP_RST_TASK_WDT:
        resetReasonLabel = "task watchdog";
        break;
      case ESP_RST_WDT:
        resetReasonLabel = "rtc watchdog";
        break;
      case ESP_RST_BROWNOUT:
        resetReasonLabel = "brownout";
        break;
      case ESP_RST_PWR_GLITCH:
        resetReasonLabel = "power glitch";
        break;
      default:
        break;
    }
    char reasonLine[160] = {};
    if (panicMessage[0] == '\0') {
      snprintf(reasonLine, sizeof(reasonLine), "(no abort message; reset reason: %s)", resetReasonLabel);
    } else {
      snprintf(reasonLine, sizeof(reasonLine), "%s (reset reason: %s)", panicMessage, resetReasonLabel);
    }

    // Faulting instruction + cause from the exception frame.
    // RISC-V mcause low nibble: 2=illegal, 5=load, 7=store, 11=ecall.
    // Xtensa exccause: 0=illegal, 9=load prohibited, 10=store prohibited.
    char excLine[160] = {};
    if (panicRegsMarker == PANIC_CAPTURE_MAGIC) {
      const char* cause = "exception";
#if defined(__XTENSA__)
      switch (panicMcause) {
        case 0:
          cause = "illegal instruction";
          break;
        case 9:
          cause = "load prohibited";
          break;
        case 10:
          cause = "store prohibited";
          break;
        default:
          break;
      }
      snprintf(excLine, sizeof(excLine), "\n\nCPU exception: %s (exccause=0x%08lX) at pc=0x%08lX, excvaddr=0x%08lX",
               cause, static_cast<unsigned long>(panicMcause), static_cast<unsigned long>(panicMepc),
               static_cast<unsigned long>(panicMtval));
#else
      switch (panicMcause & 0xF) {
        case 2:
          cause = "illegal instruction";
          break;
        case 5:
          cause = "load access fault";
          break;
        case 7:
          cause = "store access fault";
          break;
        case 11:
          cause = "environment call";
          break;
        default:
          break;
      }
      snprintf(excLine, sizeof(excLine), "\n\nCPU exception: %s (mcause=0x%08lX) at mepc=0x%08lX, mtval=0x%08lX", cause,
               static_cast<unsigned long>(panicMcause), static_cast<unsigned long>(panicMepc),
               static_cast<unsigned long>(panicMtval));
#endif
    }

    std::string info;

    info += "CrossPoint version: " CROSSPOINT_VERSION;
    info += "\n\nPanic reason: " + std::string(reasonLine);
    info += excLine;
    info += "\n\nLast logs:\n" + getLastLogs();
    info += "\n\nStack memory:\n";

    auto toHex = [](uint32_t value) {
      char buffer[9];
      snprintf(buffer, sizeof(buffer), "%08X", value);
      return std::string(buffer);
    };
    for (size_t i = 0; i < MAX_PANIC_STACK_DEPTH; i++) {
      if (panicStack[i].sp == 0) {
        break;
      }
      info += "0x" + toHex(panicStack[i].sp) + ": ";
      for (size_t j = 0; j < 8; j++) {
        info += "0x" + toHex(panicStack[i].spp[j]) + " ";
      }
      info += "\n";
    }

    return info;
  }
}

bool isRebootFromPanic() {
  const auto resetReason = esp_reset_reason();
  if (resetReason == ESP_RST_PANIC || resetReason == ESP_RST_CPU_LOCKUP) {
    return true;
  }

  const bool watchdogReset =
      resetReason == ESP_RST_INT_WDT || resetReason == ESP_RST_TASK_WDT || resetReason == ESP_RST_WDT;
  return watchdogReset && panicCaptureMarker == PANIC_CAPTURE_MAGIC;
}

}  // namespace HalSystem
