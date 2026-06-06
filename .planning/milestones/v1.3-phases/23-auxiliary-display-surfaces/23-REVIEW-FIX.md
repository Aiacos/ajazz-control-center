---
phase: 23-auxiliary-display-surfaces
fixed_at: 2026-05-27T00:15:00Z
review_path: .planning/phases/23-auxiliary-display-surfaces/23-REVIEW.md
iteration: 1
findings_in_scope: 4
fixed: 4
skipped: 0
status: all_fixed
---

# Phase 23: Code Review Fix Report

**Fixed at:** 2026-05-27T00:15:00Z
**Source review:** .planning/phases/23-auxiliary-display-surfaces/23-REVIEW.md
**Iteration:** 1

**Summary:**

- Findings in scope: 4 (WR-01, WR-02, IN-01, IN-02)
- Fixed: 4
- Skipped: 0

## Fixed Issues

### WR-02: Remove redundant ITouchStripDisplayCapable pre-check in repaintEncodersFromProfile

**Files modified:** `src/app/src/stream_dock_control_service.cpp`
**Commit:** ebfb91e
**Applied fix:** Removed the upfront `auto* strip = dynamic_cast<ITouchStripDisplayCapable*>(...)` capability check and its null guard from `repaintEncodersFromProfile`. The result was stored, null-checked, then discarded because `assignTouchStripZone` independently re-performs the same cast. The unused variable risks MSVC /W4 C4189 (hard error under /WX on Windows CI). Now relies solely on `assignTouchStripZone`'s own guard, matching `repaintFromProfile`'s pattern of delegating capability guards to the assign methods. The `if (!m_activeDevice) return;` early-out is kept.

______________________________________________________________________

### WR-01: Add m_activeDevice null guard to assignMainImage, assignEncoderImage, assignTouchStripZone

**Files modified:** `src/app/src/stream_dock_control_service.cpp`
**Commit:** 2c7705b
**Applied fix:** Added `if (!m_activeDevice) { return; }` as the first statement of all three assign methods (`assignMainImage`, `assignEncoderImage`, `assignTouchStripZone`). This matches the defensive-depth style of `repaintPage` and `repaintEncodersFromProfile`. The fix is a parity/consistency fix: `dynamic_cast(nullptr)` is defined behavior (returns `nullptr`), so the pre-existing code was not a crash, but without the guard callers could not distinguish "no device" from "device lacks the capability."

______________________________________________________________________

### IN-01: Log pending write count on device-yank in drainPendingWrites

**Files modified:** `src/app/src/stream_dock_control_service.cpp`
**Commit:** 9ce818c
**Applied fix:** Added `AJAZZ_LOG_WARN` before the `break` in the exception catch block of `drainPendingWrites`. When a device-yank exception fires mid-drain, the log records how many pending writes are being discarded (via `m_pendingWrites.size()`). The existing `m_activeDevice.reset()`, `m_activeCodename.clear()`, and `break` are unchanged, as is the final `m_pendingWrites.clear()`.

______________________________________________________________________

### IN-02: Document slot ordering invariant on profileChanged -> repaintEncodersFromProfile

**Files modified:** `src/app/src/application.cpp`
**Commit:** f3e867f
**Applied fix:** Added a comment block before the second `QObject::connect` call in `application.cpp` (the one connecting `ProfileController::profileChanged` to `StreamDockControlService::repaintEncodersFromProfile`). The comment documents: (1) this connection is registered after `repaintFromProfile`'s connection, (2) Qt delivers direct-connection slots in registration order on the same thread, (3) `repaintFromProfile` resets `m_carouselIndex = 0` and `repaintEncodersFromProfile` must not touch it, and (4) if either method is later changed to read/write `m_carouselIndex`, the ordering becomes load-bearing and should be made explicit.

______________________________________________________________________

## Build and Test Gate

**Build result:** `cmake --build build/linux-release` completed with no errors, no FAILED targets. All three targets (`ajazz-control-center`, `ajazz_unit_tests`, `ajazz_qml_tests`) linked cleanly after the fixes.

**Test result:** `ctest --preset linux-release` -- **640/640 tests passed, 0 failed** (104 s total).

______________________________________________________________________

_Fixed: 2026-05-27T00:15:00Z_
_Fixer: Claude (gsd-code-fixer)_
_Iteration: 1_
