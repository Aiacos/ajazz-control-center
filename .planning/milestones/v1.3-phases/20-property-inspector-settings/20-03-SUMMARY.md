---
phase: 20-property-inspector-settings
plan: '03'
subsystem: property-inspector
tags: [plugin-sdk, webengine, qml, relay, pi-bridge, cef-shim, PLUGIN-09]
dependency_graph:
  requires: [20-01, 20-02, 17-02]
  provides: [PLUGIN-09-load-handshake, pi-relay-endpoint]
  affects: [Inspector.qml, PropertyInspectorController, PIBridge, Application]
tech_stack:
  added: []
  patterns:
    - activeBridgeChanged signal for dynamic bridge->server relay wiring
    - toPluginRequested signal decoupling PIBridge from SdPluginServer
    - AJAZZ_HAVE_WEBENGINE && AJAZZ_HAVE_WEBSOCKETS nested guard in Application
key_files:
  created: []
  modified:
    - src/app/src/property_inspector_controller.cpp
    - src/app/src/property_inspector_controller.hpp
    - src/app/src/pi_bridge.hpp
    - src/app/src/pi_bridge.cpp
    - src/app/src/application.cpp
    - src/app/qml/Inspector.qml
    - tests/unit/test_pi_bridge.cpp
decisions:
  - 'D1: PIBridge::toPluginRequested signal decouples the bridge from SdPluginServer; Application wires dynamically via activeBridgeChanged'
  - 'D2: Relay wired only when both AJAZZ_HAVE_WEBENGINE and AJAZZ_HAVE_WEBSOCKETS are set; no cross-feature hard dependency at the type level'
  - 'D3: activeBridgeChanged uses bridge as the connection context object so the connection auto-disconnects when the bridge is destroyed'
metrics:
  duration: ~45 min
  completed: '2026-05-24T16:44:30Z'
  tasks_completed: 3
  files_modified: 7
---

# Phase 20 Plan 03: Relay Wiring + Inspector.qml Load Trigger Summary

**One-liner:** loadInspector inserts cefQuery/Mirabox shims, triggers getSettings on load, Inspector.qml drives load/close on action selection, and PIBridge::toPluginRequested is wired to SdPluginServer::sendEvent via Application.

## Tasks Completed

| Task      | Name                                                  | Commit  | Files                                                                                                                                   |
| --------- | ----------------------------------------------------- | ------- | --------------------------------------------------------------------------------------------------------------------------------------- |
| 1 (RED)   | Add failing inspector load + relay tests              | 87d5c01 | tests/unit/test_pi_bridge.cpp, tests/unit/CMakeLists.txt                                                                                |
| 1 (GREEN) | cefQuery+Mirabox shim insertion + getSettings on load | 3494abe | property_inspector_controller.cpp, pi_bridge.hpp                                                                                        |
| 2         | Inspector.qml triggers loadInspector/closeInspector   | 55075c5 | Inspector.qml                                                                                                                           |
| 3         | PIBridge relay + Application wiring                   | 47d7008 | pi_bridge.hpp, pi_bridge.cpp, property_inspector_controller.hpp, property_inspector_controller.cpp, application.cpp, test_pi_bridge.cpp |

## What Was Built

**Task 1 — cefQuery shim insertion + getSettings on load:**

- `property_inspector_controller.cpp::loadInspector` inserts `makeCefQueryShim()` and `makeMiraboxShim()` into the per-plugin profile once (guarded by `freshProfile`) at DocumentCreation/MainWorld — matching the 18-03 injection mechanism.
- After `emit activeInspectorChanged()`, `bridge->getSettings()` is called so the PI receives its persisted settings via `didReceiveSettings` immediately on load.

**Task 2 — Inspector.qml action-selection trigger:**

- Added `maybeLoadInspector()` function that reads `binding.propertyInspectorPath`, `binding.pluginUuid`, `binding.actionUuid`, `binding.contextUuid` (all optional, default "").
- When `propertyInspectorPath` is non-empty AND `PropertyInspectorController.webEngineAvailable` is true, calls `loadInspector`; otherwise calls `closeInspector`.
- Wired via `onBindingChanged: maybeLoadInspector()` so selection changes drive the PI lifecycle.

**Task 3 — Relay endpoints + Application wiring:**

- `PIBridge::sendToPlugin` now emits `toPluginRequested(pluginUuid, json)` instead of being a pure logging stub.
- `PropertyInspectorController` gained `activeBridgeChanged(PIBridge* bridge)` signal, emitted after each `loadInspector` completes.
- `application.cpp` connects `activeBridgeChanged` to a lambda that wires the new bridge's `toPluginRequested` to `SdPluginServer::sendEvent`, routing PI JS `$SD.sendToPlugin()` calls to the live plugin process.
- STOP gate checked: `17-02-SUMMARY.md` exists — live relay wired (A4 condition met).

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] `constexpr auto kUuid` caused `-Werror=unused-variable`**

- **Found during:** Task 3 test compilation
- **Issue:** `constexpr auto kUuid = "com.example.relay-stub"` cannot be used directly inside `QStringLiteral()` (which is a macro), so the variable was unused.
- **Fix:** Inlined the string literal directly into the `PIBridge` constructor call and the `REQUIRE` assertion.
- **Files modified:** tests/unit/test_pi_bridge.cpp
- **Commit:** 47d7008 (same commit as Task 3)

**2. [Rule 1 - Bug] clang-format reformatted files before commit**

- **Found during:** Pre-commit hook
- **Issue:** clang-format reformatted `application.cpp` and `test_pi_bridge.cpp` (trailing-space and indentation adjustments).
- **Fix:** Re-staged the hook-modified files and retried commit (standard pattern).
- **Commit:** 47d7008

## Test Results

- `ctest --preset linux-release`: **534/534 passed** (up from 529 before this plan; 5 new tests added by plans 20-02 + 20-03)
- Key test cases confirmed:
  - `PIBridge relay - sendToPropertyInspector is connectable and fires`
  - `PIBridge relay - sendToPlugin emits toPluginRequested with correct uuid and payload`
  - `isSdpiCssRequest` (from 20-02, also covered here)
  - `PropertyInspectorController - webEngineAvailable is true in WebEngine builds` (gated, 20-03 RED)

## Known Stubs

None. The relay endpoints are fully wired. The live plugin-process round-trip (sendToPlugin JSON reaching the actual plugin's `onSendToPlugin` handler) is a hardware integration concern verified in Phase 25 (A4 + Pitfall 5 per 20-RESEARCH.md).

## Threat Flags

No new security-relevant surface introduced. The relay wiring routes through the existing `SdPluginServer::sendEvent` path (already audited in Phase 17). The `activeBridgeChanged` signal does not expose PIBridge\* outside the Application layer; the bridge is parented to the page and is destroyed when `closeInspector` or a subsequent `loadInspector` runs.

## Self-Check: PASSED

Files verified:

- `src/app/src/property_inspector_controller.cpp` — `makeCefQueryShim` referenced: 2 times
- `src/app/qml/Inspector.qml` — `loadInspector`: 3 occurrences, `closeInspector`: 4 occurrences
- `src/app/src/pi_bridge.cpp` — `toPluginRequested` emitted in `sendToPlugin`
- `src/app/src/application.cpp` — relay connection in `AJAZZ_HAVE_WEBENGINE && AJAZZ_HAVE_WEBSOCKETS` block

Commits verified in log:

- 87d5c01 (RED)
- 3494abe (GREEN Task 1)
- 55075c5 (Task 2)
- 47d7008 (Task 3)
