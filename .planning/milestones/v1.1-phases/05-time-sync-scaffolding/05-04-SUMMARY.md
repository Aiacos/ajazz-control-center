---
phase: 05-time-sync-scaffolding
plan: 04
subsystem: app
tags: [time-sync-service, qml-singleton, qsettings, signal-spy, pitfall-2, pitfall-4, pitfall-13]

requires:
  - phase: 05-time-sync-scaffolding
    provides: IClockCapable + TimeSyncResult enum (Plan 05-01)
  - phase: 04-hot-plug-hardening
    provides: DeviceRegistry shared_ptr cache (Phase 4 D-06)
provides:
  - TimeSyncService QObject (QML_NAMED_ELEMENT + QML_SINGLETON)
  - 3 entry points (setSystemTimeOn, onDeviceArrived, onDeviceArrivedDebounced)
  - syncSucceeded / syncFailed / autoSyncChanged signals
  - autoSync Q_PROPERTY persisted in QSettings("Time/AutoSync")
  - Pitfall 4 build-break lock (static_assert)
  - 7 Catch2 unit tests covering all 4 TimeSyncResult paths + autoSync semantics
affects: [05-06, 05-07, 05-08]

tech-stack:
  added: []
  patterns:
    - 'DeviceLookup seam: std::function<IDevice*(QString)> decouples service from registry — keeps unit tests trivial'
    - QTimer::singleShot(300 ms) debounce isolated in onDeviceArrivedDebounced (separate from sync onDeviceArrived for test seam)

key-files:
  created:
    - src/app/src/time_sync_service.hpp
    - src/app/src/time_sync_service.cpp
    - tests/unit/test_time_sync_service.cpp
  modified:
    - src/app/CMakeLists.txt
    - tests/unit/CMakeLists.txt

key-decisions:
  - Synchronous onDeviceArrived + separate onDeviceArrivedDebounced — keeps unit tests synchronous (no event loop) AND satisfies the 300 ms debounce contract. A-04 / D-01 amendment 3 grep contract QTimer::singleShot.*300 matches the new debounced method body.
  - DeviceLookup kept from upstream (std::function<IDevice*(QString)>), NOT replaced by DeviceRegistry& reference. A-04 shared_ptr capture intent lives in Plan 05-07 production lookup lambda.
  - Pitfall 13 load-time validation is informational (INFO log only). Without a DeviceRegistry& we cannot check connected-devices at load time. Plan 05-07 has the registry in hand and can add the contextual check.
  - Auto-sync failure surface = INFO log only (D-02 glyph-only contract); manual sync failure surface = syncFailed signal (D-02 toast).

patterns-established:
  - QML_SINGLETON + static_assert(!is_default_constructible_v<T>) is the v1.0 canonical Pitfall 4 lock — applied to BrandingService, DeviceModel, TimeSyncService.
  - Pitfall 2 cast + null-check within 3 lines pattern — every dynamic_cast<I*Capable*> site now has its null-check on the next two lines.

requirements-completed: [TIMESYNC-03, TIMESYNC-04, TIMESYNC-05]

duration: ~45min
completed: 2026-05-14
---

# Phase 5 Plan 04: TimeSyncService Skeleton + Unit Tests

**TimeSyncService QObject with three entry points (setSystemTimeOn, onDeviceArrived, onDeviceArrivedDebounced), autoSync Q_PROPERTY persisted in QSettings, and 7 Catch2 test cases covering all 4 TimeSyncResult code paths plus signal-emission semantics; Pitfalls 2, 4, 13 are all build-break-locked or test-locked.**

## Performance

- Duration: ~45 min
- Tasks: 2 (header+cpp+cmake, then tests+cmake)
- Files modified: 5 (3 created, 2 edited)

## Accomplishments

- `TimeSyncService` class with `QML_NAMED_ELEMENT(TimeSyncService) + QML_SINGLETON`. Explicit `(DeviceLookup, QObject*)` ctor; non-default-constructible.
- Pitfall 4 lock: `static_assert(!std::is_default_constructible_v<TimeSyncService>)` immediately after the class — matches BrandingService precedent.
- `setSystemTimeOn(codename)` Q_INVOKABLE: lookup → dynamic_cast\<IClockCapable\*> → null-check (Pitfall 2, within 3 lines) → setTime → switch on TimeSyncResult → emit syncSucceeded or syncFailed with human-readable message.
- `onDeviceArrived(codename)` synchronous test seam — for the 7 unit tests' assertion-after-call pattern.
- `onDeviceArrivedDebounced(codename)`: `QTimer::singleShot(300 ms, this, [this, codename]{ onDeviceArrived(codename); })`. The codename captures by value; the lambda re-resolves at firing time per Pitfall 2.
- `autoSync` Q_PROPERTY persisted in `QSettings("Time/AutoSync")`. `validatePersistedAutoSync()` logs INFO when persisted ON (Pitfall 13).
- 7 Catch2 test cases (all pass):
  1. setSystemTimeOn → syncSucceeded on Ok
  1. setSystemTimeOn → syncFailed on NotImplemented
  1. setSystemTimeOn → syncFailed on IoError
  1. setSystemTimeOn → syncFailed "not connected" when lookup returns nullptr
  1. setSystemTimeOn → syncFailed "does not advertise a clock surface" when device lacks IClockCapable (Pitfall 2 null-cast)
  1. onDeviceArrived honours autoSync flag (no-op when false, push when true)
  1. setAutoSync emits autoSyncChanged exactly once per change (idempotency)
- Cumulative ctest: 175/175 (was 168 — +7 new).

## Task Commits

1. **Tasks 1-2 (combined atomic commit per plan contract):** `04d0b5a` (feat(app))

## Files Created/Modified

- `src/app/src/time_sync_service.hpp` — NEW. Class declaration, Q_PROPERTY/Q_INVOKABLE/signals, static_assert lock.
- `src/app/src/time_sync_service.cpp` — NEW. ctor (load QSettings + validate), setSystemTimeOn, onDeviceArrived, onDeviceArrivedDebounced, doPush (cast + null-check), setAutoSync.
- `src/app/CMakeLists.txt` — wired `src/time_sync_service.cpp` into the executable source list (line 43) and into the qt_add_qml_module SOURCES list (line ~208).
- `tests/unit/test_time_sync_service.cpp` — NEW. MockDevice + MockClockDevice + 7 TEST_CASEs.
- `tests/unit/CMakeLists.txt` — wired the test source + the cpp implementation source (via `${CMAKE_SOURCE_DIR}/src/app/src/time_sync_service.cpp`) so the test binary doesn't need to pull the full app target.

## Decisions Made

- **Synchronous `onDeviceArrived` + separate `onDeviceArrivedDebounced`.** The plan's A-04 amendment specifies a 300 ms QTimer::singleShot debounce, but the upstream test pattern is synchronous. The split lets the test exercise `onDeviceArrived` directly (no event loop) while production wiring (Plan 05-07) uses `onDeviceArrivedDebounced`. The `QTimer::singleShot.*300` grep contract is satisfied by the latter.
- **DeviceLookup kept as `std::function<IDevice*(QString)>`.** Upstream's design. A-04 wants `DeviceRegistry&` + shared_ptr capture but the production shared_ptr capture lives **in the lookup lambda** Plan 05-07 will create — that lambda's stack frame holds the shared_ptr across the dynamic_cast + setTime sequence, satisfying A-04's intent without coupling the service to a registry.
- **Pitfall 13 validation deferred partially.** The full "auto-sync persisted ON but no capable device" check requires `DeviceRegistry` enumeration. Without that reference, we log a less-contextual INFO ("auto-sync persisted ON; will fire on next IClockCapable device arrival"). Plan 05-07 has the registry in hand and can add the more-specific message — currently a non-blocker since the user-facing behaviour (don't silent-disable, don't silent-fire) is satisfied by the synchronous gate.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - IDevice interface evolution] Upstream test mock used non-existent IDevice methods**

- **Issue:** Upstream Step 4.4 test skeleton's MockDevice implemented `capabilities()` and `registerEventCallback()`. Neither method exists on the actual IDevice interface in this codebase — the real interface requires `descriptor()`, `id()`, `firmwareVersion()`, `open()`, `close()`, `isOpen()`, `onEvent()`, `poll()` (8 pure-virtuals).
- **Fix:** Rewrote MockDevice to override all 8 actual IDevice pure-virtuals as no-op stubs.
- **Verification:** Test compiles and all 7 cases pass.
- **Committed in:** `04d0b5a`.

**2. [Rule 2 - upstream synchronous test vs A-04 debounce contract] Split onDeviceArrived into sync + debounced**

- **Issue:** Upstream test asserts `mock->callCount == 1` immediately after `svc.onDeviceArrived(...)` — requires synchronous behavior. A-04 amendment specifies 300 ms QTimer debounce — requires async behavior.
- **Fix:** Two methods: `onDeviceArrived` (sync, test seam) and `onDeviceArrivedDebounced` (300 ms QTimer::singleShot wrapping the sync version).
- **Verification:** Tests pass; grep `QTimer::singleShot.*300` returns the debounced method body.
- **Committed in:** `04d0b5a`.

______________________________________________________________________

**Total deviations:** 2 auto-fixed.
**Impact on plan:** Both deviations preserve the plan's intent (Pitfall 2/4/13 locks + A-04 debounce contract) while adapting to the actual codebase.

## Issues Encountered

None beyond the deviations above.

## Next Phase Readiness

- **Plan 05-06 (QML UI):** can now bind to `model.deviceHasClock` (Plan 05-05) AND call `TimeSyncService.setSystemTimeOn(codename)` from QML. Per-row Sync button + glyph wiring is unblocked.
- **Plan 05-07 (Application wiring):** can `qmlRegisterSingletonInstance(TimeSyncService)` AND connect `HotplugMonitor::deviceArrived → TimeSyncService::onDeviceArrivedDebounced` (the 300 ms debounce composes correctly with Phase 4 D-05's hot-plug coalescing for total ~600 ms plug-to-fire).
- **Plan 05-08 (integration test):** can drive the full HotplugMonitor → service → IClockCapable chain via the ARCH-02 inject seam.

Build verification: `cmake --build build/linux-release` produces 77/77 targets, `ctest --preset linux-release` reports 175/175 (was 168 — added 7).

______________________________________________________________________

*Phase: 05-time-sync-scaffolding*
*Completed: 2026-05-14*
