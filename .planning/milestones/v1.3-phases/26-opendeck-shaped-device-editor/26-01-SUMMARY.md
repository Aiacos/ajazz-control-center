---
phase: 26-opendeck-shaped-device-editor
plan: 01
subsystem: ui
tags: [qml, qt6, stream-dock, gap-25a, req-26-a]

requires:
  - phase: 25-hardware-verification-real-plugin
    provides: GAP-25A identified — setActiveDevice never called from QML sidebar

provides:
  - StreamDockControlService.setActiveDevice(codename) wired into Main.qml onDeviceSelected (REQ-26-A)
  - Q_INVOKABLE marker on setActiveDevice in stream_dock_control_service.hpp enabling QML dispatch

affects:
  - 26-07-PLAN (REQ-26-E live walk — will retest Phase 25 Test 1 against AKP05E now that GAP-25A is closed)
  - 25-UAT (Test 1 FAIL was caused by this gap; retest deferred to Plan 26-07)

tech-stack:
  added: []
  patterns:
    - 'Q_INVOKABLE on QML-singleton methods: required for QML to dispatch any method; bare declaration without the marker is silently callable only from C++'

key-files:
  created: []
  modified:
    - src/app/qml/Main.qml
    - src/app/src/stream_dock_control_service.hpp

key-decisions:
  - "Auto-fixed missing Q_INVOKABLE on setActiveDevice (Rule 2): the plan's interface spec listed it as Q_INVOKABLE but the actual header lacked the marker, making the QML call silently a no-op dispatch. Added Q_INVOKABLE alongside the Main.qml wire-up in the same commit."

patterns-established:
  - 'Rule 2 auto-fix for missing Q_INVOKABLE: when a QML-singleton method intended for QML dispatch lacks Q_INVOKABLE, add it in the same commit as the QML call site — keeps the change atomic and self-consistent.'

requirements-completed:
  - REQ-26-A

duration: 35min
completed: 2026-05-28
---

# Phase 26 Plan 01: Wire setActiveDevice on sidebar selection (REQ-26-A) Summary

**`StreamDockControlService.setActiveDevice(codename)` wired into `Main.qml:onDeviceSelected` + `Q_INVOKABLE` marker added to the header, closing GAP-25A so sidebar selection now opens the device and enables wire writes**

## Performance

- **Duration:** ~35 min (includes CMake configure from worktree + full build + 642-test run)
- **Started:** 2026-05-28T15:20:00Z
- **Completed:** 2026-05-28T15:55:00Z
- **Tasks:** 1 of 1
- **Files modified:** 2

## Accomplishments

- GAP-25A closed: `Main.qml:onDeviceSelected` now calls `StreamDockControlService.setActiveDevice(codename)` before the `editor.codename` / `editor.capabilities` assignments. Previously `m_activeDevice` stayed null and `repaintPage()` early-returned on every binding edit.
- `Q_INVOKABLE` marker added to `setActiveDevice(QString const&)` in `stream_dock_control_service.hpp` — the plan's interface spec listed it as Q_INVOKABLE but the actual header lacked the marker (Rule 2 auto-fix).
- Build passes (`ajazz-control-center` target, 0 errors, 0 warnings). All 642 non-QML tests pass (100%, 0 failed). Pre-existing QML link-target issue (PluginDeviceBridge undefined refs) unchanged — excluded with `-E qml` per CLAUDE.md.

## Task Commits

1. **Task 1: Wire setActiveDevice on sidebar selection (REQ-26-A)** - `8935690` (fix)

## Files Created/Modified

- `src/app/qml/Main.qml` — Added `StreamDockControlService.setActiveDevice(codename);` as first statement in `onDeviceSelected` handler (line 128), with inline comment citing REQ-26-A / GAP-25A
- `src/app/src/stream_dock_control_service.hpp` — Added `Q_INVOKABLE` keyword to `setActiveDevice(QString const&)` declaration at line 153

## Decisions Made

- Added `Q_INVOKABLE` to `setActiveDevice` as a Rule 2 auto-fix (missing critical functionality). The plan's `<interfaces>` section documented the method as `Q_INVOKABLE void setActiveDevice(...)` but the header had `void setActiveDevice(...)` without the marker. Without `Q_INVOKABLE`, Qt's meta-object system does not register the method for QML dispatch and the call from QML would be a runtime no-op or error. Fixed in the same task commit to keep the change atomic.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 2 - Missing Critical] Added Q_INVOKABLE to setActiveDevice**

- **Found during:** Task 1 (Wire setActiveDevice on sidebar selection)
- **Issue:** `stream_dock_control_service.hpp` line 153 declared `void setActiveDevice(QString const& codename)` without `Q_INVOKABLE`. The plan's `<interfaces>` spec listed it as `Q_INVOKABLE`, and `setBrightness` / `clearAll` (the only other QML-callable methods) both have `Q_INVOKABLE`. Without it, the QML call in `Main.qml` could not dispatch through the meta-object system.
- **Fix:** Added `Q_INVOKABLE` keyword in front of the declaration at line 153.
- **Files modified:** `src/app/src/stream_dock_control_service.hpp`
- **Verification:** Build succeeded, `grep Q_INVOKABLE.*setActiveDevice` returns the line.
- **Committed in:** `8935690` (same task commit)

______________________________________________________________________

**Total deviations:** 1 auto-fixed (Rule 2 — missing critical functionality)
**Impact on plan:** The fix is a prerequisite for the plan's goal. Without it the QML wire-up would appear to land but silently fail at runtime. No scope creep; the plan's own interface spec mandated Q_INVOKABLE.

## Issues Encountered

- Build directory not present in worktree (expected — fresh worktree). Ran `cmake --preset linux-release` to configure, then `cmake --build`. CMake configure took ~28s; full build ~5 min. Not a problem, just the normal worktree cold-start.
- Pre-existing `ajazz_qml_tests` link failure (PluginDeviceBridge undefined refs) confirmed unchanged by running `ctest -E qml`. Documented in CLAUDE.md latent items; excluded from test run.

## User Setup Required

None — no external service configuration required. Live verification (plug in AKP05E, select in sidebar, check journalctl for `[akp05] device opened`, confirm wire writes on next binding edit) is deferred to Plan 26-07 (REQ-26-E re-walk).

## Next Phase Readiness

- REQ-26-A is met. The QML→C++→device path is now wired end-to-end from sidebar selection.
- Plans 26-02 through 26-06 (Wave 2-5) can proceed independently: DeviceDescriptor geometry extension, Profile schema bump, DeviceView.qml, layout JSONs + photo assets.
- Plan 26-07 (Wave 6 REQ-26-E live walk) can retest Phase 25 UAT Test 1 against the live AKP05E once the photo/layout assets land in Wave 5.

## Self-Check: PASSED

- `src/app/qml/Main.qml` exists and contains `StreamDockControlService.setActiveDevice(codename)` at line 128: FOUND
- `src/app/src/stream_dock_control_service.hpp` contains `Q_INVOKABLE void setActiveDevice` at line 153: FOUND
- Commit `8935690` exists in git log: FOUND
- 642/642 tests passed (ctest --preset linux-release -E qml): CONFIRMED

______________________________________________________________________

*Phase: 26-opendeck-shaped-device-editor*
*Completed: 2026-05-28*
