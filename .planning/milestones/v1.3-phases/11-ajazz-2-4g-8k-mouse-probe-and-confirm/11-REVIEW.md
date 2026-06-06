---
phase: 11-ajazz-2-4g-8k-mouse-probe-and-confirm
reviewed: 2026-05-22T00:00:00Z
depth: standard
files_reviewed: 12
files_reviewed_list:
  - src/devices/mouse/src/aj_series.cpp
  - src/devices/mouse/src/aj_series_protocol.cpp
  - src/devices/mouse/src/aj_series_protocol.hpp
  - src/devices/mouse/src/aj_series_tft_pipeline.cpp
  - src/devices/mouse/src/aj_series_tft_pipeline.hpp
  - src/devices/mouse/src/register.cpp
  - src/devices/mouse/include/ajazz/mouse/mouse.hpp
  - tests/unit/test_aj_series_mock_transport.cpp
  - tests/unit/test_aj_series_wire_format.cpp
  - tests/unit/test_aj_series_settings.cpp
  - tests/unit/test_aj_series_dpi_fn.cpp
  - CLAUDE.md
findings:
  critical: 0
  warning: 4
  info: 5
  total: 9
status: issues_found
---

# Phase 11: Code Review Report (RE-REVIEW / verification pass)

**Reviewed:** 2026-05-22
**Depth:** standard
**Files Reviewed:** 12
**Status:** issues_found

## Summary

Verification pass over the Phase 11 AJAZZ AJ-series mouse code, with focus on
the CR-01 device-yank fix (commit 02d03b5), regressions introduced by that fix,
and remaining quality defects. This OVERWRITES the prior REVIEW.md.

**CR-01 is CONFIRMED FIXED and is no longer a finding.** All four previously
unguarded void-setter write paths now wrap `m_transport->write()` in the same
`WARN`-and-swallow `try/catch (std::exception const&)` block used by sibling
setters:

- `setLiftOffDistanceMm` (aj_series.cpp:197-203) — direct write, guarded.
- `setButtonBinding` (aj_series.cpp:210-216) — direct write, guarded.
- `uploadDpiTableAtomic` (aj_series.cpp:808-814) — reached by `setDpiStages` /
  `setDpiStage` / `setActiveDpiStage`, guarded.
- `emitLedPacket` (aj_series.cpp:817-830) — reached by all `setRgb*` setters,
  guarded.

I audited every other write path in the class for a still-unguarded surface:
the bool/optional setters (`setPollingRateHz`, `setActiveOnboardProfile`,
`setMouseSettings`, `setDpiTable`, `setFnLayerBinding`, `uploadMacro`,
`factoryReset`), `readKeyMatrix` (write + read), `batteryPercent` (writeFeature

- readFeature), and `setTime` (writeFeature) are ALL wrapped. No unguarded
  `->write` / `->writeFeature` / `->read` / `->readFeature` call remains on the
  configuration path. CR-01 does not recur.

**No regression introduced by the CR-01 fix.** The guard is correctly scoped:
the void setters silently swallow (matching their void contract and the
fire-and-forget QML caller model), while the bool setters that already returned
`false` on failure were untouched and still propagate failure to callers. The
diff (verified against `git show 02d03b5`) is a pure additive try/catch around
the existing write call plus the new `ThrowingTransport` regression test — no
behavioural change beyond not crashing. The regression test
(`test_aj_series_mock_transport.cpp:239-257`) exercises all four sites
(two direct, one via `setActiveDpiStage`, one via `setRgbBrightness`) and
asserts `CHECK_NOTHROW`.

**Hard project checks:**

- **COD-031:** PASS. `grep -rn nlohmann src/core/include/` and
  `grep -rn nlohmann src/devices/mouse/` both return zero hits.
- **Wire format vs RE:** Battery now uses the 0xF7 status poll
  (`kStatusPollOpcode`, hardware-verified per the project memory note) —
  correct. OLED clock 0x28 keeps the required 0xD7 marker at byte 8, year
  big-endian, no checksum — correct. BIT7 checksum range pkt[1..63] is
  consistent across the builder and all four test files.
- **Cross-platform -Werror:** see WR-04 (unclamped percent→scale) and IN-04
  (a single-use file-scope `constexpr` at risk under `-Wunused-const-variable`).

The remaining defects are quality / maintainability / wire-format-doc-drift
issues; none is a blocker.

## Warnings

### WR-01: Stale battery 0x83 dead-code path and contradictory doc comments

**File:** `src/devices/mouse/src/aj_series.cpp:267-315`,
`src/devices/mouse/src/aj_series_protocol.cpp:73-79`,
`src/devices/mouse/src/aj_series_protocol.hpp:59-60,130-134`
**Issue:** The implemented battery read uses the **0xF7** status poll built
inline (`aj_series.cpp:294-296`: `poll[1] = kStatusPollOpcode`). The older
**0x83** path is now dead:

- `buildGetBattery()` (protocol.cpp:73-79) builds a `FeaCmd::GetBattery`
  (0x83) packet with **zero call sites** in production or tests (confirmed by
  grep). It is referenced only inside a comment.
- The `IBatteryCapable` doc block (aj_series.cpp:267-286) still describes the
  superseded "SET_FEATURE the 0x83 GET_BATTERY poke ... then GET_FEATURE that
  report ... and read the charge at byte 2" handshake. The actual code sends
  0xF7, then reads charge at byte 3 (Windows) / byte 2 (Linux) via
  `parseBatteryCharge`. The comment at line 273 ("read the charge at byte 2")
  contradicts the code's auto-detected byte-3 Windows path.
- The header doc for `buildGetBattery` (protocol.hpp:130-134) and the enum
  comment (protocol.hpp:59-60) both still document the 0x83 reply landing in a
  status report — narrative for a path the runtime no longer takes.

CLAUDE.md makes the schema/RE doc the source of truth for wire keys; a
maintainer reading this block could re-introduce the 0x83 poke.
**Fix:** Either delete `buildGetBattery()` + `FeaCmd::GetBattery` if 0x83 is
retired, or annotate them "superseded by 0xF7 — see `batteryPercent()`". Rewrite
the `batteryPercent()` doc block (267-286) to describe the 0xF7 poll and the
byte-3/byte-2 auto-detect, removing the "0x83 ... read the charge at byte 2"
language.

### WR-02: Omnibus 0x53 settings push zeroes the cached LED sub-blocks on the wire

**File:** `src/devices/mouse/src/aj_series.cpp:484-491`,
`src/devices/mouse/src/aj_series_protocol.cpp:309-310`,
`src/devices/mouse/src/aj_series_protocol.hpp:301-307`
**Issue:** `setMouseSettings` builds the omnibus packet with
`buildMouseSettings(m_activeProfile, m_pollRate, settings)` and writes it
directly. `buildMouseSettings` intentionally leaves `ledBlock` and
`logoLedBlock` zero (protocol.cpp:309-310), and BOTH the builder comment and
the header doc (protocol.hpp:301-307) explicitly promise that "the
`AjSeriesMouse` setter wires the cached blocks back in before send" / "to keep
the LED state coherent across commits." But `setMouseSettings` never injects
any cached LED block before `write()` — the mirror-back at lines 507-535 writes
INTO `m_options` for *future* `setLiftOffDistanceMm` re-emits, it does not feed
the just-built packet. Result: every settings push transmits all-zero LED +
logo-LED sub-blocks at vendor bytes 24..39, which the firmware reads as
"LED off / black". A user who sets an RGB colour and then changes any unrelated
setting (sleep timer, LOD, sensitivity) silently loses their lighting.
**Fix:** Before the `write(pkt)` in `setMouseSettings`, populate the LED
sub-blocks from cached state — e.g. build via `buildMouseSetOption0(m_options)`
after mirroring `m_lastLed` into `m_options.ledBlock`/`logoLedBlock` — so the
documented promise becomes true. Alternatively, if clearing the LED blocks on
every omnibus push is the intended firmware behaviour, fix the contradictory
docs; the current code+doc pair cannot both be right.

### WR-03: Macro `lastNonZeroPos` encoding contradicts its own spec and comments

**File:** `src/devices/mouse/src/aj_series.cpp:686-698`,
`src/devices/mouse/src/aj_series_protocol.hpp:419-427`
**Issue:** `uploadMacro` computes `lastNonZeroPos` as the **0-based** index of
the last non-zero byte:

```cpp
for (std::size_t i = 0; i < payloadBytes; ++i) {
    if (encoded[i] != 0) {
        lastNonZeroPos = static_cast<std::uint8_t>(i);
    }
}
```

But three places document it as **1-based**:

- The header param doc (protocol.hpp:419-427) specifies the §3.11 line-491
  encoding `56*(u-1) + s` where `s` is "the position of the last non-zero byte
  within that chunk" — a 1-based byte position in the vendor scheme.
- The inline comment (aj_series.cpp:690-692) claims "the last non-zero byte is
  at position 1 (the 0x01 repeatCount low byte)" for an empty macro.
- The class-level comment (aj_series.cpp:669-672) claims empty events emit
  "lastNonZeroPos=1".

For an empty macro the encoder emits `[0x01, 0x00]`, so the loop yields
`lastNonZeroPos = 0` (index of the `0x01`), NOT 1. The code is off-by-one
relative to its own documented `56*(u-1)+s` vendor formula. Either the vendor
expects a 1-based marker (then the code under-reports by one and firmware may
truncate the final macro byte) or the vendor expects 0-based (then all three
comments are wrong). There is no round-trip test pinning this, so the divergence
is a latent wire-format bug.
**Fix:** Reconcile against `aj_series_opcode_table.md` §3.11 line 491. If 1-based,
use `lastNonZeroPos = static_cast<std::uint8_t>(i + 1)` and confirm the
empty-macro case lands `1`; if 0-based, correct the three comments. Add a unit
test pinning `lastNonZeroPos` for the empty-macro case and a known multi-event
payload so the chosen convention is regression-locked.

### WR-04: `setRgbBrightness` percent→scale conversion has no upper clamp

**File:** `src/devices/mouse/src/aj_series.cpp:352-357`
**Issue:**

```cpp
m_lastLed.brightness = static_cast<std::uint8_t>((percent * 5u) / 100u);
```

The comment says "Clamp 0..100% → vendor scale 0..5", but there is no clamp.
The `percent` param is `std::uint8_t` (range 0..255); passing `percent > 100`
yields `(255 * 5) / 100 = 12`, far outside the documented vendor 0..5 range,
sending an out-of-spec brightness byte to firmware. Sibling code clamps
defensively everywhere (sensitivity/LOD in `buildMouseSetOption0`, profile slot
in `setActiveOnboardProfile`), so this is an inconsistent missing guard.
**Fix:**

```cpp
std::uint8_t const pct = std::min<std::uint8_t>(percent, 100);
m_lastLed.brightness = static_cast<std::uint8_t>((pct * 5u) / 100u);
```

## Info

### IN-01: `setActiveDpiStage` clamps out-of-range index while `setDpiStage` throws

**File:** `src/devices/mouse/src/aj_series.cpp:161-179`
**Issue:** `setDpiStage(index, ...)` throws `std::out_of_range` for
`index >= dpiStageCount()` (lines 162-164), but `setActiveDpiStage(index)`
silently clamps via `std::min<std::uint8_t>(index, 7)` (line 177). Two adjacent
`IMouseCapable` index setters handle out-of-range differently — a caller cannot
predict whether a bad index throws or is quietly clamped. Contract-asymmetry
smell, not a correctness bug.
**Fix:** Pick one policy. Given the rest of the file clamps defensively, prefer
clamping in `setDpiStage` too (or document the divergence in the interface). If
throwing is intended, throw in both.

### IN-02: `parseBatteryCharge` auto-detect can misclassify a Linux frame whose byte-0 is 0x05

**File:** `src/devices/mouse/src/aj_series.cpp:249-265`
**Issue:** The Windows-vs-Linux offset is auto-detected purely by
`frame[0] == kBatteryStatusReportId (0x05)`. On Linux (unnumbered frame) the
charge sits at `frame[2]`; if a Linux frame's `frame[0]` ever equals 0x05, the
parser takes the Windows branch (chargeIndex=3) and reads the wrong byte. The
captured Linux frame is `00 00 64 ...` so `frame[0]==0` today and the doc
acknowledges the heuristic, so risk is low — but detection is value-based, not
transport-based.
**Fix:** Low priority; if a Linux frame with a non-zero leading byte is ever
observed, switch to a transport-supplied "report-id present" flag instead of
sniffing the value.

### IN-03: `mouse.hpp` factory doc is stale (wrong transport + wrong SKU list)

**File:** `src/devices/mouse/include/ajazz/mouse/mouse.hpp:33-46`
**Issue:** The `makeAjSeries` doc-comment says the backend targets "AJ159,
AJ199, AJ339 Pro, AJ380" via a "64-byte feature-report command envelope on HID
interface #1 (keyboard-class)". Per `register.cpp` the AJ339/AJ380 SKUs were
removed as fictional; the envelope is **65-byte HID OUTPUT reports** (not
feature reports — see protocol.hpp:17-23); and the control collection is
usage-page 0xFFFF/usage 0x02, not the keyboard interface. The header doc
contradicts both `register.cpp` and the protocol header.
**Fix:** Update the doc: drop AJ339/AJ380, say "65-byte HID OUTPUT report on the
0xFFFF/usage-0x02 control collection," and reference `aj_series_opcode_table.md`
instead of the older `aj_series.md`.

### IN-04: `kDefaultProfile` is a single-use file-scope constexpr at -Werror risk

**File:** `src/devices/mouse/src/aj_series.cpp:75,923`
**Issue:** `constexpr std::uint8_t kDefaultProfile = 0;` is used exactly once,
as the in-class initialiser for `m_activeProfile`. It is a file-scope
`constexpr` in an anonymous namespace; if a refactor removes that single use,
Apple Clang `-Werror` flags `-Wunused-const-variable` on file-scope
`inline constexpr` (per CLAUDE.md cross-platform note). Minor today; flagged
because the project's build-strictness section calls this exact class out.
**Fix:** Either inline the literal `0` into the member initialiser or ensure the
constant stays referenced. Not urgent.

### IN-05: `test_aj_series_mock_transport.cpp` banner describes the OLD wrong wire format

**File:** `tests/unit/test_aj_series_mock_transport.cpp:6-19`
**Issue:** The file-level doc-comment still describes `kReportSize == 64`,
command `0x21` (kCmdDpi), sub-cmd `0x01`, payload-length `0x01`, and a
`& 0xff = 0x23` checksum — the pre-P3.12 wrong envelope. The actual test body
(lines 100-122) correctly asserts the new 65-byte, opcode-0x54, BIT7-checksum
envelope and even comments "was 0x21, wrong". The banner now misleads directly
above the corrected assertions.
**Fix:** Rewrite the banner to describe the current 0x54 / 65-byte / BIT7
envelope, matching the in-body comments.

______________________________________________________________________

_Reviewed: 2026-05-22_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
