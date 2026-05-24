---
phase: 24-family-coverage-akp03-153-815
fixed_at: 2026-05-24T22:01:00Z
review_path: .planning/phases/24-family-coverage-akp03-153-815/24-REVIEW.md
iteration: 1
findings_in_scope: 10
fixed: 10
skipped: 0
status: all_fixed
---

# Phase 24: Code Review Fix Report

**Fixed at:** 2026-05-24T22:01:00Z
**Source review:** .planning/phases/24-family-coverage-akp03-153-815/24-REVIEW.md
**Iteration:** 1

**Summary:**

- Findings in scope: 10 (2 Critical + 5 Warning + 3 Info)
- Fixed: 10
- Skipped: 0

**Build gate:** `cmake --build build/linux-release` — CLEAN (685/685 targets, 0 errors)
**Test gate:** `ctest --preset linux-release` — 629/629 PASS (0 failures)
**New tests added:** 5 (StreamDockFamily tests 219-223 — CR-01 rejection x4 + WR-05 encoder-release x1)

## Fixed Issues

### CR-01: `setKeyImage` and `clearKey` validate against total button count (9) not LCD count (6)

**Files modified:** `src/devices/streamdeck/src/akp03.cpp`
**Commit:** 76c786e
**Applied fix:** Changed both guards from `akp03::KeyCount` (9) to `akp03::DisplayKeyCount` (6).
Side buttons 7..9 have no LCD surface; sending a BAT burst to those slots is
firmware-undefined behaviour. The fix ensures indices 7-9 are silently rejected
with a WARN_LOG, while indices 1-6 continue to produce a BAT+ULEND burst.

RE cross-check: akp03.md "LCD keys: 6"; akp03_protocol.hpp:67-69 explicitly
distinguishes `DisplayKeyCount=6` from `KeyCount=9`.

Note: also required updating the LOG_WARN format arg from `akp03::KeyCount` to
`akp03::DisplayKeyCount` so the log message reflects the correct accepted range.

**Classification:** fixed

______________________________________________________________________

### CR-02: Stale file comment, class docstring, and `sendImage` function name claim PNG/72x72/1 encoder

**Files modified:** `src/devices/streamdeck/src/akp03.cpp`, `src/devices/streamdeck/src/akp03_protocol.hpp`
**Commit:** 05f21fc
**Applied fix:** Documentation-only changes (no wire bytes altered):

- File docstring: "72x72 PNG / one encoder" -> "60x60 JPEG / three encoders (1 large + 2 small)"
- Class docstring: "6-key 72x72 PNG grid / one encoder" -> "6-key 60x60 JPEG grid / three encoders"
- `buildImageHeader` in both header and .cpp: param `pngSize` -> `imageSize`; doc updated to JPEG
- `buildImageHeader` body: `buildCmdHeader(CmdImagePng)` -> `buildCmdHeader(CmdImage)` (same bytes
  since `CmdImagePng = CmdImage` as a deprecated alias — no wire change)
- `sendImage`: param `png` -> `jpeg`; docstring updated
- `setKeyImage` inline comment: "PNG-encoded" -> "JPEG-encoded", "72x72" -> "60x60"

RE cross-check: akp03.md §Image-upload: "60x60 JPEG, Rot0"; akp03_protocol.hpp:
`KeyWidthPx=60`, `EncoderCount=3`, `CmdImage=BAT`.

Runtime values (jpegEncoded=true, KeyWidthPx=60, EncoderCount=3) were already correct;
only the documentation layer was wrong.

**Classification:** fixed

______________________________________________________________________

### WR-01: AKP815 LCD strip size is internally contradicted (800x480 vs 854x480)

**Files modified:** `docs/protocols/streamdeck/akp_device_matrix.md`
**Commit:** d15bffb
**Applied fix:** Aligned the matrix table entry for AKP815 strip from 854x480 to 800x480,
marking it with a provisional warning. The 854x480 value was likely copied from
the AKP153 logo-channel dimension. The more-precisely-attributed value is 800x480
from `[ajazz-sdk]/info.rs::Kind::Akp815::lcd_strip_size()`, which also matches
akp815.md §Hardware and akp815_protocol.hpp `StripWidthPx=800`.

Neither value is hardware-confirmed; the note clearly marks it provisional pending
Phase-25 hardware capture. The 800x480 value is NOT claimed as hardware-confirmed.

RE cross-check: akp815.md §Hardware "800x480 px addressable LCD strip (per lcd_strip_size())";
akp815_protocol.hpp `StripWidthPx=800`, `StripHeightPx=480`.

**Classification:** fixed

______________________________________________________________________

### WR-02: `akp153_protocol.hpp` USB IDs are stale and contradicted by `akp153.md` research

**Files modified:** `src/devices/streamdeck/src/akp153_protocol.hpp`
**Commit:** 7c31b5f
**Applied fix:** Added `@deprecated` annotations and explanatory comments to
`ProductIdInternational=0x1001` and `ProductIdChinese=0x1002`, documenting:

- The correct canonical AKP153 international pair: VID=0x5548, PID=0x6674
- The correct canonical AKP153E China pair: VID=0x0300, PID=0x1010
- That both legacy constants are kept only for register.cpp backward compatibility

No constant values were changed; register.cpp was not touched. New code is
explicitly warned away from these constants via the @deprecated tag.

**Classification:** fixed

______________________________________________________________________

### WR-03: `akp153::parseInputReport` always returns `pressed = true`

**Files modified:** `src/devices/streamdeck/src/akp153.cpp`
**Commit:** d0e48d6
**Applied fix:** Per the fix guidance, the AKP153 release encoding is not documented
in akp153.md §Wire-protocol §Input-reports (as of 2026-05-24). We do NOT invent a
wire format. Instead, the `@note` docstring was updated to:

- Honestly describe the limitation (always pressed=true)
- Add `TODO(WR-03)` referencing the RE gap and Phase-25 hardware capture as the
  resolution path
- Explain that a stateful diff will be required when the encoding is known

The existing behavior is unchanged; the documentation is now accurate.

**Classification:** fixed (documentation + TODO; runtime unchanged by design per fix guidance)

______________________________________________________________________

### WR-04: `stream_dock_input_service.hpp` file header and class docstring still claim "AKP05E-specific"

**Files modified:** `src/app/src/stream_dock_input_service.hpp`
**Commit:** 395dccc
**Applied fix:** Updated both the file docstring and class docstring to reflect the
Phase 24 generalization. The new documentation:

- Lists all four supported families (AKP03, AKP05/05E, AKP153, AKP815) with their
  geometry (keys/encoders/touch strip)
- Documents the descriptor-driven m_encAccum sizing
- Clarifies the touch-strip gate (AKP05 only)
- Notes the AKP03 v3 real EncoderReleased behavior (WR-05 reference)
- Updates the synthesiseEncoderRelease docstring from press-only assumption to the
  accurate family-aware description

No runtime behaviour change.

**Classification:** fixed

______________________________________________________________________

### WR-05: AKP03 backend dispatches `EncoderReleased` device events but service silently drops them with misleading comment

**Files modified:** `src/app/src/stream_dock_input_service.cpp`
**Commit:** 670348e
**Applied fix:** Replaced the misleading "Dormant on hardware (akp05.md:76 press-only); ignore
safely." comment with accurate documentation:

- `EncoderReleased` case: explains AKP05 is press-only (so the case IS unreachable for AKP05),
  while AKP03 v3 firmware DOES emit real release events. Currently dropped because EncoderBinding
  has no onRelease field (profile.hpp:113-118). Added `TODO(WR-05)` for when onRelease lands.
- `EncoderPressed` case: clarified comment to note AKP03 v3 sends a real release separately
  (which is currently harmless since EncoderBinding has no onRelease).

The functional behavior (synthesise release on EncoderPressed; drop EncoderReleased) is
intentionally unchanged. EncoderBinding::onRelease does not exist yet.

**Classification:** fixed (comment + TODO; functional change deferred to when onRelease exists)

______________________________________________________________________

### IN-01: Stale comment in `akp03.cpp` `displayInfo()` claims "PNG" format

**Files modified:** `src/devices/streamdeck/src/akp03.cpp` (via CR-02 fix)
**Commit:** 05f21fc
**Applied fix:** IN-01 noted this is resolved by fixing CR-02. The CR-02 fix in the same commit
corrected the file-level docstring that was the root cause; the displayInfo() inline comment
`.jpegEncoded = true` was already correct and unchanged.

**Classification:** fixed (via CR-02)

______________________________________________________________________

### IN-02: `buildImageHeader` in `akp03_protocol.hpp` and `akp03.cpp` still use `pngSize` / `CmdImagePng`

**Files modified:** `src/devices/streamdeck/src/akp03_protocol.hpp`, `src/devices/streamdeck/src/akp03.cpp`
**Commit:** 05f21fc
**Applied fix:** Renamed `pngSize` -> `imageSize` in both the declaration (akp03_protocol.hpp:191-192)
and definition (akp03.cpp:128-133). Updated the doc-tag from `@param pngSize` to `@param imageSize`.
The deprecated alias `CmdImagePng = CmdImage` is kept as noted in the header comment.
The `buildImageHeader` body was also updated to call `buildCmdHeader(CmdImage)` directly
rather than through the `CmdImagePng` alias (no wire change).

**Classification:** fixed (via CR-02)

______________________________________________________________________

### IN-03: `stream_dock_control_service.cpp` comment at line 26-27 says "AKP05E backend" and "1..10"

**Files modified:** `src/app/src/stream_dock_control_service.cpp`
**Commit:** 1abfb7f
**Applied fix:** Updated the key-index mapping comment from:

```
//   the AKP05E backend requires 1-based indices (1..10).
```

to:

```
//   device backends expect 1-based indices (1..descriptor.keyCount). The
//   actual upper bound is per-family: AKP03=6, AKP05=10, AKP153/AKP815=15 --
//   enforced by each backend's own setKeyImage guard.
```

No runtime behaviour change.

**Classification:** fixed

______________________________________________________________________

## Regression Tests Added

**Commit:** 2c505cd
**File:** `tests/unit/test_stream_dock_family.cpp`

5 new tests added to the `[stream-dock-family]` tag:

| Test # | Test name                                                           | Finding |
| ------ | ------------------------------------------------------------------- | ------- |
| 219    | CR-01 AKP03 side-button keyIndex 7 rejected -- no BAT burst         | CR-01   |
| 220    | CR-01 AKP03 side-button keyIndex 8 rejected -- no BAT burst         | CR-01   |
| 221    | CR-01 AKP03 side-button keyIndex 9 rejected -- no BAT burst         | CR-01   |
| 222    | CR-01 AKP03 LCD keyIndex 1..6 all produce a BAT burst               | CR-01   |
| 223    | WR-05 AKP03 EncoderReleased hardware event dispatched without crash | WR-05   |

All 15 StreamDockFamily tests pass (10 pre-existing + 5 new).
Full suite: 629/629 PASS.

______________________________________________________________________

## RE Documents Cross-Checked

For each wire-format or protocol-adjacent change, the following RE docs were consulted:

| Finding | RE document consulted                                               | Key fact verified                                                        |
| ------- | ------------------------------------------------------------------- | ------------------------------------------------------------------------ |
| CR-01   | `docs/protocols/streamdeck/akp03.md` §Hardware                      | "LCD keys: 6"                                                            |
| CR-01   | `src/devices/streamdeck/src/akp03_protocol.hpp:67-69`               | `DisplayKeyCount=6`, `SideButtonCount=3`, `KeyCount=9`                   |
| CR-02   | `docs/protocols/streamdeck/akp03.md` §Image-upload                  | "60x60 JPEG, Rot0"                                                       |
| CR-02   | `src/devices/streamdeck/src/akp03_protocol.hpp:71-72,110-115`       | `KeyWidthPx=60`, `EncoderCount=3`, `CmdImage=BAT`                        |
| WR-01   | `docs/protocols/streamdeck/akp815.md` §Hardware                     | "800x480 px addressable LCD strip per lcd_strip_size()"                  |
| WR-01   | `src/devices/streamdeck/src/akp815_protocol.hpp:41-42`              | `StripWidthPx=800`, `StripHeightPx=480`                                  |
| WR-02   | `docs/protocols/streamdeck/akp153.md` §Variants                     | Canonical PIDs: VID=0x5548,PID=0x6674 (intl); VID=0x0300,PID=0x1010 (CN) |
| WR-03   | `docs/protocols/streamdeck/akp153.md` §Wire-protocol §Input-reports | Release format: NOT documented -- gap confirmed                          |
| WR-05   | `src/devices/streamdeck/src/akp03.cpp:393-397`                      | `EncoderReleased` dispatched from `poll()` for v3 firmware               |
| WR-05   | `docs/protocols/streamdeck/akp05.md:76`                             | AKP05 is press-only (release must be synthesised)                        |

No wire bytes, opcodes, or packet layouts were invented or changed. The two wire-format
fixes (CR-01, CR-02) align the code to already-correct RE documents.

______________________________________________________________________

_Fixed: 2026-05-24T22:01:00Z_
_Fixer: Claude (gsd-code-fixer)_
_Iteration: 1_
