---
phase: 28-akp05-plugin-action-completeness-drag-to-bind-on-keys-dials
plan: '03'
subsystem: plugin-catalog
tags: [plugin, drag-drop, affordance, profile, persistence, tdd]
dependency_graph:
  requires:
    - 28-02 (affordanceMask in MIME payload; controllers field in installedActions QVariantMap)
  provides:
    - 6-arg commitEncoderBinding call in EncoderDial.qml (ap.actionId passed, PLUGIN-19)
    - 6-arg commitTouchZoneBinding call in TouchStripLane.qml (ap.actionId passed, PLUGIN-19)
    - Strict affordance gating on EncoderDial (mask & 2), TouchStripLane (mask & 4), KeyCell (mask & 1) for x-ajazz-action MIME (PLUGIN-20)
    - Two C++ Catch2 round-trip tests proving encoder + touch-zone actionId survives save/load
    - QML drag-drop test extended with 4 new best-effort cases (actionId pass-through + affordance logic)
  affects:
    - src/app/qml/components/EncoderDial.qml
    - src/app/qml/components/TouchStripLane.qml
    - src/app/qml/components/KeyCell.qml
    - tests/unit/test_profile_persistence.cpp
    - tests/qml/test_device_view_drag_drop.qml
tech_stack:
  added: []
  patterns:
    - affordanceMask bitfield gate in QML DropArea onEntered (mask & N, fail-safe on zero)
    - 6-arg commit call passing ap.actionId || "" + ap.defaultSettings || "" at drop time
    - C++ ProfileController round-trip test as authoritative gate for QML-layer regressions
key_files:
  created: []
  modified:
    - src/app/qml/components/EncoderDial.qml
    - src/app/qml/components/TouchStripLane.qml
    - src/app/qml/components/KeyCell.qml
    - tests/unit/test_profile_persistence.cpp
    - tests/qml/test_device_view_drag_drop.qml
decisions:
  - defaultSettings seeded at drop time (passed as settingsJson to commit*Binding) per RESEARCH Q3 -- if action has no Settings object, empty string preserves current behavior
  - QML tests are best-effort (pre-existing tests/qml link issue per CLAUDE.md); C++ round-trip tests are authoritative PLUGIN-19 gate
  - affordanceMask=0 (Information-only) rejected by all three targets -- fail-safe T-28-07 mitigation; non-draggable items stay visible in library
metrics:
  duration_minutes: 15
  completed_date: '2026-05-31T22:30:00Z'
  tasks_completed: 2
  files_changed: 5
requirements_closed: [PLUGIN-19, PLUGIN-20]
---

# Phase 28 Plan 03: 6-arg Drop Fix + Strict Affordance Gating Summary

**Fixed silent actionId drop on encoder/touch-zone drops (5-arg -> 6-arg), added strict affordance gating to all three drop targets (Key/Dial/TouchZone bits), and proved round-trip persistence via two C++ Catch2 tests.**

## Performance

- **Duration:** 15 min
- **Started:** 2026-05-31T22:10:00Z
- **Completed:** 2026-05-31T22:30:00Z
- **Tasks:** 2
- **Files modified:** 5

## Accomplishments

- EncoderDial.qml and TouchStripLane.qml now pass actionId as the 6th argument to commit\*Binding (was silently dropped -- un-routable bindings after drop)
- All three drop targets (KeyCell, EncoderDial, TouchStripLane) STRICT-reject mismatched-affordance library drags with dragRejected=true + errorAccent border; Information-only (mask=0) rejected by all targets (fail-safe)
- C++ persistence tests prove encoder + touch-zone plugin bindings survive save/load with non-empty actionId (PLUGIN-19 authoritative gate)
- QML drag-drop test extended with 4 additional cases: 6-arg pass-through + affordance gating logic verification

## Task Commits

1. **Task 1: Fix 6-arg drops + strict affordance gating** - `2be3561` (fix)
1. **Task 2: C++ persistence round-trip + QML test extension** - `abe2c42` (test)

## Files Created/Modified

- `src/app/qml/components/EncoderDial.qml` - Fixed 5-arg commitEncoderBinding -> 6-arg (ap.actionId, ap.defaultSettings); added x-ajazz-action affordance gate in onEntered (mask & 2)
- `src/app/qml/components/TouchStripLane.qml` - Fixed 5-arg commitTouchZoneBinding -> 6-arg; added x-ajazz-action affordance gate (mask & 4)
- `src/app/qml/components/KeyCell.qml` - Added x-ajazz-action affordance gate in onEntered (mask & 1); existing x-ajazz-binding gate unchanged
- `tests/unit/test_profile_persistence.cpp` - Two new Catch2 TEST_CASEs: "ProfilePersistence encoder binding persists plugin actionId" and "ProfilePersistence touch zone binding persists plugin actionId"
- `tests/qml/test_device_view_drag_drop.qml` - Extended with 4 new tests: actionId pass-through (encoder + zone), affordance gating logic rejection cases

## Decisions Made

- Seeding defaultSettings at drop time: passed as settingsJson argument to commit\*Binding when available in the drag payload; matches Elgato SDK behavior; empty string when absent (no behavioral change for current plugins)
- C++ round-trip as authoritative gate: the QML test target has a pre-existing link issue (CLAUDE.md); the two Catch2 tests in test_profile_persistence.cpp are the definitive regression guards for PLUGIN-19
- Fail-safe affordance policy: affordanceMask=0 (Information-only) is rejected by all three targets, never silently bound (T-28-07 mitigate)

## Deviations from Plan

None - plan executed exactly as written.

## COD-031 Verification

`grep -rn '#include.*nlohmann' src/core/include/` returns 0 actual includes. Three comment-only references exist; COD-031 boundary clean. All changes are QML + Catch2 C++ test only.

## Test Results

```
ctest --preset linux-release -E qml: 732/732 passed, 0 failed
(baseline was 730; +2 new C++ profile persistence tests)
```

## Known Stubs

None -- all actionId fields are passed from real drop payload data.

## Threat Flags

| Flag              | File                                             | Description                                                                                    |
| ----------------- | ------------------------------------------------ | ---------------------------------------------------------------------------------------------- |
| T-28-07 mitigated | EncoderDial.qml, TouchStripLane.qml, KeyCell.qml | affordanceMask gate in onEntered: unknown/zero mask rejects on all three targets (fail-safe)   |
| T-28-08 pattern   | EncoderDial.qml, TouchStripLane.qml, KeyCell.qml | JSON parse wrapped in try/catch; parse failure sets dragRejected=true and rejects, never binds |

## Self-Check: PASSED

| Item                                                        | Result |
| ----------------------------------------------------------- | ------ |
| EncoderDial.qml exists with ap.actionId                     | FOUND  |
| TouchStripLane.qml exists with ap.actionId                  | FOUND  |
| KeyCell.qml exists with affordanceMask                      | FOUND  |
| EncoderDial.qml has errorAccent                             | FOUND  |
| TouchStripLane.qml has errorAccent                          | FOUND  |
| test_profile_persistence.cpp has "persists plugin actionId" | FOUND  |
| commit 2be3561 (Task 1) exists                              | FOUND  |
| commit abe2c42 (Task 2) exists                              | FOUND  |
| 732/732 ctest -E qml pass                                   | PASS   |
| COD-031 clean                                               | PASS   |
