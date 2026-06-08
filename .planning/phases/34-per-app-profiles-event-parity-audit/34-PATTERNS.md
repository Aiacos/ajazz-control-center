# Phase 34: Per-App Profiles + Event-Parity Audit - Pattern Map

**Mapped:** 2026-06-08
**Files analyzed:** 18 (8 new, 7 modified, 1 vendored XML, 2 doc/CMake)
**Analogs found:** 16 / 18 (2 NEW: Wayland protocol binding, vendored XML — no in-repo analog)

> Both tracks are mostly *wiring existing machinery* (RESEARCH "Key insight"). The genuinely new
> code is the four watcher backends + the Wayland protocol binding; everything downstream
> (lifecycle, event delivery, profile activation) already exists and must be **reused via the
> named analogs below**, not re-implemented.

______________________________________________________________________

## File Classification

| New/Modified File                                                                                  | New?     | Role                          | Data Flow             | Closest Analog                                                                                     | Match Quality                |
| -------------------------------------------------------------------------------------------------- | -------- | ----------------------------- | --------------------- | -------------------------------------------------------------------------------------------------- | ---------------------------- |
| `src/core/include/ajazz/core/active_window_watcher.hpp`                                            | NEW      | interface/header              | event-driven          | `src/core/include/ajazz/core/input_synthesizer.hpp`                                                | exact (role+flow)            |
| `src/core/src/active_window_watcher_stub.cpp` (or `src/app/src/`)                                  | NEW      | service (stub)                | event-driven          | `src/core/src/input_synthesizer_stub.cpp`                                                          | exact                        |
| `src/app/src/active_window_watcher_wayland.cpp`                                                    | NEW      | service (OS backend)          | event-driven          | NONE in-repo (new Qt Wayland client) + `input_synthesizer_linux.cpp` (gated-TU shell)              | partial                      |
| `src/app/src/active_window_watcher_x11.cpp`                                                        | NEW      | service (OS backend)          | event-driven          | `src/core/src/input_synthesizer_linux.cpp`                                                         | role-match (gated TU)        |
| `src/app/src/active_window_watcher_win.cpp`                                                        | NEW      | service (OS backend)          | event-driven          | `src/core/src/input_synthesizer_win.cpp`                                                           | exact (gated TU)             |
| `src/app/src/active_window_watcher_mac.mm`                                                         | NEW      | service (OS backend, Obj-C++) | event-driven          | `src/core/src/input_synthesizer_mac.cpp`                                                           | role-match (.mm)             |
| `src/app/wayland/wlr-foreign-toplevel-management-unstable-v1.xml`                                  | NEW      | config (vendored protocol)    | n/a                   | NONE (vendored from swaywm/wlr-protocols)                                                          | no analog                    |
| `docs/plugin-event-parity.md`                                                                      | NEW      | doc                           | n/a                   | `docs/protocols/streamdeck/akp_plugin_sdk.md`                                                      | role-match (event catalogue) |
| `src/app/qml/SettingsPage.qml` (assign-profile section)                                            | MODIFIED | component (QML)               | request-response (UI) | `src/app/qml/SettingsPage.qml` (self, section/Frame card) + `LoadedPluginsPage.qml:168-209` (chip) | exact                        |
| `tests/unit/active_window_watcher_test.cpp`                                                        | NEW      | test                          | event-driven          | `tests/unit/test_input_synth.cpp`                                                                  | exact                        |
| `tests/unit/app_profile_switch_test.cpp`                                                           | NEW      | test                          | event-driven          | `tests/unit/test_profile_*.cpp`                                                                    | role-match                   |
| `tests/unit/app_lifecycle_events_test.cpp`                                                         | NEW      | test                          | event-driven          | `tests/unit/test_stream_dock_input_service.cpp`                                                    | role-match                   |
| `tests/unit/willappear_payload_test.cpp`                                                           | NEW      | test                          | request-response      | `tests/unit/test_profile_*.cpp` + bridge fixture                                                   | role-match                   |
| `tests/unit/switch_to_profile_test.cpp` / `system_did_wake_test.cpp` / `controller_token_test.cpp` | NEW      | test                          | event-driven          | `tests/unit/test_input_synth.cpp` (factory/injection idiom)                                        | role-match                   |
| `src/app/src/sd_plugin_server.cpp`                                                                 | MODIFIED | service (WS dispatch)         | event-driven          | self (`switchToProfile` @424, `sendEvent` @534)                                                    | self                         |
| `src/app/src/plugin_device_bridge.cpp`                                                             | MODIFIED | service (lifecycle)           | event-driven          | self (reconcile pass @1141-1341)                                                                   | self                         |
| `src/app/src/application.cpp`                                                                      | MODIFIED | service (composition seam)    | event-driven          | self (actionReceived lambda @603-639, profileChanged wire @660)                                    | self                         |
| `src/app/src/profile_controller.{hpp,cpp}`                                                         | MODIFIED | controller                    | request-response      | self (Q_INVOKABLE surface)                                                                         | self                         |
| `src/app/src/debug_control_facade.cpp`                                                             | MODIFIED | service (RPC)                 | request-response      | self (`input.key` RPC @303)                                                                        | self                         |
| `src/core/CMakeLists.txt` / `src/app/CMakeLists.txt`                                               | MODIFIED | config (build)                | n/a                   | `src/core/CMakeLists.txt:24-31,73` (platform-split + feature gate)                                 | self                         |

______________________________________________________________________

## Pattern Assignments

### `src/core/include/ajazz/core/active_window_watcher.hpp` (interface, event-driven) — NEW

**Analog:** `src/core/include/ajazz/core/input_synthesizer.hpp` (the CANONICAL platform-split
interface). **COD-031:** keep it Qt-free and JSON-free — it deals in app-id strings only
(RESEARCH Anti-Patterns). Prefer a `std::function` callback seam over Qt signals so it can stay in
`ajazz_core` (RESEARCH Pattern 1 / A1: executor's discretion after a build spike).

**Value-type + bounded-enum pattern** (input_synthesizer.hpp:57-80):

```cpp
struct KeyChord {                              // -> struct ActiveWindowInfo {
    std::vector<std::uint32_t> modifiers;      //      std::string appId;     // app_id / WM_CLASS / image-base / bundle-id
    std::uint32_t key{0};                      //      std::string title;
};                                             //    };
enum class MediaKey : std::uint8_t { ... };    // (no enum needed; appId is the identity token)
```

**Interface + factory pattern** (input_synthesizer.hpp:89-162):

```cpp
class IInputSynthesizer {
public:
    virtual ~IInputSynthesizer() = default;
    virtual bool typeText(std::string_view utf8) = 0;
    // ...
};
[[nodiscard]] std::unique_ptr<IInputSynthesizer> makeDefaultInputSynthesizer();
```

For the watcher: `IActiveWindowWatcher` with `virtual void start(std::function<void(ActiveWindowInfo)> onChange) = 0;`,
`virtual void stop() = 0;`, `virtual bool capabilityAvailable() const = 0;` (drives the APROF-03 chip —
maps to `QWaylandClientExtension::isActive()`), and a `makeDefaultActiveWindowWatcher()` factory.

**COD-031 caveat (header line 9-11, verbatim project rule):** "The interface deliberately contains
NO Qt and NO nlohmann::json (COD-031 boundary)." Honor this — Qt (`QWaylandClientExtension`, `QTimer`)
lives in the **app-tier** impl TUs, not the interface.

______________________________________________________________________

### `src/core/src/active_window_watcher_stub.cpp` (service stub, event-driven) — NEW

**Analog:** `src/core/src/input_synthesizer_stub.cpp` — the recording-stub + factory-here pattern
that tests link against.

**Recording-stub class** (input_synthesizer_stub.cpp:54-104):

```cpp
class StubInputSynthesizer final : public IInputSynthesizer {
public:
    bool typeText(std::string_view utf8) override {
        std::string entry{"text:"}; entry.append(utf8);
        log_.push_back(entry);
        AJAZZ_LOG_INFO("input_synth", "stub typeText: {}", entry);
        return true;
    }
    [[nodiscard]] std::vector<std::string> const& log() const noexcept { return log_; }
    void clearLog() noexcept { log_.clear(); }
private:
    std::vector<std::string> log_;
};
```

Stub equivalent: a `StubActiveWindowWatcher` that records each injected foreground change to a
`log_`, reports `capabilityAvailable()==true` (or settable for the degradation test), and exposes a
**test seam** `injectForeground(ActiveWindowInfo)` that fires the stored `onChange` — this is the
"inject a synthetic foreground-app change" Wave-0 requirement (RESEARCH 580-582).

**Factory-defined-here + platform forward-decl** (input_synthesizer_stub.cpp:35-39, 108-114):

```cpp
#if defined(AJAZZ_FEATURE_INPUT_SYNTH)
std::unique_ptr<IInputSynthesizer> makePlatformInputSynthesizer();
#endif
// ...
std::unique_ptr<IInputSynthesizer> makeDefaultInputSynthesizer() {
#if defined(AJAZZ_FEATURE_INPUT_SYNTH)
    return makePlatformInputSynthesizer();
#else
    return std::make_unique<StubInputSynthesizer>();
#endif
}
```

Watcher factory must additionally pick wayland-vs-x11 **at runtime on Linux** via
`QGuiApplication::platformName()` / `XDG_SESSION_TYPE` (RESEARCH Pitfall 6) — NOT compile-time only.

______________________________________________________________________

### `src/app/src/active_window_watcher_{wayland,x11,win,mac}.cpp/.mm` (OS backends, event-driven) — NEW

**Analog (gated-TU shell):** `src/core/src/input_synthesizer_linux.cpp` — every per-OS TU compiles
unconditionally and is *self-empty* unless its platform `#if` (and feature gate) is satisfied.

**Self-emptying gated-TU header** (input_synthesizer_linux.cpp:26):

```cpp
#if defined(__linux__) && defined(AJAZZ_FEATURE_INPUT_SYNTH)
#include "ajazz/core/input_synthesizer.hpp"
#include "ajazz/core/logger.hpp"
// ... real backend ...
#endif   // whole TU empty otherwise — still compiles cleanly on every platform
```

For the watcher TUs:

- `_wayland.cpp` -> `#if defined(__linux__)` (Qt6::WaylandClient present); the Wayland-vs-X11 choice
  is **runtime** in the factory, so both Linux TUs guard on `__linux__`, not a session macro.
- `_x11.cpp` -> `#if defined(__linux__)` (links libX11; reads `_NET_ACTIVE_WINDOW` + `_NET_WM_PID`/`WM_CLASS`).
- `_win.cpp` -> `#if defined(Q_OS_WIN)` (`GetForegroundWindow` + `GetWindowThreadProcessId` + `QueryFullProcessImageNameW`).
- `_mac.mm` -> `#if defined(Q_OS_MACOS)` (Obj-C++; `NSWorkspace.frontmostApplication` + `didActivateApplicationNotification`).

**Wayland backend — NO in-repo analog (genuinely new).** Bind via
`QWaylandClientExtensionTemplate<T>` + the qtwaylandscanner-generated base (RESEARCH Pattern 2):

```cpp
class WlrToplevelManager
    : public QWaylandClientExtensionTemplate<WlrToplevelManager>,
      public QtWayland::zwlr_foreign_toplevel_manager_v1 {
public:
    WlrToplevelManager() : QWaylandClientExtensionTemplate(/*version*/ 3) {}
    // override _toplevel(handle) -> wrap; per-handle _state(array) -> detect
    //   ZWLR_..._STATE_ACTIVATED; _app_id(QString)/_title -> identity; _done()/_closed().
};
// connect(&mgr, &QWaylandClientExtension::activeChanged, ...) -> if (!mgr.isActive())
//   capabilityAvailable=false;  // APROF-03 warning chip
```

**Construct after QGuiApplication, on the GUI thread** (RESEARCH Pitfall 5) — invoke the factory
from Application init, never at static-init time. Debounce with a `QTimer` (RESEARCH: precedent
`HotplugDebouncer`, 300ms trailing-edge), ~150-250ms.

______________________________________________________________________

### `src/app/wayland/wlr-foreign-toplevel-management-unstable-v1.xml` (vendored config) — NEW, NO ANALOG

Copy the canonical XML from swaywm/wlr-protocols (RESEARCH Package Legitimacy Audit). Pin the source

- version in a leading comment. Not installed on this host (must be vendored). No in-repo precedent.

______________________________________________________________________

### `src/app/src/sd_plugin_server.cpp` (WS dispatch, event-driven) — MODIFIED

**Analog:** self. EVENT-03: `switchToProfile` is already routed (so it emits `actionReceived`) but
**no consumer handles it** — currently a silent no-op (RESEARCH Pattern 4). The handler goes in the
Application seam / bridge, NOT here; this file is already correct for routing.

**`switchToProfile` already in kRoutedActions** (sd_plugin_server.cpp:412-424):

```cpp
static constexpr std::array kRoutedActions = {
    // ...
    "switchToProfile",   // line 424 — routed (emits actionReceived) but unhandled downstream
};
```

**`sendEvent` has NO event-name allowlist** (sd_plugin_server.cpp:534, re-resolves socket per call —
Pitfall 4 / T-17-UAF guard). New OUTBOUND events (`applicationDidLaunch/Terminate`, `systemDidWakeUp`)
are **additive on the wire with zero server change**:

```cpp
bool SdPluginServer::sendEvent(QString const& targetUuid,    // :534 — re-resolves the live slot
                               QString const& event, QJsonObject const& payload);
// kRoutedActions gates INBOUND only (:478 loop); outbound sendEvent is ungated.
```

**Do NOT cache the resolved socket** in the new outbound paths — always call `sendEvent`
(comment at :512: "re-resolves the live slot each time").

______________________________________________________________________

### `src/app/src/plugin_device_bridge.cpp` (lifecycle, event-driven) — MODIFIED

**Analog:** self. EVENT-02 asserts the existing `willAppear` payload shape; APROF-02 **reuses** the
reconcile pass — do NOT re-implement willAppear/willDisappear (RESEARCH Pattern 3 / Don't Hand-Roll).

**willAppear payload + envelope builders (EVENT-02 asserts THIS exact shape)**
(plugin_device_bridge.cpp:311 `instancePayload`, :374 `eventEnvelope`, :1213 emit):

```cpp
QJsonObject instancePayload(ActionContext const& ctx, bool isInMultiAction = false); // :311
//   -> { settings, coordinates:{row,column}, controller, state, isInMultiAction }
QJsonObject eventEnvelope(QString const& event, ActionContext const& ctx, QJsonObject const& payload); // :374
//   -> { event, action(=ctx.actionUUID), context(=deriveContextId), device, payload }
m_server->sendEvent(owner,
    eventEnvelope(QStringLiteral("willAppear"), ctx, instancePayload(ctx)));  // :1213
```

EVENT-02 test asserts top-level `action`/`context`/`device`/`event` present AND
`payload.coordinates.row`/`column`, `payload.controller` (normalized), `payload.state`,
`payload.isInMultiAction` present.

**Reconcile pass for the APROF-02 lifecycle switch** (`populateContextsForActivePage` :1141; retire
pass :1322 — willDisappear + retire for any context no longer present):

```cpp
void PluginDeviceBridge::populateContextsForActivePage(QString const& deviceId, ...); // :1141
//   sends willAppear for incoming page (:1213/:1261/:1314 for key/encoder/touch contexts),
//   then diffs against the registry snapshot and sends willDisappear + retire (:1322-1341)
//   for outgoing contexts. This is already wired to profileChanged (application.cpp:660).
```

APROF-02: drive a profile activation through `ProfileController`; let the existing
`profileChanged -> populateContextsForActivePage` wire fire the lifecycle. Already carries the
CR WR-01 fix (no spurious didDisappear).

**EVENT-04 controller-token audit** — every emission site already uses `QStringLiteral("Encoder")`
(byCoord lookups :950/:968/:988/:1025; registration :1246/:1299; switch arms :1098/:1121). The audit
must **confirm no path emits `"Knob"` on the wire** (`"Knob"` should appear only in docs/manifest).

______________________________________________________________________

### `src/app/src/application.cpp` (composition seam, event-driven) — MODIFIED

**Analog:** self. Two seams to mirror:

**(1) INBOUND host-command dispatch (`switchToProfile` handler, EVENT-03)** — add a branch to the
existing `actionReceived` lambda (application.cpp:603-639), alongside `openUrl`/`logMessage`:

```cpp
QObject::connect(m_pluginServer.get(), &SdPluginServer::actionReceived, this,
    [](QString const& pluginUuid, QJsonObject const& action) {       // :607
        QString const event = action.value(QStringLiteral("event")).toString();
        QJsonObject const payload = action.value(QStringLiteral("payload")).toObject();
        if (event == QStringLiteral("openUrl")) { /* scheme-guarded */ }     // :610
        else if (event == QStringLiteral("logMessage")) { /* 2048-char cap */ } // :626
        // ADD: else if (event == "switchToProfile") -> map payload.profile -> ProfileController
    });
```

Note: the existing lambda captures nothing (`[]`); the new branch needs `m_profileController` —
capture `[this]` (mirror the `[this]` lambdas at :663 and :740). V5 input validation: bound/validate
the `profile`/`device` strings (RESEARCH Security V5).

**(2) OUTBOUND host-event fan-out (APROF-04, EVENT-03 systemDidWakeUp)** — mirror the PI-04 outbound
seam (application.cpp:737-748). The watcher / OS-wake source emits; Application calls `sendEvent`:

```cpp
QObject::connect(m_propertyInspector.get(),
    &PropertyInspectorController::inspectorOpened, this,
    [this](QString uuid, QString action, QString ctx) {
        if (m_pluginServer) {
            m_pluginServer->sendEvent(uuid, QStringLiteral("propertyInspectorDidAppear"),  // :742
                QJsonObject{{QStringLiteral("action"), action}, {QStringLiteral("context"), ctx}});
        }
    });
```

New outbound (additive, no allowlist):

```cpp
m_pluginServer->sendEvent(uuid, QStringLiteral("applicationDidLaunch"),
    QJsonObject{{QStringLiteral("application"), exePathOrAppId}});  // length-bound the path (V5)
m_pluginServer->sendEvent(uuid, QStringLiteral("systemDidWakeUp"), {}); // no payload per SDK
```

**V4 access control:** deliver applicationDid\* only to *registered* plugins (no broadcast) — RESEARCH
Security V4.

**(3) profileChanged -> lifecycle wire (APROF-02 reuse target)** (application.cpp:660-668) — the
auto-switch must route through this existing wire, NOT a new lifecycle path:

```cpp
QObject::connect(m_profileController.get(), &ProfileController::profileChanged,
    m_pluginBridge.get(), [this]() {                                    // :663
        if (!m_pluginBridge->activeDeviceId().isEmpty()) {
            m_pluginBridge->populateContextsForActivePage(m_pluginBridge->activeDeviceId());
        }
    });
```

The new `IActiveWindowWatcher` is constructed here (after QGuiApplication, main thread — Pitfall 5)
and its `onChange` callback feeds the debounce -> applicationHints match -> `ProfileController::activateDeviceProfile`.

______________________________________________________________________

### `src/app/src/profile_controller.{hpp,cpp}` (controller, request-response) — MODIFIED

**Analog:** self. The `switchToProfile` handler + the assign-profile UI both target existing
Q_INVOKABLEs — likely no new method needed beyond an applicationHints writer.

**Existing Q_INVOKABLE surface to reuse** (profile_controller.hpp):

```cpp
Q_INVOKABLE void loadProfileById(QString const& profileId);          // :106 — by id
Q_INVOKABLE void activateDeviceProfile(QString const& deviceCodename); // :153 — by device
[[nodiscard]] Q_INVOKABLE QVariantList profilesForDevice(QString const& deviceCodename) const; // :124
Q_INVOKABLE QString createProfile(QString const& name, QString const& deviceCodename);  // :133
```

`switchToProfile` handler maps the plugin's `profile` string to one of these (RESEARCH Open Q 3:
confirm name-vs-id addressing). The assign-profile UI needs a new Q_INVOKABLE to **write
`Profile::applicationHints`** (the field exists at profile.hpp:192; no schema change/migration —
RESEARCH Runtime State Inventory). Follow the existing `commitKeyBinding`/`commitEncoderBinding`
Q_INVOKABLE writer shape (profile_controller.hpp:214/:246).

______________________________________________________________________

### `src/app/src/debug_control_facade.cpp` (RPC, request-response) — MODIFIED

**Analog:** self — the `input.key` synthetic RPC (debug_control_facade.cpp:303). Add a
`window.setForeground` RPC that injects a synthetic foreground change into the watcher stub seam
(mirror `input.*`), for the APROF live-verification + the < 500ms latency check.

**RPC handler pattern** (debug_control_facade.cpp:303-312):

```cpp
server.registerMethod("input.key", [&app](QJsonObject const& params, QString& err) {
    auto* dbg = app.pluginDebug();
    if (dbg == nullptr) { err = QStringLiteral("plugin debug service unavailable"); return QJsonObject{}; }
    bool const pressed = params.value("pressed").toBool(true);
    dbg->simulateKey(params.value("index").toInt(), pressed);
    return QJsonObject{{"index", params.value("index").toInt()}, {"pressed", pressed}};
});
```

`window.setForeground`: `params.value("appId").toString()` -> watcher stub `injectForeground(...)` ->
return `{{"appId", appId}}`. Add the accessor (`app.activeWindowWatcher()` / `app.pluginDebug()`-style
seam) the same way `input.key` reaches `app.pluginDebug()`.

______________________________________________________________________

### `src/app/qml/SettingsPage.qml` (component, request-response) — MODIFIED

**Analog:** self (section + Frame card) for Surface 1; `LoadedPluginsPage.qml:168-209` for the chip.
See 34-UI-SPEC.md for the full token/copy/objectName contract.

**Section heading + Frame card** (SettingsPage.qml:46-64 — mirror EXACTLY):

```qml
Label {                                              // :46
    font.pixelSize: Theme.typeTitleMedium.pixelSize
    font.weight: Theme.typeTitleMedium.weight
    font.letterSpacing: Theme.typeTitleMedium.letterSpacing
    Accessible.role: Accessible.Heading              // :52
}
Frame {                                              // :55
    background: Rectangle {
        color: Theme.tile
        border.color: Theme.borderSubtle
        border.width: 1
        radius: Theme.radiusMd
    }
    ColumnLayout { /* rows */ }                       // :64
}
```

**Capability-warning chip** (LoadedPluginsPage.qml:168-209 — pill, amber/warning family ONLY):

```qml
Rectangle {
    id: trustChip
    visible: row.trustLevel !== "trusted"            // -> visible: !capabilityAvailable
    Layout.preferredHeight: 24
    radius: 12                                        // pill (not a Theme.radius* token)
    color: Theme.chipBgWarning                        // amber — degradation, NOT error red
    border.color: Theme.chipBorderWarning
    border.width: 1
    Text {
        anchors.centerIn: parent
        text: qsTr("self-signed")                     // -> qsTr("Limited on this desktop")
        color: Theme.chipFgWarning
        font.pixelSize: Theme.fontXs
        font.weight: Font.DemiBold
    }
    ToolTip.visible: chipMouseArea.containsMouse      // carry the full warning detail copy
    ToolTip.text: qsTr("...")
}
```

**objectName (VERIF-01 hard rule):** `waylandCapabilityWarningChip` (+ addressable `warningText`),
`addAppProfileMappingButton`, `appProfileAppNameField`, `appProfileProfileSelector`,
`removeAppProfileMappingButton`. Use plain `Button`/`SecondaryButton`, **NOT a `Switch`**
(`qml.invoke toggle` does not fire `onToggled` — CLAUDE.md harness gap).

______________________________________________________________________

### `tests/unit/active_window_watcher_test.cpp` + sibling test files (test, event-driven) — NEW

**Analog:** `tests/unit/test_input_synth.cpp` (factory + recording-stub + injection idiom);
`tests/unit/test_profile_*.cpp` for the profile-model assertions; Catch2 throughout.

Drive the watcher via the stub's `injectForeground(...)` seam (no real focus change). Assert:
debounce coalesces rapid changes to one emit (APROF-01); appId -> applicationHints match + default
fallback (APROF-02); willAppear envelope/payload completeness against `eventEnvelope`/`instancePayload`
(EVENT-02); `switchToProfile` dispatch activates a profile (EVENT-03); synthetic `systemDidWakeUp`
reaches a subscribed plugin (EVENT-03); no path emits `"Knob"` (EVENT-04, also grep-verifiable now).
Quick run: `ctest --preset linux-release -R active_window`.

______________________________________________________________________

### `src/core/CMakeLists.txt` / `src/app/CMakeLists.txt` (config, build) — MODIFIED

**Analog:** `src/core/CMakeLists.txt:24-31` (unconditional per-OS TU list) + `:73` (feature-gate block).

**Unconditional per-OS source list** (src/core/CMakeLists.txt:28-31):

```cmake
src/input_synthesizer_stub.cpp     # always-compiled stub (default factory)
src/input_synthesizer_linux.cpp    # self-empty unless platform #if + feature gate
src/input_synthesizer_win.cpp
src/input_synthesizer_mac.cpp
```

Add the watcher TUs the same way (stub in the source list; per-OS TUs unconditional). Wayland needs:

```cmake
find_package(Qt6 REQUIRED COMPONENTS WaylandClient)
qt_generate_wayland_protocol_client_sources(ajazz-control-center
    PRIVATE_CODE
    FILES ${CMAKE_CURRENT_SOURCE_DIR}/wayland/wlr-foreign-toplevel-management-unstable-v1.xml)
target_link_libraries(ajazz-control-center PRIVATE Qt6::WaylandClient)  # + libX11 on Linux
```

**Per-platform link branch** (mirror src/core/CMakeLists.txt:53-63 `if(Linux)/elseif(Darwin)/elseif(WIN32)`):
Linux -> `libX11` + `Qt6::WaylandClient`; macOS -> `-framework AppKit`; Windows -> `user32`.
**CI/Flatpak (planner build-env task):** add `qtwayland` (Qt6::WaylandClient), `wayland-devel`/
`wayland-protocols`, `libX11-devel` to the build environment(s) (RESEARCH Environment Availability).

______________________________________________________________________

## Shared Patterns

### Platform-split interface + per-OS TU (the spine of APROF-01)

**Source:** `src/core/include/ajazz/core/input_synthesizer.hpp` + `src/core/src/input_synthesizer_{stub,linux,win,mac}.cpp` + `src/core/CMakeLists.txt:24-31,73`
**Apply to:** all `active_window_watcher_*` files + the header + CMake.
Interface header (Qt-free, COD-031) -> `makeDefault*()` factory in the always-compiled `_stub.cpp` ->
per-OS TUs added unconditionally, each self-empty behind `#if platform [&& feature]`. Tests link the
stub. **Deviation for the watcher:** the factory must choose wayland-vs-x11 at *runtime* on Linux
(`QGuiApplication::platformName()`), unlike the synth's pure compile-time split (RESEARCH Pitfall 6).

### Host-level event delivery via SdPluginServer::sendEvent (no allowlist)

**Source:** `src/app/src/sd_plugin_server.cpp:534` (sendEvent) + `application.cpp:737-748` (PI-04 outbound seam)
**Apply to:** APROF-04 (`applicationDidLaunch`/`Terminate`), EVENT-03 (`systemDidWakeUp`).
`kRoutedActions` gates INBOUND only; outbound `sendEvent` is ungated -> new events are additive.
NEVER cache the `QWebSocket*` (re-resolves per call — Pitfall 4 / T-17-UAF). Deliver only to
registered plugins (V4 access control).

### Reconcile-pass lifecycle reuse (APROF-02 — do NOT re-implement)

**Source:** `src/app/src/plugin_device_bridge.cpp:1141` (populateContextsForActivePage) wired at `application.cpp:660`
**Apply to:** every profile auto-switch. Route through `ProfileController::activateDeviceProfile` ->
`profileChanged` -> the existing willDisappear(outgoing)+willAppear(incoming) reconcile. Carries the
CR WR-01 no-spurious-didDisappear fix.

### Composition seam via Application lambda (never raw-pointer coupling)

**Source:** `src/app/src/application.cpp:603-639` (inbound actionReceived) + `:737-748` (outbound)
**Apply to:** the `switchToProfile` handler, the watcher onChange callback, the OS-wake source.
Controllers stay free of a raw `SdPluginServer*`; Application (which owns it) does the `sendEvent`.

### Debug-channel addressability (VERIF-01 hard rule)

**Source:** `src/app/src/debug_control_facade.cpp:303` (input.key RPC) + QML `objectName` convention
**Apply to:** the new `window.setForeground` RPC AND every new QML control (objectNames per UI-SPEC).
Use plain `Button`/`SecondaryButton`, not `Switch` (harness `onToggled` gap, CLAUDE.md).

### Settings card + warning chip QML

**Source:** `src/app/qml/SettingsPage.qml:46-64` (section/Frame card) + `LoadedPluginsPage.qml:168-209` (amber pill chip)
**Apply to:** the assign-profile surface + capability-warning chip. All tokens from `Theme.*`
(no literals); chip is `Theme.chip*Warning` (amber), NOT error red — graceful degradation.

______________________________________________________________________

## No Analog Found

Files with no close in-repo match (planner uses RESEARCH.md patterns instead):

| File                                                                         | Role                       | Data Flow    | Reason                                   | Use Instead                                                                                                                                |
| ---------------------------------------------------------------------------- | -------------------------- | ------------ | ---------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------ |
| `src/app/src/active_window_watcher_wayland.cpp` (the Wayland *binding* core) | service/OS backend         | event-driven | No Qt Wayland client code exists in-repo | RESEARCH Pattern 2 (`QWaylandClientExtensionTemplate<T>` + qtwaylandscanner); gated-TU *shell* still mirrors `input_synthesizer_linux.cpp` |
| `src/app/wayland/wlr-foreign-toplevel-management-unstable-v1.xml`            | config (vendored protocol) | n/a          | No vendored Wayland protocol XML in-repo | Vendor canonical from swaywm/wlr-protocols (RESEARCH Package Legitimacy Audit)                                                             |

> The OS-wake source for `systemDidWakeUp` (RESEARCH A4 / Open Q 2) also has no in-repo analog — the
> *dispatch* path reuses the sendEvent seam, but the Linux logind `PrepareForSleep` D-Bus listen (or a
> stub everywhere with a synthetic-wake test) is new. RESEARCH recommends: implement dispatch + test
> now (inject a synthetic wake), wire the Linux source if cheap, stub Win/macOS behind the guard.

______________________________________________________________________

## Metadata

**Analog search scope:** `src/core/{include,src}/`, `src/app/src/`, `src/app/qml/`, `tests/unit/`, `docs/protocols/streamdeck/`, `src/core/CMakeLists.txt`
**Files scanned (verified live, not just RESEARCH-cited):** input_synthesizer.hpp, input_synthesizer\_{stub,linux}.cpp, core/CMakeLists.txt:20-74, profile.hpp:192, sd_plugin_server.cpp:327-582, plugin_device_bridge.cpp:311-1341, application.cpp:600-748, profile_controller.hpp, debug_control_facade.cpp:303-346, debug_control_qml.cpp, SettingsPage.qml, LoadedPluginsPage.qml:160-215, akp_plugin_sdk.md
**All RESEARCH-cited line numbers re-verified against the live tree on 2026-06-08** (input_synthesizer split, applicationHints@192, switchToProfile@424, sendEvent@534, instancePayload@311/eventEnvelope@374, willAppear@1213, Encoder byCoord sites, actionReceived@603/profileChanged@660, input.key@303, SettingsPage card@46-64, trust-chip@168-209). No drift found.
**Pattern extraction date:** 2026-06-08
