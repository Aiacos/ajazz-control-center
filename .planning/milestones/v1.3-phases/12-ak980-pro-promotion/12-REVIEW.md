---
phase: 12-ak980-pro-promotion
reviewed: 2026-05-22T00:00:00Z
depth: standard
files_reviewed: 11
files_reviewed_list:
  - src/devices/keyboard/include/ajazz/keyboard/ak980_lighting.hpp
  - src/devices/keyboard/include/ajazz/keyboard/keyboard.hpp
  - src/devices/keyboard/src/proprietary_keyboard.cpp
  - src/devices/keyboard/src/proprietary_protocol.hpp
  - src/devices/keyboard/src/register.cpp
  - docs/protocols/keyboard/proprietary.md
  - tests/unit/test_proprietary_keyboard_protocol.cpp
  - tests/unit/test_ak980_clock_sync_e2e.cpp
  - tests/unit/test_ak980_firmware_lighting.cpp
  - tests/unit/test_ak980_settings_batch.cpp
  - tests/unit/test_ak980_tft_chunked.cpp
findings:
  critical: 0
  warning: 7
  info: 4
  total: 11
status: issues_found
---

# Phase 12: Code Review Report

**Reviewed:** 2026-05-22 (re-review / verification pass)
**Depth:** standard
**Files Reviewed:** 11
**Status:** issues_found

## Summary

Re-review of the AK980 PRO promotion: proprietary HID protocol builders, the
20-mode firmware lighting picker, clock-sync, settings batch, and the chunked
TFT transfer, plus their unit/e2e tests, read fresh against the current tree.

Hard-check guardrails all hold:

- **COD-031** — `grep -rn nlohmann src/core/include/` returns 0; the keyboard
  backend (`src/devices/keyboard/`) is also nlohmann-free. Boundary intact.
- **Usage page 0xFF13** — `register.cpp:70` pins `controlUsagePage = 0xFF13`
  (not the stale 0xFF00 family default). Correct.
- **CMD_FINISH 0xF0 (issue #58)** — present in both the lighting envelope
  (`proprietary_keyboard.cpp:975`) and the settings envelope (`:1018`), and
  asserted by `test_ak980_firmware_lighting.cpp:129-130` and
  `test_ak980_settings_batch.cpp:114-116`.
- **Chunked-TFT bounds** — `uploadTftImage` slices `pixelStream` into
  `ceil(size/28)` chunks; the final partial chunk is zero-padded into a
  fixed 28-byte `slice` (`proprietary_keyboard.cpp:1128-1137`). No OOB read;
  `encodeRgb565` validates `rgba.size() == w*h*4` before producing exactly
  `kTftFrameBytes`. Safe.
- **ASCII-only test names** — every `TEST_CASE` string across the five test
  files is ASCII (no em-dash / right-arrow). Verified.

The hardware-verified time-sync framing (report id 0x00, 65-byte, magic 0x5A,
`0xAA 0x55` tail, per-packet 30 ms settle + best-effort readback) is faithfully
reproduced and has solid e2e coverage (`test_ak980_clock_sync_e2e.cpp`).

**Prior CR-01 (setRgbBuffer 0x0A off-by-two)** was investigated and DEFERRED by
deliberate decision (commit `d70503d`) — see CR-01 below. It is no longer a
fresh actionable blocker; it is now a documented known-issue with no live caller
and no test. There are therefore **no open BLOCKERs** this pass. The remaining
findings are wire-format discrepancies between the shipped control packets and
the corrected deep-RE byte maps in `ak980pro_vendor.md` §13, plus the
robustness/quality items carried over from the prior review (all re-confirmed as
still applicable).

## Critical Issues

_None open. CR-01 below is retained for traceability but is DEFERRED, not an
open blocker._

### CR-01 (DEFERRED — known issue, not fixed in code): `setRgbBuffer()` 0x0A off-by-two

> **Status: DEFERRED by deliberate decision (commit `d70503d`, 2026-05-22).**
> Not fixed in code, by design. Surfaced here for traceability only.

**File:** `src/devices/keyboard/src/proprietary_keyboard.cpp:663-718`
(KNOWN-ISSUE comment block + `setRgbBuffer`), `proprietary_protocol.hpp:207`
(`RgbBufferChunk = 60`); cross-referenced in
`docs/protocols/keyboard/proprietary.md:51-69`.

**Issue (still real):**
`RgbBufferChunk` is `60` but the per-LED RGB report's 6-byte header
(`id, cmd, zone, off-hi, off-lo, len`) leaves only `ReportSize - 6 = 58`
payload bytes per report. The loop sets `pkt[5] = take` (up to 60) and advances
`offset += take`, while the `memcpy` clamps to `min(take, ReportSize-6)` = 58 —
dropping 2 bytes/chunk and claiming a length the report does not carry.

**Why it is correctly DEFERRED (verified this pass):**

1. The KNOWN-ISSUE comment is present and accurate
   (`proprietary_keyboard.cpp:663-683`), and the matching note now exists on the
   `0x0A` row of `docs/protocols/keyboard/proprietary.md:51-69`.
1. `setRgbBuffer` (the `IRgbCapable` surface) has **no live caller and no unit
   test** — confirmed: no `setRgbBuffer(` call site in the keyboard backend and
   no `CmdSetRgbBuffer` assertion in any test file. So the off-by-two corrupts
   nothing in production today.
1. RE cross-check (`ak980pro_vendor.md` flags `0x0A` as "similar idea → unify";
   `ak980pro_perkey_rgb_protocol.md` supersedes §3.7-§3.8) shows the whole
   `0x0A` zone-buffer path is legacy and should be unified onto the
   Ghidra-confirmed per-key protocol `0x20/sub-0x04`, already implemented as
   `buildPerKeyRgbWriteHeader`. Patching the constant in isolation would polish
   a superseded path against competing/provisional RE.

**Proper resolution (future):** unify on `0x20/0x04` and verify the
RE-flagged-unconfirmed wired LED-to-byte mapping against a physical AK980 PRO,
rather than fixing `RgbBufferChunk` in place.

## Warnings

### WR-01: Lighting + settings envelope control packets use report-id-0x04 framing with opcode at byte 1 — the corrected deep-RE (§13.1) says report-id 0x00, frame-magic 0x04 at byte 1, opcode at byte 2

**File:** `src/devices/keyboard/src/proprietary_keyboard.cpp:936-987` (lighting
envelope), `:1001-1038` (settings envelope); `proprietary_protocol.hpp:224-229`
(`makeReport`)
**Issue:**
`ak980pro_vendor.md` §13.1 (lines 910-920) is the *corrected* deep-RE finding
for every 33-byte short report sent through the FEATURE path (`FUN_0044eed0`):
the HID Report ID is **0x00**, the `0x04` is a fixed frame-magic byte inserted
at **byte 1**, and the real opcode lands at **byte 2**. This is the exact same
"off-by-one" framing the time-sync path was *hardware-proven* to require
(report id 0x00, not 0x04) on this same AK980 PRO / 0xFF13 collection. The
shipped lighting + settings envelopes instead go out via `makeReport(cmd)` /
`writeFeature()`, which produces `byte0 = 0x04`, `byte1 = opcode`, `byte2 = 0x00`
— the older, superseded layout. Additionally §4 (lines 562-567) documents the
envelope control packets (START / SAVE / FINISH) carrying `byte2 = 0x04`
("type select"), which the shipped packets also leave at 0x00.

Because the device is the same model on the same collection where report-id-0x04
framing was empirically a silent no-op, there is a strong likelihood these
commits do nothing on real hardware. The code comments
(`:940-944`, `:1003-1005`) candidly mark the envelope "not yet hardware-verified,
kept as-shipped", but the byte map they ship is the one the deep RE explicitly
flags as wrong, and the tests (`test_ak980_firmware_lighting.cpp:92-130`,
`test_ak980_settings_batch.cpp:86-116`) assert the unverified layout
(`p1[0]==0x04`, opcode at `[1]`), so the divergence is *locked in* by the suite
rather than flagged by it.

Note this is a WARNING, not a BLOCKER: the vendor doc is internally inconsistent
(§10 line 780 recommends `{0x04, 0xF0, …}`; §4/§13.1 say report-id 0x00 / byte-2
opcode), there is no live AK980 PRO hardware witness for these two paths either
way, and a wrong-but-no-op write does not corrupt data. But shipping the layout
the deep RE marks wrong, while the tests pin it as correct, is the trap.
**Fix:** Re-derive the lighting + settings envelopes against §13.1/§13.4 (report
id 0x00, frame-magic 0x04 at byte 1, opcode at byte 2; §13.4 even gives the full
`0x0B 0x1C` lighting-params byte map), OR record in `proprietary.md` that the
report-id-0x04 layout was hardware-verified to be accepted for these opcodes.
Until one of those, do not let the tests assert the unverified layout as ground
truth — mark those byte assertions PROVISIONAL.

### WR-02: Settings-batch DATA packet uses report id 0x04, places the opcode at byte 1, and omits the fixed `0x01` at byte 5

**File:** `src/devices/keyboard/src/proprietary_keyboard.cpp:243-257`
(`buildSettingsBatch`)
**Issue:**
`ak980pro_vendor.md` §13.2 (lines 922-945) is the *corrected* settings-batch
byte map (re-derived from `FUN_00414290`, explicitly superseding the wrong §3.2):
`byte 0 = 0x00 (HID Report ID)`, `byte 1 = 0x07`, `byte 2 = 0x10`,
`byte 5 = 0x01 (fixed)`, fn/sleep/response at bytes 9/10/12, trailer 0xAA 0x55
at 18/19. `buildSettingsBatch` calls `makeReport(CmdSettingsBatch)` which yields
`byte0 = 0x04` (should be 0x00) and never writes the fixed `0x01` at byte 5; it
also omits the disable_winkey / disable_alt_f4 / disable_alt_tab bytes (6/7/8)
and the byte-8 checksum slot the corrected map documents. The fn/sleep/response
offsets (9/10/12) and the trailer (18/19) DO match §13.2 — only the framing
header (report id, byte-5 fixed) is wrong. The test
(`test_ak980_settings_batch.cpp:88-99`) asserts `p2[0] == 0x04` and the opcode
at `p2[1]`, locking in the superseded framing. Given the same device proved it
needs report id 0x00 for time-sync, this DATA packet's 0x04 report id + missing
fixed byte is a likely silent no-op against hardware.
**Fix:** Reconcile `buildSettingsBatch` against §13.2 — set byte 0 to 0x00 (or
confirm 0x04 is accepted on hardware), write the fixed `0x01` at byte 5, and
update the test to assert the documented layout. Where the C++ field name and
the schema differ, the schema wins (CLAUDE.md).

### WR-03: `firmwareVersion()` swallows all exceptions and silently returns "unknown"

**File:** `src/devices/keyboard/src/proprietary_keyboard.cpp:528-542`
**Issue:**
The `catch (...)` block (`:539`) discards the error entirely with no log line,
unlike `batteryPercent` (`:792`) and `setTime` (`:882`) which both
`AJAZZ_LOG_WARN`. On a device yank or transport failure, the function returns
`"unknown"` indistinguishably from a device that genuinely reports an unparsable
version. The only guard on the reply is `n >= 5` (`:534`), so a short/garbage
reply still produces a plausible-looking but bogus `"x.y.z"`. Operationally this
hides I/O failures that every sibling method records.
**Fix:** Catch `std::exception const& e` and `AJAZZ_LOG_WARN("keyboard.ak980", "firmwareVersion: HID I/O failed: {}", e.what())` before falling through to
`"unknown"`, mirroring the other capability methods.

### WR-04: `batteryPercent()` treats a genuine 0% charge as "no battery"

**File:** `src/devices/keyboard/src/proprietary_keyboard.cpp:781-783`
**Issue:**
`if (pct == 0) return std::nullopt;` (`:782`) conflates two distinct states: a
wired keyboard with no battery (the comment's intended suppression) and a
wireless keyboard genuinely at 0% / critically drained. A real near-empty
battery shows "unknown" in the UI instead of "0%", exactly when the user most
needs the warning. The comment at `:746-749` references the `resp[1]` opcode
echo as a sanity check, but the code only validates `resp[1] == CmdBatteryQuery`
(`:778`) — it does not use any echo byte to disambiguate drained-wireless from
no-battery before collapsing both to `nullopt`.
**Fix:** Disambiguate "wired, no battery" from a wireless 0% reading using a
device/echo signal (e.g. gate the `nullopt` on the descriptor's battery/wireless
state, or on a distinct echo byte the response carries), rather than on the
percent value alone.

### WR-05: `buildSetTimeData` high-year wrap above 2255 is unguarded

**File:** `src/devices/keyboard/src/proprietary_keyboard.cpp:196`
**Issue:**
`pkt[4] = (year >= 2000) ? static_cast<std::uint8_t>(year - 2000) : 0;` guards
the low end (pre-2000 saturates to 0) but not the high end: `year = 2256` gives
`year - 2000 = 256`, which truncates to `0` — 2256 silently encodes as the year
2000\. The test suite pins 2255 → 0xFF
(`test_proprietary_keyboard_protocol.cpp:208-213`) but never exercises the 2256
wrap, so the asymmetry is invisible. Not reachable from a real `system_clock`
today, but it is a latent silent-corruption path the symmetric-saturation
docstring (`:184-186`) implies is handled.
**Fix:** Clamp the high end too:
`year >= 2255 ? 0xFF : (year >= 2000 ? year - 2000 : 0)` and add a
2256-saturates test.

### WR-06: Stale block comment claims `setTime()` "returns NotImplemented … with a WARN-once"

**File:** `src/devices/keyboard/src/proprietary_keyboard.cpp:798-822` (the
IClockCapable banner comment); echoed in `register.cpp:48-52`
**Issue:**
The banner comment above `setTime()` still reads "ProprietaryKeyboard inherits
IClockCapable and returns NotImplemented from setTime() with a WARN-once"
(`:822` / `register.cpp:48-50`). The actual `setTime()` is fully implemented —
it emits the 4-packet RTC envelope, returns `TimeSyncResult::Ok` on success, and
the e2e test asserts exactly that. The comment describes a long-superseded
behaviour and will mislead the next contributor about what the method does. This
is a documentation-vs-code contradiction (low risk, but a maintainability trap
in load-bearing protocol code).
**Fix:** Update the IClockCapable banner (and the `register.cpp` D-03 note) to
describe the shipped 4-packet handshake; drop the "NotImplemented / WARN-once"
language.

### WR-07: Lighting envelope inline comment says the 4-packet variant ships and FINISH is "not yet shipped" — but the code ships the 5-packet form including FINISH

**File:** `src/devices/keyboard/src/proprietary_keyboard.cpp:900-908`
**Issue:**
The comment block above `setFirmwareLightingMode` states: "4-packet envelope …
The 5th packet CMD_FINISH (0xF0) … our project does not yet ship it (Phase 3
P3.6 pending)". The code immediately below (`:968-975`) DOES emit the FINISH
packet, and the test (`test_ak980_firmware_lighting.cpp:89-130`) asserts a
5-packet envelope ending in 0xF0. The comment directly contradicts the shipped
behaviour it sits on top of — a regression introduced when FINISH was wired in
for issue #58 without updating this banner. A future reader trusting the comment
would believe FINISH is absent.
**Fix:** Rewrite the `setFirmwareLightingMode` banner to describe the shipped
5-packet envelope (START → MODE_BEGIN → DATA → SAVE → FINISH); remove the
"does not yet ship it / P3.6 pending" sentence.

## Info

### IN-01: `ak980pro_vendor.md` §5.2 TFT chunk layout contradicts the shipped (correct) §3.3 / `ak980pro_tft_protocol.md`

**File:** `docs/protocols/keyboard/ak980pro_vendor.md:601-607` vs
`src/devices/keyboard/src/proprietary_keyboard.cpp:303-313` (`encodeTftChunkIndex`)
**Issue:**
`ak980pro_vendor.md` §5.2 documents the chunk-count at "bytes 4..7" of the header
(line 598) and an older chunk-index split, while the shipped code (and the
authoritative `ak980pro_tft_protocol.md` §3.3) put the 24-bit count at bytes 5..7
with LCD-select at byte 4, and the chunk index as `byte1 = 0x80 | high7`,
`byte2 = low8`, `byte3 = mid8`. The code follows the corrected doc and is
self-consistent with its tests; the stale §5.2 in the vendor doc is a
documentation trap. CLAUDE.md mandates updating the RE doc when it disagrees with
the pinned layout.
**Fix:** Update `ak980pro_vendor.md` §5.2 to match §3.3 / `ak980pro_tft_protocol.md`
or cross-reference it as superseded.

### IN-02: Lighting DATA trailer byte order (`0x55 0xAA`) is inverted relative to the settings/time trailer (`0xAA 0x55`)

**File:** `src/devices/keyboard/src/proprietary_keyboard.cpp:436-437`
**Issue:**
`buildSetRgbModeData` writes `pkt[14]=0x55, pkt[15]=0xaa`, matching
`ak980pro_vendor.md` §3.4, but the settings batch and time-sync trailers are
`0xAA 0x55` (bytes 18/19 and 63/64). §13 of the same doc lists the lighting
trailer as `0xAA 0x55`, contradicting §3.4. The code is internally consistent
with §3.4, but the intra-doc conflict means only a hardware witness can settle
which order opcode 0x13 actually wants.
**Fix:** Record in `proprietary.md` which trailer order was hardware-verified for
opcode 0x13, and reconcile §3.4 vs §13.

### IN-03: `stampTftChecksum` / chunked-TFT output-report transport is PROVISIONAL but tests pin its arithmetic as ground truth

**File:** `src/devices/keyboard/src/proprietary_keyboard.cpp:283-301`,
`:1044-1067`; `tests/unit/test_ak980_tft_chunked.cpp:106-108, 140-141`
**Issue:**
The chunked TFT path (byte-32 checksum + `write()` output reports vs
`writeFeature()`) is explicitly unverified ("whether it accepts output reports
for image upload is UNVERIFIED — no USB/Frida capture exists yet", `:1060-1064`).
The tests assert exact checksum values (`pkt[32] == 0x16`, `pkt[32] == 0x97`) as
if they were ground truth, so a future hardware-driven correction will look like
a test regression rather than an expected change.
**Fix:** Annotate the checksum `REQUIRE`s in `test_ak980_tft_chunked.cpp` as
PROVISIONAL, mirroring the source comment, so a hardware fix isn't mistaken for a
defect.

### IN-04: Magic offsets in the lighting DATA builder are bare literals

**File:** `src/devices/keyboard/src/proprietary_keyboard.cpp:422-438`
**Issue:**
`buildSetRgbModeData` uses bare byte indices (1, 2, 3, 4, 8, 9, 10, 11, 14, 15)
and the `0x55`/`0xaa` trailer literals inline, unlike the settings builder which
uses named `kSettingsByte*` and `SettingsBatchTrailer*` constants. The lighting
path is the most likely to be re-derived if the §3.4/§13 trailer conflict
(IN-02) or the framing question (WR-01) is resolved against it, so named
constants would localise the change and reduce the risk of an off-by-one when
someone shifts the opcode to byte 2.
**Fix:** Introduce
`kLightingByteMode/Rainbow/Brightness/Speed/Direction/TrailerHi/TrailerLo`
constants in `proprietary_protocol.hpp` and use them in the builder and test.

______________________________________________________________________

_Reviewed: 2026-05-22 (re-review)_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
