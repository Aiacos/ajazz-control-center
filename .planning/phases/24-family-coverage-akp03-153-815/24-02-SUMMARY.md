---
phase: 24-family-coverage-akp03-153-815
plan: '02'
subsystem: devices
tags: [stream-dock, akp03, akp153, akp815, capability-generic, descriptor-driven, mock-transport, byte-test]

# Dependency graph
requires:
  - phase: 24-family-coverage-akp03-153-815
    provides: makeAkp03/153/815WithTransport DI seams (plan 24-01)
  - phase: 15-stream-dock-input-routing
    provides: StreamDockInputService (ProfileAccessor + spy ActionEngine seams)
  - phase: 14-stream-dock-control-service
    provides: StreamDockControlService (IDisplayCapable + setActiveDevice seams)
provides:
  - Descriptor-driven StreamDockControlService + StreamDockInputService (no hardcoded AKP05 geometry)
  - Per-family MockTransport byte test pinning image-header/terminator + input-fires-action for AKP03/153/815
  - AKP03 3-encoder coverage (press/CW/CCW on all 3 via descriptor.encoderCount=3)
  - encoderCount=0 inert-path proof for AKP153/815 (no crash, no spurious dispatch)
affects: [25-hardware-verification, any phase using StreamDockControlService or StreamDockInputService]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - Descriptor-driven sizing: runtime m_encoderCount + std::vector<int32_t> m_encAccum sized from descriptor.encoderCount on setActiveDevice()
    - hasTouchStrip guard: dispatch branch gated on descriptor.hasTouchStrip (defence-in-depth)
    - displayInfo()-driven paint dimensions: repaintPage() reads widthPx/heightPx per family
    - AKP05-specific zoneForX documented with local kAkp05EncoderCount=4 constant (not re-exported)

key-files:
  created:
    - tests/unit/test_stream_dock_family.cpp
  modified:
    - src/app/src/stream_dock_control_service.cpp
    - src/app/src/stream_dock_input_service.cpp
    - src/app/src/stream_dock_input_service.hpp
    - tests/unit/test_stream_dock_input_service.cpp
    - tests/unit/CMakeLists.txt

key-decisions:
  - Encoder accumulator changed from std::array<int32_t,4> to std::vector<int32_t> sized at setActiveDevice() time - enables AKP03=3 / AKP05=4 / AKP153+815=0 without code branching
  - zoneForX kept AKP05-specific with local kAkp05EncoderCount=4 constant (touch strip is AKP05-only; no generalization needed)
  - TouchStrip dispatch gated on m_hasTouchStrip (defence-in-depth) even though AKP03/153/815 backends never emit TouchStrip events
  - 'repaintPage() background fill now reads displayInfo().widthPx/heightPx: AKP03=60x60, AKP05/AKP153=85x85, AKP815=100x100 - single parameterization, not per-device branch'
  - Live hardware confirmation deferred entirely to Phase 25 - AKP03 is the only family member confirmed physically present; AKP153 and AKP815 are MockTransport-only

patterns-established:
  - 'Descriptor-driven vector sizing: assign m_encAccum from descriptor.encoderCount at setActiveDevice() - zero-size means inert path, non-zero is exact fit'
  - 'Per-family byte test pattern: makeAkp*WithTransport + control-service + input-service + spy ActionEngine - generalised from the AKP05E Phase 14-15 template'

requirements-completed: [DEVICES-10]

# Metrics
duration: 95min
completed: 2026-05-24
---

# Phase 24 Plan 02: Family Coverage Descriptor-Driven Services + Byte Tests Summary

**Killed hardcoded AKP05-specific geometry in both app services (encoder array, paint dimensions, touch guard) and proved AKP03/153/815 drive through the same code path via 10 MockTransport byte tests; AKP03's 3 encoders, AKP153/815's encoderCount=0 inert path, and max-key-index burst all verified (DEVICES-10).**

## Performance

- **Duration:** ~95 min
- **Started:** 2026-05-24T (session resumed from context)
- **Completed:** 2026-05-24
- **Tasks:** 2
- **Files modified:** 5

## Accomplishments

- Replaced `static constexpr std::size_t kEncoderCount = 4` + `std::array<int32_t,4>` with runtime `m_encoderCount` + `std::vector<int32_t> m_encAccum` sized from `descriptor.encoderCount` at `setActiveDevice()` - AKP03 gets 3, AKP05 gets 4, AKP153/815 get 0 (inert path)
- Added `m_hasTouchStrip` guard: TouchStrip dispatch branch is inert for non-touch families even if a TouchStrip event were somehow emitted (defence-in-depth; T-24-05 mitigated)
- Fixed `repaintPage()` background fill from hardcoded `85x85` to `displayInfo().widthPx/heightPx` - single parameterization serving AKP03 (60x60), AKP05/AKP153 (85x85), AKP815 (100x100)
- Created 10-test `test_stream_dock_family.cpp` proving assign-image emits per-family wire markers and fed input fires the bound action chain across all three new families, including AKP03's 3 encoders

## Task Commits

Each task was committed atomically:

1. **Task 1: Make control + input services descriptor-driven (kill AKP05 geometry assumptions)** - `db71fff` (feat)
1. **Task 2: Add StreamDockFamily MockTransport byte test (DEVICES-10)** - `ea0ed84` (feat)

**Plan metadata:** (this SUMMARY commit - see final commit)

## Files Created/Modified

- `src/app/src/stream_dock_input_service.hpp` - Replaced `kEncoderCount=4` + `std::array` with `m_encoderCount` (runtime) + `std::vector<int32_t>` + `m_hasTouchStrip` member; `#include <array>` -> `#include <vector>`
- `src/app/src/stream_dock_input_service.cpp` - `onEncoderTurned`/`drainCoalescedRotation` use `m_encoderCount`; TouchStrip branch gated on `m_hasTouchStrip`; `m_encAccum.fill()` -> `std::fill(begin, end)`; `setActiveDevice()` populates all three fields from descriptor
- `src/app/src/stream_dock_control_service.cpp` - `repaintPage()` reads `disp->displayInfo()` for `w`/`h` instead of literal `85, 85`
- `tests/unit/test_stream_dock_input_service.cpp` - Added `d.encoderCount = 4; d.hasTouchStrip = true;` to AKP05E test descriptor (Rule 1 fix: descriptor-driven seam broke existing test)
- `tests/unit/test_stream_dock_family.cpp` - 10 test cases tagged `[stream-dock-family]` (CREATED, 710 lines)
- `tests/unit/CMakeLists.txt` - Registered `test_stream_dock_family.cpp` in the unit test target

## Decisions Made

**Audit result:** The services were NOT fully descriptor-driven at Phase 14-15 close. Four specific AKP05 assumptions were found and parameterised:

1. **`kEncoderCount = 4` (static)** -> `m_encoderCount` (from `descriptor.encoderCount` at `setActiveDevice()`) + `std::vector<int32_t>` for the accumulator. This is the most load-bearing change: AKP153/815 now get an empty accumulator and the coalescer loop is inert; AKP03 gets a 3-element accumulator and all 3 encoders route.

1. **`m_encAccum.fill(0)` on `std::array`** -> `std::fill(m_encAccum.begin(), m_encAccum.end(), 0)` (vector doesn't have `.fill()`).

1. **`85, 85` hardcode in `repaintPage()`** -> `displayInfo().widthPx/heightPx` with fallback to 85 if zero (defensive; real descriptors all return non-zero).

1. **Implicit touch-path always active** -> gated on `m_hasTouchStrip` from descriptor (AKP03/153/815 = false).

**`zoneForX` static function:** Kept AKP05-specific. The touch strip (640px wide, 4 zones) is hardware unique to AKP05E. Renamed the internal constant from `StreamDockInputService::kEncoderCount` to local `kAkp05EncoderCount = 4u` with documentation explaining it is AKP05-specific; the function's AKP05 scope is now explicit.

**Test discriminator pattern:** Used `settingsJson` field as unique string identifier (`"press0"`, `"cw0"`, `"ccw2"`, etc.) for the AKP03 encoder spy so the `keyPress` executor can distinguish which binding fired without a separate mock type.

## Per-family image header/terminator offsets pinned by the test

Sourced from reading `akp03_protocol.hpp`, `akp153_protocol.hpp`, `akp815_protocol.hpp` and the existing per-family protocol tests (`test_akp03_protocol.cpp`, `test_akp153_protocol.cpp`, `test_akp815_protocol.cpp`):

| Family | Header cmd               | Header bytes[5..7] | Terminator bytes[5..9] |
| ------ | ------------------------ | ------------------ | ---------------------- |
| AKP03  | `CmdImage`               | `'B','A','T'`      | `'U','L','E','N','D'`  |
| AKP153 | `CmdBat`                 | `'B','A','T'`      | `'U','L','E','N','D'`  |
| AKP815 | (AKP153 builders reused) | `'B','A','T'`      | `'U','L','E','N','D'`  |
| AKP05E | `CmdBat`                 | `'B','A','T'`      | `'U','L','E','N','D'`  |

All four families share the same BAT/ULEND wire signature at offsets [5..7] / [5..9]. The per-family distinction in the test is the `makeAkp*WithTransport` factory used and the key/image dimensions passed.

## AKP03 3-encoder coverage

The AKP03 input test feeds 7 events and asserts 9 spy fires (one release is synthesised per press):

| Input frame                | Spy expected                            |
| -------------------------- | --------------------------------------- |
| key press (frame[9]=0x01)  | `"keypress"`                            |
| enc0 press (0x33)          | `"press0"` + synthesised release signal |
| enc0 CW (0x91) + coalesce  | `"cw0"`                                 |
| enc0 CCW (0x90) + coalesce | `"ccw0"`                                |
| enc1 press (0x35)          | `"press1"` + synthesised release signal |
| enc2 press (0x34)          | `"press2"` + synthesised release signal |
| enc2 CW (0x61) + coalesce  | `"cw2"`                                 |

Encoder action bytes sourced from `akp03_protocol.hpp` (`Encoder0Press=0x33`, `Encoder1Press=0x35`, `Encoder2Press=0x34`, `Encoder0Cw=0x91`, `Encoder0Ccw=0x90`, etc.).

## encoderCount=0 inert-path handling for AKP153/815

With `descriptor.encoderCount = 0`, `setActiveDevice()` calls `m_encAccum.assign(0, 0)` (empty vector). The coalescer's `for (e = 0; e < m_encoderCount; ++e)` body never executes. No crash, no spurious dispatch. Asserted by the AKP153 and AKP815 input test cases: only the bound key-press chain fires; no encoder signal fires.

## Physical device status (honesty log for Phase 25)

| Family | Status                                                                                                                          |
| ------ | ------------------------------------------------------------------------------------------------------------------------------- |
| AKP05E | Live hardware confirmed (Phase 14-15 UAT + Phase 25 scope)                                                                      |
| AKP03  | Physically present per user context (24-CONTEXT decision 4); MockTransport-only for this plan - Phase 25 owns live confirmation |
| AKP153 | NOT physically present; MockTransport-only (24-CONTEXT decision 4 / honesty)                                                    |
| AKP815 | NOT physically present; MockTransport-only (24-CONTEXT decision 4 / honesty)                                                    |

Phase 25 (hardware verification plan) must live-test whichever family members are connected at that point. AKP153 and AKP815 remain MockTransport-proven only.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Fixed AKP05E test descriptor missing encoderCount/hasTouchStrip**

- **Found during:** Task 1 (after applying descriptor-driven change, existing INPUT-04a / INPUT-04c tests failed)
- **Issue:** `makeDescriptor()` in `test_stream_dock_input_service.cpp` left `encoderCount` at default (0) and `hasTouchStrip` at false. After the descriptor-driven seam was wired, `m_encoderCount=0` and `m_encAccum` was empty; encoder events were silently dropped, breaking the CW/coalesce tests.
- **Fix:** Added `d.encoderCount = 4; d.hasTouchStrip = true;` to the AKP05E test descriptor builder.
- **Files modified:** `tests/unit/test_stream_dock_input_service.cpp`
- **Verification:** `ctest --preset linux-release -R StreamDockInput` passed 100%.
- **Committed in:** `db71fff` (part of Task 1 commit)

**2. [Rule 1 - Bug] Removed unused `makeVerResponse` helper from family test**

- **Found during:** Task 2 compilation
- **Issue:** `-Werror=unused-function` on `makeVerResponse` (AKP03/153/815 backends don't require a VER handshake for the test).
- **Fix:** Removed the function.
- **Files modified:** `tests/unit/test_stream_dock_family.cpp`
- **Committed in:** `ea0ed84` (part of Task 2 commit)

**3. [Rule 3 - Blocking] Fixed `std::array::fill` not available on `std::vector`**

- **Found during:** Task 1 compilation
- **Issue:** Changing `m_encAccum` from `std::array` to `std::vector` left two `m_encAccum.fill(0)` calls that don't compile (vector has no `.fill()`).
- **Fix:** Changed both to `std::fill(m_encAccum.begin(), m_encAccum.end(), 0)`.
- **Files modified:** `src/app/src/stream_dock_input_service.cpp`
- **Committed in:** `db71fff` (part of Task 1 commit)

**4. [Rule 3 - Blocking] Fixed `.settings` field not existing on `Action` struct**

- **Found during:** Task 2 compilation
- **Issue:** Brace-initializer used `.settings = "press0"` but the actual field is `settingsJson`.
- **Fix:** Changed to `.settingsJson = "press0"` throughout the family test.
- **Files modified:** `tests/unit/test_stream_dock_family.cpp`
- **Committed in:** `ea0ed84` (part of Task 2 commit)

______________________________________________________________________

**Total deviations:** 4 auto-fixed (2 x Rule 1 - Bug, 2 x Rule 3 - Blocking)
**Impact on plan:** All auto-fixes necessary for correctness and compilation. No scope creep.

## Issues Encountered

- Pre-commit clang-format reformatted `test_stream_dock_family.cpp` and aborted the Task 2 commit; re-staged and committed in the next attempt (normal per CLAUDE.md, hook not skipped).
- `settingsJson` vs `.settings` field name: resolved by reading `profile.hpp` directly.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- Phase 25 (hardware verification) can proceed. The two app services are now fully descriptor-generic.
- Phase 25 must confirm AKP03 live (user has the device); AKP153/AKP815 remain MockTransport-only until physical units are available.
- No API changes: Phase 19 / Phase 21-03 seams (`engine()`, `activeDeviceCodename()`, `deviceEvent` signal) unaffected.
- The `zoneForX` AKP05-specific touch formula is documented but unchanged; Phase 25 hardware reconciliation may refine the zone count if `akp05.md §5` provisional values are corrected.

______________________________________________________________________

*Phase: 24-family-coverage-akp03-153-815*
*Completed: 2026-05-24*
