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

- Overlay (叠加): FAST 1-bit then gray `lut_grayscale` (two refreshes).
- Combined (合成): one FAST 1-bit refresh. Absolute 4-level (factory or
  VSL-only 00) left gray shadows on white and a slow full refresh. Overlay
  is the gray-edge mode.

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
