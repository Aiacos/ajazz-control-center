---
phase: 16-device-controls-binding-persistence-pages
fixed_at: 2026-05-24T13:00:00Z
review_path: .planning/phases/16-device-controls-binding-persistence-pages/16-REVIEW.md
iteration: 1
findings_in_scope: 9
fixed: 9
skipped: 0
status: all_fixed
---

# Phase 16: Code Review Fix Report

**Fixed at:** 2026-05-24T13:00:00Z
**Source review:** .planning/phases/16-device-controls-binding-persistence-pages/16-REVIEW.md
**Iteration:** 1

**Summary:**

- Findings in scope: 9
- Fixed: 9
- Skipped: 0

## Fixed Issues

### CR-01: StreamDockControlService::create() returns nullptr silently

**Files modified:** `src/app/src/stream_dock_control_service.cpp`
**Commit:** e59243b
**Applied fix:** Replaced the `if (g_instance != nullptr)` conditional guard (which still returned null on failure) with `Q_ASSERT_X` matching the established ProfileController::create() pattern. The ownership call is now unconditional after the assert.

______________________________________________________________________

### CR-02: commitKeyBinding unchecked int -> uint16_t and int -> ActionKind casts

**Files modified:** `src/app/src/profile_controller.cpp`
**Commit:** 02b481e
**Applied fix:** Added input validation at the top of `commitKeyBinding` before any cast. Negative or >65534 `keyIndex` and out-of-range `actionKind` (> `ActionKind::BackToParent` = 6) are rejected with `AJAZZ_LOG_WARN` and an early return. Also added `#include "ajazz/core/logger.hpp"` and `#include <limits>` which were needed for the guards. The `constexpr int kMaxActionKind` is computed at compile time from the enum value so it stays in sync automatically.

______________________________________________________________________

### WR-01: m_carouselIndex not reset when new profile loaded

**Files modified:** `src/app/src/stream_dock_control_service.cpp`
**Commit:** 364b625
**Applied fix:** Added `m_carouselIndex = 0;` at the top of `repaintFromProfile()` before the `repaintPage("root")` call, satisfying the documented contract ("reset when a new profile is loaded") that was previously unimplemented.

______________________________________________________________________

### WR-02: resetActiveProfile() preserves pages while clearing bindings

**Files modified:** `src/app/src/profile_controller.cpp`
**Commit:** 7dc3ff1
**Applied fix:** Applied option (a): iterate `m_profile.pages` and call `page.keys.clear()` for each entry. Page structure (folder navigation entries, page ids) is preserved; only the key bindings within each page are cleared. This matches "Restore defaults" semantics while keeping the machine-generated folder structure intact.

______________________________________________________________________

### WR-03: saveActiveProfile() ignores QDir::mkpath() return value

**Files modified:** `src/app/src/profile_controller.cpp`
**Commit:** 251e2f9
**Applied fix:** Changed `!dir.exists()` block to `!dir.exists() && !dir.mkpath(...)`. On mkpath failure, emits `saveFailed(tr("Cannot create profiles directory: %1").arg(dir.absolutePath()))` and returns early, providing an accurate diagnostic message before the write is even attempted.

______________________________________________________________________

### WR-04: saveProfile() always emits profilesChanged

**Files modified:** `src/app/src/profile_controller.cpp`
**Commit:** fb17120
**Applied fix:** Removed `emit profilesChanged()` from `saveProfile()` (low-level write, does not alter the profile library list). In `saveActiveProfile()`, capture `isNewId = (m_path != path)` before the call, then emit `profilesChanged()` only when `isNewId && m_path == path` (save succeeded and the path is new to the library). In-place re-saves no longer emit profilesChanged.

______________________________________________________________________

### WR-05: No test for setBrightness lower-bound clamp (-50 -> 0)

**Files modified:** `tests/unit/test_stream_dock_controls.cpp`
**Commit:** 0120022
**Applied fix:** Added `TEST_CASE("StreamDockControlService: setBrightness clamps negative value to 0", ...)` mirroring the existing upper-bound test. Calls `setBrightness("akp05e", -50)`, drains the queue, finds the LIG packet, and asserts `ligPkt[10] == 0`. New test count: 445 (was 444).

______________________________________________________________________

### IN-01: commitKeyBinding @param keyIndex doc ambiguous "0-based or 1-based"

**Files modified:** `src/app/src/profile_controller.hpp`
**Commit:** 9050340
**Applied fix:** Replaced the ambiguous "0-based or 1-based" doc text with a clear statement that the function expects 0-based indices, explains that the paint service adds 1 for the device's 1-based scheme, warns that passing 1-based will silently paint the wrong key, and documents the [0, 65534] valid range enforced by CR-02.

______________________________________________________________________

### IN-02: sanitizeProfileId does not cap path length

**Files modified:** `src/app/src/profile_controller.cpp`
**Commit:** 5351926
**Applied fix:** Added `if (result.size() > 200) { result.truncate(200); }` after the leading-dot strip and before the empty-check return, comfortably under all platform filename limits (Linux 255 bytes, macOS 255 UTF-8 chars, Windows 260 total path chars).

______________________________________________________________________

## Build and Test Results

- Build: clean (no warnings, no errors) after all 9 fixes
- Test suite: `ctest --preset linux-release` -- **445/445 passed** (0 failed)
- New test added by WR-05: 1 (brings total from 444 to 445)
- No regressions detected

______________________________________________________________________

_Fixed: 2026-05-24T13:00:00Z_
_Fixer: Claude (gsd-code-fixer)_
_Iteration: 1_
