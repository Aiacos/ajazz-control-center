---
phase: 05-time-sync-scaffolding
plan: 05
subsystem: app
tags: [device-model, qml-role, view-model, hasClock]

requires:
  - phase: 05-time-sync-scaffolding
    provides: DeviceDescriptor.hasClock flag (Plan 05-01)
provides:
  - DeviceModel.HasClockRole (Qt::UserRole + 12, append-only)
  - QML role "deviceHasClock" exposed via roleNames()
  - toMap() helper surfaces hasClock for completeness
affects: [05-06]

tech-stack:
  added: []
  patterns:
    - Append-only role enum addition (mirror of Pitfall 13 spirit for model role enums)

key-files:
  created: []
  modified:
    - src/app/src/device_model.hpp
    - src/app/src/device_model.cpp

key-decisions:
  - HasClockRole = Qt::UserRole + 12 (sequential after HasTouchStripRole = Qt::UserRole + 11). No existing role re-numbered.
  - toMap() also surfaces hasClock for parity with the existing hasRgb / hasTouchStrip surface — keeps QML capabilitiesFor(codename) callers consistent.

patterns-established: []

requirements-completed: [TIMESYNC-01]

duration: ~5min
completed: 2026-05-14
---

# Phase 5 Plan 05: DeviceModel HasClockRole

**DeviceModel exposes `deviceHasClock` role to QML so the Sync button + glyph (Plan 05-06) can gate on `model.deviceHasClock` without runtime cast.**

## Performance

- Duration: ~5 min
- Tasks: 1
- Files modified: 2

## Accomplishments

- `HasClockRole` appended to `DeviceModel::Roles` enum at `Qt::UserRole + 12` (sequential after `HasTouchStripRole`). Append-only — no existing role renumbered.
- `data(index, HasClockRole)` returns `d.hasClock` (bool).
- `roleNames()` adds `{HasClockRole, "deviceHasClock"}` entry. QML binds via `model.deviceHasClock`.
- `toMap()` (capabilitiesFor's helper) surfaces `hasClock` for parity with the existing `hasRgb` / `hasTouchStrip` entries.

## Task Commits

1. **Task 1 (Steps 5.1-5.4 atomic):** `23329f7` (feat(app))

## Files Created/Modified

- `src/app/src/device_model.hpp` — `HasClockRole` added to `Roles` enum.
- `src/app/src/device_model.cpp` — `data()` switch + `roleNames()` hash + `toMap()` helper, three sites updated in lockstep.

## Decisions Made

None — plan executed exactly as written. Append-only role addition; binary-compat preserved.

## Deviations from Plan

None.

## Issues Encountered

None.

## Next Phase Readiness

- **Plan 05-06 (QML UI)** can bind:
  - `visible: model.deviceHasClock` on the per-row "Sync time" ToolButton.
  - Glyph state binding to the (Plan 05-04) TimeSyncService syncSucceeded / syncFailed signals.

Build: 77/77 targets, ctest 175/175 pass — no regression.

______________________________________________________________________

*Phase: 05-time-sync-scaffolding*
*Completed: 2026-05-14*
