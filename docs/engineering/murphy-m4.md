# Murphy M4 experimental target

`murphy_m4` is a separate ESP32-S3 N16R8 build. It inherits the normal DIO
flash mode, 16 MB partition table, one 48,000-byte framebuffer, OPI PSRAM and
USB CDC settings, and uses native 4-bit SDMMC storage.

## ⚠️ 硬件约束（无 ESD 防护批次）— 改 GPIO 相关代码前必读

厂商确认**本生产批次未做防静电（ESD）防护**，同批次已有多台设备因静电
故障返厂（2026-08 确认）。对软件的硬性要求：

1. **用户可触及的引脚严禁运行时输出驱动**：GPIO0=电源键、GPIO1=上键、
   GPIO2=下键。不得切换为输出模式、不得剥离其上拉/下拉。静电脉冲可
   直接击穿无防护的输出级；按键被按住时输出驱动等同于对地短路。
2. **上电早期不做主动电气操作**：电源域未稳时不做 ADC 采样、引脚方向
   切换、充放电。等电源稳定、外设就位后再做。
3. **每次开机路径上的 GPIO 操作都要过一遍**："用户此刻可能正按着按键吗？"

前车之鉴：开机面板批次探测曾对 GPIO1（上键）输出放电 2ms×7 并剥离上拉，
一台设备随后完全冻结（官方固件同样无响应、电源键无效、仅 USB 可识别），
已返厂。该探测已退役（`lib/hal/HalGPIO.cpp` 中 `probeMurphyM4Batch` 保留
但永不执行），批次信息只从 NVS 或编译期默认读取。此探测 technique 只允
许工厂夹具在受控工位使用，不得回到用户设备的开机路径。

插拔 USB 线是静电引入的常见时机；向用户建议插拔前先释放身上静电（触摸
金属门窗/暖气），选用带屏蔽的优质数据线。

```bash
pio run -e murphy_m4
pio run -e murphy_m4 -t upload
pio run -e simulator_murphy_m4 -t run_simulator
```

Hardware profiles and drivers live in the pinned `0x1abin/freeink-sdk`
submodule on its long-lived `crossmux` branch. The M4 button, FT6336U touch and
GPIO/LEDC frontlight paths track upstream FreeInk commit `e4d3cc33`; CrossMux
retains the two display batches, RX8010, GPIO43 charge input, SDMMC and product
gates. AirPage, reading, library, settings, Web file transfer, and same-target
SD firmware update remain available; remote OTA/catalog publication remains
withheld.

The M4 keeps its boot CPU frequency fixed because hardware validation found
FT6336U input unreliable after runtime clock changes. Idle power saving still
uses the normal 50 ms main-loop delay. Touch initialization reads back the
volatile mode, threshold, and report-rate registers before accepting input;
invalid status/event/coordinate frames are discarded without latching contact.

Reader anti-aliasing on this panel:

- Swift (快刷叠刷, experimental, v36): the TW recipe rebuilt as its own AA
  mode — pass 1 is a differential REPAINT of the B/W frame with a halved-TP
  variant of the TW repaint table (`lut_m4_repaint_swift`; every pixel is
  driven toward its target, whites stay clean, no inversion), pass 2 is the
  TW weak edge drive verbatim (`lut_m4_edge_weak`, ~7 frames; BW = the new
  frame's absolute MSB plane, RED = its complement so edge bits get the
  weak black-ward nudge and land gray). `displaySwiftAa` in the driver owns
  both activations; the reader renders only the MSB plane. If whites dirty,
  step `lut_m4_repaint_swift`'s TP back up toward `lut_m4_repaint_fast`.
  EPUB-only; TXT falls back, night mode and backgrounds fall back to the
  overlay path.


- REVERTED (v39): v35 wired the TW repaint LUT (`lut_m4_repaint_fast`,
  registry slot 1, byte-identical to stock v632 "B") into EVERY full-screen
  FAST refresh — on the device this showed a black flash on every
  interaction, so it was rolled back to the stock differential FAST
  everywhere. The table remains available for experiments; the opt-in
  Swift AA mode carries its own repaint tables through displaySwiftAa and
  is unaffected by this revert.


- Overlay (叠加): FAST 1-bit then gray `lut_grayscale` (two refreshes).
- Combined (合成): one FAST 1-bit refresh. Absolute 4-level (factory or
  VSL-only 00) left gray shadows on white and a slow full refresh. Overlay
  is the gray-edge mode.
- Direct (直刷, experimental): one absolute gray refresh per turn — no B/W
  paint. The 2-bit planes use the absolute four-level encoding (11=black …
  00=white) and `lut_m4_aa_direct` drives them — a hybrid of two vendor
  tables, all convergent drive-toward-target groups, no inversion: 00/11
  take vendor B's white/black REPAINT sequences (A8 00 55 / 54 00 AA,
  real doses, no opposite-extreme swing), 01/10 keep lut_grayscale's edge
  phases, timing is B's verbatim (TP 0C 0D …, FR 0x22, VCOM 0x30).
  v29-v31 proved erase-class drives need the full 4A→88 inversion cycle
  (cleaning AND flashing are inseparable that way); B's repaint drives
  standing pixels invisibly at their endpoint, so whites stay clean every
  turn without a flash. If the grays land too dark under B's longer TP,
  swap in milder 55-class phases. cleanWhite=true still
  runs the factory tier. Night mode and reading backgrounds fall back to
  the overlay path. The desktop simulator keeps previewing 11 as dark gray
  (its shim models lut_grayscale), so core blackness is only meaningful on
  hardware.

Silent restarts (USB eject/disconnect, web-server teardown) save the panel's
physical frame before the undisplayed loading popup joins the framebuffer,
and `BootResume::Silent` seeds RED from it — the first FAST home paint drives
the pre-reboot screen's ink away instead of ghosting it (same mechanism as
ClockUnlock). The driver's `displayWindow` streams rows with no heap buffer
(a ~29KB std::vector aborted under -fno-exceptions in the sleep-tick boot,
where every store has just loaded — the empty-reason panic of 2026-09-03).

The CALENDAR/COUNTDOWN lock faces paint entry with HALF (one flash, the
clock lock's choice) and the daily timer tick with `sleepCleanRefresh()`
(FULL on M4 — a face that sits ~24h needs the deep bleach, and nobody is
watching). Entry AND tick both save the unlock frame
(`saveSleepFrameBuffer()`) so `BootResume::ClockUnlock` seeds RED from what
is actually on the panel — the entry save was the missing half of the wake
ghost fix.

Opening the reader menu/settings over an AA page used HALF (0xD4), which
black-flashes. That path now resyncs grayscale RAM then FAST. Home from
the reader is FAST. Clock lock: one HALF to bleach the white field, then
windowed FAST on the digit band only so the background is not re-driven.

The desktop target (`simulator_murphy_m4`) defines both
`SIMULATOR_DEVICE_MOFEI_M4` — the pinned simulator fork still uses the
pre-rename board name for its 800x480 profile with touch, rotation, RTC,
buttons, and dual-channel frontlight state — and `FREEINK_DEVICE_MURPHY_M4`,
so all firmware-side M4 gates (combined AA, M4 settings, board tag) run on the
host. Use the mouse for touch, arrows for Up/Down, `P` for Power, and `S` for
sleep; M4 has no Home key, so `H` is ignored. The host shim cannot model the
clock-sleep timer wake (`WakeupReason::Timer`, the two-argument
`startDeepSleep`) and has no environment sensor (`HalEnvironment` reports
absent), and the shim's gray preview stands in for the absolute waveform. It
does not replace hardware tests for display batches/ghosting, FT6336U
IRQ/reset behavior, SDMMC contention, PWM curves, PSRAM, or standby current.

The default SSD1677 configuration targets the second production batch with
R13 fitted and uses the verified `0x50` pseudo-temperature. To build for the
first no-R13 batch, add `-DFREEINK_MURPHY_M4_BATCH1=1` in
`platformio.local.ini`; this selects `0x3C`. Keep this compile-time until both
batches pass the display gate.

## First flash and backup

Back up the complete flash before the first write:

```bash
esptool --chip esp32s3 --port /dev/ttyACM0 read-flash 0 0x1000000 murphy-m4-backup.bin
shasum -a 256 murphy-m4-backup.bin
```

Keep that backup outside the device. It is the only full-flash recovery image;
the Beta release contains only the four segments required by the Web installer.
Restore the original backup with:

```bash
esptool --chip esp32s3 --port /dev/ttyACM0 write-flash 0 murphy-m4-backup.bin
```

Do not flash an ESP32-C3 or eego A4 artifact. The first-install flow writes the
bootloader at `0x0`, partition table at `0x8000`, `boot_app0.bin` at `0xe000`,
and app at `0x10000` without overwriting NVS.

## USB Mass Storage ("USB 传输")

`env:murphy_m4_cn` builds with `FREEINK_CAP_USB_MSC=1` and
`ARDUINO_USB_MODE=0`: the File Transfer menu's mode list gains a "USB 数据线"
entry that hands the whole SD card to the PC as a mass-storage device
(`UsbTransferActivity` + the SDK `UsbMassStorage` library over TinyUSB). The
FAT volume is detached (`SDCardManager::detachFilesystemForRawAccess()`, via
`HalStorage::detachForRawUsbAccess()`) and the card is served as raw sectors;
the firmware must not touch the card while the session is live.

Build-stack notes (why the env looks the way it does):

- arduino-esp32 ships TinyUSB only in its prebuilt libs; the pioarduino
  custom-sdkconfig core rebuild drops every `CONFIG_TINYUSB_*` symbol unless
  the `espressif/esp_tinyusb` component is in the rebuild's component set.
- Do NOT re-add `custom_component_add: espressif/esp_tinyusb`: registry
  versions newer than the core's vendored tinyusb 0.20 renamed the `CFG_TUD_*`
  config macros, and their sources fail to compile against the pristine
  headers (or vice versa). Instead the env compiles the Arduino core against
  the pristine `framework-arduinoespressif32-libs/esp32s3/include/arduino_tinyusb`
  headers (first in the include path) and defines
  `CONFIG_TINYUSB_{ENABLED,CDC_ENABLED,MSC_ENABLED}=1` on the command line;
  the pristine `sdkconfig` lines in `custom_sdkconfig` only exist to force the
  one-time core rebuild and to restore the symbols if the rebuild repeats.
- `ARDUINO_USB_MODE=0` makes `Serial` the TinyUSB CDC instead of the hardware
  USB-Serial/JTAG (`lib/Logging/Logging.h` binds `logSerial` accordingly).
  Flashing is unaffected — the ROM's USJ owns the USB pads until the app runs —
  but the serial console speaks TinyUSB CDC while the app is running.

Session rules (enforced by `UsbTransferActivity`):

- Entry is the File Transfer mode list only, so no reader activity with an
  open chapter-build handle can be on the stack. SD-font caches are released
  and stats/state stores flushed before the volume detaches.
- `preventAutoSleep()` blocks the idle timer; the power long-press and
  `enterDeepSleep()` additionally check `usbMscActive()`
  (`src/usb/UsbMassStorageControl.h`) because that path bypasses
  `preventAutoSleep()` and would write `APP_STATE` onto a detached volume.
- The home gesture is consumed while the session is live; Back cancels only
  before a host has ever connected (in-place re-mount via `Storage.begin()`).
  After a host connected, eject/disconnect/error always ends in
  `silentRestart()` — the SDK-documented reboot-or-remount contract, matching
  the web-server activity's teardown precedent.
- The no-ESD hardware constraint above applies to plugging/unplugging the
  cable as to any USB event.
- The entry also appears on `simulator_murphy_m4` as a stub screen ("needs
  real hardware"): a host build has no USB OTG controller or SD block device,
  so only the menu flow is exercisable there.

## Hardware release gate

- Verify direction, edge pattern, Full/Fast/grayscale and ghosting on both
  display batches.
- Verify four-corner touch, swipes and rotations, including a short touch while
  an e-paper refresh blocks the main loop; confirm GPIO44 active-low IRQ and
  that GPIO7 display reset is followed by successful FT6336U reinitialization
  with `0x00=0x00`, `0x80=0x16`, and `0x88=0x04` read back correctly. Confirm
  invalid frames neither create phantom touches nor leave an active touch stuck.
- Verify GPIO1/2 navigation and GPIO0 shared input: short press emits only
  Confirm (or only Power when the existing short-power option is enabled), and
  long press emits only Power.
- Exercise touch while repeatedly reading/writing RX8010; confirm both devices
  share I²C1 without conflicts or bus/device recreation. Also verify concurrent
  4-bit SDMMC/display use, ADC9 battery, active-low GPIO43 charging, and RX8010
  power-loss retention/VLF handling.
- Measure GPIO47/48 at about 25 kHz / 10-bit and verify the gamma curve at
  0/1/5/50/100%, both color-temperature endpoints, off, and wake restoration.
- Cycle deep sleep and confirm GPIO10/45 rails turn off, frontlight is off,
  GPIO0 wakes the device, and standby current is stable.
- Repeatedly alternate more than three seconds of idle time with touch input;
  confirm the CPU clock remains at its boot frequency and touch stays responsive.
- Record free heap, minimum free heap, largest block and PSRAM before/after
  initialization and through repeated touch/RTC/sleep, reading, grayscale and
  Wi-Fi cycles. The removed M4-only touch task must be absent, I²C handles must
  be allocated only at startup, and no metric may show a continuing decline.

SC7A20, runtime panel-batch settings, remote OTA/catalog publication and
complex SD fallback remain outside this experimental target. AHT20 on I²C1
`0x38` (shared with FT6336U / RX8010) is used for lock-screen and standby
temperature/humidity.
