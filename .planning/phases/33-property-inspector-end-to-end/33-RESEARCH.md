# Phase 33: Property Inspector End-to-End - Research

**Researched:** 2026-06-08
**Domain:** Qt 6 WebEngine Property Inspector (PI) — `$SD` JS bridge, lifecycle events, settings persistence
**Confidence:** HIGH (every claim below is anchored to a read of the live code on `experiment/mirajazz`)

\<user_constraints>

## User Constraints (from CONTEXT.md)

### Locked Decisions

- **Research-first verify-vs-build.** First task is to determine what PI-01..04 already deliver
  against the live code and build ONLY the real gaps. Do not rebuild working machinery.
- **Per-plugin WebEngine isolation:** each plugin UUID gets its own `QQuickWebEngineProfile`.
- **cefQuery polyfill injected at `QWebEngineScript::DocumentCreation`** — the mandatory injection
  point; NEVER defer to `runJavaScript`.
- **Settings persistence is per-context AND plugin-wide global**, both round-trip through the bridge
  AND survive an app restart (persisted to disk). `didReceiveSettings` reaches the plugin (PI-03).
- **Full PI-04 event set:** `propertyInspectorDidAppear` on PI open, `propertyInspectorDidDisappear`
  on PI close, `titleParametersDidChange` immediately after `willAppear` is registered.
- **Headless verification path:** `plugin.simulatePiSettings` -> `didReceiveSettings` in
  `plugin.protocolLog`; PI panel open/close controls `objectName`-addressed (reachable via
  `qml.invoke`); `titleParametersDidChange` payload completeness asserted by a Catch2 unit test;
  `screenshot` confirms PI HTML rendered (not blank). ctest necessary but NOT sufficient.
- **The PI HUMAN-VERIFY checkpoint (criterion 5) is DEFERRED to end-of-phase.** Build +
  headless-verify everything first, then PAUSE and hand the user a concrete walk-through. Use a
  real `.sdPlugin` with a settings PI (not a synthetic test PI).
- **The thin UI contract:** the PI-panel open affordance + the PIWebView container must be
  `objectName`-addressable (`qml.invoke` open; `qml.get`/`screenshot` "not blank").

### Claude's Discretion

- Exact WebEngineProfile lifetime management, the precise gap set (determined by research), and PI
  panel control naming — at the executor's discretion, consistent with existing PI patterns.

### Deferred Ideas (OUT OF SCOPE)

- Per-app profile auto-switching -> Phase 34 (APROF-..).
- Full event-parity audit -> Phase 34 (EVENT-01).
- Windows-only plugin PI handling -> Phase 35.
  \</user_constraints>

\<phase_requirements>

## Phase Requirements

| ID                     | Description                                                                                                                                                              | Research Support                                                                                                                                                                                      |
| ---------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| PI-01                  | PI HTML renders in `QWebEngine` + `QQmlWebChannel` with `$SD`; cefQuery at `DocumentCreation`; `sdpi.css` from qrc; per-plugin `QQuickWebEngineProfile` isolation        | **GREEN** — all four sub-claims verified in live code (see status table). Only a UI affordance + objectName gap remains, which is PI-02 territory.                                                    |
| PI-02                  | Device-canvas affordance opens PI for the selected action (`loadInspector` wired from QML; control carries `objectName`)                                                 | **PARTIAL** — `loadInspector` is wired (Inspector.qml:112) but auto-triggered by selection, not a discrete affordance, and NO `objectName` exists on the PI panel/container. This is the main UI gap. |
| PI-03                  | `sendToPlugin`/`sendToPropertyInspector` relay; per-context + global `getSettings`/`setSettings` round-trip and survive restart; `didReceiveSettings` reaches the plugin | **GREEN** — full relay + persistence + restart round-trip + plugin notify all present and unit-tested. Only the human-verify (real PI JS) is deferred by design.                                      |
| PI-04                  | `propertyInspectorDidAppear` on open, `propertyInspectorDidDisappear` on close, `titleParametersDidChange` after `willAppear`                                            | **MISSING** — none of the three events are emitted anywhere in `src/`. This is the core build work of the phase.                                                                                      |
| \</phase_requirements> |                                                                                                                                                                          |                                                                                                                                                                                                       |

## Summary

This phase is **~70% already built**. The PI machinery (WebEngine host, `$SD` bridge, cefQuery
shim, per-plugin profile isolation, URL policy, settings persistence with restart round-trip, the
PI->plugin and plugin->PI relays, the `simulatePiSettings` debug RPC) is all present, wired into
`Application`, and unit-tested. WebEngine is **enabled in the live `linux-release` binary**
(confirmed by `ldd`), so the human-verify checkpoint can actually render PI HTML.

The **real gaps are two, both small and surgical:**

1. **PI-04 lifecycle events (MISSING — the bulk of the work).** `propertyInspectorDidAppear`,
   `propertyInspectorDidDisappear`, and `titleParametersDidChange` are emitted **nowhere** in
   `src/`. The only occurrence in the whole tree is `titleParametersDidChange` as a string literal
   in `tests/unit/test_sd_plugin_server.cpp:486` (a generic event-passthrough test, NOT a PI-04
   assertion). All three need to be emitted at the existing well-understood seams.

1. **PI-02 thin-UI contract (PARTIAL — objectName + affordance).** The PI panel is auto-shown via
   a `Loader` gated on `hasHtmlInspector` (Inspector.qml:206-222); `loadInspector` is correctly
   wired (Inspector.qml:112). But **none of the four PI QML files carry a single `objectName`**, so
   the debug channel cannot drive open/close or screenshot the panel. This must be added for the
   headless success criteria.

**Primary recommendation:** Plan exactly two small tasks (PI-04 event emission + PI-02
objectName/affordance) plus a Catch2 assertion for `titleParametersDidChange` and the deferred
human-verify. Do NOT touch pi_bridge, pi_cef_shim, plugin_settings_store, the URL policy, or the
relay wiring — they are green and tested.

## Architectural Responsibility Map

| Capability                                  | Primary Tier                                                                             | Secondary Tier                                 | Rationale                                                                                                  |
| ------------------------------------------- | ---------------------------------------------------------------------------------------- | ---------------------------------------------- | ---------------------------------------------------------------------------------------------------------- |
| Render PI HTML, inject `$SD` + cefQuery     | App / WebEngine (`PropertyInspectorController`)                                          | QML (`PIWebView.qml`)                          | Chromium page + user-script injection is C++; QML only binds the trio profile/channel/url.                 |
| Settings persistence (per-context + global) | App (`plugin_settings_store`)                                                            | —                                              | Pure Qt-Core disk I/O behind the bridge; no QML or device tier involved.                                   |
| PI->plugin / plugin->PI relay               | App (`Application` wiring + `SdPluginServer`)                                            | —                                              | The relay is an app-layer signal/slot fan-out to the WebSocket.                                            |
| PI-04 lifecycle events                      | App (`PluginDeviceBridge` for title; `PropertyInspectorController` for appear/disappear) | —                                              | The willAppear emitter owns title; the PI controller owns the panel open/close lifecycle.                  |
| PI panel open/close affordance              | QML (`Inspector.qml`)                                                                    | App (`PropertyInspectorController` invokables) | The selection-driven Loader + a debug-addressable control are pure QML; they call existing C++ invokables. |

## Standard Stack

No new external packages. This phase is entirely in-tree C++/QML against the already-linked Qt 6
WebEngine stack. **No `## Package Legitimacy Audit` needed — zero external installs.**

### Core (already present, do not change)

| Component                                      | Location                                              | Purpose                                                              | Status                |
| ---------------------------------------------- | ----------------------------------------------------- | -------------------------------------------------------------------- | --------------------- |
| `Qt6::WebEngineQuick` + `Qt6::WebChannelQuick` | linked in app                                         | PI HTML host + `$SD` channel                                         | GREEN, in live binary |
| `PropertyInspectorController`                  | `src/app/src/property_inspector_controller.{hpp,cpp}` | PI lifecycle owner, per-plugin profile, loadInspector/closeInspector | GREEN                 |
| `PIBridge` (`$SD`)                             | `src/app/src/pi_bridge.{hpp,cpp}`                     | SDK-2 JS surface (setSettings/getSettings/sendToPlugin/...)          | GREEN                 |
| `plugin_settings_store`                        | `src/app/src/plugin_settings_store.cpp`               | per-context + global on-disk JSON (QSaveFile atomic)                 | GREEN                 |
| `makeCefQueryShim()`                           | `src/app/src/pi_cef_shim.cpp`                         | cefQuery polyfill at DocumentCreation                                | GREEN                 |

**Version verification:** No registry packages to verify. Qt version is pinned by the existing
`static_assert(QT_VERSION >= QT_VERSION_CHECK(6, 7, 0), ...)` in
`property_inspector_controller.cpp:52`.

______________________________________________________________________

## PI-01..04 STATUS TABLE (the core deliverable)

### PI-01 — PI HTML renders with `$SD`, cefQuery@DocumentCreation, sdpi.css, per-plugin profile → **GREEN**

| Sub-claim                                                 | Status                                        | Anchor                                                                                                                                                                                                                                                       |
| --------------------------------------------------------- | --------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| PI HTML renders in `QWebEngineView`                       | GREEN                                         | `PIWebView.qml:62-70` binds `profile`/`webChannel`/`url`; `WebEngine` linked in live binary (`ldd` shows `libQt6WebEngineQuick.so.6`).                                                                                                                       |
| `$SD` registered on a `QQmlWebChannel`                    | GREEN                                         | `property_inspector_controller.cpp:245-247` — `new QQmlWebChannel`, `new PIBridge`, `channel->registerObject("$SD", bridge)`.                                                                                                                                |
| cefQuery polyfill at `QWebEngineScript::DocumentCreation` | **GREEN — injection point CONFIRMED CORRECT** | `pi_cef_shim.cpp:45` `script.setInjectionPoint(QWebEngineScript::DocumentCreation)`; `:49` `MainWorld`; `:53` `setRunsOnSubFrames(true)`. Inserted into the per-plugin profile's userScripts at `property_inspector_controller.cpp:231` (NOT runJavaScript). |
| Mirabox shim aliased                                      | GREEN                                         | `property_inspector_controller.cpp:232` `profile->userScripts()->insert(makeMiraboxShim())`.                                                                                                                                                                 |
| `sdpi.css` served from a bundled qrc                      | GREEN                                         | `pi_url_request_interceptor.cpp` / `pi_url_policy.cpp` serve it; resource at `:/qt/qml/AjazzControlCenter/streamdock/sdpi.css`, asserted present+non-empty by `tests/unit/test_pi_bridge.cpp:682`.                                                           |
| Per-plugin `QQuickWebEngineProfile` isolation             | GREEN                                         | `property_inspector_controller.cpp:106-128` `WebEngineImpl::profilesByPluginUuid` (one profile per plugin UUID), created lazily at `:188-198`; one URL interceptor per profile at `:207-213`.                                                                |

**PI-01 verdict: GREEN. No build work.** (The shim-injection point the CONTEXT flags for re-check
is correct — do not change it; `pi_cef_shim.cpp:43` carries the "do not change" Pitfall-4 note.)

### PI-02 — device-canvas affordance opens PI; `loadInspector` wired; control has `objectName` → **PARTIAL**

| Sub-claim                                                 | Status            | Anchor                                                                                                                                                                                                                                                                             |
| --------------------------------------------------------- | ----------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `loadInspector` wired from QML                            | GREEN             | `Inspector.qml:112` `PropertyInspectorController.loadInspector(pluginUuid, piAbs, actionId, ctx)`; `closeInspector()` at `:86,93,101,109`. Triggered on `onBindingChanged`/`onKeyIndexChanged`/`onEncoderIndexChanged` (`:142-147`).                                               |
| PI panel shown when a bound action with a PI is selected  | GREEN             | `Inspector.qml:206-222` `Loader { source: "PIWebView.qml"; visible: hasSelection && webEngineAvailable && hasHtmlInspector }`.                                                                                                                                                     |
| Wire context id matches the bridge byte-for-byte          | GREEN             | `Inspector.qml:121-139` `_contextUuid()` builds `device#root#Keypad#row#col` / `device#root#Encoder#0#col` matching `ContextRegistry::deriveContextId`.                                                                                                                            |
| **The PI affordance / container carries an `objectName`** | **MISSING (gap)** | `grep objectName` across `Inspector.qml`, `PIWebView.qml`, `PropertyInspector.qml`, `NativePropertyInspector.qml` returns **zero hits**. The debug channel (`findByName`) cannot address the panel -> cannot `qml.invoke` open/close, cannot `qml.get`/`screenshot` the container. |

**PI-02 verdict: PARTIAL.** The functional wiring is GREEN; the **thin-UI debug contract is the
gap.** See "Gap 2" below.

### PI-03 — relays + per-context/global round-trip + restart survival + `didReceiveSettings` to plugin → **GREEN**

| Sub-claim                                                   | Status | Anchor                                                                                                                                                                                                                                                            |
| ----------------------------------------------------------- | ------ | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `setSettings`/`getSettings` per-context persist             | GREEN  | `pi_bridge.cpp:258-299` route through `plugin_settings_store::writeContext`/`readContext`.                                                                                                                                                                        |
| `setGlobalSettings`/`getGlobalSettings` plugin-wide persist | GREEN  | `pi_bridge.cpp:301-344` (global.json, atomic). Store path: `plugin_settings_store.cpp:50-56,196-206`.                                                                                                                                                             |
| **Survives app restart** (disk persistence)                 | GREEN  | Atomic `QSaveFile` write to `<AppDataLocation>/plugins/<uuid>/settings/<ctx>.json` + `/global.json` (`plugin_settings_store.cpp:39-89`); **unit-tested across a fresh `PIBridge`** at `test_pi_bridge.cpp:224` ("survives a fresh PIBridge") and `:252` (global). |
| Per-context isolation                                       | GREEN  | `test_pi_bridge.cpp:279` "settings are isolated per context".                                                                                                                                                                                                     |
| `sendToPlugin` relay -> live plugin                         | GREEN  | `pi_bridge.cpp:346-365` emits `toPluginRequested`; `application.cpp:684-700` connects it to `SdPluginServer::sendEvent("sendToPlugin", ...)`.                                                                                                                     |
| `sendToPropertyInspector` (plugin->PI) relay                | GREEN  | `pi_bridge.cpp:251-256` `deliverToPropertyInspector` -> `sendToPropertyInspector` signal; `application.cpp:715-719` connects `relayToPropertyInspector`.                                                                                                          |
| `didReceiveSettings` reaches the plugin (PI->plugin half)   | GREEN  | `pi_bridge.cpp:289` emits `contextSettingsChanged`; `application.cpp:705-709` -> `PluginDeviceBridge::onPropertyInspectorSettings`; the bridge echoes `didReceiveSettings` at `plugin_device_bridge.cpp:543,1453`.                                                |
| Headless verify RPC                                         | GREEN  | `debug_control_facade.cpp:603-623` `plugin.simulatePiSettings` drives `onPropertyInspectorSettings`; observe `didReceiveSettings` in `plugin.protocolLog` (`:630-646`).                                                                                           |
| Path-traversal hardening                                    | GREEN  | `pi_bridge.cpp:76-104` + `plugin_settings_store.cpp:158-181`; `test_pi_bridge.cpp:304`.                                                                                                                                                                           |

**PI-03 verdict: GREEN. No build work.** The only outstanding item is the **deferred human-verify**
(real PI JS `$SD.setSettings()` + restart) — by design, not a gap.

### PI-04 — `propertyInspectorDidAppear` / `propertyInspectorDidDisappear` / `titleParametersDidChange` → **MISSING**

| Event                           | Status                   | Evidence of absence                                                                                                                                                                        |
| ------------------------------- | ------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| `propertyInspectorDidAppear`    | **MISSING**              | Zero emit sites in `src/`. Not in `sd_plugin_server.cpp`, not in `plugin_device_bridge.cpp`, not in `property_inspector_controller.cpp`.                                                   |
| `propertyInspectorDidDisappear` | **MISSING**              | Same — zero references in `src/` at all.                                                                                                                                                   |
| `titleParametersDidChange`      | **MISSING (as emitter)** | The ONLY occurrence in the tree is a string in `test_sd_plugin_server.cpp:486` inside a generic "host->plugin events arrive" passthrough test. It is **never emitted by production code.** |

**PI-04 verdict: MISSING. This is the phase's primary build work.** See "Gap 1" below.

______________________________________________________________________

## Gap 1 (PI-04 lifecycle events) — exact file + insertion points + pattern to follow

All three are **purely additive** emits using the existing `m_server->sendEvent(owner, eventEnvelope(...))`
pattern. **Important: `SdPluginServer::sendEvent` does NOT gate on an event-name allowlist**
(`sd_plugin_server.cpp:534-566` sends any event string), so no allowlist edit is needed — adding a
new event name "just works" on the wire.

### 1a. `titleParametersDidChange` — emit immediately after each `willAppear`

**File:** `src/app/src/plugin_device_bridge.cpp`
**Insertion points (3, one after each existing `willAppear` send):**

- Keys: after line **1174** (`m_server->sendEvent(owner, eventEnvelope("willAppear", ctx, instancePayload(ctx)))`).
- Encoders: after line **1217**.
- Touch zones: after line **1266**.

**Pattern to follow (already in the file):** build a payload via a small `titlePayload(ctx)` helper
(model it on `instancePayload`, `plugin_device_bridge.cpp:311-328`) and send via the existing
`eventEnvelope` (`:335-343`). The SDK-2 `titleParametersDidChange` payload shape is:

```cpp
// payload = { "settings": {...}, "coordinates": {row,column}, "controller": "Keypad"|"Encoder",
//             "state": <int>, "title": "<string>",
//             "titleParameters": { "fontFamily":"", "fontSize":12, "fontStyle":"",
//                                  "fontUnderline":false, "showTitle":true,
//                                  "titleAlignment":"middle", "titleColor":"#ffffff" } }
```

`title`/`titleParameters` can be sourced from the binding's label (`ctx`) with sensible SDK defaults
— the **payload-completeness Catch2 test (criterion 4)** asserts every key above is present.
\[CITED: Elgato SDK-2 `titleParametersDidChange` event shape\] [ASSUMED: exact default values — confirm with the human-verify plugin if it reads them].

**Why "immediately after willAppear":** the CONTEXT and ROADMAP require the title event to follow
`willAppear` for the same context so a plugin that renders based on title parameters has them at
appear time. Emitting inline at the three sites above guarantees ordering with no extra plumbing.

### 1b. `propertyInspectorDidAppear` — emit when the PI panel opens

**Recommended owner:** `PropertyInspectorController::loadInspector`
(`property_inspector_controller.cpp:169-283`), at the end of the `AJAZZ_HAVE_WEBENGINE` branch
(after `:270 bridge->getSettings()`), routed to the plugin via the **existing app-layer seam**.

**Pattern:** `loadInspector` already emits `activeBridgeChanged(bridge)` (`:256`) which Application
captures (`application.cpp:684-710`). The cleanest wiring is to add a new controller signal
(e.g. `inspectorOpened(pluginUuid, actionUuid, contextUuid)` / `inspectorClosed(...)`) and connect
it in `Application` to `m_pluginServer->sendEvent(uuid, "propertyInspectorDidAppear", payload)` /
`"propertyInspectorDidDisappear"`. The payload is the SDK-2 envelope `{action, context, device}`
(reuse the shape from `eventEnvelope`). This keeps the controller free of a raw `SdPluginServer*`
(matches the existing `toPluginRequested`/`contextSettingsChanged` indirection rationale,
`pi_bridge.hpp:159-170`).

Alternative (executor discretion): emit directly from `PluginDeviceBridge` if a context-id->plugin
resolution already exists there — but the controller is the natural lifecycle owner.

### 1c. `propertyInspectorDidDisappear` — emit when the PI panel closes

**File:** `PropertyInspectorController::closeInspector` (`property_inspector_controller.cpp:285-301`),
inside the `webEngine_->activeChannel != nullptr` branch before teardown (so the plugin/context
identity is still known). Same signal->Application->`sendEvent` seam as 1b.

**Edge case to handle:** `loadInspector` for a *new* action tears down the previous channel
(`:239-243`) without calling `closeInspector`. To fire `didDisappear` for the outgoing PI on a
switch, emit the `inspectorClosed` signal in that teardown block too (capture the previous
pluginUuid/context before nulling). Otherwise switching directly between two PIs would skip the
disappear for the first. \[ASSUMED: this ordering matters to plugins — confirm at human-verify; the
safe choice is to emit it\].

______________________________________________________________________

## Gap 2 (PI-02 thin-UI contract) — objectName + open affordance

**File:** `src/app/qml/Inspector.qml` (and a one-liner in `PIWebView.qml`).

**What's missing:** zero `objectName`s. The debug channel addresses by `objectName` via `findByName`
(CLAUDE.md "Debug-channel verification"). Without it, the success criteria
("PI open control reachable via `qml.invoke`", "`qml.get`/`screenshot` not blank") cannot be met.

**Minimal surgical additions:**

1. On the `Loader` that hosts the PI (`Inspector.qml:206`): add `objectName: "piPanelLoader"`.
1. On the root `Item` in `PIWebView.qml:59` (and/or the `WebEngineView` at `:62`): add
   `objectName: "piWebView"` so `screenshot` + `qml.get` can target the rendered container.
1. Add an explicit **open affordance** the debug channel can `qml.invoke`. Today the PI auto-opens
   on selection (no discrete button). Add a tiny `objectName`-bearing control (e.g. a Button or a
   `Q_INVOKABLE`-backed action) that calls `PropertyInspectorController.loadInspector(...)` for the
   current selection, named e.g. `objectName: "openPiButton"`. Note the **harness gap** (CLAUDE.md):
   `qml.invoke toggle`/`click` emit `clicked()` but not `Switch.toggled` side-effects — so prefer a
   plain `Button` (its `clicked()` runs `onClicked`) or expose a dedicated `Q_INVOKABLE` on the
   controller (e.g. `openForCurrentSelection()`), which is the most robust debug-drivable path.

**Exact objectName(s) to land (present: NONE; to add):**

| objectName                                                             | On                                              | Purpose (debug criterion)                                                     |
| ---------------------------------------------------------------------- | ----------------------------------------------- | ----------------------------------------------------------------------------- |
| `openPiButton` (or a controller `Q_INVOKABLE openForCurrentSelection`) | `Inspector.qml`                                 | criterion 1/3: `qml.invoke` opens the PI; drives `propertyInspectorDidAppear` |
| `piPanelLoader`                                                        | `Inspector.qml:206` Loader                      | criterion 1: `qml.get` "active/visible" to confirm panel shown                |
| `piWebView`                                                            | `PIWebView.qml:59/62` root Item / WebEngineView | criterion 1: `screenshot` target -> confirm "not blank"                       |
| `closePiButton` (or controller `Q_INVOKABLE`)                          | `Inspector.qml`                                 | criterion 3: `qml.invoke` close -> drives `propertyInspectorDidDisappear`     |

(Naming is at executor discretion per CONTEXT; the table is the recommended set.)

## Architecture Patterns

### System Architecture Diagram (PI data flow — already built except the dashed edges)

```
 PI HTML page (plugin-authored)
   | window.cefQuery({request})        [cefQuery shim @ DocumentCreation, MainWorld]
   v
 QWebChannel "$SD"  ── PIBridge (per page, per context)
   |  setSettings/getSettings/setGlobalSettings/sendToPlugin/...
   v
 plugin_settings_store  (disk: <AppData>/plugins/<uuid>/settings/<ctx>.json + global.json)  [survives restart]
   |  contextSettingsChanged
   v
 Application wiring (activeBridgeChanged seam) ── PluginDeviceBridge::onPropertyInspectorSettings
   |  sendEvent("didReceiveSettings")           SdPluginServer::sendEvent (NO event allowlist)
   v
 live plugin process (WebSocket)

 == GAPS (dashed) ==
 PropertyInspectorController.loadInspector  --(NEW signal)-->  Application  --(NEW)-->  sendEvent("propertyInspectorDidAppear")
 PropertyInspectorController.closeInspector --(NEW signal)-->  Application  --(NEW)-->  sendEvent("propertyInspectorDidDisappear")
 PluginDeviceBridge willAppear sites (x3)   --(NEW inline)-->  sendEvent("titleParametersDidChange")
 Inspector.qml / PIWebView.qml              --(NEW objectName x3-4 + open affordance)-->  debug channel addressable
```

### Component Responsibilities

| File                                      | Responsibility                   | Touch in Phase 33?                                                                                        |
| ----------------------------------------- | -------------------------------- | --------------------------------------------------------------------------------------------------------- |
| `pi_bridge.{hpp,cpp}`                     | `$SD` JS surface                 | NO (green)                                                                                                |
| `property_inspector_controller.{hpp,cpp}` | PI lifecycle, per-plugin profile | YES — add appear/disappear signals + emits (Gap 1b/1c); optionally an `openForCurrentSelection` invokable |
| `pi_cef_shim.cpp`                         | cefQuery shim                    | NO (green, injection point correct)                                                                       |
| `plugin_settings_store.cpp`               | settings disk I/O                | NO (green)                                                                                                |
| `plugin_device_bridge.cpp`                | willAppear emitter               | YES — add `titleParametersDidChange` after the 3 willAppear sends (Gap 1a)                                |
| `application.cpp`                         | PI wiring seam                   | YES — connect new appear/disappear signals to `sendEvent` (Gap 1b/1c)                                     |
| `Inspector.qml`, `PIWebView.qml`          | PI panel UI                      | YES — objectNames + open affordance (Gap 2)                                                               |
| `sd_plugin_server.cpp`                    | wire transport                   | NO (no allowlist; sends any event)                                                                        |

### Anti-Patterns to Avoid

- **Do NOT rebuild the bridge / store / shim / relay.** They are green + tested; re-touching them
  risks regressing the 700+ green ctest baseline.
- **Do NOT switch cefQuery to `runJavaScript`-after-load.** `pi_cef_shim.cpp:43` documents the
  Pitfall-4 race; DocumentCreation is mandatory and correct.
- **Do NOT add an event-name allowlist to `sendEvent`.** It intentionally forwards any event string.
- **Do NOT give the open affordance a `Switch`/`CheckBox`.** The debug harness can't reproduce
  `onToggled` (CLAUDE.md harness gap) — use a `Button` or a `Q_INVOKABLE`.

## Don't Hand-Roll

| Problem              | Don't Build                                | Use Instead                                                                  | Why                                                           |
| -------------------- | ------------------------------------------ | ---------------------------------------------------------------------------- | ------------------------------------------------------------- |
| Settings persistence | a new store                                | `plugin_settings_store::write/readContext`/`Global`                          | atomic QSaveFile + path hardening already done + tested       |
| PI->plugin notify    | direct `SdPluginServer*` in the controller | the `contextSettingsChanged`/`activeBridgeChanged` seam                      | preserves the audited security boundary (`pi_bridge.hpp:159`) |
| Event envelope       | bespoke JSON                               | `eventEnvelope()` + `instancePayload()` (`plugin_device_bridge.cpp:311-343`) | matches the exact Elgato SDK-2 shape real plugins parse       |

## Runtime State Inventory

Not a rename/refactor/migration phase. **Section omitted (greenfield-additive).**
(One adjacent note: PI settings persist to `<AppDataLocation>/plugins/<uuid>/settings/*.json` +
`global.json` — relevant only to the restart human-verify, not a migration.)

## Common Pitfalls

### Pitfall 1: titleParametersDidChange ordering

**What goes wrong:** emitting the title event before `willAppear` (or detached from it) means a
plugin that sizes its render from title params gets them late.
**How to avoid:** emit inline immediately after each of the 3 existing `willAppear` sends
(`plugin_device_bridge.cpp:1174/1217/1266`). **Warning sign:** a unit test asserting order fails.

### Pitfall 2: didDisappear skipped on PI-to-PI switch

**What goes wrong:** `loadInspector` tears down the previous channel (`:239-243`) without
`closeInspector`, so the outgoing PI never gets `propertyInspectorDidDisappear`.
**How to avoid:** emit the disappear signal in that teardown block too (Gap 1c edge case).

### Pitfall 3: un-named control invisible to the debug channel

**What goes wrong:** an affordance without `objectName` can't be `qml.invoke`d -> criterion 1/3
cannot be headlessly verified. **How to avoid:** the objectName table above; verify with
`scripts/ajazz-debug qml.tree`.

### Pitfall 4: assuming WebEngine isn't in the binary

**What goes wrong:** skipping the render human-verify because of the `AJAZZ_HAVE_WEBENGINE` gate.
**Reality:** it IS enabled in `build/linux-release` (`CMakeCache.txt:30 AJAZZ_BUILD_PROPERTY_INSPECTOR=ON`,
`:57 AJAZZ_HAVE_WEBENGINE=1`; `ldd` shows `libQt6WebEngineQuick.so.6`). The headless **test** path
uses the minimal-Qt fallback (`test_pi_bridge.cpp` exercises the Qt-Core-only persistence + policy +
shim-source paths without booting WebEngine); the **app** path renders real HTML.

## Code Examples

### Emitting titleParametersDidChange after willAppear (pattern, plugin_device_bridge.cpp)

```cpp
// Source pattern: existing willAppear send at plugin_device_bridge.cpp:1173-1174
m_server->sendEvent(owner, eventEnvelope(QStringLiteral("willAppear"), ctx, instancePayload(ctx)));
// NEW (immediately after, same ctx):
m_server->sendEvent(owner,
    eventEnvelope(QStringLiteral("titleParametersDidChange"), ctx, titlePayload(ctx)));
// where titlePayload(ctx) mirrors instancePayload() but adds "title" + "titleParameters" (SDK-2).
```

### Wiring appear/disappear via the existing Application seam (application.cpp)

```cpp
// Source pattern: application.cpp:684-710 (activeBridgeChanged seam).
QObject::connect(m_propertyInspector.get(),
    &PropertyInspectorController::inspectorOpened,   // NEW controller signal
    this, [this](QString uuid, QString action, QString ctx, QString device) {
        if (m_pluginServer) {
            m_pluginServer->sendEvent(uuid, QStringLiteral("propertyInspectorDidAppear"),
                QJsonObject{{"action",action},{"context",ctx},{"device",device}});
        }
    });
```

## State of the Art

| Old Approach                 | Current Approach                                | When Changed             | Impact                                                         |
| ---------------------------- | ----------------------------------------------- | ------------------------ | -------------------------------------------------------------- |
| `WebEngineView.page` binding | trio `profile`/`webChannel`/`url`               | M2 (PIWebView.qml:25-33) | `page` has no Q_PROPERTY (CLAUDE.md gotcha) — already migrated |
| cefQuery via runJavaScript   | `QWebEngineScript::DocumentCreation` userScript | 20-02                    | avoids the Pitfall-4 race — already in place                   |

## Assumptions Log

| #   | Claim                                                                                                                                                        | Section | Risk if Wrong                                                                                                                                 |
| --- | ------------------------------------------------------------------------------------------------------------------------------------------------------------ | ------- | --------------------------------------------------------------------------------------------------------------------------------------------- |
| A1  | SDK-2 `titleParametersDidChange` payload keys are `title`+`titleParameters{fontFamily,fontSize,fontStyle,fontUnderline,showTitle,titleAlignment,titleColor}` | Gap 1a  | A plugin reads a differently-named field; the Catch2 completeness test should lock the chosen shape, and the human-verify plugin confirms it. |
| A2  | Default titleParameters values (size 12, color #ffffff, alignment "middle")                                                                                  | Gap 1a  | Cosmetic only unless a plugin renders from them; confirm at human-verify.                                                                     |
| A3  | Plugins care about `didDisappear` on a PI->PI switch                                                                                                         | Gap 1c  | Emitting it is the safe superset; a plugin ignoring it is harmless.                                                                           |
| A4  | A `Button`/`Q_INVOKABLE` open affordance satisfies the debug-channel "open control" criterion                                                                | Gap 2   | If the harness still can't drive it, fall back to a controller `Q_INVOKABLE openForCurrentSelection()` (most robust).                         |

## Open Questions

1. **Where exactly should appear/disappear be emitted from — controller or PluginDeviceBridge?**

   - Known: the controller owns the open/close lifecycle and already has the bridge/context;
     Application has the `SdPluginServer*`.
   - Recommendation: new controller signals -> Application -> `sendEvent` (keeps the controller free
     of a raw server pointer, matching the existing `toPluginRequested` pattern). Executor discretion
     per CONTEXT.

1. **Does `titleParametersDidChange` need a fresh emit when a binding's label changes mid-session
   (not just at willAppear)?** ROADMAP criterion 4 only requires "after willAppear is registered."
   Live re-emit on label edit is NOT required by PI-04 and is Phase-34 event-parity territory — keep
   it out of scope.

1. **Restart human-verify plugin choice:** CONTEXT says use a real `.sdPlugin` with a settings PI
   (System Monitor / a weather plugin). Confirm which one is installed/available at checkpoint time
   (memory `project_plugin_install_demo_working` has the System Monitor install recipe).

## Environment Availability

| Dependency                            | Required By                          | Available                                | Version                                      | Fallback                                                              |
| ------------------------------------- | ------------------------------------ | ---------------------------------------- | -------------------------------------------- | --------------------------------------------------------------------- |
| Qt6 WebEngineQuick                    | PI HTML render (PI-01, human-verify) | YES                                      | linked in `build/linux-release` binary (ldd) | — (PI gated off without it, but it's present)                         |
| `scripts/ajazz-debug` channel         | headless verify (criteria 1-4)       | YES (in-tree)                            | —                                            | none needed                                                           |
| A real `.sdPlugin` with a settings PI | human-verify (criterion 5)           | likely (System Monitor recipe in memory) | —                                            | install via `plugin.installFromFile` (`debug_control_facade.cpp:648`) |

**No blocking missing dependencies.**

## Validation Architecture

### Test Framework

| Property    | Value                                                                  |
| ----------- | ---------------------------------------------------------------------- |
| Framework   | Catch2 (ctest) + `scripts/ajazz-debug` live channel                    |
| Config file | `ctest --preset linux-release` (CLAUDE.md)                             |
| Quick run   | `ctest --preset linux-release -R "pi-bridge\|plugin-server"`           |
| Full suite  | `ctest --preset linux-release` (~727 green baseline at Phase 32 close) |
| Filter flag | `-R` / `--tests-regex` (NOT `--test-regex` — CLAUDE.md)                |

### Phase Requirements → Test Map

| Req   | Behavior                                                   | Test Type | Command / Method                                                                                                           | Exists?                              |
| ----- | ---------------------------------------------------------- | --------- | -------------------------------------------------------------------------------------------------------------------------- | ------------------------------------ |
| PI-01 | cefQuery@DocumentCreation, sdpi.css, isolation             | unit      | `ctest ... -R pi-bridge` (`test_pi_bridge.cpp:419,682`)                                                                    | ✅                                   |
| PI-02 | loadInspector wired; PI panel renders not-blank            | live      | `ajazz-debug qml.invoke openPiButton` -> `qml.get piPanelLoader` -> `screenshot piWebView`                                 | ❌ Wave 0 (objectNames + affordance) |
| PI-03 | per-context+global round-trip + restart                    | unit      | `ctest ... -R "pi-bridge"` (`:224,252,279`)                                                                                | ✅                                   |
| PI-03 | didReceiveSettings reaches plugin (headless)               | live      | `ajazz-debug plugin.simulatePiSettings {...}` -> `plugin.protocolLog` shows OUT didReceiveSettings                         | ✅ (RPC exists)                      |
| PI-04 | titleParametersDidChange payload completeness              | unit      | NEW Catch2 in `test_plugin_device_bridge.cpp` (or `test_sd_plugin_server.cpp`) asserting all payload keys after willAppear | ❌ Wave 0                            |
| PI-04 | propertyInspectorDidAppear on open / DidDisappear on close | live      | `ajazz-debug qml.invoke openPiButton` then `closePiButton` -> `plugin.protocolLog` shows both events                       | ❌ Wave 0 (emit + objectNames)       |
| PI-03 | real PI JS `$SD.setSettings()` + survives restart          | manual    | **DEFERRED human-verify** (open real PI, edit, restart, confirm)                                                           | deferred by design                   |

### Sampling Rate

- **Per task commit:** `ctest --preset linux-release -R "pi-bridge"` (fast).
- **Per wave merge:** full `ctest --preset linux-release`.
- **Phase gate:** full suite green + the live debug-channel pass (criteria 1-4) before
  `/gsd:verify-work`; then the deferred human-verify (criterion 5).

### Wave 0 Gaps

- [ ] Catch2 test asserting `titleParametersDidChange` payload completeness (every SDK-2 key) and
  that it follows `willAppear` for the same context — covers PI-04.
- [ ] `objectName`s (`openPiButton`/`closePiButton`/`piPanelLoader`/`piWebView`) + an open affordance
  (Button or `Q_INVOKABLE`) — covers PI-02 thin-UI contract.
- [ ] (No new framework install — Catch2 + debug channel already present.)

## Security Domain

> `security_enforcement` not found as `false` in config — included.

### Applicable ASVS Categories

| ASVS Category       | Applies | Standard Control                                                                                                                                    |
| ------------------- | ------- | --------------------------------------------------------------------------------------------------------------------------------------------------- |
| V5 Input Validation | yes     | settings payload parse + size cap (`pi_bridge.cpp:51,308-322`); uuid/context sanitiser (`plugin_settings_store.cpp:158-181`) — GREEN, do not weaken |
| V12 File / Resource | yes     | path-traversal refusal + per-plugin dir scoping (`isSafeUuidComponent`/`isSafeComponent`) — GREEN                                                   |
| V14 Config / SSRF   | yes     | URL request interceptor + CDN allowlist + nav guard (`pi_url_policy.cpp`, `PIWebView.qml:87-94`) — GREEN                                            |
| V6 Cryptography     | no      | n/a                                                                                                                                                 |

### Known Threat Patterns for the PI stack

| Pattern                                   | STRIDE                    | Standard Mitigation                                                | Status                |
| ----------------------------------------- | ------------------------- | ------------------------------------------------------------------ | --------------------- |
| Hostile PI path-traversal write           | Tampering                 | uuid/context sanitiser refuses `..`, slashes, control/non-ASCII    | GREEN (tested `:304`) |
| Disk-fill via setSettings/logMessage loop | DoS                       | 1 MiB settings cap, 64 KiB log cap (`pi_bridge.cpp:51-60`)         | GREEN                 |
| PI redirect / SSRF to arbitrary URL       | Tampering/Info-disclosure | per-profile interceptor + nav deny-by-default (`PIWebView.qml:87`) | GREEN                 |
| openUrl abuse                             | Spoofing                  | https-only allowlist + audit log (`pi_bridge.cpp:383-430`)         | GREEN                 |

**Phase 33 adds no new attack surface** — the new emits go host->plugin (outbound, plugin already
trusted on its own socket); the new QML objectNames/affordance call existing hardened invokables.
**Do not weaken any control above when adding the gaps.**

## Sources

### Primary (HIGH confidence — live code read this session)

- `src/app/src/pi_bridge.{hpp,cpp}`, `property_inspector_controller.{hpp,cpp}`, `pi_cef_shim.cpp`,
  `plugin_settings_store.cpp`, `plugin_device_bridge.cpp`, `application.cpp`,
  `debug_control_facade.cpp`, `sd_plugin_server.cpp` — exact line anchors throughout.
- `src/app/qml/{Inspector,PIWebView,PropertyInspector,NativePropertyInspector}.qml` — objectName
  audit (zero hits).
- `tests/unit/test_pi_bridge.cpp`, `tests/unit/test_sd_plugin_server.cpp` — existing coverage map.
- `src/app/CMakeLists.txt` + `build/linux-release/CMakeCache.txt` + `ldd` — WebEngine enablement.
- `git log --all --grep="PI-0\|loadInspector\|simulatePiSettings"` — provenance (M1..M5 + Phase 20/29).

### Secondary (MEDIUM)

- Elgato Stream Deck SDK-2 event reference (`titleParametersDidChange` payload shape) — training
  knowledge, flagged `[ASSUMED]` in the Assumptions Log; lock via the Catch2 test + human-verify.

## Metadata

**Confidence breakdown:**

- PI-01/PI-03 GREEN verdicts: HIGH — every sub-claim has a file:line anchor + a passing unit test.
- PI-04 MISSING verdict: HIGH — exhaustive grep of `src/` confirms zero emit sites.
- PI-02 PARTIAL verdict: HIGH — zero objectNames confirmed; loadInspector wiring confirmed.
- titleParametersDidChange payload shape: MEDIUM — SDK-2 from training, to be locked by the test.

**Research date:** 2026-06-08
**Valid until:** 2026-07-08 (stable in-tree code; re-grep if the PI files change before planning)
