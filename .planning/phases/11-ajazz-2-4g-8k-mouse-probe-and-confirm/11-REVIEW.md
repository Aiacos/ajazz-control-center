---
phase: 11-ajazz-2-4g-8k-mouse-probe-and-confirm
reviewed: 2026-05-22T00:00:00Z
depth: standard
files_reviewed: 18
files_reviewed_list:
  - src/devices/mouse/CMakeLists.txt
  - src/devices/mouse/include/ajazz/mouse/mouse.hpp
  - src/devices/mouse/src/aj_series.cpp
  - src/devices/mouse/src/aj_series_protocol.cpp
  - src/devices/mouse/src/aj_series_protocol.hpp
  - src/devices/mouse/src/aj_series_tft_pipeline.cpp
  - src/devices/mouse/src/aj_series_tft_pipeline.hpp
  - src/devices/mouse/src/register.cpp
  - tests/unit/test_aj_series_dpi_fn.cpp
  - tests/unit/test_aj_series_keymatrix_readback.cpp
  - tests/unit/test_aj_series_mock_transport.cpp
  - tests/unit/test_aj_series_mouse_macros.cpp
  - tests/unit/test_aj_series_omnibus_settings.cpp
  - tests/unit/test_aj_series_settings.cpp
  - tests/unit/test_aj_series_tft_clock.cpp
  - tests/unit/test_aj_series_wire_format.cpp
findings:
  critical: 1
  warning: 7
  info: 6
  total: 14
status: issues_found
---

# Phase 11: Code Review Report

**Reviewed:** 2026-05-22
**Depth:** standard
**Files Reviewed:** 18
**Status:** issues_found

## Summary

Reviewed the AJ-series mouse backend (lifecycle/caching class, wire-format
builders, TFT renderer, registration table) plus 8 unit-test files at standard
depth. The wire-format builders are well-tested at byte level and the COD-031
boundary is clean (no `nlohmann::json` anywhere under `src/devices/mouse/`).
Polling-rate, profile, omnibus-settings, DPI-table, Fn-remap, macro, and
key-matrix paths all have solid byte-pinned coverage.

The most serious problems are around **error-handling consistency on device
removal** and **stale/contradictory documentation that no longer matches the
shipped battery code**. Four `void` setters write to the transport without the
try/catch guard that every sibling setter uses, so a HID-write exception on a
yanked device propagates uncaught and can crash the caller. A whole TFT upload
pipeline (`renderClockDpiFace` / `encodeRgb565Chunks` / `buildSetTftLcdData`)
and the `buildGetBattery()` 0x83 builder are dead code paths whose surrounding
comments actively misdescribe what the production code does — a maintenance
hazard given CLAUDE.md's hard rule that protocol code must match the RE source
of truth.

## Critical Issues

### CR-01: Unguarded transport writes crash the caller on device-yank

**File:** `src/devices/mouse/src/aj_series.cpp:197, 206, 799, 811`
**Issue:** Four setters call `m_transport->write(pkt)` with **no** try/catch,
while every other setter in the same class wraps the write and returns
`false` / logs on `std::exception`:

- `setLiftOffDistanceMm` (line 197)
- `setButtonBinding` (line 206)
- `uploadDpiTableAtomic` (line 799) — reached by `setDpiStages`,
  `setDpiStage`, and `setActiveDpiStage`
- `emitLedPacket` (line 811) — reached by `setRgbStatic`, `setRgbEffect`,
  `setRgbBrightness`

`makeHidTransport`'s `write()` throws on a failed HID write (the catch blocks
elsewhere in this file exist precisely because `write()` can throw). When the
mouse is physically removed mid-session, these four void setters let the
exception propagate out of the `IMouseCapable` / `IRgbCapable` surface. Most
GUI/QML call sites invoke setters fire-and-forget and are not exception-safe,
so this is a crash / data-loss-on-disconnect path. The asymmetry also means
the RGB and legacy-DPI paths behave differently from the poll-rate/profile/
settings paths under identical fault conditions.

**Fix:** Wrap each write in the same guard the sibling setters use. Example for
`emitLedPacket`:

```cpp
void emitLedPacket() {
    auto const pkt = buildSetLedParam(m_lastLed.effect, m_lastLed.speed,
                                      m_lastLed.brightness, m_lastLed.modeBits,
                                      m_lastLed.r, m_lastLed.g, m_lastLed.b);
    try {
        (void)m_transport->write(pkt);
    } catch (std::exception const& e) {
        AJAZZ_LOG_WARN("mouse.aj_series", "emitLedPacket: HID write failed: {}", e.what());
    }
}
```

Apply the same pattern to `uploadDpiTableAtomic`, `setLiftOffDistanceMm`, and
`setButtonBinding`. (The `void`-returning IMouseCapable/IRgbCapable signatures
can't return a status, but they must not throw out of the device surface.)

## Warnings

### WR-01: Battery code contradicts its own comments and CLAUDE.md (0x83 vs 0xF7)

**File:** `src/devices/mouse/src/aj_series.cpp:258-306`, `aj_series_protocol.cpp:73-79`, `aj_series_protocol.hpp:59-60,130-134`
**Issue:** The shipped `batteryPercent()` poke is a hand-built **0xF7** status
poll (`poll[1] = kStatusPollOpcode` = `0xF7`, line 287) — which matches the
HARDWARE-VERIFIED method recorded in CLAUDE.md ("mouse battery = 0xF7 status
poll"). But the in-function comment block at lines 258-264 says the opposite:
*"SET_FEATURE the 0x83 GET_BATTERY poke (buildGetBattery() = [0x05, 0x83, …])"*.
The header (`FeaCmd::GetBattery = 0x83`, `buildGetBattery()` doc) and the
`test_aj_series_wire_format.cpp:342-360` "no-standalone-battery-opcode" guard
all assert the 0x83 method as if it were the live path. Per CLAUDE.md's hard
rule ("Always cross-check the reverse engineering before touching protocol …
the RE is the source of truth"), a comment that lies about the actual opcode
is a release-grade hazard: the next contributor will "fix" the code to match
the comment and regress the verified battery read.
**Fix:** Rewrite the lines 258-264 comment to describe the actual 0xF7
report-id-0x00 status poll. Either delete the now-misleading 0x83 references in
`aj_series_protocol.hpp:130-134` and the `buildGetBattery` doc, or annotate
them clearly as the superseded theory. Reconcile against CLAUDE.md and the RE
dossier so the single source of truth is the 0xF7 poll.

### WR-02: `buildGetBattery()` (opcode 0x83) is dead code

**File:** `src/devices/mouse/src/aj_series_protocol.cpp:73-79`
**Issue:** `buildGetBattery()` is exported and documented but is never called
anywhere in `src/` or `tests/` (grep confirms zero call sites outside its own
definition + comments). `batteryPercent()` builds its own 0xF7 poll inline
instead. Dead code that purports to be the battery poke compounds WR-01's
documentation drift.
**Fix:** Remove `buildGetBattery()` and `FeaCmd::GetBattery` if the 0x83 poke
is genuinely superseded by 0xF7; or, if 0x83 is still a real fallback, wire it
in and add a test. Do not leave an unused builder that contradicts the live
code path.

### WR-03: Entire TFT upload pipeline is unreachable from the backend

**File:** `src/devices/mouse/src/aj_series.cpp:914`, `aj_series_tft_pipeline.cpp` (whole file), `aj_series.cpp:6-11,22-26`
**Issue:** `renderClockDpiFace`, `encodeRgb565Chunks`, and
`buildSetTftLcdData` (opcode 0x25) are never invoked by `AjSeriesMouse` — the
only `IClockCapable` implementation, `setTime()` (lines 363-400), uses the
**0x28** firmware-RTC packet instead. The member `m_tftPanelSize` (line 914)
that the pipeline would consume is written once at construction and never read.
The file-level doc-comment (lines 22-26) still advertises *"IClockCapable —
clock + DPI face on the dock TFT (opcode 0x25 chunked RGB565)"* which is no
longer how the clock works. The pipeline is exercised only by isolated unit
tests, so the dead production wiring is invisible to the test suite.
**Fix:** Either (a) delete the unused pipeline + `m_tftPanelSize` and correct
the file header to describe the 0x28 RTC path, or (b) if a future custom-image
feature will use it, mark it explicitly as not-yet-wired in the header and add
a `// NOLINT(...)`/`[[maybe_unused]]` on `m_tftPanelSize` so the intent is
clear and `-Wunused-private-field` (Clang) stays quiet.

### WR-04: `m_tftPanelSize` is an unread private member (cross-platform -Werror risk)

**File:** `src/devices/mouse/src/aj_series.cpp:914`
**Issue:** `m_tftPanelSize{128, 128}` is assigned in the member initialiser and
never read. Under Apple Clang / Clang `-Werror` with `-Wunused-private-field`
this is a hard build error (CLAUDE.md flags Apple Clang `-Werror` as the strict
gate that catches things GCC misses). The default-init does not suppress the
warning because the field is genuinely never used.
**Fix:** Remove the member (preferred — see WR-03), or annotate
`[[maybe_unused]] QSize m_tftPanelSize{128, 128};` if it must stay for an
imminent feature.

### WR-05: `setActiveDpiStage` silently clamps while `setDpiStage` throws — inconsistent contract

**File:** `src/devices/mouse/src/aj_series.cpp:161-179`
**Issue:** `setDpiStage(index, …)` throws `std::out_of_range` when
`index >= dpiStageCount()` (line 162-164), but `setActiveDpiStage(index)`
silently clamps to 7 via `std::min<std::uint8_t>(index, 7)` (line 177). Two
index-taking methods on the same capability handle out-of-range input with
opposite policies (throw vs swallow). A caller that relies on the throw for
validation gets none from `setActiveDpiStage`, and one that catches nothing
gets surprised by `setDpiStage`. No test covers the `setActiveDpiStage`
out-of-range path, so the clamp is unverified.
**Fix:** Pick one policy. Given the rest of the class clamps (profile, poll
rate, slot, LOD), prefer clamping in `setDpiStage` too (or document the
deliberate divergence). Add a test for `setActiveDpiStage(99)`.

### WR-06: `parseBatteryCharge` auto-detect can misread a Linux frame whose data byte 0 == 0x05

**File:** `src/devices/mouse/src/aj_series.cpp:240-256`
**Issue:** The Windows-vs-Linux offset is auto-detected purely from
`frame[0] == kBatteryStatusReportId` (0x05). On Linux the frame is unnumbered,
so if the first *data* byte ever legitimately equals 0x05, the parser treats it
as a Windows report-ID prefix and reads charge from `frame[3]` instead of
`frame[2]`, returning a wrong percentage. The documented layout (`00 00 charge …`)
makes this unlikely (byte 0 is documented as 0x00), but the heuristic has no
corroborating check (e.g. verifying the trailing `01 01 01 02` status tail).
**Fix:** Tighten the detection: confirm the status tail bytes, or pass the
platform/transport-known report-id-present flag down rather than sniffing
`frame[0]`. At minimum add a comment+test pinning the documented invariant that
Linux byte 0 is always 0x00.

### WR-07: Test header doc-comment describes a stale wire format (0x21/kCmdDpi, 64-byte report)

**File:** `tests/unit/test_aj_series_mock_transport.cpp:9-18`
**Issue:** The file-level doc-comment still describes the *old* envelope
(`kReportSize == 64`, command `0x21` kCmdDpi at byte 1, sub-cmd 0x01, checksum
`0x23`). The actual test body (lines 88-99) correctly asserts the new format
(65-byte, opcode `0x54`, BIT7 checksum `0x54`). The doc-comment is now false and
references constants (`kCmdDpi`, `kReportSize == 64`) that no longer exist —
exactly the kind of drift CLAUDE.md warns leads contributors astray.
**Fix:** Update the doc-comment to match the 0x54 / 65-byte / BIT7 reality the
test now verifies.

## Info

### IN-01: BIT7 checksum range doc-comment off by one (pkt[1..62] vs pkt[1..63])

**File:** `src/devices/mouse/src/aj_series_protocol.hpp:107-113`
**Issue:** The doc says `Checksum = sum(pkt[1..62]) & 0x7F`, but the impl
(`aj_series_protocol.cpp:63`, `accumulate(begin()+1, end()-1, …)`) and the test
(`test_aj_series_wire_format.cpp:57` "checksum range is pkt[1..63]") both sum
**pkt[1..63]**. The header comment understates the range by one byte.
**Fix:** Change the header doc to `sum(pkt[1..63])` to match code and test.

### IN-02: Macro `lastNonZeroPos` is 0-based but spec/doc describe a 1-based `56*(u-1)+s`

**File:** `src/devices/mouse/src/aj_series.cpp:677-689`
**Issue:** The code computes `lastNonZeroPos` as the **0-based** index of the
last non-zero byte, but the surrounding comment (and `aj_series_protocol.hpp`
line 419-427) cite vendor §3.11 line 491 as `56*(u-1) + s` (a 1-based scheme).
The empty-payload comment claims "the last non-zero byte is at position 1" for
`[0x01, 0x00]` though index of `0x01` is 0. The tests match the 0-based code,
but no hardware witness confirms the firmware expects 0-based — this is an
unverified-against-device value (CLAUDE.md: treat provisional RE values as
hypotheses). Flag for a hardware round-trip before relying on macros.
**Fix:** Reconcile the comment with the code (state it is 0-based), and add a
HANDOFF note that `lastNonZeroPos` encoding is pending hardware confirmation.

### IN-03: `setRgbBrightness` integer math truncates and ignores `setRgbStatic` speed reset

**File:** `src/devices/mouse/src/aj_series.cpp:343-348`
**Issue:** `(percent * 5u) / 100u` truncates toward zero — e.g. 19% → 0
(off), 39% → 1. Combined with the fact that brightness is the only field
`setRgbBrightness` touches (effect/speed/color come from whatever the last
`setRgbStatic`/`setRgbEffect` left in `m_lastLed`), a brightness-only call after
a non-static effect re-emits that effect. Behaviorally defensible but the
truncation makes low-percent inputs map to "off" surprisingly.
**Fix:** Round to nearest: `static_cast<std::uint8_t>((percent * 5u + 50u) / 100u)`,
and document that brightness rides the cached effect.

### IN-04: `register.cpp` battery-offset comment contradicts the §4 RE / CLAUDE.md

**File:** `src/devices/mouse/src/register.cpp:101-106`
**Issue:** The comment says charge is "at byte 3 on Windows / byte 2 on Linux"
and describes a "passive GET_FEATURE" — but per WR-01 the live read is an active
0xF7 poll, and CLAUDE.md pins the verified offset narrative. Same drift as WR-01,
lower severity since it's only descriptive text in the registration file.
**Fix:** Align with the corrected battery narrative once WR-01 is resolved.

### IN-05: Magic number `7` for max DPI-stage index repeated across the class

**File:** `src/devices/mouse/src/aj_series.cpp:177, 444, 589, 590`
**Issue:** `std::min<std::uint8_t>(index, 7)` and friends hard-code `7`
(=`dpiStageCount()-1` / `onboardProfileCount()-1`) in several places rather than
deriving from the accessor. If a future SKU changes stage/profile counts, these
literals silently desync.
**Fix:** Derive from `dpiStageCount() - 1` / `onboardProfileCount() - 1`, or a
named `kMaxStageIndex` constant.

### IN-06: `firmwareVersion()` hard-codes "unknown" despite a `buildGetRev()` (0x80) being available

**File:** `src/devices/mouse/src/aj_series.cpp:124`
**Issue:** `firmwareVersion()` always returns `"unknown"` even though
`buildGetRev()` (opcode 0x80, §3.1) exists in the protocol layer to query it.
Not a bug, but the version surface is permanently stubbed and the GetRev builder
is (like buildGetBattery) effectively dead.
**Fix:** Wire `buildGetRev()` + a response parse into `firmwareVersion()`, or
document why it stays stubbed (no hardware witness for the response shape).

______________________________________________________________________

_Reviewed: 2026-05-22_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
