---
phase: 19-device-plugin-bridge
plan: 03
subsystem: plugins
tags: [plugin-bridge, stream-dock, websocket, device-event, outbound, lifecycle, willappear, hotplug, e2e-tests]

# Dependency graph
requires:
  - phase: 19-01-device-plugin-bridge
    provides: PluginDeviceBridge shell, ContextRegistry (snapshot added here), ActionContext, coordsForKeyIndex, ownerForActionUuid
  - phase: 19-02-device-plugin-bridge
    provides: PluginDeviceBridge ctor, onAction inbound router, bridge wired into Application
  - phase: 15-stream-dock-input-routing
    provides: StreamDockInputService (deviceEvent signal added here as tap seam)
  - phase: 17-sd-plugin-server
    provides: SdPluginServer::sendEvent(targetUuid, eventName, payload) — re-resolves socket per call, returns false safely
provides:
  - DeviceEvent tap seam: new deviceEvent(deviceId, DeviceEvent) Qt signal on StreamDockInputService; emitted after ActionChain dispatch in StreamDockInputService::dispatch()
  - PluginDeviceBridge::onDeviceEvent — DeviceEvent->§4.4 envelope dispatch table (keyDown/keyUp, dialRotate, dialDown/dialUp + legacy aliases, touchTap)
  - ContextRegistry::snapshot() — safe enumeration for retirePageContexts without mutating during iteration
  - PluginDeviceBridge::populateContextsForActivePage — mints contexts + sends willAppear per bound ActionKind::Plugin action on root page
  - PluginDeviceBridge::retirePageContexts — sends willDisappear + retires contexts for a page+plugin scope
  - Lifecycle slots: onPluginRegistered/onPluginDisconnected/onDeviceConnected/onDeviceDisconnected/onActivePageChanged
  - Application wires: setProfileAccessor, deviceEvent tap, pluginRegistered/Disconnected, hot-plug codename + bridge lifecycle
  - 5 outbound e2e tests: keyDown coordinates, dialRotate signed ticks, willAppear on registration, unbound-drop, cross-plugin no-leak
affects:
  - Phase 16 (page navigation: onActivePageChanged wire; root-page simplification lifted when Phase 16 multi-page lands)
  - Phase 23 (keyCols sourced from DeviceRegistry; setFeedback/setText full aux-surface rendering)
  - Phase 25 (touch zone formula hardware-reconciliation; provisional zoneForX used here)

# Tech tracking
tech-stack:
  added: []
  patterns:
    - 'DeviceEvent tap via additive Qt signal: minimal StreamDockInputService change; bridge observes raw events without losing coordinates (Pitfall 3 from plan)'
    - 'Encoder-as-coordinate convention: encoders stored as (controller="Encoder", row=0, column=encoderIndex) in ContextRegistry — consistent across registration (populateContextsForActivePage) and lookup (onDeviceEvent)'
    - 'T-19-leak silent-drop: byCoord registry miss → return with no sendEvent; address by owner uuid only, never broadcast'
    - 'T-19-sock safe pattern: sendEvent re-resolves socket per call via uuid; bridge never holds QWebSocket*'
    - 'snapshot()-then-retire pattern: ContextRegistry::snapshot() returns a stable list before the retirePageContexts mutation loop'
    - 'A6 willAppear scope: root page only for Phase 19; multi-page navigation is Phase 16 authority'
    - 'AJAZZ_HAVE_WEBSOCKETS guard: all bridge lifecycle + test code conditioned on macro; QML smoke test CMake bug fixed by same guard pattern'

key-files:
  created: []
  modified:
    - src/app/src/stream_dock_input_service.hpp
    - src/app/src/stream_dock_input_service.cpp
    - src/app/src/plugin_device_bridge.hpp
    - src/app/src/plugin_device_bridge.cpp
    - src/app/src/application.cpp
    - tests/unit/test_plugin_device_bridge.cpp
    - tests/qml/CMakeLists.txt

key-decisions:
  - DeviceEvent tap seam: added new deviceEvent(deviceId, DeviceEvent) Qt signal to StreamDockInputService (emitted at end of dispatch() after ActionChain) rather than reusing the plugin ActionExecutor stub. The executor approach loses coordinate data (ev.index and ev.value not exposed through the executor API); a direct signal preserves the full DeviceEvent struct. Decision aligns with plan Pitfall 3 guidance.
  - Encoder-as-coordinate convention: encoders are stored in ContextRegistry with controller="Encoder", row=0, column=encoderIndex (0-based). This is consistent with willAppear payload coordinates and the onDeviceEvent byCoord lookup. Documented inline in populateContextsForActivePage and onDeviceEvent.
  - willAppear scope A6 simplification: scoped to root page only. Multi-page navigation authority belongs to Phase 16 (page model not yet wired). A no-op onActivePageChanged is wired so the slot is available; the implementation retires root page contexts and repopulates them — sufficient for single-page Phase 19 usage.
  - kDefaultKeyCols=5 hardcoded (same as 19-02): StreamDockControlService does not expose displayInfo(). Phase 23+ will source from DeviceRegistry.
  - setActiveDeviceCodename() paired with setActiveDevice(): Application calls setActiveDeviceCodename(codename) before setActiveDevice so the first events carry the correct deviceId in the deviceEvent signal.
  - ContextRegistry::snapshot() added as Rule 2 (missing critical functionality): retirePageContexts needed to enumerate then retire without mutating the map during iteration. snapshot() returns a QList of (contextId, ActionContext) pairs captured before the loop.
  - Touch zone derivation for touchTap: mirrors the zoneForX formula in StreamDockInputService — zone = min((x * kEncoderCount) / kTouchStripRangeX, kEncoderCount - 1). Marked PROVISIONAL pending Phase 25 hardware reconciliation.

patterns-established:
  - 'Outbound event dispatch: switch(ev.kind) -> byCoord(controller, row, col) -> sendEvent(ctx.pluginUuid, eventName, payload)'
  - 'Lifecycle registration: onPluginRegistered adds to m_registeredPlugins, calls populateContextsForActivePage for that plugin'
  - 'snapshot-then-retire: snapshot() captures stable list, loop sends willDisappear + retires individual contexts'
  - 'AJAZZ legacy alias pair: EncoderPressed sends both dialDown + keyDownCord; EncoderReleased sends dialUp + keyUpCord'

requirements-completed: [PLUGIN-10]

# Metrics
metrics:
  duration: 45 minutes (includes prior session implementation + this session commit/summary)
  completed: 2026-05-24
  tasks-completed: 2
  files-modified: 7
---

# Phase 19 Plan 03: Outbound Bridge + Lifecycle Summary

**One-liner:** Outbound DeviceEvent->§4.4 envelope dispatch (keyDown/keyUp, dialRotate, dialDown/dialUp, touchTap) + willAppear/deviceDidConnect lifecycle via new Qt signal tap on StreamDockInputService, closing PLUGIN-10 bidirectional bridge.

## What Was Built

### Task 1: onDeviceEvent DeviceEvent->§4.4 envelope mapping

Added a new `deviceEvent(QString const& deviceId, ajazz::core::DeviceEvent const& ev)` Qt signal to `StreamDockInputService`. The signal is emitted at the end of `dispatch()` after the ActionChain has executed — preserving execution order (local action bindings first, then bridge observers).

`PluginDeviceBridge::onDeviceEvent()` implements a switch on `DeviceEvent::Kind`:

| Kind                   | Event sent             | Key payload fields                                        |
| ---------------------- | ---------------------- | --------------------------------------------------------- |
| KeyPressed             | keyDown                | coordinates:{row,column}, isInMultiAction:false           |
| KeyReleased            | keyUp                  | coordinates:{row,column}, isInMultiAction:false           |
| EncoderTurned          | dialRotate             | ticks (signed int32), pressed:false, controller:"Encoder" |
| EncoderPressed         | dialDown + keyDownCord | controller:"Encoder"                                      |
| EncoderReleased        | dialUp + keyUpCord     | controller:"Encoder"                                      |
| TouchStrip gesture 0   | touchTap               | x, y:0, hold:false                                        |
| TouchStrip gesture 1/2 | (dropped)              | page-nav intent; Phase 16                                 |
| Connected/Disconnected | (handled by lifecycle) |                                                           |

All routing goes through `ContextRegistry::byCoord()`. A registry miss is a silent drop (T-19-leak). The owner uuid comes from `ctx.pluginUuid` set at registration time. `sendEvent` re-resolves the socket per call (T-19-sock).

### Task 2: Lifecycle + Application wires + e2e tests

**Lifecycle:**

- `populateContextsForActivePage(deviceId, pluginUuid)` — enumerates root-page key and encoder bindings from the injected profile accessor. For each `ActionKind::Plugin` action owned by a registered plugin (via `ownerForActionUuid`), registers a context and sends `willAppear`.
- `retirePageContexts(deviceId, pageId, pluginUuid)` — uses `ContextRegistry::snapshot()` to enumerate, sends `willDisappear` to each, retires contexts individually.
- `onPluginRegistered` / `onPluginDisconnected` — maintain `m_registeredPlugins`; on register: `populateContextsForActivePage` for that plugin; on disconnect: `retirePageContexts` scoped to that plugin.
- `onDeviceConnected` — populates contexts + sends `deviceDidConnect` to all registered plugins.
- `onDeviceDisconnected` — sends `deviceDidDisconnect` + `retireDevice`.
- `onActivePageChanged` — retires root page contexts (willDisappear) + repopulates (willAppear).

**Application wires** (inside `AJAZZ_HAVE_WEBSOCKETS`):

- `setProfileAccessor` lambda wired from `m_profileController->activeProfile()`
- `deviceEvent` signal connected to `PluginDeviceBridge::onDeviceEvent`
- `pluginRegistered` / `pluginDisconnected` connected to bridge lifecycle slots
- Hot-plug Arrived: `setActiveDeviceCodename(codename)` + `onDeviceConnected(codename)`
- Hot-plug Removed: `setActiveDeviceCodename({})` + `onDeviceDisconnected(codename)`

**5 new e2e tests** (tests 503-507, all inside `AJAZZ_HAVE_WEBSOCKETS`):

1. `outbound keyDown delivers to bound plugin with coordinates` — feeds KeyPressed index=3, asserts keyDown {row:0,col:2}
1. `outbound dialRotate delivers signed ticks to bound plugin` — feeds EncoderTurned value=-2, asserts dialRotate {ticks:-2}
1. `willAppear sent on plugin registration with bound action` — profile key 2 bound to plugin, onPluginRegistered, asserts willAppear {row:0,col:2} with non-empty context
1. `outbound unbound coordinate sends no event no crash` — no contexts, feeds KeyPressed, asserts no keyDown received
1. `outbound event for other-plugin context does not reach us` — key 4 owned by "com.other.plug", client is "com.test.plug", asserts no leakage

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Fixed pre-existing QML smoke test link failure**

- **Found during:** Task 1 (pre-commit verification)
- **Issue:** `tests/qml/CMakeLists.txt` included `plugin_device_bridge.cpp` (via `ACC_QML_MODULE_SOURCES`) but never linked `Qt6::WebSockets` or compiled `sd_plugin_server.cpp`. Phase 19-02 introduced `SdPluginServer` references into the bridge, causing undefined-reference link errors in the QML smoke test when `AJAZZ_HAVE_WEBSOCKETS=1`.
- **Fix:** Added conditional block to `tests/qml/CMakeLists.txt` mirroring the pattern in `tests/unit/CMakeLists.txt`: `if(AJAZZ_HAVE_WEBSOCKETS) target_sources(... sd_plugin_server.cpp) target_link_libraries(... Qt6::WebSockets) target_compile_definitions(... AJAZZ_HAVE_WEBSOCKETS=1) endif()`
- **Files modified:** `tests/qml/CMakeLists.txt`
- **Commit:** `363513d`

**2. [Rule 2 - Missing Critical Functionality] Added ContextRegistry::snapshot()**

- **Found during:** Task 2 implementation of `retirePageContexts`
- **Issue:** `retirePageContexts` needed to enumerate all contexts matching a filter, then retire them. Direct iteration while modifying the map is undefined behavior. No safe enumeration accessor existed.
- **Fix:** Added `QList<std::pair<QString, ActionContext>> ContextRegistry::snapshot() const` — captures a stable copy before any mutation. Used in `retirePageContexts` to iterate the snapshot and retire each entry individually.
- **Files modified:** `src/app/src/plugin_device_bridge.hpp`, `src/app/src/plugin_device_bridge.cpp`
- **Commit:** `363513d`

## Threat Surface Scan

No new network endpoints, auth paths, or trust boundaries beyond those in the plan's `<threat_model>`. All T-19-\* mitigations implemented:

- T-19-leak: byCoord miss = silent drop; sendEvent addresses by owner uuid only
- T-19-sock: no cached QWebSocket\* (`grep -c QWebSocket plugin_device_bridge.cpp` = 0)
- T-19-stale: contexts retired before post-retire events can be dispatched
- T-19-owner: ownerForActionUuid longest-prefix match against m_registeredPlugins

## Known Stubs

- `kDefaultKeyCols=5` hardcoded in `onDeviceEvent` and `populateContextsForActivePage` — AKP05E canonical. Phase 23+ will source keyCols from DeviceRegistry when multi-device support lands. Not a correctness issue for Phase 19 (AKP05E is the only connected Stream Dock family device).
- Touch zone formula (`zone = min((x * kEncoderCount) / kTouchStripRangeX, kEncoderCount - 1)`) marked PROVISIONAL — mirrors StreamDockInputService's `zoneForX`; hardware-reconciled in Phase 25.
- `onActivePageChanged` retires/repopulates root page only — multi-page scope is Phase 16 authority.

## Phase 19 PLUGIN-10 Convergence Status

All three Phase 19 plans are now complete:

- **19-01**: ContextRegistry + pure helpers (coordsForKeyIndex, ownerForActionUuid, decodeDataUriImage)
- **19-02**: Inbound action router (plugin→device paint: setImage/setTitle/setBG)
- **19-03**: Outbound event dispatch (device→plugin: keyDown/dialRotate/willAppear/lifecycle) ← this plan

PLUGIN-10 is closed. The bidirectional bridge round-trip is proven hardware-free via loopback WebSocket tests (32/32 PluginDeviceBridge tests pass).

## Commits

| Task                                               | Commit    | Description                                                               |
| -------------------------------------------------- | --------- | ------------------------------------------------------------------------- |
| Task 1 (mapping + tap seam + QML fix)              | `363513d` | feat(19-03): implement onDeviceEvent DeviceEvent to §4.4 envelope mapping |
| Task 2 (lifecycle + Application wires + e2e tests) | `f95c721` | feat(19-03): wire outbound bridge in Application + loopback e2e tests     |

## Self-Check: PASSED

- [x] `src/app/src/stream_dock_input_service.hpp` modified (deviceEvent signal, setActiveDeviceCodename)
- [x] `src/app/src/plugin_device_bridge.cpp` modified (onDeviceEvent, lifecycle, snapshot)
- [x] `src/app/src/application.cpp` modified (wires)
- [x] `tests/unit/test_plugin_device_bridge.cpp` modified (5 e2e tests)
- [x] `tests/qml/CMakeLists.txt` modified (Rule 1 fix)
- [x] Commits 363513d and f95c721 exist in git log
- [x] 32/32 PluginDeviceBridge tests pass
- [x] grep -c QWebSocket plugin_device_bridge.cpp = 0
- [x] grep -c HotplugMonitor plugin_device_bridge.cpp = 0
- [x] grep for nlohmann in bridge files returns comments only (no #include)
