# Architecture Research: v2.0 Modular Plugin & Binding System

**Domain:** Stream controller plugin-host abstraction + device-generic binding layer (Qt6/C++20, mirajazz sidecar)
**Researched:** 2026-06-06
**Confidence:** HIGH — based on reading every existing source file for the affected components, studying the OpenDeck Rust reference implementation via gh API, and cross-referencing the v1.3 phase context documents.

______________________________________________________________________

## What Already Exists and What It Means

This is a refactor milestone, not a green-field one. The existing codebase already contains substantial working implementation. The analysis below distinguishes what is **REUSED as-is**, what is **MODIFIED**, and what is **NEW**.

### Components That Are REUSED As-Is (Do Not Touch)

| Component                                | File                                                                            | Why Reused                                                                                                                                                                                                                                                                             |
| ---------------------------------------- | ------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `SdPluginServer`                         | `src/app/src/sd_plugin_server.hpp/.cpp`                                         | Full Elgato v6 WS protocol, 41 actions routed, auth handshake, loopback-only invariant; the `sendEvent` seam is exactly what Phase 19 built it for                                                                                                                                     |
| `PluginManager`                          | `src/app/src/plugin_manager.hpp/.cpp`                                           | Discovery, Node/HTML/native spawn, crash-window lifecycle (3-in-30s), exitApp-then-kill shutdown — all complete                                                                                                                                                                        |
| `PluginDeviceBridge`                     | `src/app/src/plugin_device_bridge.hpp/.cpp`                                     | `ContextRegistry`, `coordsForKeyIndex`/`keyIndexForCoords`, `decodeDataUriImage`, `ownerForActionUuid`, inbound action routing (setImage/setTitle/setBG), outbound device-event routing (keyDown/dialRotate/touchTap), willAppear/willDisappear lifecycle — all implemented and tested |
| `StreamDockControlService`               | `src/app/src/stream_dock_control_service.hpp/.cpp`                              | Device paint path (assignKeyImage, repaintPage, setBrightness, keepAlive, navigatePage), coalesced drain, live-key-image mirror — complete                                                                                                                                             |
| `StreamDockInputService`                 | `src/app/src/stream_dock_input_service.hpp/.cpp`                                | Poll loop, DeviceEvent dispatch, encoder coalescer, touch gesture synthesis, `deviceEvent` signal (Phase 19 seam), `injectSyntheticEvent` — complete                                                                                                                                   |
| `BuiltinActionsService`                  | `src/app/src/builtin_actions_service.hpp/.cpp`                                  | BuiltinActionRegistry for ~24 `com.hotspot.streamdock.*` UUIDs, IInputSynthesizer, OBS client — complete                                                                                                                                                                               |
| `PropertyInspectorController`            | `src/app/src/property_inspector_controller.hpp/.cpp`                            | QWebEngine + QQmlWebChannel + PIBridge skeleton — exists; the stub gap is real but the scaffolding is in place                                                                                                                                                                         |
| `SidecarStreamDockDevice`                | `src/app/src/sidecar_stream_dock_device.hpp/.cpp`                               | mirajazz sidecar proxy; persistent handle; ACK-frame filter; JSON protocol — complete and hardware-confirmed                                                                                                                                                                           |
| `sdplugin_extractor`                     | (in ajazz_plugins)                                                              | zip-slip-guarded .sdPlugin extractor                                                                                                                                                                                                                                                   |
| Signature verify-gate                    | `src/plugins/`                                                                  | Ed25519 manifest signature verification                                                                                                                                                                                                                                                |
| `PluginCatalogModel`                     | `src/app/src/plugin_catalog_model.hpp/.cpp`                                     | OpenDeck catalog fetch, local install management                                                                                                                                                                                                                                       |
| `Profile` / `ProfileController`          | `src/core/include/ajazz/core/profile.hpp`, `src/app/src/profile_controller.hpp` | Profile model with keys/encoders/touchZones/pages/applicationHints; load/save complete                                                                                                                                                                                                 |
| `ActionEngine`                           | `src/core/include/ajazz/core/action_engine.hpp`                                 | Multi-action chain interpreter; page navigation stack; Sleep deferral                                                                                                                                                                                                                  |
| `IPluginHost` / `OutOfProcessPluginHost` | `src/plugins/`                                                                  | Python OOP host abstraction — reused, though it is a distinct host surface from the Node/.sdPlugin surface                                                                                                                                                                             |
| mirajazz crate / streamdock-host         | `streamdock-host/`                                                              | Pristine git dependency; sidecar binary — do NOT modify crate                                                                                                                                                                                                                          |

### Components That Are MISSING and Must Be Created NEW

| Component                       | Purpose                                                                              | Where                                                               |
| ------------------------------- | ------------------------------------------------------------------------------------ | ------------------------------------------------------------------- |
| `ApplicationWatcher`            | Foreground-app detection → profile auto-switch                                       | `src/app/src/application_watcher.hpp/.cpp`                          |
| `ActionInstanceStore`           | Per-slot `ActionInstance` model (states[], current_state, children) for Multi/Toggle | `src/core/include/ajazz/core/action_instance.hpp` or `src/app/src/` |
| Per-app profile switch plumbing | Wire `applicationHints` (already in `Profile`) to a live OS watcher                  | `ApplicationWatcher` + `ProfileController` slot                     |
| Wine plugin path                | Optional: spawn Windows-only `.sdPlugin` via Wine if no native path                  | `src/app/src/wine_runner.hpp/.cpp` (conditional)                    |
| Full PI end-to-end              | Close the v1.3 stub: real PI load + `$SD` bridge object + settings round-trip        | extend `PropertyInspectorController` + `PIBridge`                   |
| Event-parity coverage table     | Explicit audit artifact (not code)                                                   | As a doc/test fixture                                               |

### Components That Need MODIFICATION (Extend, Not Rewrite)

| Component                                   | What Changes                                                                                                                                                                                           |
| ------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| `ProfileController`                         | Wire `applicationHints` switching: when `ApplicationWatcher` emits `foregroundAppChanged(name)`, `ProfileController` finds the matching profile and activates it                                       |
| `PluginDeviceBridge`                        | Encoder/touch input PROVISIONAL decode will need updating once retail hardware confirms wire format; currently the `dialRotate`/`touchTap` emit path is correct in structure but provisional in values |
| `Profile` / profile schema                  | Add per-`ActionInstance` state model: `states: Vec<ActionState>`, `current_state: u16`, `children: Vec<ActionInstance>` per slot (Multi/Toggle)                                                        |
| `DeviceView.qml`                            | OpenDeck row/overflow layout rules: key rows, then one encoder row if `encoders > 0`, then one touch row if `touchpoints > 0`; `overflowsX`/`overflowsY` scroll gates                                  |
| `ContextRegistry` (in `PluginDeviceBridge`) | Extend `ActionContext` to carry `stateIndex` + `children` pointer for Multi/Toggle dispatch                                                                                                            |

______________________________________________________________________

## System Architecture Diagram

```
┌─────────────────────────────────────────────────────────────────────────┐
│                            QML UI Layer                                  │
│  DeviceView.qml  ActionLibraryPane.qml  Inspector.qml  PluginStore.qml  │
│  (MODIFIED: OpenDeck row/overflow rules, live-key mirror via livekey://) │
└────────────────────────────────┬────────────────────────────────────────┘
                                 │ Q_INVOKABLE / signals / context props
┌────────────────────────────────┴────────────────────────────────────────┐
│                         Qt Application Layer                             │
│                                                                          │
│  ┌─────────────────────────────────────────────────────────────────┐     │
│  │  Application (composition root — owns all services)             │     │
│  │  REUSED: wires seams; constructs services in dependency order   │     │
│  └─────────────────────────────────────────────────────────────────┘     │
│                                                                          │
│  ┌──────────────────┐  ┌──────────────────┐  ┌──────────────────────┐   │
│  │ ProfileController│  │ ApplicationWatcher│  │  PluginManager       │   │
│  │ REUSED+MODIFIED  │  │  NEW             │  │  REUSED              │   │
│  │ Adds per-app     │  │ OS foreground-app│  │  discover/spawn/     │   │
│  │ switch on hint   │  │ poll; emits      │  │  crash-lifecycle     │   │
│  │ match from       │  │ foregroundApp-   │  │  for .sdPlugin       │   │
│  │ ApplicationWatcher│ │ Changed(name)    │  │  (Node/HTML/native)  │   │
│  └────────┬─────────┘  └────────┬─────────┘  └──────────────────────┘   │
│           │                     │                                        │
│  ┌────────┴─────────────────────┘                                        │
│  │                                                                       │
│  ▼                                                                       │
│  ┌────────────────────────────────────────────────────────────────────┐  │
│  │                      PluginDeviceBridge  REUSED+MINOR-EXT          │  │
│  │                                                                    │  │
│  │  ContextRegistry  (opaque context-id <-> ActionContext)            │  │
│  │  INBOUND:  SdPluginServer::actionReceived                         │  │
│  │    → setImage / setTitle / setState / setSettings / showAlert     │  │
│  │    → context ownership check → StreamDockControlService           │  │
│  │  OUTBOUND: StreamDockInputService::deviceEvent                    │  │
│  │    → keyDown/keyUp, dialRotate, dialDown/dialUp, touchTap        │  │
│  │    → SdPluginServer::sendEvent(pluginUuid, fullEventObject)       │  │
│  │  LIFECYCLE: willAppear/willDisappear, deviceDidConnect/Disconnect │  │
│  │  PI:  PIBridge::contextSettingsChanged -> didReceiveSettings      │  │
│  └───────────────────────┬────────────────────────────────────────────┘  │
│                          │                                               │
│  ┌───────────────────────┴──────────────────┐                            │
│  │                                          │                            │
│  ▼                                          ▼                            │
│  ┌──────────────────────────┐  ┌──────────────────────────────────────┐  │
│  │ StreamDockControlService │  │ StreamDockInputService               │  │
│  │ REUSED                   │  │ REUSED                               │  │
│  │ assignKeyImage → sidecar │  │ poll(device) → DeviceEvent           │  │
│  │ repaintPage / keepAlive  │  │ dispatch → ActionEngine              │  │
│  │ setBrightness / clearAll │  │ coalescer / touch-gesture synthesis  │  │
│  │ LiveKeyImageStore mirror │  │ deviceEvent signal → PluginBridge    │  │
│  └──────────────┬───────────┘  └────────────────┬─────────────────────┘  │
│                 │                               │                        │
│  ┌──────────────┴───────────────────────────────┘                        │
│  │                                                                       │
│  ▼                                                                       │
│  ┌────────────────────────────────────────────────────────────────────┐  │
│  │             SidecarStreamDockDevice (per device instance)          │  │
│  │             REUSED — IDevice impl; QProcess + JSON/stdio           │  │
│  │             Persistent HID handle; keepAlive; ACK-frame filter     │  │
│  └─────────────────────────────────┬──────────────────────────────────┘  │
└────────────────────────────────────┼───────────────────────────────────┘
                                     │ JSON newline-delimited over stdin/stdout
┌────────────────────────────────────┴───────────────────────────────────┐
│                    Rust Sidecar: streamdock-host                        │
│                    (mirajazz crate — PRISTINE, do NOT modify)           │
│  Drives AKP05/N4/AKP03/AKP153 with one persistent HID handle           │
│  Commands: open/close/set_image/set_brightness/keep_alive/get_firmware  │
│  Events:   key_down/key_up/encoder_turn/encoder_press/touch_down/up    │
└────────────────────────────────────────────────────────────────────────┘

                          PARALLEL TRACK
┌────────────────────────────────────────────────────────────────────────┐
│                    Plugin Subprocess Layer                              │
│                                                                        │
│  SdPluginServer (WS, port 0, loopback-only)  REUSED                   │
│     ↔ Node.js child processes (.js/.mjs/.cjs .sdPlugin plugins)        │
│     ↔ HTML/WebEngine (Mirabox shim)                                    │
│     ↔ Native subprocess (platform-specific .sdPlugin)                 │
│                                                                        │
│  PluginManager  REUSED — spawn/lifecycle/crash-window orchestration    │
│  PropertyInspectorController  REUSED+EXTENDED — full PI round-trip     │
│  IPluginHost / OutOfProcessPluginHost  REUSED — Python OOP host        │
│  BuiltinActionsService  REUSED — 24 com.hotspot.streamdock.* UUIDs     │
│  OBS client  REUSED — auth-default-on WS bridge                        │
└────────────────────────────────────────────────────────────────────────┘
```

______________________________________________________________________

## Module Boundaries and Interfaces

### COD-031 Boundary (Inviolable)

The `nlohmann::json` library is PRIVATE-linked to `ajazz_plugins` only. Every component in `src/core/` and any installed public header must never include or depend on it. The canonical rule for new code:

- **Core layer** (`src/core/`): C++20 stdlib only. No Qt, no nlohmann. Profile, ActionEngine, IDevice, ActionInstance (when added to core) — all POD structs with hand-rolled JSON in `profile_io.cpp`.
- **App layer** (`src/app/src/`): `QJsonDocument` / `QJsonObject` for JSON parsing. `QImage` for image decode. Never nlohmann.
- **Plugins layer** (`src/plugins/`): nlohmann allowed (PRIVATE). Ed25519 trust-roots parsing uses it today.

Any new `ActionInstance` or `ActionState` struct introduced at core scope must serialize via the hand-rolled writer pattern in `profile_io.cpp`, not via nlohmann.

### IPluginHost Interface (REUSED as-is for Python OOP Host)

The existing `IPluginHost` in `src/plugins/include/ajazz/plugins/i_plugin_host.hpp` covers the Python OOP host path: `addSearchPath`, `loadAll`, `plugins()`, `dispatch(pluginId, actionId, settingsJson)`. This is the Python-SDK path — distinct from the Node.js `.sdPlugin` path driven by `SdPluginServer` + `PluginManager`. These two paths are complementary: the `.sdPlugin` path is the primary Elgato-protocol path; the Python OOP path is for native Python plugins (not standard .sdPlugin bundles).

For v2.0, the question is whether to unify them behind a single `IPluginHost2` that handles both kinds. The evidence from reading the code says: **do not unify them in v2.0**. They speak different protocols (Elgato WS vs. Python JSON-RPC subprocess). Unification is premature abstraction. Keep the two paths separate and co-operative through `BuiltinActionsService`'s fallback chain.

### ActionInstance Model (NEW — Core Scope)

OpenDeck's `ActionInstance` has: `action: Action`, `context: ActionContext`, `states: Vec<ActionState>`, `current_state: u16`, `settings: serde_json::Value`, `children: Option<Vec<ActionInstance>>`.

The AJAZZ equivalent should live in `src/core/include/ajazz/core/action_instance.hpp` as a plain C++ struct:

```cpp
// src/core/include/ajazz/core/action_instance.hpp
// COD-031: no nlohmann, no Qt — pure C++20 stdlib.
namespace ajazz::core {

struct ActionState {
    std::string image;       // path or resource URL
    std::string name;        // display name
    std::string text;        // title overlay
    bool showTitle{true};
    std::string titleColour{"#FFFFFF"};
    std::string titleAlignment{"middle"};
    uint16_t fontSize{16};
};

struct ActionInstance {
    std::string actionUuid;         // e.g. com.vendor.plugin.action
    std::string pluginUuid;         // owning plugin
    std::vector<ActionState> states;
    uint16_t currentState{0};
    std::string settingsJson;       // per-instance settings blob (opaque)
    // children: for Multi Action / Toggle Action
    std::vector<ActionInstance> children;
};

} // namespace ajazz::core
```

Serialization via the existing hand-rolled writer in `profile_io.cpp` (extend it). The `Binding` and `EncoderBinding` structs in `profile.hpp` should gain an `std::optional<ActionInstance> instance` field that carries this richer model when present, so existing profiles without instances still parse cleanly.

### ApplicationWatcher (NEW — App Scope)

OpenDeck uses the `active_win_pos_rs` crate polling at 250 ms. The C++/Qt equivalent polls at 250–500 ms using platform APIs:

- **Linux/X11:** `_NET_ACTIVE_WINDOW` root property via `xcb` or `QX11Info::connection()`; or `QProcess("xdotool getwindowfocus getwindowname")` as a simple fallback that avoids X11 linking.
- **Linux/Wayland:** `wlr-foreign-toplevel-management-v1` protocol via QProcess(`hyprctl activewindow`) or `swaymsg -t get_tree` — both return the active window name without needing compositor-specific headers.
- **macOS:** `NSWorkspace.shared.frontmostApplication` via a minimal Objective-C++ helper (500 ms poll timer, emits signal).
- **Windows:** `GetForegroundWindow` + `GetWindowText` on a 250 ms QTimer.

The interface is:

```cpp
// src/app/src/application_watcher.hpp
namespace ajazz::app {
class ApplicationWatcher : public QObject {
    Q_OBJECT
public:
    explicit ApplicationWatcher(QObject* parent = nullptr);
    void start();  // arms the poll timer
    void stop();
signals:
    void foregroundAppChanged(QString const& processName);
private slots:
    void poll();
private:
    QTimer* m_timer{nullptr};
    QString m_lastApp;
};
} // namespace ajazz::app
```

`ProfileController` connects to `foregroundAppChanged(name)` and scans `Profile::applicationHints` across all loaded profiles for the device; on match, calls `setActiveProfile(matchedProfileId)`. The `applicationHints` field already exists in the `Profile` struct and is serialized to JSON.

**Confidence on Wayland:** LOW — the correct API depends on which compositor is running; the `xdotool`/`hyprctl`/`swaymsg` fallback approach works but is brittle. Flag this for phase-specific research.

______________________________________________________________________

## Data Flow: Both Directions

### Direction 1: Device Input → Binding Resolution → Built-in Action OR Plugin

```
Physical device (AKP05E key press)
    |
    | HID report (over USB → sidecar stdin)
    v
streamdock-host (Rust, mirajazz)
    | JSON line: {"event":"key_down","key":3}
    v
SidecarStreamDockDevice::handleLine()   [app thread via QProcess::readyRead]
    | decodes JSON → DeviceEvent{Kind::KeyPressed, index=3}
    | calls registered onEvent callback
    v
StreamDockInputService::dispatch(ev)
    |-- Profile lookup: keys[index].onPress ActionChain
    |-- ActionEngine::execute(chain)
    |   |-- Kind::Plugin AND builtin UUID?
    |   |   → BuiltinActionsService::onPluginAction(uuid, settings)
    |   |   → handler: brightness/page-nav/hotkey/OBS/etc.
    |   |-- Kind::Plugin AND third-party UUID?
    |   |   → fallback: no-op here (bridge handles it below)
    |   |-- Kind::OpenFolder → push page, emit pageNavigated
    |   |-- Kind::BackToParent → pop page, emit pageNavigated
    |   |-- Kind::KeyPress → IInputSynthesizer::sendChord(...)
    |   |-- Kind::OpenUrl → system browser
    |   |-- Kind::RunCommand → QProcess detached
    |   `-- Kind::Sleep → QtExecutor deferred
    |
    | emit deviceEvent(deviceId, ev)     [AFTER ActionEngine dispatch]
    v
PluginDeviceBridge::onDeviceEvent(deviceId, ev)
    | ContextRegistry::byCoord(deviceId, "Keypad", row, col) → ActionContext
    | Ownership check: ctx.pluginUuid must be registered
    | Build §4.4 event envelope:
    |   KeyPressed → keyDown{action, context, device, payload{coordinates, state, settings}}
    |   EncoderTurned → dialRotate{ticks, pressed, controller="Encoder"}
    |   EncoderPressed → dialDown + legacy keyDownCord alias
    |   TouchUp (completed tap) → touchTap{x, y, hold}
    v
SdPluginServer::sendEvent(pluginUuid, fullEventObject)
    | Lookup live QWebSocket for pluginUuid
    | Write JSON text frame
    v
Node.js plugin process receives WebSocket message → user code executes
```

### Direction 2: Plugin Outbound (setImage/setFeedback) → Sidecar Render

```
Node.js plugin calls streamDeck.setImage(context, dataUri)
    |
    | WebSocket text frame → SdPluginServer
    | emit actionReceived(pluginUuid, action)
    v
PluginDeviceBridge::onAction(pluginUuid, action)
    | event == "setImage"
    | ContextRegistry::byContext(context) → ActionContext
    | Ownership: ctx.pluginUuid == pluginUuid? else drop
    | decodeDataUriImage(dataUri) → {ok, QImage}
    | ok? → assignKeyImage(keyIndex, img)
    |        [keyIndex = keyIndexForCoords(row, col, keyCols)]
    | !ok? → paintPlaceholder(keyIndex)
    v
StreamDockControlService::assignKeyImage(keyIndex, img)
    | Enqueue in m_pendingWrites (last-write-wins per key)
    | Arm single-shot QTimer (coalesced drain)
    v
[drain timer fires]
    | IDisplayCapable::setKeyImage(keyIndex, rgba, w, h)
    v
SidecarStreamDockDevice::setKeyImage(...)
    | Encode: RGBA → QByteArray → JSON: {"cmd":"set_image","key":N,"data":"<base64>","format":"jpeg"}
    | Write to QProcess stdin
    v
streamdock-host (Rust)
    | Decode JSON → mirajazz set_image call
    | JPEG encode → BAT header + 1024-byte chunks + ULEND
    | HID write to device
    v
Physical AKP05E key LCD updated
```

### Direction 3: Plugin-to-PI and PI-to-Plugin Settings Round-Trip

```
Plugin:  streamDeck.setSettings(context, {myKey: value})
    → SdPluginServer::actionReceived → PluginDeviceBridge::handleSettingsAction
    → persist to plugin_settings_store[contextId]
    → sendEvent(uuid, "didReceiveSettings", {settings}) back to plugin

PI:  $SD.setSettings({myKey: newValue})
    → PIBridge::contextSettingsChanged(pluginUuid, contextId, json)
    → PluginDeviceBridge::onPropertyInspectorSettings(...)
    → registry.updateSettings(contextId, json)
    → SdPluginServer::sendEvent(pluginUuid, fullDidReceiveSettings)

Plugin:  streamDeck.sendToPropertyInspector(context, payload)
    → SdPluginServer::actionReceived → PluginDeviceBridge → relayToPropertyInspector(...)
    → PIBridge forwards to WebChannel → PI JS receives sendToPlugin event
```

### Direction 4: Per-App Profile Auto-Switch

```
OS foreground changes to "obs" (or matching window title)
    |
    | ApplicationWatcher::poll() → detects change
    | emit foregroundAppChanged("obs")
    v
ProfileController slot (connected to signal)
    | Scan all profiles on active device:
    |   for (profile in allProfiles):
    |     if any(hint in profile.applicationHints matches "obs"):
    |       setActiveProfile(profile.id); break
    | If match found and different from current:
    |   load profile → emit profileChanged
    v
ProfileController::profileChanged
    → StreamDockControlService: repaintFromProfile (new keys painted)
    → PluginDeviceBridge: retireAllContexts + populateContextsForActivePage
      (willDisappear for old plugin instances, willAppear for new ones)
    → QML: DeviceView re-binds to new profile bindings
```

______________________________________________________________________

## Where Action Instances and States Live

### Current State (v1.3)

`Profile::keys` is `std::unordered_map<uint16_t, Binding>`. `Binding` has `onPress`/`onRelease`/`state` (a `KeyState` with imagePath/text/background). There is no `states[]` array, no `currentState` index, no `children` for Multi/Toggle.

`ActionEngine` walks `ActionChain` (a `std::vector<Action>`) — multi-step sequences are already the model, but they are flat and do not carry per-state images.

### v2.0 Extension

Add `std::optional<ActionInstance>` to `Binding` and `EncoderBinding`:

```cpp
struct Binding {
    ActionChain onPress;
    ActionChain onRelease;
    KeyState state;
    std::optional<ActionInstance> instance; // NEW: rich state/children model
};
```

When `instance` is present:

- `instance.states[instance.currentState].image` drives the key render (replaces `state.imagePath`)
- `setState` updates `instance.currentState` and triggers re-render
- Multi Action: `instance.children` contains child `ActionInstance`s; dispatch walks them in order
- Toggle Action: `instance.children` are the rotating targets; `currentState` indexes them

The profile schema gets a new optional `"instance"` key per binding (backwards compatible: absent = old model). Serialize/deserialize in `profile_io.cpp` hand-rolled writer.

**Where the in-memory model lives:** `ProfileController` holds the active `Profile` in memory. `ContextRegistry` in `PluginDeviceBridge` holds the live per-instance state (contextId ↔ ActionContext with stateIndex). The two models are synchronized: `setState` in the bridge updates the `ActionContext::stateIndex` in the registry AND the in-memory `Profile::Binding::instance.currentState` (via the profile accessor); `ProfileController::saveProfile` persists both.

______________________________________________________________________

## Integration Points

### Integration Point 1: SdPluginServer ↔ PluginDeviceBridge

**Already wired.** `SdPluginServer::actionReceived` → `PluginDeviceBridge::onAction`. `SdPluginServer::pluginRegistered/Disconnected` → `PluginDeviceBridge::onPlugin*`. `SdPluginServer::sendEvent` ← called by `PluginDeviceBridge` to push events to plugins.

**Status for v2.0:** REUSED. Verify Multi-Action `setState` and `showAlert`/`showOk` paths are complete. Add `SwitchProfile` and `DeviceBrightness` routing (currently OpenDeck-gated by UUID whitelist; decide policy).

### Integration Point 2: StreamDockControlService ↔ PluginDeviceBridge

**Already wired.** `StreamDockControlService::deviceActivated(codename)` → `PluginDeviceBridge::onDeviceConnected`. `StreamDockControlService::pageNavigated(deviceId, pageId)` → `PluginDeviceBridge::onActivePageChanged`. `StreamDockControlService::assignKeyImage` ← called by bridge's `onSetImage`.

**Status for v2.0:** REUSED. No changes needed.

### Integration Point 3: StreamDockInputService ↔ PluginDeviceBridge

**Already wired.** `StreamDockInputService::deviceEvent(deviceId, ev)` → `PluginDeviceBridge::onDeviceEvent`. `StreamDockInputService::pageNavRequested(direction)` → `StreamDockControlService::navigatePage`.

**Status for v2.0:** REUSED. The PROVISIONAL encoder/touch decode paths (dialRotate ticks, touchTap coordinates) will need updating when retail hardware confirms wire format, but the signal structure itself is correct.

### Integration Point 4: ApplicationWatcher ↔ ProfileController (NEW)

`ApplicationWatcher::foregroundAppChanged(name)` → `ProfileController` slot that scans `Profile::applicationHints` and switches profiles.

`ProfileController::profileChanged` → `StreamDockControlService::repaintFromProfile` (already wired) + `PluginDeviceBridge::populateContextsForActivePage` (needs wiring for per-app switch path; today it is wired on manual device selection, not on profile swap).

### Integration Point 5: PropertyInspectorController ↔ PluginDeviceBridge ↔ PIBridge

**Partially wired.** `PluginDeviceBridge::relayToPropertyInspector` signal exists. `PluginDeviceBridge::onPropertyInspectorSettings` exists (receives settings changes from PI). The gap is the PI content loading end-to-end: `PropertyInspectorController::openForContext(contextId, pluginUuid)` → load PI HTML URL into `QQuickWebEngineView` → inject `$SD` bridge object via `QQmlWebChannel`.

**Status for v2.0:** MODIFY `PropertyInspectorController` to complete the end-to-end:

1. Given a context (from QML: user clicks a bound key's PI icon), open the plugin's PI HTML URL in the WebEngineView.
1. The `$SD` JS object (exposed via `QQmlWebChannel`) forwards PI→plugin messages to `SdPluginServer` (via `PIBridge`), and plugin→PI messages arrive via `PluginDeviceBridge::relayToPropertyInspector`.

### Integration Point 6: PluginManager ↔ PluginDeviceBridge

`PluginManager::ownerForAction(actionUuid)` is injected into `PluginDeviceBridge` via `setActionOwnerResolver`. This maps action UUIDs to their owning plugin UUID using the discovered manifests (avoids relying on dotted-prefix heuristic when action IDs don't share prefix with plugin ID).

`PluginManager::stateImagePath(actionUuid, stateIndex)` is injected into `PluginDeviceBridge` via `setStateImageResolver` for auto-rendering manifest-declared state images on `setState`.

**Status:** REUSED. These injection seams already exist in the bridge's API.

### Integration Point 7: SidecarStreamDockDevice ↔ StreamDockControlService + StreamDockInputService

The sidecar proxy exposes `IDevice`. `StreamDockControlService` holds it via `shared_ptr<IDevice>` and calls `dynamic_cast<IDisplayCapable*>` for render operations. `StreamDockInputService` holds it for the poll loop and `onEvent` callback.

**Status for v2.0:** REUSED. The sidecar's JSON protocol (`set_image`, `set_brightness`, `keep_alive`, `get_firmware`) is stable; the app side calls `IDisplayCapable::setKeyImage` which the sidecar proxy implements by encoding to JSON and writing to QProcess stdin.

______________________________________________________________________

## Architectural Patterns to Follow

### Pattern 1: Composition at Application Root, Not in Components

`PluginDeviceBridge` does not own `SdPluginServer` or `StreamDockControlService` — it observes them via non-owning pointers injected at construction. `Application` is the composition root that wires all the signal/slot connections. This keeps each component independently testable (you can construct `PluginDeviceBridge` with a mock server and a mock control service in a unit test without needing a full app stack).

Continue this for `ApplicationWatcher`: inject the `ProfileController` pointer; the watcher emits a signal; `Application` connects the signal to the `ProfileController` slot.

### Pattern 2: Std::function Injection Seams for Testability

`PluginDeviceBridge::setProfileAccessor(std::function<Profile const&()>)` is the canonical form. All "I need access to X" dependencies that cross ownership boundaries are injected as `std::function` lambdas, not raw pointer dependencies. This avoids header pollution and makes unit tests trivial (inject a lambda returning a stack-local mock profile).

For `ActionInstanceStore` (the new per-slot richer model), add a `setInstanceMutator(std::function<void(uint16_t keyIndex, ActionInstance)>)` seam so the bridge can update the in-memory profile on `setState` without knowing the full `ProfileController` API.

### Pattern 3: Last-Write-Wins Coalesced Paint (Already Established)

`StreamDockControlService::m_pendingWrites` (a `std::map<PendingKey, QImage>`) + single-shot QTimer drain. All new render calls (from new Multi-Action state image rendering, from per-app profile switch repaint) must go through `assignKeyImage` — never call `setKeyImage` directly on the device.

### Pattern 4: Persistent Sidecar Handle (Not Per-Interaction Open/Close)

`SidecarStreamDockDevice` holds one `QProcess` for the session. Never re-spawn the sidecar on each render. The AKP05E wedge history (DIS on every open/close) is why this matters. For v2.0 Multi-Action dispatch (which may paint keys rapidly in sequence), the coalesced drain already protects against bursting the sidecar.

### Pattern 5: QML_SINGLETON With Static Factory + Static_Assert Build-Break Lock

All QML-exposed singletons use: `static StreamDockControlService* create(QQmlEngine*, QJSEngine*)` + `static void registerInstance(T*)` + `static_assert(!std::is_default_constructible_v<T>)`. Never the bare `QML_SINGLETON` macro alone. New v2.0 singletons (if any are needed in QML) must follow this pattern.

### Pattern 6: Debug-Channel Addressability for All New Controls

Every new interactive QML control must set `objectName:` so it is reachable by `scripts/ajazz-debug qml.invoke/click/set`. Every new C++ behavioral surface should expose a debug-control RPC method/state. This is a mandatory definition-of-done item, not optional.

______________________________________________________________________

## Anti-Patterns to Avoid

### Anti-Pattern 1: Big-Bang "Rewrite the Plugin System"

The temptation from v2.0's ambition is to replace `SdPluginServer` + `PluginDeviceBridge` with a clean-room reimplementation. This is exactly what caused the v1.3 "everything stubbed" failure: features were marked done before the vertical slice was end-to-end verified. The v1.3 phases landed all the stubs in the right shape, so v2.0 should extend them, not replace them.

Do this instead: identify the specific *gaps* (Multi/Toggle action instances, per-app profile switching, full PI round-trip) and land each as a targeted extension with its own unit test + debug-channel verification.

### Anti-Pattern 2: Skipping the Live Debug-Channel Verification Gate

v1.3 Phase 27 passed 713/713 unit tests but the "Allow" button was a no-op for `.sdPlugin` plugins, caught only by the live debug channel. Unit tests are necessary but not sufficient. For every new behavioral feature in v2.0 (Multi-Action dispatch, Toggle-Action state cycle, per-app profile switch, PI settings round-trip), the verification procedure is:

1. `ctest --preset linux-release` — all green.
1. `AJAZZ_DEBUG_CONTROL=1 ./ajazz-control-center &`
1. Drive via `scripts/ajazz-debug` using `qml.invoke` / `device.renderTest` / `plugin.simulateAction`.
1. `scripts/ajazz-debug screenshot` → **read the screenshot** → confirm real behavior.

### Anti-Pattern 3: Hardcoded Device Geometry in New Code

Do not add `if (codename == "akp05e")` branches to new Multi-Action or per-app switch logic. All device geometry comes from `DeviceDescriptor.keyCount`/`gridColumns`/`encoderCount`/`hasTouchStrip`. New code must be descriptor-driven.

### Anti-Pattern 4: nlohmann in New Core/Public Headers

Any new `ActionInstance`/`ActionState` structs added to `src/core/include/ajazz/core/` must serialize using the hand-rolled writer pattern in `profile_io.cpp`. The compiler check is `grep -rn nlohmann src/core/include/` must return 0. CI enforces this.

### Anti-Pattern 5: Modifying the mirajazz Crate

The `mirajazz` crate is a pristine git dependency. Wire-format changes (encoder ticks polarity, touch zone geometry) go in the sidecar's Cargo test coverage, not in the crate. Sidecar protocol changes between sidecar and `SidecarStreamDockDevice` go in `streamdock-host/src/main.rs` (the sidecar JSON protocol layer).

______________________________________________________________________

## Suggested Build Order: Dependency-Ordered With Early Vertical Slice

The critical principle from v1.3 retrospective: **land the end-to-end path first, then extend coverage**. Each phase must be runnable through the debug channel before marking it done.

### Phase A: ActionInstance Core Model + Profile Schema Extension (Foundation)

**What:** Add `ActionInstance` / `ActionState` structs to `src/core/`, extend `Binding::instance` in `profile.hpp`, extend `profile_io.cpp` serializer/deserializer, update `docs/schemas/profile.schema.json`. Add unit tests for round-trip (empty instance, single-state, multi-state, children).

**NEW:** `src/core/include/ajazz/core/action_instance.hpp`, `profile.hpp` changes, `profile_io.cpp` changes.

**Dependency:** None. This is the foundation everything else builds on. Isolated — no app-layer changes.

**Done gate:** `test_profile_io` round-trips `ActionInstance` with 0, 1, 3 states and 2 children. COD-031 grep passes.

______________________________________________________________________

### Phase B: Multi-Action + Toggle-Action Dispatch (First Vertical Slice)

**What:** Wire `instance.children` dispatch in `BuiltinActionsService` for the `opendeck.multiaction` UUID (execute children in order) and `opendeck.toggleaction` UUID (cycle through children, update `currentState`). Integrate setState rendering: when `setState` fires in `PluginDeviceBridge`, if the `ActionInstance` has a manifest-declared state image, auto-render it via `StreamDockControlService::assignKeyImage`.

**MODIFIED:** `BuiltinActionsService` (new handlers for multiaction/toggleaction), `PluginDeviceBridge` (extend setState handler to check `instance.states[idx].image`).

**NEW:** Unit tests for multi-action sequence and toggle-action cycle.

**Dependency:** Phase A (ActionInstance model must exist).

**Done gate:** Debug channel `plugin.simulateAction` with a Multi-Action binding fires child actions in order. Toggle-Action cycles state image on repeated press. Screenshot confirms key image changes.

______________________________________________________________________

### Phase C: Full Property Inspector Round-Trip (Closes the v1.3 Stub)

**What:** Complete `PropertyInspectorController` — given a context ID (clicked key with a plugin action), load the plugin's PI HTML into the `QQuickWebEngineView`, inject the `$SD` WebChannel bridge, and wire the settings round-trip:

- PI JS calls `$SD.setSettings({...})` → `PIBridge` → `PluginDeviceBridge::onPropertyInspectorSettings` → `SdPluginServer::sendEvent(uuid, "didReceiveSettings")` to plugin.
- Plugin calls `sendToPropertyInspector(context, payload)` → `PluginDeviceBridge::relayToPropertyInspector` → `PIBridge` → PI JS receives `sendToPlugin` event.

**MODIFIED:** `PropertyInspectorController` (close the stub gap). `Inspector.qml` / `PIWebView.qml` (ensure objectName coverage for debug-channel verification).

**Dependency:** Phase A (for instance model context ID stability), existing `PIBridge` + `PluginDeviceBridge::onPropertyInspectorSettings` (already wired in v1.3).

**Done gate:** Install System Monitor plugin. Bind a CPU action. Open PI in UI. Change a setting in PI. Plugin receives `didReceiveSettings`. Screenshot confirms. Drive via `scripts/ajazz-debug qml.invoke` and `plugin.list` + `plugin.simulateAction`.

______________________________________________________________________

### Phase D: Per-App Profile Auto-Switch (ApplicationWatcher)

**What:** Implement `ApplicationWatcher` (platform-specific foreground app detection at 250–500 ms poll). Wire `foregroundAppChanged(name)` → `ProfileController` slot that scans `applicationHints` and auto-switches profile. Wire profile switch → bridge context lifecycle (willDisappear for old instances, willAppear for new ones) — this path already exists for manual device selection, just needs a trigger from profile change.

**NEW:** `src/app/src/application_watcher.hpp/.cpp` (Linux X11/Wayland + macOS + Windows backends). Platform-specific detection behind `#ifdef Q_OS_LINUX` / `#ifdef Q_OS_MACOS` / `#ifdef Q_OS_WIN`.

**MODIFIED:** `ProfileController` (add `onForegroundAppChanged` slot). `Application` (wire `ApplicationWatcher` signal to slot). `PluginDeviceBridge` (connect `profileChanged` → `populateContextsForActivePage` for the profile-swap path).

**Dependency:** Phase A (profile model), Phases B and C recommended but not strictly required.

**Done gate:** Create two profiles with different `applicationHints`. Switch between applications (e.g., a text editor and a browser). Observe keys repaint and plugin willAppear/willDisappear fire correctly. Verify via debug-channel `log.tail` + screenshot.

**Research flag for phase-specific work:** Wayland active-window detection is compositor-specific. Phase D needs a sub-agent sweep of `wlr-foreign-toplevel-management-v1`, KDE/GNOME equivalents, and the `xdotool`/`hyprctl`/`swaymsg` subprocess fallback approach before implementation.

______________________________________________________________________

### Phase E: Event-Parity Audit + Coverage Table

**What:** Systematic comparison of our `SdPluginServer` + `PluginDeviceBridge` outbound event surface against OpenDeck's `events/inbound` + `events/outbound` modules. Produce a coverage table (event name, supported/partial/missing, test reference). Fix any missing events found.

**REUSED:** Everything. This is an audit + targeted gap-fill phase.

**Key events to verify:** `showAlert` / `showOk` (flash + restore), `titleParametersDidChange`, `systemDidWakeUp`, `applicationDidLaunch`/`applicationDidTerminate` (requires ApplicationWatcher from Phase D), `sendToPlugin` relay, `SwitchProfile` (OpenDeck-gated; decide our policy).

**Done gate:** Coverage table document committed. All non-hardware-gated events have unit tests. Zero regression on existing 408+ test suite.

______________________________________________________________________

### Phase F: Native Windows .sdPlugin Support (Optional; Parallel to D/E)

**What:** For Windows-only `.sdPlugin` plugins that ship `.exe` code, investigate whether a native shim can load them without Wine. OpenDeck's approach is Wine on non-Windows. The v2.0 goal is "native where feasible, Wine as fallback."

**Research flag:** This requires a dedicated sub-agent to study the `.sdPlugin` Windows-native execution model (does the plugin communicate only via WS, or does it call Stream Deck SDK DLLs?). If it is WS-only, native spawn works on Linux/macOS too. If it calls vendor DLLs, Wine is required on Linux/macOS.

**MODIFIED** (if WS-only confirmed): `PluginManager` spawn logic (add a "native exe on non-Windows" path as a first try before Wine).

______________________________________________________________________

## Scalability and Component Isolation Considerations

The system runs on a single desktop computer with one to four concurrently connected AJAZZ devices and fewer than 20 installed plugins. Traditional software scalability (users, requests/second) is not relevant. The relevant scaling dimension is:

**Number of concurrently loaded plugins × number of bound key actions**: at 20 plugins × 15 keys × 4 pages = 1200 `ActionContext` entries in `ContextRegistry`. The dual-index `QHash` scales to this easily. No concerns.

**Multi-device isolation**: `ContextRegistry` already namespaces context IDs by `deviceId` (`deviceId#pageId#controller#row#col`), so two devices sharing the same grid geometry cannot collide.

**Encoder/touch provisional values**: the current PROVISIONAL values for encoder ticks polarity and touch zone geometry (from `akp05_input_corrections.md`) are correct in structure but unconfirmed against retail hardware. The system is designed so that changing these values is a one-file change in `StreamDockInputService::dispatch` (for gesture synthesis thresholds) or in `streamdock-host/src/main.rs` (for sidecar-side decode), not a cross-cutting refactor.

______________________________________________________________________

## Sources

- Existing source files read directly: `src/app/src/plugin_device_bridge.hpp`, `src/app/src/sd_plugin_server.hpp`, `src/app/src/stream_dock_control_service.hpp`, `src/app/src/stream_dock_input_service.hpp`, `src/app/src/builtin_actions_service.hpp`, `src/plugins/include/ajazz/plugins/i_plugin_host.hpp`, `src/core/include/ajazz/core/profile.hpp`, `src/app/src/property_inspector_controller.hpp` (HIGH confidence — live code).
- OpenDeck Rust source via `gh api` (HIGH confidence — live repository): `src-tauri/src/shared.rs` (ActionInstance, Profile, ActionState), `src-tauri/src/events/inbound/mod.rs` (InboundEventType enum, ownership validation), `src-tauri/src/events/outbound/will_appear.rs` (willAppear/willDisappear shape), `src-tauri/src/store/profiles.rs` (DeviceStores, ProfileStores, per-device selected_profile), `src-tauri/src/application_watcher.rs` (250 ms poll, APPLICATION_PROFILES map, switch_profile event).
- `.planning/opendeck-ui-plugin-study.md` (HIGH confidence — in-repo).
- `.planning/milestones/v1.3-phases/17-CONTEXT.md`, `19-CONTEXT.md`, `21-CONTEXT.md` (HIGH confidence — in-repo phase context documents).
- `.planning/codebase/ARCHITECTURE.md`, `.planning/codebase/STRUCTURE.md` (HIGH confidence — in-repo codebase map, refreshed 2026-06-02).
- `.planning/PROJECT.md` (HIGH confidence — in-repo project specification).

______________________________________________________________________

*Architecture research for: v2.0 Modular Plugin + Binding System (AJAZZ Control Center)*
*Researched: 2026-06-06*
