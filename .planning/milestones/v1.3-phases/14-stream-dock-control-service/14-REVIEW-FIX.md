---
phase: 14-stream-dock-control-service
fixed_at: 2026-05-24T11:21:00Z
review_path: .planning/phases/14-stream-dock-control-service/14-REVIEW.md
iteration: 1
findings_in_scope: 7
fixed: 7
skipped: 0
status: all_fixed
---

# Phase 14: Code Review Fix Report

**Fixed at:** 2026-05-24T11:21:00Z
**Source review:** `.planning/phases/14-stream-dock-control-service/14-REVIEW.md`
**Iteration:** 1

**Summary:**

- Findings in scope: 7 (2 Critical + 5 Warning)
- Fixed: 7
- Skipped: 0
- Info findings also fixed: 2 (IN-01, IN-02 — trivial one-line comment fixes)

**Build result:** Clean — 414/414 tests passing, 0 failures, 0 build warnings.

## Fixed Issues

### CR-01: Unhandled exception from open()/setBrightness() in setActiveDevice()

**Files modified:** `src/app/src/stream_dock_control_service.cpp`
**Commit:** `9035ab7`
**Applied fix:** Wrapped `m_activeDevice->open()` in `try/catch(std::exception)` — on
failure, resets both `m_activeDevice` and `m_activeCodename` and returns early. Wrapped
`disp->setBrightness()` in a second `try/catch` (non-fatal — device is open; panel may
just be dark). Added `<stdexcept>` include. Both calls now safely absorb device yanks
between lookup and open on the GUI thread.

### CR-02: Unhandled exception from setKeyImage() in drainPendingWrites()

**Files modified:** `src/app/src/stream_dock_control_service.cpp`
**Commit:** `def5388`
**Applied fix:** Wrapped `disp->setKeyImage()` in `try/catch(std::exception)` inside the
for-loop. On exception: logs WARN, resets `m_activeDevice` + `m_activeCodename` (releases
the held handle for clean hot-plug rebind), then `break`s out of the loop. Moved
`m_pendingWrites.clear()` to after the loop (unconditional) so the queue is always drained
even after a partial failure — eliminating the permanently-stuck-queue scenario.

### WR-01: IDisplayCapable::setKeyImage doxygen says "Zero-based" but backend enforces 1-based

**Files modified:** `src/core/include/ajazz/core/capabilities.hpp`
**Commit:** `0a8ec3d`
**Applied fix:** Updated all three `@param keyIndex` docs in `IDisplayCapable`:
`setKeyImage` now says "1-based key index (1 .. keyCount)" with a note about
`keyIndexInRange()` enforcing this; `setKeyColor` updated to match; `clearKey` updated
with the 0xFF all-keys sentinel note preserved. Other "Zero-based" usages in the file
(encoders, DPI stages, buttons) are genuinely zero-based and were left unchanged.

### WR-02: setProfileAccessor() doxygen claims "no-op if already set" but code unconditionally replaces

**Files modified:** `src/app/src/stream_dock_control_service.hpp`
**Commit:** `c4fce3f`
**Applied fix:** Chose option (b) — removed the false "no-op" claim from the doxygen and
replaced it with accurate documentation of the actual unconditional-replace behavior, with
a note that Phase 16 may intentionally replace the accessor with a richer one.

### WR-03: repaintFromProfile() silently drops keys when profileKeyIndex + 1 overflows uint8_t

**Files modified:** `src/app/src/stream_dock_control_service.cpp`
**Commit:** `dc29d00`
**Applied fix:** Added a guard before the cast: `if (profileKeyIndex >= std::numeric_limits<uint8_t>::max())` — logs WARN with the out-of-range index and `continue`s. Added
`<limits>` include for `std::numeric_limits`.

### WR-04: Two constants alias the same VID value (0x6603) in register.cpp

**Files modified:** `src/devices/streamdeck/src/register.cpp`
**Commit:** `8936f1d`
**Applied fix:** Collapsed `MiraboxN3VendorNew = 0x6603` and `MiraboxN4Vendor = 0x6603`
into a single `MiraboxVendorV2 = 0x6603` with a comment explaining both N3 (AKP03
protocol) and N4 (AKP05 protocol) families share this VID and PID distinguishes the
family. Updated all three usage sites to use `MiraboxVendorV2`.

### WR-05: m_activeCodename not cleared when setActiveDevice() lookup fails

**Files modified:** `src/app/src/stream_dock_control_service.cpp`
**Commit:** `3a29a77`
**Applied fix:** Added `m_activeCodename.clear()` in the `!m_activeDevice` early-return
path, so both fields are cleared atomically when the lookup fails. This ensures
`m_activeCodename.isEmpty()` correctly reflects "no active device" rather than retaining
a stale codename with a null device pointer.

## Also Fixed (Info)

### IN-01/IN-02: Stale comments in register.cpp

**Files modified:** `src/devices/streamdeck/src/register.cpp`
**Commit:** `13de405`
**Applied fix (IN-02):** Replaced the stale AKP815 section comment ("the backend reuses
the AKP153 implementation today ... will graduate into a dedicated makeAkp815 factory in
a future change") with accurate text — the future change has already landed.
**Applied fix (IN-01):** Updated the AKP815 `hasClock = true` inline comment from the
sweeping "every Stream Dock advertises Capability::Clock" to a qualified statement
acknowledging that AKP05E is `hasClock=false` per DEVICES-11/ARCH-05.

## Skipped Issues

None.

______________________________________________________________________

_Fixed: 2026-05-24T11:21:00Z_
_Fixer: Claude (gsd-code-fixer)_
_Iteration: 1_
