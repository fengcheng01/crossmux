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

After each M4 deep-sleep wake, `HalClock::begin()` restores system UTC from the
external RX8010 when it provides a valid date. The ESP32-S3 internal RC clock
retains an apparently valid epoch through sleep but can drift, so checking
`hasValidTime()` alone skipped the more stable RTC. Wi-Fi SNTP still writes
the RX8010 on sync. A serial log records the system-vs-RTC delta on wake; if
the clock still gains tens of seconds over a day after this change, compare
the RX8010 against a reference to determine whether its offset calibration
needs adjustment. This change does not alter the clock display waveform or
request additional Wi-Fi connections.

The v170 Clock-lock FULL trial is rejected: from a Direct AA reading page, the
stock SSD1677 OTP FULL (`0xF7`) visibly cycled the entire panel several times,
but the prior page's characters remained visible immediately on the clock
screen. Recording-bus tests showed one FULL activation, so the visible cycles
were phases within that waveform, not repeated software requests. The current
Clock entry returns to the single OTP HALF (`0xD7`); hourly cleaning and
minute windows retain their previous behavior. Do not stack further FULL
updates without a measured drive or optical result. Compare the same lock
transition with one-bit reading (AA off) and from Home to distinguish gray
waveform carry-over from a general lock/panel condition. Inspect the panel
immediately and after settling; controller RAM synchronization alone does not
prove the physical pigment state is clean.

After the v172 A/B test, Direct AA still left prior-page text that developed
into dirty Clock background after several turns; AA Off did not. The Direct
gray4 path ends with two custom `0xCC` activations, whose low control bits do
not shut analog/clock down, yet it recorded `_isScreenOn = false`. The next
SleepClean called `resetForClean()`, whose `powerOffController()` returned
early on that false state and then SWRESET ran while the modeled analog drive
was still active. The recording-bus test reproduces this as a reset-while-on
assertion. The driver now records the actual powered state after gray4, so the
existing M4 `0x83` shutdown completes before SWRESET and the OTP HALF clock
paint. No page-turn LUT, activation or buffer changes; the exit to lock/menu
may take the existing 200 ms shutdown interval. This fixes a state/sequence
error, not a proven optical root cause. Check Direct page turns remain fast,
then compare lock residue immediately and after one minute against v172.

PIN input accepts a stable touch after 90 ms of contact and suppresses its
later release. The fourth digit immediately verifies the code instead of
waiting for a redundant complete PIN repaint. EPUB reading defers idle font
prewarming and background section building while touch/button input or a page
turn is pending, and builds at most one background page per loop tick. These
changes add no persistent buffer or heap allocation; verify rapid PIN entry,
single-action touch release across unlock, and taps during EPUB pagination on
hardware. Power-key wake can still spend time in deep-sleep boot and the first
physical PIN paint; this patch does not claim to shorten that boot path.

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
- Direct (无闪直刷（差分四灰阶）): v129 sent absolute two-bit glyph coverage,
  including the status bar, in one `lut_m4_direct_pulse` activation; v145 tried
  the Single-flash E waveform every turn and the user rejected losing no-flash.
  v146-v148 rebuilt the mode on the no-flash two-pass differential recipe:
  pass 1 is the stock differential repaint (FAST; HALF on the scheduled cleanup
  boundary or manual refresh), pass 2 (`displayDifferentialGray4`) drives the
  absolute-encoded planes (RED = MSB, BW = LSB) through lut_m4_gray4_step —
  lut_grayscale's doses re-indexed so light/dark edges take the Overlay lifts
  and black cores take Swift's VSH1 hold. Direct keeps its rendering
  differences over Overlay: nearest-coverage TTF quantization and the shared
  convex-corner glyph smoothing. EPUB only (TXT falls back to the two-pass
  overlay); night mode and reading backgrounds retain one B/W FAST refresh;
  the rare no-plane OOM page paints 1-bit, single-plane OOM falls back to
  Swift's one-level edge pass.

### v129 pulse grayscale experiments

Current white-flash follow-up: v161's 9/13/24 result still left diagonal
strokes visibly stepped. The next calibration uses 11/17/24 doses while the
24-frame total stays unchanged. White-flash also shares Direct's render-time convex-corner
softening for both TTF and cpfont. Only supported full-ink corners become dark
gray; existing gray pixels, straight stems, isolated dots and one-pixel
diagonals keep their coverage. Direct's B/W ink threshold is still Direct-only.
There are no extra glyph buffers or panel activations. Render CPU time and
optical improvement require hardware comparison; this cannot remove the panel's
pixel grid or turn four gray levels into continuous tones.

Device feedback also reported the UI background progressively dirtying from
the lower right after exiting White-flash, continuing across main-tab changes
and later appearing on the lock screen. This is not yet a confirmed root cause,
but M4 absolute-gray updates left analog drive on while cleanup replaced
the gray RAM with a B/W baseline. M4 now finishes absolute gray with `0xCF`
even when fading fix is disabled, before cleanup RAM writes. OTP whitening
self-powers through `0xFF`, so its redundant standalone `0xC0` is skipped:
successive white turns remain `FF, C0, CF`, with two display activations.
Gray exit still resets the controller and paints the UI with one powered-down
`D7` clean. Other SSD1677 boards retain their previous power policy. Recording-
bus tests cover repeated white turns, exit, window updates, delayed BUSY,
baseline writes, main-tab-style B/W paints, sleep clean and other-board
isolation. Check home and every main tab immediately and after 30/60 seconds,
then enter the lock screen and check it after the same delay. Also compare
page-turn timing on the actual M4.

The Paper journal's reading-habit card takes the available height down to the
fixed bottom action, leaving one 6px gap. Its minimum height is derived from
the two actual font line heights, padding and line gap; both lines render
through clipped paper-text rectangles so they cannot cross the card border.
The first attempt kept the 190px chart and a 44px action, leaving less than
the 71px two-line minimum on the 480x800 M4 and hiding the card. The portrait
chart is now 166px and the action 38px, reserving room for the full two-line
card. The bottom action stays right-aligned as `查看更多 >` and keeps its
full-width hit target. Check the actual portrait screen for both complete
lines, chart labels, and separation above the action.


Chinese UI fallback is now the fixed installed `NotoSansSC` cpfont family,
matching the embedded UI face instead of borrowing the current reading font.
Install its 8/10/12/14pt files for rare-title coverage. Existing UI-size slots
load lookup metadata/on-demand glyphs, with no extra full TTF arena or PSRAM
glyph cache. The official files have 4000 compact intervals each: 24,000 bytes
per size (96,000 bytes total) now explicitly allocated in PSRAM, with the usual
256KB reserve and checked failure rather than an internal-RAM fallback. Glyph
overflow/prewarm still uses the existing bounded caches. Font objects and the
five-slot manager registry remain internal; tables unload with their fonts.
The reader remains independently selectable. If those
files are absent, the built-in subset remains and unsupported characters may
be missing; another reading face is never substituted. Verify the same recent
book list before and after switching reading family and size.

Hardware feedback: Single-flash's full vendor E waveform produces good final
CangEr JinKai text, but flashes black. Both the original idle-white table and
v128's `lut_m4_white_hold` produced dirty/dark intermediate backgrounds. The
latter was also reported slow; it is retained as historical data, not selected.

White flash (快速灰阶试验), setting value 6, still OTP-whitens first. It then
uses `lut_m4_white_pulse`, removing the old 60-frame erase/settle sequence in
favor of 24 timing frames. White targets select VSL throughout; the current
light gray, dark gray and black targets select VSH1 for 11, 17 and 24 frames.
The accepted Single-flash mode (value 5) remains unchanged as a comparison.

The v156 white-flash calibration trial reduced only light gray from 9 to
8 VSH1 frames (phase durations 8/4/12, still 24 frames total). Device feedback
reported mediocre quality and a discontinuity in a straight stroke; the cause
is not established. The second-round trial restored light gray to 9 and raised
dark gray from 12 to 13 VSH1 frames, using phase durations 9/4/11. Device
feedback still described diagonal strokes as visibly jagged. The current trial
uses phase durations 11/6/7, producing 11/17/24 doses so both edge shades land
farther from white while black remains unchanged.
The 24-frame total and complete black, white and VCOM drive schedules are
preserved. OTP whitening, activation count, frame-rate/voltage registers and
reader rendering remain unchanged; there are no added waits or allocations.
This tests a darker intermediate edge, not a proven fix for the discontinuity.
Compare the same font, size and page against the prior white-flash firmware
and Single-flash; check fine strokes, 30+ page turns for residue, and measured
turn time plus visible settling. Unchanged electrical timing does not prove
unchanged optical settling or Single-flash quality.

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

### v140 Stability and Direct font coverage candidate

v139 hardware feedback: clock lock still dirties immediately, faint journal
residue survives tab switches, the reading bottom looks dirty, Direct sometimes
dirties after a page turn, and its AA remains weaker than single flash.

This candidate addresses concrete state/transport issues, not a proven optical
root cause:

- M4 display SPI is now 20 MHz, the SSD1677 rev 1.0 write limit, instead of
  the board profile's 40 MHz. Other board profiles are unchanged. Transferring
  one 48 KB plane has a theoretical wire time of 19.2 ms instead of 9.6 ms;
  actual page latency also includes SPI overhead, rasterization and waveform.
- M4 normal B/W updates always include analog/clock shutdown. Gray overlays
  explicitly bring power back up. The software power flag now follows the
  actual custom update disable bits rather than claiming CC powered off.
- Clock entry/hourly cleanup powers off, SWRESETs/reinitializes the controller,
  then paints the composed clock with one OTP HALF D7 update. Gray-to-B/W UI
  exits use the same reset followed by HALF, replacing prewhite + FAST. This
  discards prior custom registers; it never toggles the touch-shared reset pin.
  SWRESET and power-only commands add no display activation. Ordinary clock
  minute windows remain partial; deep sleep adds no further display activation.
- Cross-tab replacements arm one HALF under the render lock, after outgoing
  onExit. Focus movement within a tab remains FAST. A single clean flash is
  expected on large layout changes; multi-inversion FULL is not introduced.
- Every full gray activation restores the full RAM window after strip writes.
  The recording-bus regression deliberately leaves a one-row window selected.
- Direct TTF text uses nearest coverage among 0/85/170/255 before 2-bit packing.
  Compared with truncation, 43..63 survives as light gray and 192..212 remains
  dark gray instead of black. This is an uncalibrated coverage trial, not a
  measured reflectance curve or a promise to remove the optical settling.
  A page scope selects it before prewarm and restores legacy mapping afterward;
  glyph caches are invalidated on policy transitions. EPUB and TXT share it.
  Single/white flash, bitmap fonts, images, glyph geometry, advances and the
  existing Direct/white waveform bytes are unchanged. No added refresh pass,
  heap allocation, framebuffer, UI option or on-disk font/cache format.

Host QA: real-driver recording bus checks reset-before-clean, power off before
reset/RAM synchronization, one paint, async finish, minute tick, full gray
window and default-board isolation. Coverage QA exhausts all 256 inputs and
checks the unchanged legacy map, monotonicity, endpoints and nearest-level
error. Neither host tests nor a successful build establish optical stability.

Hardware QA: compare Direct/single flash using the same font, text, size and
lighting; watch text immediately and 10 seconds later. Lock both from a reader
and freshly booted Home, observe immediately/10 seconds/next minute; check
journal-to-other tabs. Repeat a top/bottom identical pattern with rotated
content to distinguish physical-position dirt from logical-address residue.
If time-dependent dirt persists, capture BUSY/SPI and panel-supply traces before
further waveform dose tuning. Do not mask it by forcibly interrupting refresh.

### v141 Shared glyph smoothing and no-black tab transition

The v141 candidate incorporates the latest hardware feedback that Direct was
slow and that both TTF and cpfont edges remained jagged:

- Direct rendering now applies one shared corner-step smoothing helper in the
  renderer. It reads the existing glyph bitmap for both 2-bit TTF and 1-bit
  cpfont data, lowers only a full-ink convex corner to the intermediate gray
  class, and leaves stems, interiors and white pixels unchanged. It adds no
  cache, framebuffer or heap allocation.
- The M4 Direct pulse schedule is 24 frames instead of 36. Gray target VSH1
  doses remain 8/12 and black remains 24; the removed tail is a VSS/settle
  portion, so optical settling and residual ink still require device QA.
- Main-tab replacements request the existing drive-all differential transport
  through `GfxRenderer::displayBufferDriveAll`, avoiding the explicit HALF
  black-clean waveform. The same one-shot path is consumed by the incoming
  activity's first paint; focus movement remains FAST.

Host tests cover the shared helper, the 24-frame LUT schedule, and the gray
exit/reset recording bus. Hardware QA must compare Direct and single flash on
the same TTF/cpfont pages, measure page-turn time, and switch every main tab
while watching for black flash and residual text.

### v142 Whiteward Direct balance

v142 responds to the first v141 device result: Direct looked dirty because its
24-frame table had too little VSL preconditioning. The table now uses 30 frames
with 18 VSL frames for white/gray classes, a 4-frame VSS settle, and the same
24-frame VSH1 black dose. The waveform remains one custom activation without
the HALF black-clean sequence. The shared corner helper also recognizes TTF
light-gray neighbors, so its edge smoothing is applied consistently to TTF and
cpfont bitmaps. Hardware QA must verify that the added six frames clean the
background without making page turns unacceptably slow.

### v143 TTF edge neighbor correction

The v143 firmware keeps the v142 30-frame Direct waveform and fixes the
renderer guard so a TTF light-gray neighbor (coverage 1) participates in the
same convex-corner test as a cpfont full-ink neighbor. This changes only the
edge classification; glyph interiors and the waveform are unchanged.

### v144 Direct periodic Single-flash quality tier

User feedback after v143: Direct's anti-aliasing still falls short of
Single-flash quality for both TTF and cpfont text. Byte-level decoding of the
two tables shows why no pulse-table retune can close the gap: the E waveform
defines its gray levels through a balanced oscillation (white +15/-15,
dark gray +15/-15, black +24/-24 frames) whose final phases land each pixel at
its target from any start state, and that oscillation is itself the visible
flash. The v142 pulse table instead preconditions monotonically (18 VSL
frames) and doses once (8/12 VSH1), so its levels depend on the previous
page's ink. Cleaning and level definition are inseparable in one activation —
the same conclusion the v29-v33 iterations reached on device.

v144 therefore composes the two hardware-proven tiers instead of recalibrating:
Direct keeps the no-flash pulse table for ordinary turns, and the scheduled
cleanup boundary (the reader's existing `pagesUntilFullRefresh` cadence, user
configurable) or an EPUB manual refresh upgrades that one turn to the
Single-flash activation (`displayGrayBufferAbsolute(false)`, E-verbatim
clean-and-paint). Boundary/manual Direct turns no longer paint the B/W HALF
first — the E activation is itself a full clean, matching the singleFlashAa
branch. Both readers (EPUB tiled/strip/buffered paths and TXT) select the tier
from the same consumed `HALF_REFRESH` boundary. No LUT bytes, voltages, frame
counts, settings or allocations change; the tier adds one bool per page render.

Hardware QA: read a long stretch in Direct and confirm the boundary turn
flashes once and restores factory-level edges; check that the cadence interval
matches the refresh-frequency setting; test manual refresh in Direct (expect
the E activation, not B/W HALF + pulse); re-verify night mode and reading
backgrounds still take the B/W FAST fallback, and that Swift/Overlay/Single
flash/White flash modes are unchanged.

### v145 Direct adopts the E waveform every turn

Device result on v144: pages still dirtied between boundary turns (default
cadence 15 pages), and the user's own A/B — reader-menu return (which sets
`forcedRefreshPending`, so v144 rendered it with the E activation) versus an
ordinary turn (pulse table) — showed E-quality text judged good and
pulse-quality text judged bad, for both TTF and cpfont. The pulse tier is
therefore retired from the reader: Direct now selects the Single-flash
activation on every turn in EPUB (tiled buffered, tiled strip, and non-tiled
paths) and TXT, keeping Direct's rendering differences (nearest-coverage
quantization, convex-corner glyph smoothing) over plain Single-flash. The mode
label is renamed accordingly (no longer "no-flash"); `GrayRefresh::Direct` and
the SDK `displayGrayBufferDirect` pulse path remain as experiment surface.
The Style tab's anti-aliasing row moves to the top of the list (it was last
and is the most adjusted row while tuning gray modes).

Hardware QA: Direct vs Single-flash on the same TTF and cpfont pages (Direct
should match its quality with slightly softer corners); confirm each Direct
turn flashes once like Single-flash and the background stays clean across 30+
turns; night mode/backgrounds still fall back to B/W FAST; the Style tab opens
with the anti-aliasing row first, with guide-line rows still hiding when
disabled.

### v146 Direct rebuilt as no-flash differential four-level gray

User feedback on v145: the flash itself was never acceptable — Direct exists
to be the no-flash mode ("我要的就是无闪"). The E-every-turn change is reverted
and the mode is rebuilt on Swift's two-pass differential recipe (the TW
firmware's no-flash gray4 architecture), widened from one edge level to four:

- New driver path `Ssd1677Driver::displayDifferentialGray4` (facade
  `displayDifferentialGray4Buffer`, HAL/GfxRenderer forwards). Pass 1 is the
  stock differential repaint exactly as Swift's — no inversion flash; the host
  picks its mode so the scheduled cleanup boundary and manual refresh run HALF
  (deep clean) through the same activation instead of a third pass. Pass 2
  loads RED = the new frame's MSB plane and BW = its LSB plane (absolute
  coverage encoding), so the differential classes split the four levels:
  light gray (01, resting on white) selects the same lut_grayscale g1 doses
  Swift's edges already run on this panel; dark gray (10, resting on black)
  selects g2; black cores (11) select g3; white (00) idles. Every dose acts on
  the known B/W endpoint pass 1 just established — the missing ingredient that
  sank the single-pass pulse table. Both activations restore the B/W
  differential baseline afterwards.
- EPUB wires Direct to the new path on the tiled buffered route (both planes);
  single-plane OOM falls back to Swift's one-level edge pass; the no-plane
  strip route restores the B/W baseline and paints 1-bit (no flash); the
  non-tiled route and TXT fall back to the generic two-pass overlay. The
  periodic boundary now maps to pass-1 HALF, addressing the v144 "dirty pages"
  report within the standard refresh-frequency cadence.
- The mode label returns to no-flash naming (无闪直刷（差分四灰阶）); the
  anti-aliasing row stays first on the Style tab (v145 change kept).
- No new LUT tables, voltages or frame counts: pass 2 reuses
  `_cfg.swiftEdgeLut` (lut_grayscale). No new settings or heap allocations —
  the planes reuse the existing tiled buffers.

Calibration caveat (device QA required): g1-on-white is proven by Swift, but
g2-on-black (dark-gray landing) and g3-on-black-cores are first drives of
lut_grayscale's other classes in the differential role on GDEQ0426T82. Watch
for dark-gray edges not separating from black cores (g2 too weak — Swift shows
g2 invisible on black) and for core lightening (g3). If g2 proves inert, the
documented escalation is a dedicated four-level step table derived from the TW
slot-0 magnitudes (see lut_m4_edge_weak), not a return to absolute modes.

Hardware QA: Direct EPUB page turns must show two quick passes with no
inversion flash; compare edges against Swift (should gain the light-gray
level) and Single-flash (reference quality); turn 30+ pages and check residue
against the refresh-frequency setting; verify boundary/manual turns clean via
pass-1 HALF; check image pages, night mode, backgrounds, menu return, and TXT
fallback; confirm the AA picker row order.

### v147 Direct gray4 uses the overlay plane encoding

Device result on v146: pages turned whitened (发白) — glyph cores washed to
gray, edges too light, no visible AA levels. Root cause: the tiled path still
rendered Direct's planes under the ABSOLUTE encoding, whose 11 class (black
cores) selected lut_grayscale's dark-gray group — every core was actively
driven off black — and whose 10 class (dark edges) took the mid-gray group.
The table expects the OVERLAY encoding: black and white both encode 00 and
idle, light-gray edges select g2, dark-gray edges select g3 — the exact
combination the Overlay mode runs on this panel daily.

v147 drops directAa from the tiled path's `setAbsoluteGrayPlanes(true)`
(Swift and Single-flash keep it) and re-wraps the single-plane OOM fallback
(Swift's edge pass classifies by the absolute MSB plane) in an
absolute-encoding scope. Driver pass-2 timing, voltages and LUT bytes are
unchanged; the fix is purely which pixels each LUT group sees.

Hardware QA: Direct page turns must now keep solid black cores with gray
edges and no whitening; compare against the Overlay mode on the same page
(should match, plus softer corners from the smoothing); re-check 30+ turns,
boundary/manual HALF cleaning, image pages, night mode and TXT fallback.

### v149 Direct step table re-indexed for solid cores

Device result on v147: the anti-aliasing levels became visible (the encoding
fix worked) but glyph cores still read washed-out (字体发白). With the overlay
encoding, black cores and white background share the idle 00 group, so cores
relied solely on pass 1's differential FAST — the same reason Swift's pass 2
tops cores up with its g1 VSH1 hold every turn.

v149 introduces `lut_m4_gray4_step`: lut_grayscale's group bytes re-indexed
for the ABSOLUTE encoding the Direct planes render — 01 light-gray edges take
the Overlay mode's 9-frame VSL lift (old g2 bytes), 10 dark-gray edges its
6-frame lift (old g3), and 11 black cores take Swift's 7-frame VSH1 hold (old
g1). Every dose therefore runs in a role the panel already executes daily
(Overlay mode for the lifts, Swift for the hold); only the group indexing is
new. TP, frame rate and voltages stay lut_grayscale verbatim; the table is a
constexpr flash permutation with no runtime allocation. The reader returns
Direct to `setAbsoluteGrayPlanes(true)` (single-plane OOM fallback simplified
back — both paths classify absolutely now), and the driver gates the gray4
path on the new `gray4StepLut` config field (M4 sets it; other SSD1677 boards
report unsupported and keep the base-class fallback).

Hardware QA: cores must read solid black (compare Single-flash's cores);
edges keep the two gray levels; no flash on ordinary turns; re-check the
boundary/manual HALF, image pages, night mode and TXT fallback.

### v150 Direct ink-in turn: pass-1 threshold + white-hold clean

Device result on v149: AA quality and core blackness accepted, but the page
turn showed the gray step as a visible flash — pass 1 painted every glyph
pixel (edges included) full black, then pass 2 lifted the edge classes, so
text appeared bold and visibly thinned ~100ms later (black-then-thin). The
same structure is why Overlay's second pass reads as a blink.

v150 makes the whole turn ink-adding instead:

- The Direct render scope arms a new `directInkThreshold`: BW glyph draws skip
  coverage-1 pixels, so pass 1's differential repaint lands cores + dark-gray
  edges only (light edges stay white). Plane renders are unaffected (they do
  not run in BW mode). The rare B/W fallback pages render slightly thinner
  text; the menu/status-bar paints outside the scope are unchanged.
- `lut_m4_gray4_step` retunes two groups: 01 (light gray, now resting on
  white) takes a 3-frame VSH1 nudge — the single from-white estimate to
  retune if light edges land wrong; 00 (white) takes the overlay g2 white-hold
  (9 VSL frames, Swift's non-edge clean) so background and orphaned-gray
  pixels are re-driven every turn. 10/11 keep the v149 doses (6-frame VSL
  lift from black, Swift's core hold).
- Turn optics: thin black text sweeps in, then the light-gray rings darken
  within one short step — ink settling, no lightening flash.

Known trade-off (documented in the table): a pixel persisting in the SAME
coverage class across turns is idled by the differential pass 1 and drifts
one dose-step per turn toward its class drive; text normally moves, so this
only affects static furniture. Hardware QA: confirm the turn reads flash-free
on TTF and cpfont; light-gray edge depth after the 3-frame nudge; background
cleanliness over 30+ turns; cores stay solid; boundary HALF still cleans.

### v151 Direct fast-transition pass 1

Device result on v150: the flash is gone, but the turn still read as
non-AA text appearing and then being de-jaggified — the 0xFC DU pass 1 held
the interim B/W state for ~500ms, long enough to register. v151 replaces the
ordinary-turn pass 1 with the device-proven page-turn transition table
(`lut_m4_page_turn`, ~5 frames): every changed pixel still moves
differentially (no inversion flash), but the interim state passes in ~40ms —
below perception — and the ~80ms gray step lands as part of the same turn
(total ≈150ms, also ~4x faster than v150). The short drive may under-shoot
endpoints; pass 2's class holds top them up (7 VSH1 frames on cores, 9 VSL
on white), and the HALF boundary/manual path still runs the deep clean
unchanged. If text dirt accumulates, the documented knob is the transition
TP (bump from 0x05 toward 0x0A, per the page-turn LUT's tuning note).

Hardware QA: the turn must read as one step with the AA already applied;
verify text cleanliness over 30+ turns (transition under-drive), edge/core
quality unchanged from v150, boundary HALF cleaning, and the first Direct
turn after boot/menu-return.

### v152 Direct residue: longer transition + gray-exit hygiene

Device result on v151: the turn itself is good, but faint residue remained
after page turns and the lock screens picked it up (the single-pass HALF
sleep entry only partially lifts AA gray ink — the line 50 device note).
Both symptoms trace to the 5-frame transition's under-drive.

- Pass 1 now runs `lut_m4_gray4_trans`: lut_m4_page_turn with TP raised from
  0x05 to the anti-ghosting 0x0A its own tuning note documents (~80ms; turn
  total ≈180ms, still one perceptual step). Copied so the strip animation
  keeps its faster timing.
- `displayDifferentialGray4` arms `BwPreparation::GrayExit` (the v130
  absolute-gray exit, gated on whiteBeforeBwAfterAbsoluteGray and not
  clobbering a pending preparation): the next B/W paint after a Direct page —
  menu, home, popups — white-cleans first instead of diffing gray ink away
  as if it were a valid baseline. `requestSleepClean()` overrides the
  preparation with SleepClean, so the sleep entry keeps its settled OTP-HALF
  path with no extra white flash. Direct page turns themselves never consult
  the preparation, so reading stays flash-free.

Hardware QA: 30+ turns with no accumulating reader residue; enter every lock
face from a Direct page and check for reader ghosting; open the reader menu
(expect one white clean, then a clean menu); unlock/relock; confirm the turn
still reads as a single step at the longer transition.

### v154 Direct post-build first page garbled: BUSY race in the gray step

Device result on v153: re-entering a book whose section cache rebuilds
("索引中") rendered the first Direct page as two interleaved glyph layers
with a small horizontal offset and mesh noise; the next page turn was clean.
Only the post-build (and menu-return) first render was affected.

Root cause: those renders run pass 1 as HALF, whose forced power-off bits
shut the panel's rails down. Pass 2's 0xC0 re-enable delays BUSY assertion,
and the gray step waited with Swift's level-only `waitBusy` — it returned
before the waveform started, so the baseline restore (BW=RED=fb) rewrote the
planes while the activation was scanning: rows classified before the rewrite
used the gray planes, rows after used the thresholded fb — the interleaved
double rendering. Ordinary turns were safe because their pass 1 (transition
LUT, 0xCC without the disable bits) keeps the rails up and BUSY asserts
immediately. The fix waits with `waitRefreshComplete`, the same remedy the
v132 absolute-gray completion race needed (displayGray's "abs_gray" wait).
Swift's own edge pass still uses waitBusy and carries the same theoretical
exposure after its powered-down pass 1; it was never observed on device —
left unchanged pending evidence.

Hardware QA: rebuild a section (change font size, re-enter) and verify the
first Direct page renders cleanly; return from the reader menu; ordinary
turns unchanged.

### v155 Direct pass 1 always takes the transition table

The v154 BUSY fix did not cure the post-rebuild garble (device retest), which
re-opens the root cause. The remaining unique state of those renders was the
OTP HALF pass 1 — and one asymmetry stood out: the tiled path pre-writes both
gray planes into controller RAM (the legacy flow for Overlay/Single-flash),
so when pass 1 ran, RED held the MSB gray plane, not a frame baseline. The
stock displayImpl writes BOTH planes (BW and RED = fb) before any non-Fast
activation; the differential-gray4 HALF branch wrote only BW. If the panel's
0xD4 consults RED at all despite the BYPASS_RED label, the OTP waveform
classified against gray-plane bits — the garbled composite.

v155 removes the HALF branch from the reader entirely: every Direct pass 1
(ordinary, boundary, manual, post-build, menu return) runs the
transition-table repaint, byte-identical to the proven ordinary turn. That
path classifies against the pre-written planes and, because the planes mark
all ink while the thresholded fb agrees, effectively re-drives every pixel —
the strongest no-flash clean, so the periodic HALF ride-along is not mourned:
deep cleaning remains with the GrayExit white clean when leaving the reader.
The driver's Half branch stays for API completeness, now defensively writing
RED = fb like displayImpl does.

Hardware QA: rebuild a section and check the first Direct page; menu return;
30+ ordinary turns; residue watch (the boundary HALF no longer exists — if
background dirt accumulates over long reads, the knob is the transition TP
or reinstating a corrected HALF later).

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
- Confirm About displays v141 for this candidate. An unchanged rebuild must
  retain it; the next source change/build must display v142.
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
