---
phase: 28-akp05-plugin-action-completeness-drag-to-bind-on-keys-dials
plan: '04'
subsystem: plugin-device-bridge
tags: [plugin, bridge, ActionContext, profileChanged, touchZone, encoder, PLUGIN-19]
dependency_graph:
  requires: [28-03]
  provides: [profileChanged-to-bridge-wiring, touchZone-context-registration, activeDeviceId-accessor]
  affects: [plugin_device_bridge.hpp, plugin_device_bridge.cpp, application.cpp, test_plugin_device_bridge.cpp]
tech_stack:
  added: []
  patterns:
    - inline-accessor style (branding_service.hpp:74 analog)
    - QObject::connect lambda with isEmpty() guard (T-28-09)
    - controller="Encoder" convention for touch-zone context registration (A2/Pitfall-5)
key_files:
  created: []
  modified:
    - src/app/src/plugin_device_bridge.hpp
    - src/app/src/plugin_device_bridge.cpp
    - src/app/src/application.cpp
    - tests/unit/test_plugin_device_bridge.cpp
decisions:
  - populateContextsForActivePage moved to public section (was private) to enable direct test + application.cpp lambda wiring without a dedicated slot
  - Touch-zone contexts registered under controller="Encoder" col=zoneIndex — matches the LOCKED byCoord("Encoder",0,zone) lookup in onDeviceEvent TouchUp (~line 628); NOT changed
  - profileChanged lambda guards on activeDeviceId().isEmpty() with no "akp05e" fallback (T-28-09 clean no-op)
metrics:
  duration_minutes: 25
  completed_date: '2026-05-31'
  tasks_completed: 2
  files_changed: 4
---

# Phase 28 Plan 04: profileChanged Wiring + touchZone Context Registration Summary

Wire `profileChanged` to `PluginDeviceBridge::populateContextsForActivePage` (guarded on
non-empty `activeDeviceId()`) and enumerate `prof.touchZones.onTap` in
`populateContextsForActivePage` under `controller="Encoder"` so dropped touch-zone
plugin actions are immediately invocable via the existing LOCKED TouchUp lookup.

## Tasks Completed

| Task          | Name                                                                        | Commit  | Files                                                                             |
| ------------- | --------------------------------------------------------------------------- | ------- | --------------------------------------------------------------------------------- |
| 1 (RED+GREEN) | Add activeDeviceId() accessor + touchZones.onTap enumeration + bridge tests | ba3a291 | plugin_device_bridge.hpp, plugin_device_bridge.cpp, test_plugin_device_bridge.cpp |
| 2             | Wire profileChanged -> populateContextsForActivePage in application.cpp     | ffaeb0c | application.cpp                                                                   |

## What Was Done

### Task 1

**`src/app/src/plugin_device_bridge.hpp`:**

- Added `[[nodiscard]] QString activeDeviceId() const noexcept { return m_activeDeviceId; }` to the public section, following the `branding_service.hpp:74` inline-accessor style
- Moved `populateContextsForActivePage` from the private section to the public section (required for the `application.cpp` lambda wiring and for direct-call tests)

**`src/app/src/plugin_device_bridge.cpp`:**

- Extended `populateContextsForActivePage` with a `prof.touchZones` loop after the existing `prof.encoders` loop
- Each touch-zone `onTap` Plugin action is registered under `controller="Encoder"`, `row=0`, `column=zoneIndex` — exactly matching the LOCKED `m_registry.byCoord(deviceId, "Encoder", 0, zone)` lookup in `onDeviceEvent`'s `TouchUp` case (~line 628)
- Added a co-documentation comment pinning the `controller="Encoder"` convention to the locked lookup (Assumption A2 / Pitfall 5)

**`tests/unit/test_plugin_device_bridge.cpp`:**

- Added `"PluginDeviceBridge populateContexts registers encoder plugin context"` — builds a profile with `encoder[0].onPress` plugin action, calls `populateContextsForActivePage("akp05e")`, asserts `byCoord("akp05e","Encoder",0,0)` has value and `willAppear` was received
- Added `"PluginDeviceBridge populateContexts registers touch zone as Encoder context"` — same but with `touchZones[1].onTap` plugin action; asserts `byCoord("akp05e","Encoder",0,1)` has value

### Task 2

**`src/app/src/application.cpp`:**

- Inside `#ifdef AJAZZ_HAVE_WEBSOCKETS`, after the existing three `profileChanged` connections (lines 452-483), added `QObject::connect(m_profileController, &ProfileController::profileChanged, m_pluginBridge, lambda)`
- Lambda calls `populateContextsForActivePage(activeDeviceId())` guarded by `if (!m_pluginBridge->activeDeviceId().isEmpty())`
- No "akp05e" fallback — pure no-op when no device is active (T-28-09)
- Comment references PLUGIN-19 and IN-02 ordering invariant

## Deviations from Plan

### Auto-fixed Issues

None. The plan was executed exactly as written. One deviation in implementation:

**Move of populateContextsForActivePage from private to public:** the plan said "Add a public `activeDeviceId()` accessor" but did not explicitly say to change the visibility of `populateContextsForActivePage`. The tests in the plan called it directly, which required it to be public. This was the correct interpretation — the function needs to be callable from the application.cpp lambda (which is outside the class) and from tests. The move preserves all prior call sites (they were all through the bridge QObject pointer, which works with both access levels at the call site).

## Verification Results

```
ctest --preset linux-release -E qml
100% tests passed, 0 tests failed out of 734
```

Both new bridge tests green:

- `Test #708: PluginDeviceBridge populateContexts registers encoder plugin context ... Passed`
- `Test #709: PluginDeviceBridge populateContexts registers touch zone as Encoder context ... Passed`

COD-031: `grep -rn nlohmann src/core/include/` returns 3 hits — all pre-existing doc comments,
zero new violations. All changes are in `src/app/src/` (app layer only).

`onDeviceEvent` TouchUp lookup (`byCoord(deviceId, "Encoder", 0, zone)`) at line 628 is UNCHANGED.
No protocol/wire/opcode change.

## Known Stubs

None. All enumeration paths are fully wired.

## Threat Flags

None new. All changes are within the existing trust boundary (Profile binding ->
ContextRegistry). T-28-09 (empty-deviceId guard) and T-28-10 (existing UUID-prefix
ownership check via `ownerForActionUuid` in the touch-zone loop) are both implemented.

## Self-Check: PASSED

- `src/app/src/plugin_device_bridge.hpp` modified: FOUND
- `src/app/src/plugin_device_bridge.cpp` modified: FOUND
- `src/app/src/application.cpp` modified: FOUND
- `tests/unit/test_plugin_device_bridge.cpp` modified: FOUND
- Commit `ba3a291` exists: FOUND
- Commit `ffaeb0c` exists: FOUND
- `grep -q 'QString activeDeviceId() const noexcept'`: FOUND
- `grep -v '^//' plugin_device_bridge.cpp | grep -c 'touchZones'` >= 1: 2 (FOUND)
- `byCoord(...,"Encoder",0,1)` test assertion: FOUND
- onDeviceEvent line 628 unchanged: VERIFIED
- 734/734 tests pass: VERIFIED
- COD-031 clean: VERIFIED
