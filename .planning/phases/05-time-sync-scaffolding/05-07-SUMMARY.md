---
phase: 05-time-sync-scaffolding
plan: 07
subsystem: app
tags: [application, qml-singleton, hotplug-wiring, device-lookup, a-04, shared-ptr]

requires:
  - phase: 05-time-sync-scaffolding
    provides: TimeSyncService class (Plan 05-04); QML UI (Plan 05-06)
  - phase: 04-hot-plug-hardening
    provides: shared_ptr<IDevice> registry cache (Phase 4 D-06)
provides:
  - Application::m_timeSync owned + constructed with production DeviceLookup
  - TimeSyncService::registerInstance — QML singleton runtime-live
  - Application::onHotplug forwards Arrived → onDeviceArrivedDebounced
  - DeviceLookup signature: std::function<shared_ptr<IDevice>(QString)>
affects: [05-08]

tech-stack:
  added: []
  patterns:
    - DeviceLookup returning shared_ptr<IDevice> structurally enforces A-04 lifetime contract (no raw-pointer juggling).
    - Codename ↔ VID/PID round-trip via DeviceRegistry::enumerate() (O(N) over ~20 descriptors on GUI thread; acceptable).

key-files:
  created: []
  modified:
    - src/app/src/application.hpp
    - src/app/src/application.cpp
    - src/app/src/time_sync_service.hpp
    - src/app/src/time_sync_service.cpp
    - tests/unit/test_time_sync_service.cpp

key-decisions:
  - DeviceLookup signature promoted from std::function<IDevice*(QString)> to std::function<shared_ptr<IDevice>(QString)>. A-04 / D-01 amendment 3 structurally enforced — doPush holds the shared_ptr in a local for the dynamic_cast → setTime sequence.
  - Production lookup walks DeviceRegistry::enumerate() to resolve codename → DeviceId, then calls registry.open(id). The empty serial in DeviceId means open() matches on (vid, pid) per Phase 4 D-04.
  - Plan 05-04 unit tests updated in lockstep — 7 cases still pass with the new signature.

patterns-established:
  - 'Production shape for capability lookups: lambda captures registry by reference; returns shared_ptr from registry.open(); lambda lifetime is the function call only.'

requirements-completed: [TIMESYNC-03, TIMESYNC-05]

duration: ~30min
completed: 2026-05-14
---

# Phase 5 Plan 07: Application Wiring — TimeSyncService + Hotplug Hook

**Application owns TimeSyncService wired to a production DeviceLookup (shared_ptr-returning, registry-capturing); QML singleton registered; HotplugMonitor::deviceArrived → onDeviceArrivedDebounced wired; the full hotplug → debounce → setTime stub chain is now runtime-live.**

## Performance

- Duration: ~30 min
- Tasks: 1 (Application wiring + DeviceLookup signature change + test update)
- Files modified: 5

## Accomplishments

- `Application::m_timeSync` (std::unique_ptr<TimeSyncService>) owned and constructed in the initializer list with a DeviceLookup lambda that captures `m_deviceRegistry` by reference and returns the shared_ptr<IDevice> from `registry.open(id)` directly.
- `TimeSyncService::registerInstance(m_timeSync.get())` added to `exposeToQml()` — QML singleton now resolves at runtime.
- `Application::onHotplug()` forwards `HotplugAction::Arrived` events to `m_timeSync->onDeviceArrivedDebounced(codename)`. The 300 ms QTimer::singleShot inside that method stacks on top of Phase 4 D-05's 300 ms hot-plug coalescing → ~600 ms plug-in to auto-sync fire (well within the design doc budget).
- DeviceLookup signature change: `std::function<IDevice*(QString)>` → `std::function<shared_ptr<IDevice>(QString)>`. A-04 / D-01 amendment 3 is now structurally enforced — `doPush()` holds the shared_ptr in a local across the dynamic_cast → setTime sequence, closing the UAF window from Phase 4 D-06.
- 7 Plan 05-04 unit tests updated to the new signature — all still pass.
- Build: 74/74 targets. ctest 175/175 pass.

## Task Commits

1. **Task 1 (full Application wiring + DeviceLookup change):** `fadf6e2` (feat(app))

## Files Created/Modified

- `src/app/src/application.hpp` — added `m_timeSync` member; included `time_sync_service.hpp`.
- `src/app/src/application.cpp` — added TimeSyncService construction with production DeviceLookup lambda; `registerInstance` call; `onHotplug` Arrived branch forwarding to `onDeviceArrivedDebounced`.
- `src/app/src/time_sync_service.hpp` — DeviceLookup signature changed to return shared_ptr; doc comment updated.
- `src/app/src/time_sync_service.cpp` — doPush() now holds shared_ptr<IDevice> in a local; dynamic_cast operates on dev.get().
- `tests/unit/test_time_sync_service.cpp` — 7 cases updated to new lambda signature.

## Decisions Made

- **DeviceLookup signature promotion.** Plan 05-04's original DeviceLookup returned raw `IDevice*`. The original justification: testability. The actual cost: a small UAF window between `registry.open()` returning and the cast/setTime running. Plan 05-07's production lookup naturally returned `shared_ptr<IDevice>` from `registry.open()`, so I promoted the signature change up to TimeSyncService — A-04's shared_ptr capture intent is now a type-system enforcement, not a convention. Tests update is mechanical (same return semantics, just changed the lambda return type from raw to shared_ptr).
- **Codename ↔ VID/PID via enumerate().** No existing DeviceRegistry method maps codename directly to DeviceId. enumerate() is O(N) with N≈20; on the GUI thread; acceptable for the lookup-then-open path.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Cleaner-than-plan refactor] DeviceLookup signature promoted to shared_ptr**

- **Issue:** Plan 05-04 originally specified `DeviceLookup = std::function<IDevice*(QString)>`. Plan 05-07's A-04 amendment intended to keep the shared_ptr in the production lookup's stack frame, but the raw-pointer return left a small lifetime gap between lookup return and TimeSyncService deref.
- **Fix:** Changed DeviceLookup to return shared_ptr; doPush() holds the shared_ptr for the dynamic_cast → setTime window.
- **Verification:** All 7 Plan 05-04 unit tests still pass + build still clean.
- **Committed in:** `fadf6e2`.

______________________________________________________________________

**Total deviations:** 1 (signature refactor — net improvement).

## Issues Encountered

None beyond the pre-commit hook noise.

## Next Phase Readiness

- **Plan 05-08 (docs + integration test)** can now exercise the full hotplug → debounce → setTime chain end-to-end through the real Stream Dock factories (the integration test mirrors the production DeviceLookup pattern from this plan).

Build: 74/74 targets. ctest 175/175.

______________________________________________________________________

*Phase: 05-time-sync-scaffolding*
*Completed: 2026-05-14*
