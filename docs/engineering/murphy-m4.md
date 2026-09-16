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


- Single-flash (单闪): one vendor E absolute four-level activation using
  `displayGrayBufferAbsolute(false)` / `lut_m4_aa_direct`. Render both planes
  before activating; the previous page stays visible during rendering. EPUB
  (buffered and strip fallback) and TXT no longer call `flashToWhite()` or
  `displayGrayBufferFromWhite()` for this mode. The complete white clean sequence
  replaces the experimental `lut_m4_from_white` late settle. The two LUTs differ
  only at byte 1 of the white group (`0x00` becomes `0x4A`): the light gray,
  dark gray, black, timing and voltage bytes are preserved from v126. This is a flashing
  quality mode; one controller activation does not guarantee one visible pulse.
  Final background cleanliness and the visible transition need M4 hardware QA.
  Night mode / reading backgrounds still use the overlay path.

- Overlay (叠加): FAST 1-bit then gray `lut_grayscale` (two refreshes).
- Combined (合成): removed in v131; old stored selections migrate to Off.
- Direct (无闪直刷（灰阶试验）): v129 sends absolute two-bit glyph coverage,
  including the status bar, using one `lut_m4_direct_pulse` activation. There is
  no preceding B/W page activation or later gray overlay on ordinary light-mode
  pages. v127/v128's 4x4 B/W spatial pattern was reported jagged and is no longer
  enabled by the reader. Night mode and reading backgrounds retain one B/W FAST
  refresh. Automatic HALF cleanup remains suppressed; explicit EPUB manual
  cleanup still works. Low-memory allocation failure falls back to B/W.

### v129 pulse grayscale experiments

Hardware feedback: Single-flash's full vendor E waveform produces good final
CangEr JinKai text, but flashes black. Both the original idle-white table and
v128's `lut_m4_white_hold` produced dirty/dark intermediate backgrounds. The
latter was also reported slow; it is retained as historical data, not selected.

White flash (快速灰阶试验), setting value 6, still OTP-whitens first. It then
uses `lut_m4_white_pulse`, removing the old 60-frame erase/settle sequence in
favor of 24 timing frames. White targets select VSL throughout; light gray,
dark gray and black select VSH1 for 9, 12 and 24 frames respectively. The
accepted Single-flash mode (value 5) remains unchanged as a comparison.

Direct (value 3) uses a separate 36-frame sequence. v129 drove black in
frames 0..24, light gray in 24..33, and dark gray in 24..36. Hardware feedback
reported thin text followed by thickening. v130 retains the total doses and
frame count but moves black to 12..36 and light gray to 27..36; dark gray stays
24..36. Gray targets still receive 24 VSL preconditioning frames. All three
ink pulses now end at frame 36. This is intended to reduce the staggered
appearance; equal electrical end times do not prove equal optical settling.
There is no separate B/W paint, full-white activation or later gray overlay.

These are calibration candidates, not validated no-flash/white-flash waveforms.
24 is the E waveform's black VSH1 total; 9 and 12 are estimates from its gray
group's positive-minus-negative frame counts. Pigment motion is not linear in
net dose. Previous VSL-only cleaning failed on this panel: direct may retain
old-page ink, and neither candidate has proven optical whiteness or gray levels.
Frame counts exclude OTP whitening, rendering, transfers, power-up and BUSY
handling; they are not measured page-turn times. Frame-rate and analog-voltage
registers remain unchanged. Source code mapping follows
[SSD1677 Rev 1.0, table 6-6](https://files.waveshare.com/upload/2/2a/SSD1677_1.0.pdf);
VSS/VSL selection alone does not establish the optical result relative to VCOM.

EPUB prepares both planes before whitening when its existing buffers fit. It
waits for OTP BUSY before writing either controller RAM plane, since whitening
overwrites both. TXT overlaps whitening with LSB rendering, then waits before
writing. There is no separate B/W paint before grayscale, nor any activation
in the final differential-baseline resynchronization.

Memory: two 112-byte constexpr flash tables, no new allocation sites. Direct
now reuses the existing grayscale pipeline: EPUB can temporarily use two
48,000-byte planes with heap/PSRAM headroom checks, falling back to one plane
or an 8,000-byte strip; TXT uses the existing chunked 48,000-byte B/W backup.
Thus Direct uses more working memory than the previous one-bit implementation.
Allocation failure before activation displays B/W. Once controller planes are
being written, complete the page before honoring navigation; an earlier abort
keeps the previous page visible. Night/background white-trial fallback stays
on the existing overlay path. Persisted setting IDs and defaults are unchanged.

### v130 grayscale-to-UI cleanup

User feedback accepted v129 White flash but reported residue when opening
settings or returning home in both Direct and White flash. Its 24-frame white
pulse LUT remains byte-identical in v130. The reader/menu paths resynchronized
RED from the thresholded B/W backup, then requested FAST. That restored RAM
contents but did not remove physical intermediate gray ink; the driver also
considered factory gray self-cleaned, so no physical exit cleanup ran.

M4 now tracks absolute grayscale on the panel separately from RAM validity.
The next B/W paint first performs OTP whitening and waits for completion,
then seeds RED to white and paints the prepared target frame. A facade-provided
previous-frame pointer is discarded for this paint because the actual panel
is now white. Window requests repaint the full composed framebuffer after
whitening, preserving content outside the window. RAM-only cleanup does not
consume this state. Repeated grayscale pages do not invoke the B/W exit path;
an explicit white activation consumes it. Other SSD1677 boards opt out.

This adds one white interval when leaving a gray page for B/W UI. The firmware
adds two boolean fields (one board policy and one panel-state flag), no heap
allocation or framebuffer. White-baseline writes reuse EpdBus::fillPlane's
128-byte stack chunk. Recording-bus host tests exercise the actual driver for
RAM-only cleanup, repeated gray pages, one-shot UI cleanup, partial windows,
asynchronous completion and other-board isolation. They cannot validate
physical residue or flash color; repeat these transitions on M4 hardware.

### v131 clock lock and retired Combined AA

User feedback: v130 White flash is satisfactory; Direct resembles a faster
white flash. Both reading waveforms remain byte-identical in v131. Combined
AA is removed from the text picker and web settings, and its reader branch is
removed. Persisted ID 2 remains reserved and migrates to Off, the corresponding
one-bit behavior. Other stored IDs remain 0/1/3/4/5/6; picker indices are mapped
separately. JSON explicitly reads/writes the stable ID because the dynamic
picker is excluded from the generic persistence loop; legacy binary loading
uses the same normalization. No stored binary layout changes.

The reported lock problem uses the Clock face. v130 could white-clean gray
residue and then run the Clock's requested HALF, re-driving the white field;
its inverted-entry helper could also request FULL. v131 Clock entry and hourly
cleanup instead use an explicit white-clean paint through GfxRenderer and HAL.
M4 runs OTP white, waits, seeds the white baseline and paints FAST, regardless
of whether the prior screen was gray text or B/W Home. A pending gray exit and
a Clock clean coalesce into the same one-shot request. The known-white state
consumes boot's initial HALF/FULL promotion on hourly timer wake. Minute ticks
remain windowed; other lock faces keep their existing policies. The saved
clock framebuffer stays intact for unlock baseline restoration.

There are no new framebuffers or heap buffers. The explicit request reuses the
driver's existing pending-white boolean and fillPlane stack chunk. AA choices
reuse the existing settings vector and captureless DynamicEnum accessors.
Host bus tests cover gray/Home clock entry and initialized timer wake: each
must emit exactly white FAST + target FAST with no HALF/FULL sequence. Test
real Clock entry from both readers, Home and night mode, then a minute tick,
an hourly tick and unlock. Check residual text, background dirt and flash
color; bus traces cannot establish optical cleanliness.

### v132 clock endpoint paint and completion wait

Hardware rejected v131: after entering Clock lock the field progressively
became dirty, with severe negative-text residue across the background. The
v131 OTP-white + B/W FAST trace was electrically as intended, but its premise
that white preconditioning left an optically clean field was not validated.

Explicit Clock white-clean now uses OTP whitening followed by the exact
accepted `lut_m4_white_pulse`, with both controller planes streamed as ~fb.
Thus white targets select 00 and black targets select 11; no intermediate gray
values occur in a binary Clock face. Unlike the v131 differential target paint,
this waveform actively drives white endpoints as well as the black clock.
No LUT bytes, voltages or reading AA paths are retuned. The two activations are
white FAST + absolute custom 0xCC, with no HALF/FULL inversion train. A promoted
HALF from the facade's night-mode transition is superseded by this explicit
request. Normal gray-to-menu cleanup retains its v130 behavior.

After completion, both RAM planes are restored to the binary clock target
and the pending preparation is cleared, so minute ticks and unlock can use a
matching baseline. The request state is now an enum (None/GrayExit/WhiteClean)
to distinguish ordinary UI cleanup from explicit clock endpoint painting.
An explicit clean completes synchronously even via displayStart and reports
that fact. Other SSD1677 boards opt out via a null config LUT pointer.

Absolute-gray completion also now uses waitRefreshComplete instead of
waitBusy. The ActiveHigh level-only wait could return before BUSY asserted;
RAM resync or sleep power-off could then interrupt an active waveform. The
refresh-specific wait handles delayed assertion on the normal interrupt and
slice-hook paths. Its existing no-semaphore fallback and bounded waits remain
unchanged. [SSD1677 command 0x20](https://files.waveshare.com/upload/2/2a/SSD1677_1.0.pdf)
requires the host not to interrupt the activation while BUSY. This race is a
code finding, not a claim that it alone caused the photographed failure.

No new heap allocation or framebuffer: inverse writes reuse the driver's
128-byte stack chunk, the configuration references the existing LUT, and one
byte enum replaces the pending boolean. Recording-bus tests check the actual
loaded LUT, 00/11-only planes, restored binary baseline, no extra UI clear,
polarity-promotion override, cold wake, deferred-call reporting and power-off.
A delayed-BUSY model rejects RAM writes or shutdown before gray completion.
Hardware QA must check Clock entry immediately, after settling/power-off,
after a minute tick, and on unlock; the model cannot prove optical cleanliness.

### v133 clock sleep waveform and explicit shutdown

Hardware rejected v132 as well: the Clock face rapidly developed a mottled
background after locking. The short reading white pulse remains acceptable
for page turns, but that does not establish that it leaves a stable image
through power-down. No measured electrical trace yet establishes the root cause.

Clock entry and hourly cleanup now use `displaySleepClean()` through renderer,
HAL and SDK. M4 selects the existing full vendor E table (`lut_m4_aa_direct`),
including all 60 timing frames, instead of OTP whitening plus the 24-frame
reading pulse. Both planes still stream ~fb to select only white/black endpoints.
There is exactly one display activation (0xCC), with no preliminary OTP clear
or HALF/FULL activation stacked on it. The table has a visible black cleaning
phase; this candidate trades one black flash for a complete cleanup sequence,
not a repeated black-flash train. Reading white/direct LUTs remain unchanged.

The sleep clean always waits for completion and powers the analog/clock off
(0x03), regardless of the reading fading-fix setting. Only then are both RAM
planes restored to the binary baseline. The later deepSleep call sends 0x10
without another master activation. Power-on also uses the delayed-assertion
aware completion wait before issuing subsequent commands. Ordinary minute
window updates and ordinary gray-to-menu cleanup retain their existing paths.

The change reuses the existing LUT and 128-byte stack streaming buffer, with no
new firmware heap allocation or framebuffer. Recording-bus tests check exact
full-E LUT bytes, endpoint polarity, a single paint followed by a single
power-off, late BUSY on power-on/paint, cold wake, polarity promotion, and
absence of another activation at deep sleep. Host tests verify command order;
they do not establish optical stability after power-down.

Hardware validation: from EPUB and TXT white-flash reading, lock the clock and
inspect immediately, after 10 seconds, after 60 seconds and after a minute tick.
Repeat from Home and with the reading fading fix disabled. Check that the field
stays white, prior text does not emerge, entry has no repeated black flashes,
and normal minute changes do not perform a full-screen clean. Also unlock and
confirm the reading white-flash behavior is unchanged.

### v135 Direct light-edge calibration trial

The user accepts Direct's overall appearance but finds diagonal edges slightly
harder than Overlay in matched-page photographs. The photographs do not establish
the correct optical gray values, so this trial changes one variable only:
Direct's light-gray VSH1 dose goes from 9 to 8 frames. The spare frame becomes
idle before that dose (3 -> 4), keeping its 24-frame white preconditioning and
its end at frame 36. Dark gray remains 12 ink frames; black remains 24. The
per-frame drive of white, dark gray and black is unchanged, despite the shared
timing-group split changing from 12/12/3/9 to 12/12/4/8.

This tests whether a lighter outer edge gives a smoother transition. It may
instead make thin edges less visible; improvement is not established by code
checks. Direct still performs one display activation. Voltage, frame-rate,
font coverage mapping, reading white flash, Overlay and clock sleep are unchanged.
No runtime allocation or additional framebuffer is introduced.

The waveform host test compares every target's per-frame source with the accepted
baseline and allows only the single light-edge ink-to-idle change. It also checks
end times, totals, white-flash doses and voltage/rate bytes. Device QA: compare
the same page/font/size/weight at a fixed camera position after settling, especially
人/以/厉 diagonals and fine tips. Repeat across 30 page turns in EPUB and TXT;
check for fading, stronger jaggies, residual ink, and thin-then-thick transitions.
The pre-build v134 binary is preserved alongside the v135 test artifact for rollback.

### v136 Clock B/W cleanup and Direct shared ink window

Device feedback on v135: Clock sleep still has severe residue, and Direct
still visibly changes from thin, smooth text to thicker, rougher text. The
v133 full-E sleep experiment is therefore not an accepted solution. Completion
and power-off ordering tests cannot establish that its physical field is clean.

Clock entry/hourly cleanup now selects the existing M4 OTP HALF sequence
(0xD4) once with CTRL1 BYPASS_RED and normal B/W target polarity in both RAM
planes. It consumes gray-exit preparation before painting, reloads the OTP LUT
instead of the custom gray table, respects the existing batch-specific HALF
temperature, waits, restores both binary RAM planes, then powers off. Thus
neither the former white preclear nor a later gray paint is stacked onto HALF.
Deep sleep adds no display activation; ordinary minute updates stay local.
This is a single clean request, not the multi-inversion FULL (0xF7) request;
the visible flash and cleanup still need device verification. The analog-off
sequence (0x22=0x03) remains as documented in the
[SSD1677 command table](https://files.waveshare.com/upload/2/2a/SSD1677_1.0.pdf).

Direct v135 synchronized ink END times but not START times: black began at
frame 12, dark gray at 24, light gray at 28. That code finding is consistent
with a visible core followed by expanding gray edges. v136 keeps gray erase
at 24 frames, delays black ink until then, and distributes the gray ink pulses
inside the same 24-frame write window. All three start at frame 24 and finish
at 48. Black retains 24 ink frames, light retains 8, and dark is reduced from
12 to 10 as an optical calibration trial to reduce edge darkening. White keeps
its original first 36 erase frames and then idles for 12; there is no white
inversion pulse. Voltages and frame-rate bytes are unchanged. Sparse gray
pulses can have a different optical response than contiguous pulses, so equal
start/end times and pulse counts do not prove equal pigment settling.

The tradeoff is 48 timing frames instead of 36 (one third longer at the same
frame rate); actual page-turn time also includes rendering and SPI. There is
still one Direct activation, with no B/W preview or edge overlay. White-flash
and Overlay reading modes, font coverage and font weight are unchanged. LUT
construction is compile-time, with no new runtime allocation or framebuffer.

Host checks cover Direct's common start/end, 8/10/24 ink doses, complete gray
erase, unchanged white drive/voltage/rate, and absence of white-going reversal
once ink starts. Recording-bus checks cover a single sleep 0xD4, BYPASS_RED,
normal B/W planes, no custom LUT upload, batch temperature, delayed BUSY,
power-off, minute updates and no extra activation at deep sleep. Hardware QA:
compare Direct in EPUB/TXT using CangEr JinKai, including a slow-motion video
and a settled photo; check the thin-to-thick interval, fine strokes, gray loss,
page speed and residue after 30 turns. Lock from both gray modes and Home,
inspect immediately/10 seconds/one minute, then unlock; confirm no repeated
full-screen flashes and no old text emerging after shutdown. Keep v135 as a
rollback comparison, not as a known-good sleep baseline.

### v137 Revert sparse gray pulses; finish shutdown inside the update

Hardware rejected v136: Direct became thicker and Clock immediately developed
background residue. Its sparse 48-frame gray waveform is removed; Direct returns
to the v135 36-frame 8/12/24 VSH1 baseline. This is rollback of a regression,
not a claim that the old thin-to-thick transition is solved by those doses.

A correction to the pulse model: the
[SSD1677 table 6-6 and waveform registers](https://files.waveshare.com/upload/2/2a/SSD1677_1.0.pdf)
define VS=00 as VSS, not Hi-Z. With a nonzero DCVCOM, it must not be modeled as
an electrically neutral pause. Longer interleaved VSS intervals and equal VSH1
counts do not guarantee equal optical gray. Existing historical sections above
record the experiments; their use of the word idle is not proof of neutrality.

M4 now supports a completed update that includes analog/clock shutdown before
BUSY finishes. Direct always requests this shutdown, independently of the
optional reading fading fix. Its sequence is 0xCF (Mode 2, custom LUT, power-off),
not 0xC7 (Mode 1). Both gray planes remain intact throughout the update and
shutdown; the reader can only restore its thresholded B/W baseline afterward.
Clock's single OTP HALF uses 0xD7 instead of 0xD4 followed by a separate 0x03.
The low two bits enable analog/clock disable stages; they do not add a display
activation or change the selected display mode. After the clock sync, deep sleep
therefore issues no further master activation.

Standalone M4 shutdown (e.g. after a powered minute update) uses 0x83, starting
the sequencer clock before disabling analog/clock, as used by the
[GDEQ0426T82 reference driver](https://github.com/ZinggJM/GxEPD2/blob/master/src/gdeq/GxEPD2_426_GDEQ0426T82.cpp).
It contains no display stage. The existing 200ms settle is retained, followed
by a refresh-completion wait that handles delayed BUSY assertion. No change is
made to deep-sleep mode, GPIO wiring, analog voltage settings, font weight or
font coverage. White-flash LUT bytes are unchanged; a requested fading-fix
shutdown also uses the M4 integrated path. Other boards retain their previous
separate shutdown policy and 0x03 standalone sequence.

This targets powered post-refresh/cleanup behavior; without electrical traces,
neither persistent drive nor shutdown timing has been proven to cause the
photographed residue. Host tests verify CF/D7 complete power-off before RAM
cleanup, late BUSY, no extra display activation, standalone 83, unchanged
white-flash CC when shutdown was not requested, and other-board isolation.
No extra framebuffer or runtime allocation is introduced. Hardware QA must
compare stable text and a slow-motion page turn, then lock from the reader
and from a fresh boot's Home without opening a book. Observe immediately and
at 10 seconds/next minute; this distinguishes gray carry-over from a general
sleep transition failure. If the issue persists, obtain refresh/BUSY timing
logs or a power/logic trace before further arbitrary waveform calibration.

### Local M4 test version increments

`scripts/m4_test_version.py` runs for `murphy_m4_cn` builds with a `.vN` version
marker. It hashes source inputs (including SDK sources and translations) and
stores the last fingerprint/revision in `.pio/m4-test-version.json`. The first
changed-source build after v126 uses v127; subsequent changed-source builds
increment once. Rebuilding unchanged sources, including retrying a failed build,
keeps the same revision. `pio run -t clean` does not consume a revision. Keep the
state file when cleaning the workspace; if deleting it, first raise the `.vN`
seed in the ini to the last issued revision. Other environments and release
version formats are unaffected. The effective compiler version is printed in
the build log; it overrides the static ini seed without rewriting the ini.

### AA verification

- Use the same page, CangEr JinKai font, size, weight and lighting for comparisons.
  Record settled images separately from a slow-motion transition video.
- In Direct, test EPUB/TXT, image pages, backgrounds and night mode: the completed
  gray page must use one activation, with no subsequent edge-overlay
  pass. Check whether the physical settling still looks like a second render. Turn at least 30 pages across the configured cleanup interval and check
  residual ink, guide lines and the status bar. Test manual EPUB cleanup.
- In v129 select White flash (fast gray trial), then compare against Single-flash
  using the same page. Record: whether the flash is white or black; whether
  speckles appear transiently or persist after BUSY; whether gray edges and
  black cores match the accepted mode. Repeat in EPUB/TXT and after 30 turns.
- In both gray modes, open settings, return to reading, then return home.
  Check that old text clears and subsequent menu navigation does not white-flash
  again. Test a partial popup and fast consecutive navigation.
- Check navigation during the white interval and low-memory fallback. The page
  must not remain blank or lose its status bar.
- Confirm About displays v137 for this candidate. An unchanged rebuild must
  retain it; the next source change/build must display v138.
- The simulator cannot validate physical gray response, ghosting or flash color.

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

Opening the reader menu/settings over an AA page requests FAST to avoid
HALF (0xD4) black flashes. From v130 the driver first white-cleans physical
absolute-gray ink, even after RAM resync; the same applies to Home. Clock lock: one HALF to bleach the white field, then
windowed FAST on the digit band only so the background is not re-driven.

Optional **page-turn animation** (Reader settings, default off): ten
8-aligned vertical windows uncover the new page (forward = right-to-left,
back = left-to-right). Strips use `lut_m4_page_turn` (~40ms, transition
pixels only) rather than OTP FAST (~400ms regardless of area), so the
frontier can sweep instead of popping in thirds. Unchanged pixels stay idle
to avoid the retired-repaint black flash. Skipped for overlay/direct/swift
AA, single-flash AA, night mode, reading backgrounds, image pages, auto-turn, and the
scheduled HALF cleanup. The desktop shim maps `displayWindow` to a full
FAST, so the wipe is hardware-only.

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

## M4 INX paper layout

The M4 INX theme uses a paper layout. Other device profiles retain the existing
INX layout. Its bottom tabs are Now Reading, Library, Reading Journal, Settings,
and Apps; `MainTabs::values` defines both drawing and input order.
Display settings includes Bottom Tab Order when the theme has main tabs. Select
each tab’s number (1–5) in a fixed list, then choose Save Order. Editing one
number does not modify the other numbers or the active bar. Duplicate numbers
are allowed while editing and rejected on save. Back discards unsaved edits.
Restore Default Order fills the board-specific default numbers; Save Order
applies them. Saved changes survive restart.
`settings.json` stores `mainTabOrder` as five stable MainTab IDs (Recent=1,
Library=2, Apps=3, Settings=4, Statistics=5). Missing, duplicate, incomplete or
invalid arrays fall back to the board default. Drawing, touch and physical
left/right navigation read the same persisted order. The field occupies five
bytes; the editor uses a fixed seven-row list, a five-byte draft and ten bytes of
number-label storage and the existing five-choice popup whose storage is released with the activity. JSON adds only five scalar entries
to the existing settings document; there is no additional framebuffer.

The main UI retains its existing portrait orientation; Reading Orientation
applies to the reader. The paper geometry also handles landscape content if
the renderer is configured that way.

- The Flow home view presents one book with its cover, title, chapter/progress
  and a filled Continue Reading action following the progress, a larger 12pt
  reading summary and up to three recent entries.
  Missing covers use small vertical Chinese titles (Latin titles wrap horizontally);
  the main title supports up to three lines. Completed books offer Read Again. Landscape
  places the recent entries beside the main book. Other home layouts remain
  selectable.
- Library uses an indexed catalog with All/In Progress/Finished filters and an
  Import action opening File Transfer. Directories remain visible in each filter
  so nested books can be reached. Existing reader statistics supply titles,
  authors and completion/progress; uncached books fall back to filenames.
  Long-press file operations and destination pickers retain their existing flows.
- Reading Journal places the weekly total beside today and reading-day summaries,
  followed by larger full-width daily bars, the peak day and a solid hourly
  activity track in portrait. Portrait statistics, bar values, dates and hourly
  labels use the existing 12pt common-character font. Daily bars use solid black
  fill. Tapping a bar opens that day; book previews are omitted from the journal.
  More Details retains the analytics view.
- Settings retains the four existing categories, presented as numbered rows
  (two columns in landscape). Apps uses borderless icons (3×3 in portrait, 4×2 in landscape), with a
  bookmark focus marker, visible page count and 44px-wide previous/next touch
  targets. Swipes and physical navigation use the same page capacity. Library/Apps layout selectors are hidden for
  this theme; stored preferences still apply to other themes.

`BaseTheme` owns the shared paper geometry and monochrome drawing helpers in
`src/components/themes/inx/PaperUi.cpp`. Activities use `GUI` and logical input.
No additional framebuffer or directory-sized metadata container is introduced:
the catalog reuses its row caches, and wrapping is capped at three lines with a
192-byte UTF-8-safe stack buffer. Built-in Chinese book titles use the 12pt
common-character font; the HTML preview's desktop font appearance is indicative.

Build with `pio run -e murphy_m4_cn` and `pio run -e simulator_murphy_m4`.
`MainTabOrderTest` checks all 120 permutations, every source/destination move,
relative-order preservation, invalid orders and navigation/touch consistency.
`MurphyMainTabsTest` checks the M4 default order, wrap navigation and every horizontal
touch position at 480px and 800px. On hardware, verify long titles/authors,
missing covers, empty recents, nested folders, each catalog filter, recent-list
paging, day-detail taps and hourly activity, custom tab order across restart,
restore-default order, settings categories and all app pages. Change reader
orientation and return to the main UI; check input after refresh, and inspect thin rules,
hatching and ghosting on the real panel before release.

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
