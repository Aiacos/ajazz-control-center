---
phase: 12-ak980-pro-promotion
reviewed: 2026-05-22T00:00:00Z
depth: standard
files_reviewed: 10
files_reviewed_list:
  - src/devices/keyboard/include/ajazz/keyboard/ak980_lighting.hpp
  - src/devices/keyboard/include/ajazz/keyboard/keyboard.hpp
  - src/devices/keyboard/src/proprietary_keyboard.cpp
  - src/devices/keyboard/src/proprietary_protocol.hpp
  - src/devices/keyboard/src/register.cpp
  - tests/unit/test_proprietary_keyboard_protocol.cpp
  - tests/unit/test_ak980_clock_sync_e2e.cpp
  - tests/unit/test_ak980_firmware_lighting.cpp
  - tests/unit/test_ak980_settings_batch.cpp
  - tests/unit/test_ak980_tft_chunked.cpp
findings:
  critical: 1
  warning: 5
  info: 4
  total: 10
status: issues_found
---

# Phase 12: Code Review Report

**Reviewed:** 2026-05-22
**Depth:** standard
**Files Reviewed:** 10
**Status:** issues_found

## Summary

Reviewed the AK980 PRO promotion: proprietary HID protocol builders, the
20-mode firmware lighting picker, clock-sync, settings batch, and the chunked
TFT transfer, plus their unit/e2e tests.

Project-specific guardrails check out: the COD-031 boundary holds
(`grep -rn nlohmann src/core/include/` and `src/devices/keyboard/` both return
zero), the control interface is correctly pinned to usage page `0xFF13` in
`register.cpp`, and the CMD_FINISH `0xF0` packet (issue #58) is present in both
the lighting and settings envelopes. The hardware-verified time-sync framing
(report id 0x00, 65-byte, magic 0x5A, `0xAA 0x55` tail) is faithfully
reproduced and has solid e2e coverage.

However there is one data-corruption BLOCKER in `setRgbBuffer()` (an off-by-two
chunk-size error that silently drops two RGB bytes per report and lies to the
firmware about the chunk length), plus several wire-format discrepancies between
the shipped control packets and the byte-2 `0x04` "type select" byte that all
three Ghidra-decompiled vendor envelopes carry. The shipped envelope packets
leave that byte 0x00 and the tests never assert it, so the divergence is masked
by the test suite rather than caught by it.

## Critical Issues

### CR-01: `setRgbBuffer()` drops 2 RGB bytes per chunk and reports a false length

> **Resolution (2026-05-22): DEFERRED after RE cross-check.** The off-by-two is
> real, but an RE sweep (`ak980pro_perkey_rgb_protocol.md`, `ak980pro_vendor.md`)
> shows the whole `0x0A` zone-buffer path is a legacy "similar idea" the RE flags
> to *unify* with the Ghidra-confirmed per-key protocol `0x20/sub-0x04` (already
> implemented as `buildPerKeyRgbWriteHeader`). `setRgbBuffer` has no live caller
> and no test, so nothing is corrupted in production. Changing the wire constant
> in isolation would polish a superseded path against competing/provisional RE.
> Documented in code + `docs/protocols/keyboard/proprietary.md`; proper fix is to
> unify on `0x20/0x04` and verify the wired LED-to-byte mapping (RE-flagged
> unconfirmed) on a physical AK980 PRO. Not fixed in code this pass, by design.

**File:** `src/devices/keyboard/src/proprietary_keyboard.cpp:683-696`
**Issue:**
`RgbBufferChunk` is `60` (proprietary_protocol.hpp:207) but the per-LED RGB
report places its payload at byte 6 (`pkt[2]=zone`, `pkt[3]=offset-hi`,
`pkt[4]=offset-lo`, `pkt[5]=length`, payload at `pkt.data()+6`). Only
`ReportSize - 6 = 58` payload bytes fit in a 64-byte report. The loop computes
`take = min(60, remaining)`, writes `pkt[5] = take` (up to 60), then
`memcpy(..., min(take, ReportSize-6))` which clamps the copy to **58** bytes —
but advances `offset += take` (60). Result for any zone larger than 58 bytes
(ZoneKeys = 104 LEDs × 3 = 312 bytes is the normal case):

- flat[58] and flat[59] of each 60-byte window are never transmitted (two LEDs'
  colour data corrupted/shifted every chunk),
- the length byte `pkt[5]` claims 60 bytes when only 58 are present, so the
  firmware reads two bytes of stale/zero report tail as pixel data.

The header is 6 bytes, so the chunk constant must be 58, or the loop must be
rewritten to copy exactly `take` bytes. The macro path
(`setMacro`, payload at byte 8 with `MacroChunk=56` = `64-8`) is correct by
contrast — only the RGB-buffer constant is wrong. There is no unit test
exercising `setRgbBuffer()`, so this ships undetected.

**Fix:**

```cpp
// proprietary_protocol.hpp — header is 6 bytes (id, cmd, zone, off-hi,
// off-lo, len), so only 58 payload bytes fit, not 60.
inline constexpr std::size_t RgbBufferChunk = ReportSize - 6; // 58

// proprietary_keyboard.cpp — copy exactly `take`, never silently clamp.
auto const take = std::min<std::size_t>(RgbBufferChunk, flat.size() - offset);
pkt[5] = static_cast<std::uint8_t>(take);
std::memcpy(pkt.data() + 6, flat.data() + offset, take); // take <= 58 now
```

Add a `setRgbBuffer` MockTransport test that uploads a full ZoneKeys buffer and
asserts every flat byte appears once, in order, across the emitted chunks.

## Warnings

### WR-01: Control envelope packets omit the byte-2 `0x04` "type select" that all three vendor decompiles carry

**File:** `src/devices/keyboard/src/proprietary_keyboard.cpp:924-954` (lighting),
`:985-997` (settings); `proprietary_protocol.hpp:224-229` (`makeReport`)
**Issue:**
`ak980pro_vendor.md` §4 (lines 562-567) and §13.7 (lines 1033-1038) document the
5-packet envelope with `byte2 = 0x04` on START / MODE_BEGIN / SAVE / FINISH
("type select"), confirmed in three independent Ghidra functions
(`FUN_0042b0a0`, `FUN_004340c0`, `FUN_0044b910`). The shipped code emits these
control packets via `makeReport(cmd)`, which only sets `byte0=ReportId(0x04)` and
`byte1=cmd`, leaving `byte2 = 0x00`. So every START, MODE_BEGIN, SAVE, and FINISH
goes out with `byte2 = 0x00` instead of the documented `0x04`. The code comments
(lines 919-923, 982-984) acknowledge the envelope is "not yet hardware-verified"
and "kept as-shipped", but per CLAUDE.md the RE doc is the source of truth for
wire formats and three corroborating decompiles is not "provisional". If the
firmware treats byte 2 as a required discriminator, the lighting and settings
commits are silent no-ops on hardware (the same failure mode the time-sync path
already hit once).
**Fix:** Either set `byte2 = 0x04` on the envelope control packets (a dedicated
`makeEnvelopeControl(cmd)` helper) and add the missing assertions, or
explicitly record in `proprietary.md` that byte 2 was hardware-verified to be
ignored. Do not leave it both undocumented-as-verified and unasserted.

### WR-02: Settings-batch DATA packet uses report id 0x04 and omits the fixed `0x01` at byte 5

**File:** `src/devices/keyboard/src/proprietary_keyboard.cpp:243-257`
(`buildSettingsBatch`)
**Issue:**
`ak980pro_vendor.md` §13.2 (lines 926-945) gives the corrected settings-batch
byte map: `byte 0 = 0x00 (HID Report ID)` and `byte 5 = 0x01 (fixed)`.
`buildSettingsBatch` calls `makeReport(CmdSettingsBatch)` which sets
`byte0 = 0x04` (not 0x00) and never writes the fixed `0x01` at byte 5. The same
doc also documents bytes 6/7/8 (disable_winkey / disable_alt_f4 / disable_alt_tab,
with byte 8 doubling as the checksum slot) — none of which the builder emits.
The test (`test_ak980_settings_batch.cpp:88-104`) asserts `p2[0] == 0x04`,
locking in the wrong report id rather than catching it. Given the time-sync path
was proven to need report id 0x00 on this exact device, shipping the settings
DATA packet with report id 0x04 and a missing fixed byte is a likely silent
no-op against hardware.
**Fix:** Reconcile against §13.2: set byte 0 to 0x00 (or confirm 0x04 is
accepted), write the fixed `0x01` at byte 5, and update the test to assert the
documented layout. Where the C++ field name and the schema differ, the schema
wins (CLAUDE.md).

### WR-03: `firmwareVersion()` swallows all exceptions and silently returns "unknown" on device-yank

**File:** `src/devices/keyboard/src/proprietary_keyboard.cpp:528-542`
**Issue:**
The `catch (...)` block discards the error entirely (no log line, unlike
`batteryPercent`/`setTime` which `AJAZZ_LOG_WARN`). On a device yank or transport
failure, the function returns `"unknown"` indistinguishably from a device that
genuinely reports an unparsable version. There is also no `read`-count guard
beyond `n >= 5`, so a short/garbage reply produces a plausible-looking but bogus
`"x.y.z"` string. Operationally this hides I/O failures from the logs that every
sibling method records.
**Fix:** Catch `std::exception const& e` and `AJAZZ_LOG_WARN("keyboard.ak980", "firmwareVersion: HID I/O failed: {}", e.what())` before falling through to
`"unknown"`, mirroring the other capability methods.

### WR-04: `batteryPercent()` treats a genuine 0% charge as "no battery"

**File:** `src/devices/keyboard/src/proprietary_keyboard.cpp:760-763`
**Issue:**
`if (pct == 0) return std::nullopt;` conflates two distinct device states: a
wired keyboard with no battery (which the comment intends to suppress) and a
wireless keyboard that is genuinely at 0% / critically drained. A real
near-empty battery will therefore show "unknown" in the UI instead of "0%",
exactly when the user most needs the warning. The header comment
(proprietary_protocol.hpp:294-296) only documents "0 means no battery" without a
way to disambiguate the drained-wireless case.
**Fix:** Use the byte-0/opcode echo already mentioned in the comments
(lines 723-728) to distinguish "wired, no battery" (echo 0x00) from a wireless
0% reading, or gate the nullopt on `descriptor` battery/wireless state rather
than on the percent value alone.

### WR-05: `buildSetTimeData` high-year wrap above 2255 is unguarded and untested

**File:** `src/devices/keyboard/src/proprietary_keyboard.cpp:196`
**Issue:**
`pkt[4] = (year >= 2000) ? static_cast<std::uint8_t>(year - 2000) : 0;` guards
the low end (pre-2000 saturates to 0) but not the high end: `year = 2256` yields
`year - 2000 = 256`, which truncates to `0` — i.e. 2256 silently encodes as the
year 2000. The test suite pins 2255 → 0xFF (test_proprietary_keyboard_protocol
.cpp:208-213) but never exercises the 2256 wrap, so the asymmetry is invisible.
Not reachable from a real `system_clock` today, but it is a latent
silent-corruption path the symmetric saturation comment implies is handled.
**Fix:** Clamp the high end too: `year >= 2255 ? 0xFF : (year >= 2000 ? year-2000 : 0)` and add a 2256-saturates test.

## Info

### IN-01: `ak980pro_vendor.md` §5.2 TFT chunk layout contradicts the shipped (correct) `ak980pro_tft_protocol.md` §3.3

**File:** `docs/protocols/keyboard/ak980pro_vendor.md:604-607` vs
`src/devices/keyboard/src/proprietary_keyboard.cpp:303-313` (`encodeTftChunkIndex`)
**Issue:**
`ak980pro_vendor.md` §5.2 still documents the superseded chunk layout
(byte1 = index low, byte2 = `0x80 | (i>>16)`), while `ak980pro_tft_protocol.md`
§3.3 (lines 135-150) and the shipped code put the 0x80 marker on byte 1 and the
low byte on byte 2. The code follows the corrected doc and is right; the stale
§5.2 in the vendor doc is a documentation-correctness trap for the next
contributor. CLAUDE.md mandates updating the RE doc when it disagrees with the
hardware-pinned layout.
**Fix:** Update `ak980pro_vendor.md` §5.2 to match §3.3 (or cross-reference it as
superseded).

### IN-02: Lighting DATA trailer byte order (`0x55 0xAA`) is inverted relative to the settings/time trailer (`0xAA 0x55`)

**File:** `src/devices/keyboard/src/proprietary_keyboard.cpp:436-437`
**Issue:**
`buildSetRgbModeData` writes `pkt[14]=0x55, pkt[15]=0xaa`, matching
`ak980pro_vendor.md` §3.4 (line 354 "0x55 0xAA"), but the settings batch and
time-sync trailers are `0xAA 0x55` (bytes 18/19 and 63/64). §13.7 line 1054 of
the same doc lists the lighting trailer as `0xAA 0x55`, contradicting §3.4. The
code is internally consistent with §3.4, but the intra-doc conflict means one of
the two is wrong and only a hardware witness can settle it.
**Fix:** Add a one-line note in `proprietary.md` recording which trailer order
was hardware-verified for opcode 0x13, and reconcile §3.4 vs §13.7.

### IN-03: `stampTftChecksum` / TFT output-report transport is flagged PROVISIONAL with no hardware witness

**File:** `src/devices/keyboard/src/proprietary_keyboard.cpp:283-301, 1035-1043`
**Issue:**
The chunked TFT path (byte-32 checksum + `write()` output reports vs
`writeFeature()`) is explicitly unverified ("no USB/Frida capture exists yet").
The tests pin the checksum arithmetic (e.g. `pkt[32] == 0x16`) as if it were
ground truth, which will make a future hardware correction look like a test
regression. This is acceptable scaffolding but the test comments should mark the
checksum expectations as provisional so a hardware-driven change isn't mistaken
for a defect.
**Fix:** Annotate the checksum REQUIREs in `test_ak980_tft_chunked.cpp` as
PROVISIONAL, mirroring the source comment.

### IN-04: Magic offsets in the lighting DATA builder are bare literals

**File:** `src/devices/keyboard/src/proprietary_keyboard.cpp:428-437`
**Issue:**
`buildSetRgbModeData` uses bare byte indices (1, 2, 3, 4, 8, 9, 10, 11, 14, 15)
and the `0x55`/`0xaa` trailer literals inline, unlike the settings builder which
uses named `kSettingsByte*` and `SettingsBatchTrailer*` constants. The lighting
path is the one most likely to be re-derived if the §3.4/§13.7 trailer conflict
(IN-02) is resolved against it, so named constants would localise the change.
**Fix:** Introduce `kLightingByteMode/Rainbow/Brightness/Speed/Direction/TrailerHi /TrailerLo` constants in `proprietary_protocol.hpp` and use them in the builder
and test.

______________________________________________________________________

_Reviewed: 2026-05-22_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
