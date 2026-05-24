---
phase: 16-device-controls-binding-persistence-pages
plan: 01
subsystem: app-layer device controls / QML live panel control
tags: [stream-dock, display, qml-singleton, hid, dynamic-cast, tdd, brightness, clear]

# Dependency graph
requires:
  - phase: 14-stream-dock-control-service/14-02
    provides: StreamDockControlService (held handle, setBrightness, clearKey, DeviceLookup pattern)
  - phase: earlier phases
    provides: |
      Akp05Device backend (IDisplayCapable, LIG/CLE wire layer),
      Application::exposeToQml singleton registration machinery,
      ProfileEditor.qml Keys tab + capability-gating
provides:
  - StreamDockControlService: Q_INVOKABLE setBrightness/clearAll (DISPLAY-09)
  - StreamDockControlService: QML_SINGLETON exposure (create/registerInstance/static_assert)
  - ProfileEditor.qml Keys tab: debounced brightness Slider + Clear-all SecondaryButton
  - QML element 'StreamDockControlService' accessible in AjazzControlCenter import scope
affects:
  - Phase 16-02 (binding persistence): reuses same singleton surface
  - Phase 19 (plugin bridge): brightness/clear also callable by plugin-driven repaint
  - Phase 25 (hardware smoke): live witness of DISPLAY-09 LIG/CLE on real AKP05E

# Tech tracking
tech-stack:
  added: []
  patterns:
    - Q_INVOKABLE brightness/clear pair: resolve held-or-lookup device, dynamic_cast<IDisplayCapable*> + null-check within 3 lines, std::clamp, try/catch for device yank (CR-01 / Pitfall 1 pattern)
    - QML_SINGLETON machinery (create/registerInstance/static_assert): mirrors LightingService exactly (CLAUDE.md gotcha avoided)
    - QML Timer debounce (80 ms single-shot + final write on release): coalesces Slider drags to bounded LIG writes (T-16a-01)
    - TDD cycle: RED commit (725852e) -> GREEN commit (4bdad29) -> QML commit (ba9b2f6)

key-files:
  created:
    - tests/unit/test_stream_dock_controls.cpp
  modified:
    - src/app/src/stream_dock_control_service.hpp
    - src/app/src/stream_dock_control_service.cpp
    - src/app/src/application.cpp
    - src/app/qml/ProfileEditor.qml
    - tests/unit/CMakeLists.txt

key-decisions:
  - 'QML exposure: Phase 14 deferred QML_SINGLETON; added here in Phase 16 mirroring LightingService (QML_NAMED_ELEMENT + QML_SINGLETON + create()/registerInstance() + static_assert)'
  - 'QML element name: StreamDockControlService (matches QML_NAMED_ELEMENT in the header)'
  - 'Debounce interval: 80 ms single-shot Timer (T-16a-01); final write on onPressedChanged(!pressed) ensures the last dragged value always commits'
  - 'Keys tab placement: controls row below KeyDesigner grid, inside the keyDesignerComp Component, gated by _showKeys && codename != ""'
  - 'Singleton path: Application::exposeToQml calls StreamDockControlService::registerInstance(m_streamDockControl.get()) -- the same held instance drives the QML slider'
  - 'Phase 14 held-handle preserved: setBrightness/clearAll prefer m_activeDevice when codename matches, falling back to m_lookup only on mismatch'

requirements-completed: [DISPLAY-09]

# Metrics
duration: ~9 min
completed: 2026-05-24
---

# Phase 16 Plan 01: StreamDockControlService DISPLAY-09 live controls Summary

**Q_INVOKABLE setBrightness (-> LIG) and clearAll (-> CLE) added to StreamDockControlService with QML_SINGLETON exposure, wired to a debounced Slider + Clear-all button in the ProfileEditor Keys tab.**

## Performance

- **Duration:** ~9 min
- **Started:** 2026-05-24T10:17Z
- **Completed:** 2026-05-24T10:26Z
- **Tasks:** 2/2 completed
- **Files modified:** 5

## Accomplishments

- DISPLAY-09 live controls delivered: dragging the brightness Slider in the Keys tab drives a LIG write to the real AKP05E panel via the Phase-14 StreamDockControlService; the "Clear all keys" button drives a CLE write (clearKey(0xFF))
- QML_SINGLETON machinery added following LightingService pattern exactly (QML_NAMED_ELEMENT + QML_SINGLETON + create()/registerInstance() + static_assert -- CLAUDE.md QML_SINGLETON gotcha avoided)
- Timer debounce (80 ms) coalesces Slider drag steps; one final write on pointer release ensures the committed value is always sent (T-16a-01 / Pitfall 3)
- 4 new MockTransport wire assertions (LIG byte[10]==42, LIG clamp to 100, CLE bytes[5..7], no-op for null device); all pass; full suite 428/428 green
- TDD gate compliance: RED commit 725852e -> GREEN commit 4bdad29

## Task Commits

1. **Task 1 (RED): Failing tests for setBrightness/clearAll** - `725852e` (test)
1. **Task 1 (GREEN): Q_INVOKABLE + QML_SINGLETON implementation** - `4bdad29` (feat)
1. **Task 2: Debounced brightness Slider + Clear-all button in Keys tab** - `ba9b2f6` (feat)

## Files Created/Modified

- `tests/unit/test_stream_dock_controls.cpp` - 4 Catch2 TEST_CASEs tagged [stream-dock-controls][DISPLAY-09]; LIG byte[10] asserts, CLE bytes[5..7] assert, null-lookup no-op assert; ASCII-only titles
- `tests/unit/CMakeLists.txt` - Added test_stream_dock_controls.cpp entry (no new source links needed; stream_dock_control_service.cpp already in the binary via the Phase-14 entry)
- `src/app/src/stream_dock_control_service.hpp` - Added QML_NAMED_ELEMENT + QML_SINGLETON + create()/registerInstance() static methods; Q_INVOKABLE setBrightness/clearAll declarations; QtQmlIntegration include; static_assert(!is_default_constructible)
- `src/app/src/stream_dock_control_service.cpp` - Module-level g_instance pointer; create()/registerInstance() bodies (mirroring LightingService); setBrightness/clearAll implementations with resolve-prefer-held, dynamic_cast+null-check within 3 lines, std::clamp, try/catch device-yank guard
- `src/app/src/application.cpp` - StreamDockControlService::registerInstance(m_streamDockControl.get()) in exposeToQml() after FirmwareUpdateService registration
- `src/app/qml/ProfileEditor.qml` - keyDesignerComp expanded from single-line Component to ColumnLayout: KeyDesigner + RowLayout with Label + Slider (id:brightnessSlider, from:0,to:100,stepSize:1,value:80) + Timer debounce (80 ms, repeat:false) + SecondaryButton "Clear all keys"; both controls gated visible: \_showKeys && codename != ""

## Key Design Decisions

### QML_SINGLETON Was Not Yet Added by Phase 14

Phase 14 SUMMARY explicitly states: "QML Exposure Deferred -- StreamDockControlService is a plain QObject for Phase 14; no QML_SINGLETON / qmlRegisterSingletonInstance." This plan added the full QML_SINGLETON machinery from scratch.

### Debounce Interval: 80 ms

Chosen to match the RESEARCH.md Pattern 2 recommendation. At 80 ms a 1-second drag at 60 fps produces at most 13 LIG writes (bounded by the interval, not by pixel movement). The final write on `onPressedChanged(!pressed)` guarantees the last value is always committed regardless of where the timer fired.

### Keys Tab Placement

The controls row lives inside the expanded `keyDesignerComp` Component, below the `KeyDesigner` grid. This keeps the brightness/clear controls adjacent to the keys they control without disturbing other tabs or the Apply/Revert footer.

### Singleton Reference in QML

The QML element name `StreamDockControlService` (set via `QML_NAMED_ELEMENT(StreamDockControlService)`) is referenced directly in ProfileEditor.qml, matching the precedent of `LightingService` in RgbPicker.qml. No property binding or `import`-path change is needed -- the element is already part of the `AjazzControlCenter` module.

## Deviations from Plan

None -- plan executed exactly as written. The Phase 14 QML deferral was expected (plan blocked on 14-02-SUMMARY confirmation), and the implementation followed the specified LightingService mirror pattern without modification.

## TDD Gate Compliance

- RED gate: commit `725852e` (test(16-01)) -- 4 failing tests for setBrightness/clearAll
- GREEN gate: commit `4bdad29` (feat(16-01)) -- implementation; all 4 tests pass

## Follow-up Items (Not Phase 16-01 Scope)

| Item                                                              | Phase       | Reason deferred                                                          |
| ----------------------------------------------------------------- | ----------- | ------------------------------------------------------------------------ |
| Live hardware witness: brightness slider drives real AKP05E panel | 25          | Phase 16 proof is MockTransport-only; Phase 25 does live power-cycle UAT |
| Persist last-set brightness value to profile (survive restart)    | 16-02/16-03 | Profile persistence is Phase 16-02 scope                                 |
| Active-device selection UI (device picker)                        | 16          | Phase 14 first-arrival wins; Phase 16 scope deferred to 16-02/16-03      |

## Threat Mitigations Applied

| Threat                                     | Mitigation                                                                                                  |
| ------------------------------------------ | ----------------------------------------------------------------------------------------------------------- |
| T-16a-01: Slider drag floods LIG writes    | 80 ms single-shot Timer debounce + one final write on release; bounded write count per drag                 |
| T-16a-02: dynamic_cast nullptr deref       | Null-check within 3 lines of every cast (5 sites confirmed)                                                 |
| T-16a-03: QML_SINGLETON dual-instance trap | create()/registerInstance()/static_assert + qmlRegisterSingletonInstance of live instance; never bare macro |
| T-16a-04: brightness percent out of range  | std::clamp(percent, 0, 100) at service boundary; backend also clamps                                        |

## Self-Check: PASSED

Files confirmed present:

- `src/app/src/stream_dock_control_service.hpp` - FOUND (Q_INVOKABLE + QML_SINGLETON)
- `src/app/src/stream_dock_control_service.cpp` - FOUND (setBrightness/clearAll + g_instance)
- `src/app/src/application.cpp` - FOUND (StreamDockControlService::registerInstance)
- `src/app/qml/ProfileEditor.qml` - FOUND (StreamDockControlService.setBrightness/clearAll + Timer)
- `tests/unit/test_stream_dock_controls.cpp` - FOUND (4 TEST_CASEs)

Commits confirmed:

- `725852e` (RED) - FOUND
- `4bdad29` (GREEN) - FOUND
- `ba9b2f6` (QML) - FOUND

ctest suite: 428/428 green.
