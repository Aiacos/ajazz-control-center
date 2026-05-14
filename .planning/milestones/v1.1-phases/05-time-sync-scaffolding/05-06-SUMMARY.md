---
phase: 05-time-sync-scaffolding
plan: 06
subsystem: ui
tags: [qml, devicerow, settingspage, time-sync, toast, glyph, d-02, a-05-deferred]

requires:
  - phase: 05-time-sync-scaffolding
    provides: DeviceModel HasClockRole (Plan 05-05); TimeSyncService class (Plan 05-04)
provides:
  - DeviceRow "Sync time" ToolButton gated by hasClockCapability
  - DeviceRow inline status glyph with per-error tooltip
  - DeviceList syncTimeRequested signal + syncGlyphByCodename map
  - SettingsPage "Time sync" section with Auto-sync SwitchDelegate
  - Main.qml Connections to TimeSyncService (D-02 manual=toast+glyph, auto=glyph-only)
  - timeSyncLastTrigger latch for D-02 disambiguation
affects: [05-07]

tech-stack:
  added: []
  patterns:
    - Cross-component glyph push via DeviceList syncGlyphByCodename map (set in Main.qml, read by DeviceRow via root.syncGlyphByCodename[codename] indexed binding)
    - Trigger-source latch (timeSyncLastTrigger) to tease apart manual vs auto-sync surfaces given the shared signal shape

key-files:
  created: []
  modified:
    - src/app/qml/components/DeviceRow.qml
    - src/app/qml/DeviceList.qml
    - src/app/qml/SettingsPage.qml
    - src/app/qml/Main.qml

key-decisions:
  - Renamed DeviceRow.deviceHasClock → hasClockCapability to dodge the QML self-binding trap (the model role and consumer property both named deviceHasClock would produce silent always-false binding). Same convention as deviceCodename / deviceConnected.
  - A-05 (Toast cap) DEFERRED-WITH-TODO. Toast.qml uses a single-instance pattern which already enforces a natural cap of 1. Adding an explicit queue + cap touches Phase 4 D-01 silent-badge surface; per the plan escape hatch, deferred and tracked here.
  - D-02 disambiguation via Main.qml timeSyncLastTrigger latch — manual click sets it to "manual"; Connections handler reads then resets. Auto-sync arrives with empty latch → no toast fires.
  - Glyph state derived from syncFailed message text substring (no enum on signal — see time_sync_service.cpp doPush() returns).

patterns-established:
  - 'QML self-binding trap convention: when a model role and a consumer property would share a name, rename the consumer property (deviceCodename/deviceConnected/hasClockCapability pattern).'
  - D-02 manual-vs-auto surface disambiguation lives in the QML Connections handler (signal-source latch), not in the C++ service. TimeSyncService emits the same signals for both paths.

requirements-completed: [TIMESYNC-01, TIMESYNC-03, TIMESYNC-04, TIMESYNC-05]

duration: ~40min
completed: 2026-05-14
---

# Phase 5 Plan 06: QML UI — Sync button + glyph + auto-sync toggle

**DeviceRow Sync ToolButton + glyph wired via DeviceList signal bubble; SettingsPage auto-sync toggle bound to TimeSyncService; Main.qml Connections enforce D-02 manual=toast+glyph / auto=glyph-only via timeSyncLastTrigger latch. A-05 toast cap deferred-with-TODO.**

## Performance

- Duration: ~40 min
- Tasks: 3 (DeviceRow+DeviceList; SettingsPage; Main.qml+A-05)
- Files modified: 4

## Accomplishments

- DeviceRow.qml: ToolButton with `view-refresh` icon (Qt 6 themed icon), tooltip "Sync time to device", `Accessible.name/description`. Visible iff `hasClockCapability` true. Glyph (✓/!) rendered inline; tooltip carries the per-error wording (NotImplemented / IoError / NotCapable).
- DeviceList.qml: required `deviceHasClock` model role → DeviceRow's `hasClockCapability` consumer property. Bubbles `syncTimeRequested(codename)` to Main.qml. Exposes `syncGlyphByCodename` map for cross-row state.
- SettingsPage.qml: new section "Time sync" with SwitchDelegate (`checked: TimeSyncService.autoSync`, `onToggled: TimeSyncService.autoSync = checked`) + Pitfall-12/13 honest hint about firmware status.
- Main.qml: Connections to `TimeSyncService.syncSucceeded` / `syncFailed`. D-02 latch — `timeSyncLastTrigger = "manual"` set on user click; consumed (and reset to "") by handler. Auto-sync arrives with empty latch → no Toast.show fires.
- Build clean: `cmake --build build/linux-release` produces all targets (66 / 77 incrementally rebuilt). ctest 175/175 pass — no regression.

## Task Commits

1. **Tasks 1-3 combined + 05-03/04/05 summary files:** `a38eded` (feat(qml,docs))

## Files Created/Modified

- `src/app/qml/components/DeviceRow.qml` — added `hasClockCapability`, `syncGlyphState`, `syncTimeRequested` signal, ToolButton, glyph Text.
- `src/app/qml/DeviceList.qml` — added `syncTimeRequested` signal, `syncGlyphByCodename` map property; per-delegate `required property bool deviceHasClock`; wired `hasClockCapability: deviceHasClock` and `syncGlyphState: root.syncGlyphByCodename[codename] || ""`; bubbled `onSyncTimeRequested`.
- `src/app/qml/SettingsPage.qml` — new "Time sync" Frame + SwitchDelegate + hint Label.
- `src/app/qml/Main.qml` — `onSyncTimeRequested` on sidebar that sets `timeSyncLastTrigger = "manual"` + calls `TimeSyncService.setSystemTimeOn`; Connections to TimeSyncService for D-02 disambiguation.

## Decisions Made

- **Renamed DeviceRow's consumer property `deviceHasClock` → `hasClockCapability`.** The plan's literal grep contract `property bool deviceHasClock` is satisfied indirectly (the **model role** name is `deviceHasClock`, declared via `required property bool deviceHasClock` in DeviceList's delegate scope). The DeviceRow's own consumer property uses a distinct name to avoid the QML self-binding trap — same convention as `deviceCodename` / `deviceConnected`.
- **A-05 toast cap deferred-with-TODO.** Toast.qml uses a single-instance pattern (the `id: toast` Item in Main.qml) — calling `.show()` while one is up replaces it. Functionally, this caps to 1 already. Implementing an explicit queue + cap (Pitfall 3) would touch Phase 4 D-01's silent-badge toast surface; deferred per the plan's escape hatch. Captured in the commit body TODO.
- **No new icon asset.** Used Qt 6's `icon.name: "view-refresh"` themed icon (resolves on Linux/macOS via system icon themes; falls back to a `↻` glyph string on platforms without the theme). Keeps v1.0 qrc small.
- **Glyph state derived from syncFailed message text.** TimeSyncService's `syncFailed(QString codename, QString message)` carries a human-readable message; no reason enum on the signal. The handler classifies via substring match against the exact strings returned by `time_sync_service.cpp::doPush()`. Brittle but localized — if doPush's text changes, the Main.qml handler is the single update site.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - QML self-binding trap] DeviceRow consumer property renamed to dodge the same trap the plan would have hit**

- **Issue:** Upstream plan's acceptance criterion `property bool deviceHasClock` on DeviceRow + role name `deviceHasClock` would have produced the self-binding trap when binding `deviceHasClock: deviceHasClock` in DeviceList's delegate. The existing codebase comment on DeviceRow (lines 18-23) documents the same trap for `connected` / `deviceConnected`.
- **Fix:** Renamed DeviceRow's consumer property to `hasClockCapability`. Documented inline (DeviceRow lines ~38-46) with the same convention rationale.
- **Verification:** App builds; QML compiles; Sync button visibility on Stream Dock rows confirms binding works (needs Plan 05-07 runtime + manual visual verification — see Next Phase Readiness).
- **Committed in:** `a38eded`.

**2. [Rule 2 - Toast cap A-05] Deferred-with-TODO**

- **Issue:** Toast.qml has no explicit queue cap. The plan's A-05 amendment requests one (Pitfall 3 mitigation). The plan's escape hatch allows deferral if extending Toast.qml would significantly touch Phase 4 D-01 hot-plug surface.
- **Fix:** Document the gap; current single-instance pattern functionally caps at 1.
- **Committed in:** `a38eded` with TODO marker in commit body.

______________________________________________________________________

**Total deviations:** 2 (1 implementation rename for QML correctness; 1 deferred-with-TODO per plan escape hatch).
**Impact on plan:** No functional regression. Visibility chain (Plan 05-01 hasClock flag → Plan 05-02/03 backend → Plan 05-05 role → Plan 05-06 QML binding → Plan 05-07 runtime) is intact.

## Issues Encountered

- Two pre-commit hook conflicts (clang-format + mdformat) required re-staging. Standard CI interactions; no semantic content changed.
- One mid-iteration self-binding trap discovered during Edit (caught before commit by my own review of the QML pattern). Triggered the rename described above.

## User Setup Required

None — but **manual visual verification needed** (Plan 05-07 wires the runtime and Plan 05-08 lands an integration test that exercises the wiring without a real device):

1. Build + run `./build/linux-release/src/app/ajazz-control-center` (after Plan 05-07).
1. Connect a Stream Dock or AKB980 PRO (any of the 18 hasClock=true rows).
1. Verify the per-row Sync time ToolButton renders.
1. Click it — toast "Time sync not yet supported on this device" + glyph "!" with tooltip.
1. Open Settings → Time sync section — verify the SwitchDelegate + hint label render.
1. Toggle Auto-sync ON — verify QSettings persists across app restart.
1. Toggle a Stream Dock plug-in event — verify NO toast fires for auto-sync (glyph only).

## Next Phase Readiness

- **Plan 05-07 (Application wiring):** can `qmlRegisterSingletonInstance(TimeSyncService)` (so the QML `TimeSyncService.autoSync` references resolve at runtime) AND wire `HotplugMonitor::deviceArrived → TimeSyncService::onDeviceArrivedDebounced` (the 300 ms debounce method from Plan 05-04). All UI surfaces wait on these two C++ wirings to become runtime-live.
- **Plan 05-08 (integration test + docs):** the integration test will exercise the full HotplugMonitor → onDeviceArrivedDebounced → service → IClockCapable chain. The QML toast latch (timeSyncLastTrigger) is unit-test-untestable (would need a QML test rig); manual visual verification covers it.

A-05 (toast cap) TODO recap — to be promoted to a tracked issue after Phase 5 lands:

- File: `src/app/qml/components/Toast.qml`
- Status: single-instance `.show()` replace pattern (effective cap = 1)
- Target: explicit queue cap of 2 + identical-message coalesce within 1s
- Risk: Phase 4 D-01 silent-badge surface (other call sites?)

______________________________________________________________________

*Phase: 05-time-sync-scaffolding*
*Completed: 2026-05-14*
