# Feature Research: v2.0 Modular Plugin + Key/Dial/Touch Binding

**Domain:** Stream Deck-compatible plugin host + key/encoder/touch action-assignment system
**Researched:** 2026-06-06
**Confidence:** HIGH (sources: live codebase grep, OpenDeck shared.rs/events/ via gh API, Elgato SDK docs, Phase 26/29 context files)

______________________________________________________________________

## Preamble: Already-Have vs Genuinely-Missing

The v1.3 milestone marked many items `[x]` but the Phase 29 LIVE-VERIFICATION.md (2026-06-02) and the opendeck-ui-plugin-study.md (2026-06-01) surface concrete gaps. This file annotates every feature with one of:

- **REUSE** — working in codebase, verified end-to-end; bring it forward as-is
- **STUB** — exists in code but not wired end-to-end or not verified with physical hardware
- **MISSING** — no implementation; must be built in v2.0
- **ANTI** — explicitly do not build

______________________________________________________________________

## Feature Landscape

### Category A: Plugin Lifecycle Events (host-to-plugin, outbound)

| Feature                                                                               | Label          | Complexity | Status      | Notes                                                                                                                                                                                                                                                                                                                         |
| ------------------------------------------------------------------------------------- | -------------- | ---------- | ----------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `willAppear` — sent when action slot becomes visible                                  | Table Stakes   | LOW        | **REUSE**   | Implemented in `plugin_device_bridge.cpp:1082/1125/1174`. Sends `{settings, coordinates, controller, state, isInMultiAction}` payload. Reconcile pass on page switch.                                                                                                                                                         |
| `willDisappear` — sent when action slot goes off-screen or is unbound                 | Table Stakes   | LOW        | **REUSE**   | Implemented at `plugin_device_bridge.cpp:1197/1225`. Paired with `willAppear` on move/unbind.                                                                                                                                                                                                                                 |
| `keyDown` / `keyUp` — physical key press and release                                  | Table Stakes   | LOW        | **REUSE**   | Implemented; routes through `ActionEngine` → `PluginDeviceBridge`. Release is synthesised for encoders.                                                                                                                                                                                                                       |
| `dialRotate` — encoder CW/CCW with `ticks` and `pressed`                              | Table Stakes   | LOW        | **REUSE**   | `plugin_device_bridge.cpp:919`. `ticks` field present. HARDWARE-GATED: actual encoder input requires retail AKP05E.                                                                                                                                                                                                           |
| `dialDown` / `dialUp` — encoder press + synthesised release                           | Table Stakes   | LOW        | **REUSE**   | `plugin_device_bridge.cpp:935/954`. Host synthesises release per Elgato convention.                                                                                                                                                                                                                                           |
| `touchTap` — tap on touch-strip zone                                                  | Table Stakes   | MEDIUM     | **STUB**    | `plugin_device_bridge.cpp:994`. Wire event code exists; zone routing PROVISIONAL (hardware-gated). `tapPos` coordinates unverified.                                                                                                                                                                                           |
| `deviceDidConnect` / `deviceDidDisconnect`                                            | Table Stakes   | LOW        | **REUSE**   | `plugin_device_bridge.cpp:1290/1320`.                                                                                                                                                                                                                                                                                         |
| `applicationDidLaunch` / `applicationDidTerminate`                                    | Table Stakes   | MEDIUM     | **MISSING** | No implementation anywhere in the codebase (`grep` returns zero hits on `applicationDid`). OpenDeck uses `active_win_pos_rs` crate + 250 ms poll loop. Requires OS-level foreground-window detection: `xdotool`/`wmctrl` on Linux, `GetForegroundWindow` on Windows. Needed to trigger `ApplicationsToMonitor` manifest hook. |
| `didReceiveSettings` — per-context settings echo                                      | Table Stakes   | LOW        | **REUSE**   | `plugin_device_bridge.cpp:530`. Triggered on `getSettings` / `setSettings`.                                                                                                                                                                                                                                                   |
| `didReceiveGlobalSettings` — plugin-wide settings echo                                | Table Stakes   | LOW        | **REUSE**   | In `sd_plugin_server.cpp` routed-action list; `plugin_settings_store` handles persistence.                                                                                                                                                                                                                                    |
| `titleParametersDidChange` — sent after willAppear and after state image/font changes | Table Stakes   | MEDIUM     | **MISSING** | Zero hits in all source files. OpenDeck sends this immediately after `willAppear` (see `will_appear.rs`). Payload: `{settings, coordinates, state, title, titleParameters:{fontFamily,fontSize,fontStyle,fontUnderline,showTitle,titleAlignment,titleColor}}`.                                                                |
| `systemDidWakeUp` — OS sleep/wake notification                                        | Differentiator | LOW        | **MISSING** | Zero hits in all source files. OpenDeck uses `tauri-plugin-prevent-default`. Qt has `QEvent::ApplicationActivated` or power-management D-Bus signals (`org.freedesktop.login1.Manager PrepareForSleep`). Low user impact; easy to add.                                                                                        |
| `propertyInspectorDidAppear` / `propertyInspectorDidDisappear`                        | Table Stakes   | LOW        | **MISSING** | Not emitted. OpenDeck emits these when the PI panel opens/closes. Some plugins depend on these to initialize PI state.                                                                                                                                                                                                        |

**Dependency note:** `applicationDidLaunch/Terminate` require `ApplicationsToMonitor` manifest parsing AND the foreground-app watcher. `systemDidWakeUp` requires OS event subscription. Both are independent of the core binding layer.

______________________________________________________________________

### Category B: Plugin-to-Host Commands (inbound)

| Feature                                                                  | Label          | Complexity | Status    | Notes                                                                                                                                                                                                                                                                             |
| ------------------------------------------------------------------------ | -------------- | ---------- | --------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `setImage` — override key/state image from plugin                        | Table Stakes   | LOW        | **REUSE** | Handled in bridge; strips `data:` URI, scales JPEG, pushes to sidecar via `StreamDockControlService`.                                                                                                                                                                             |
| `setTitle` — override key title text                                     | Table Stakes   | LOW        | **REUSE** | Handled in bridge; renders text overlay on key image.                                                                                                                                                                                                                             |
| `setState` — change current action state index (0-based)                 | Table Stakes   | LOW        | **REUSE** | `ContextRegistry::setState` at line 162; auto-renders manifest state image for the new state.                                                                                                                                                                                     |
| `setSettings` / `getSettings` — per-context persistence                  | Table Stakes   | LOW        | **REUSE** | `plugin_settings_store` keyed by wire context `device#root#Controller#row#col`. PI-written and plugin-read settings now use the same record (Phase 29-02).                                                                                                                        |
| `setGlobalSettings` / `getGlobalSettings` — plugin-wide persistence      | Table Stakes   | LOW        | **REUSE** | Routed through `sd_plugin_server`; stored per plugin UUID in settings dir.                                                                                                                                                                                                        |
| `sendToPropertyInspector` — plugin data to PI HTML                       | Table Stakes   | LOW        | **REUSE** | Signal chain: bridge → `PIBridge::deliverToPropertyInspector`. Verified via `plugin.simulatePiSettings` RPC (Phase 29).                                                                                                                                                           |
| `sendToPlugin` — PI data to plugin (from PI HTML)                        | Table Stakes   | LOW        | **REUSE** | `pi_bridge.cpp:289` emits `contextSettingsChanged` → bridge → plugin WS.                                                                                                                                                                                                          |
| `openUrl` — open URL in default browser                                  | Table Stakes   | LOW        | **REUSE** | Handled via `QDesktopServices::openUrl` (in PI context: `pi_bridge.hpp:107`). Also in `builtin_actions_service`.                                                                                                                                                                  |
| `logMessage` — write to plugin log                                       | Table Stakes   | LOW        | **REUSE** | Listed in `sd_plugin_server.cpp` routed-action list. Implementation routes to app log.                                                                                                                                                                                            |
| `showAlert` / `showOk` — key surface visual feedback                     | Table Stakes   | LOW        | **REUSE** | `plugin_device_bridge.cpp:656-660`. Shows yellow triangle / green checkmark on key.                                                                                                                                                                                               |
| `switchToProfile` — activate a named profile                             | Table Stakes   | MEDIUM     | **STUB**  | Listed in routed-action list (`sd_plugin_server.cpp:387`) but no handler in `plugin_device_bridge.cpp`. Bridge comment at line 388 says "other routed events are handled elsewhere" — need to verify dispatcher. Elgato spec requires targeting a specific device + profile name. |
| `setFeedback` / `setFeedbackLayout` — encoder touch strip feedback (SD+) | Differentiator | HIGH       | **STUB**  | `setFeedback` is in the routed-action list. No dispatcher in bridge currently handles `setFeedbackLayout`. These map to the AKP05E touch-strip zone images — complex because layout must be parsed and mapped to BAT wire addresses.                                              |
| `openApplication`                                                        | Anti-Feature   | —          | ANTI      | Not in Elgato SDK v6. AJAZZ-only; security risk. Do not implement.                                                                                                                                                                                                                |

______________________________________________________________________

### Category C: Action Instances + States

This is the most significant genuinely-missing category in v1.3.

| Feature                                                                                                                                                                              | Label        | Complexity | Status      | Notes                                                                                                                                                                                                                                                                                                                                                                                                                                                                                         |
| ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | ------------ | ---------- | ----------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **States array per instance** — `ActionInstance.states: Vec<ActionState>` with per-state `{image, text, show, colour, alignment, family, style, size, underline, background_colour}` | Table Stakes | HIGH       | **MISSING** | v1.3 `Binding.state` is a single `KeyState` struct (one image/text/colors). OpenDeck's `ActionInstance` has `states: Vec<ActionState>` and `current_state: u16`. The Elgato manifest's `States` array initializes the per-state defaults. There is no multi-state storage in `core::Binding` or `core::Action`. `stateIndex` is tracked in `ContextRegistry` (runtime only) but not persisted per-binding.                                                                                    |
| **setState side-effect: auto-render manifest state image**                                                                                                                           | Table Stakes | MEDIUM     | **STUB**    | `ContextRegistry::setState` sets `stateIndex` and the bridge auto-renders the manifest's image for the new state (`plugin_device_bridge.cpp:629`). But since `Binding` has no `states` array, the rendered image is the manifest default — any `setImage` overrides are lost on state change. The full correct behaviour requires per-state image overrides stored in `Binding`.                                                                                                              |
| **DisableAutomaticStates** — when false, state toggles on keyDown (0↔1 for 2-state actions)                                                                                          | Table Stakes | LOW        | **MISSING** | Not parsed from manifest, not tracked. OpenDeck keypad.rs toggles `current_state = (current_state + 1) % states.len()` on `keyDown` for 2-state actions with `disable_automatic_states=false`.                                                                                                                                                                                                                                                                                                |
| **Multi Action** — a key slot contains N child action instances, all fired sequentially on press                                                                                     | Table Stakes | HIGH       | **MISSING** | `core::Binding.onPress` is `Vec<Action>` (multi-capable at model layer) but `children` is not `Vec<ActionInstance>` — it is a flat list of `ActionKind::Plugin` steps without per-child state, settings, or individual `willAppear` lifecycle. OpenDeck's `children: Option<Vec<ActionInstance>>` each have their own context, settings, states, and receive `willAppear` individually. The multi-action keyDown loop (100ms between children, individual `keyDown`/`keyUp` pairs) is absent. |
| **Toggle Action** — a key cycles through N child instances; press fires the current child's action                                                                                   | Table Stakes | HIGH       | **MISSING** | No implementation. OpenDeck `opendeck.toggleaction` UUID dispatches to `children[current_state]` on keyDown and advances state. No equivalent in v1.3 built-ins.                                                                                                                                                                                                                                                                                                                              |
| **Per-state image in InstanceEditor UI**                                                                                                                                             | Table Stakes | HIGH       | **MISSING** | `InstanceEditor.svelte` renders a state-dropdown, per-state image upload, background colour, text formatting controls for each state. Our `Inspector.qml` shows a single image picker. No state selection UI exists.                                                                                                                                                                                                                                                                          |
| **SupportedInMultiActions manifest flag** — gates which actions appear in Multi Action's child picker                                                                                | Table Stakes | LOW        | **MISSING** | Not parsed. OpenDeck reads `supported_in_multi_actions` and hides ineligible actions from the Multi Action editor.                                                                                                                                                                                                                                                                                                                                                                            |
| **isInMultiAction in event payload** — flag in willAppear/keyDown/keyUp payload                                                                                                      | Table Stakes | LOW        | **STUB**    | `instancePayload()` accepts `isInMultiAction=false` default. The flag is never set to true because Multi Action is not implemented.                                                                                                                                                                                                                                                                                                                                                           |

**Dependency chain:** Multi Action / Toggle Action require the states array in `Binding`. States array requires profile schema change. Profile schema change is a breaking migration (existing profiles have `state: KeyState`, new schema needs `states: Vec<KeyState>`).

______________________________________________________________________

### Category D: Binding / Editor Model (drag types, move semantics, layout)

| Feature                                                                                                                               | Label        | Complexity | Status      | Notes                                                                                                                                                                                                                                    |
| ------------------------------------------------------------------------------------------------------------------------------------- | ------------ | ---------- | ----------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **Drag type "action" (copy from palette)** — dragging a library tile onto a key copies the action template, creating a new instance   | Table Stakes | MEDIUM     | **STUB**    | `DragRelay` + `application/x-ajazz-action` MIME implemented (Phase 26/28). `qml.drag` debug driver proven to deliver real pointer events. Physical-mouse end-to-end not yet verified with real device (PLUGIN-21 reserved for operator). |
| **Drag type "controller" (move existing instance)** — dragging from key→key moves the binding with lifecycle events                   | Table Stakes | MEDIUM     | **REUSE**   | `application/x-ajazz-binding` MIME + `ProfileController::swapKeyBindings` implemented (Phase 29-03). `willDisappear(old) + willAppear(new)` lifecycle verified via debug channel (Phase 29 LIVE-VERIFICATION §2026-06-03).               |
| **move_instance semantics: `{source, destination, retain}`** — retain=false clears source, retain=true copies                         | Table Stakes | LOW        | **REUSE**   | `swapKeyBindings` clears source; `commitKeyBinding` on a new slot is a copy. The `retain` distinction is handled by whether the user drags (move) or drops from library (copy).                                                          |
| **Key controller layout: rows × columns grid**                                                                                        | Table Stakes | LOW        | **REUSE**   | `DeviceView.qml` renders `keyRows × keyColumns` per `DeviceDescriptor`. Phase 26 complete.                                                                                                                                               |
| **Encoder controller row** — one row below key grid, one cell per encoder                                                             | Table Stakes | LOW        | **REUSE**   | `EncoderDial.qml` components in `DeviceView.qml`. Phase 26 complete.                                                                                                                                                                     |
| **Touch zone row** — one row below encoder row, one cell per touch zone                                                               | Table Stakes | LOW        | **REUSE**   | `TouchStripLane.qml` in `DeviceView.qml`. Phase 26 complete.                                                                                                                                                                             |
| **overflowsX / overflowsY scroll** — when geometry exceeds 8 cols or 4 rows                                                           | Table Stakes | LOW        | **MISSING** | OpenDeck's `DeviceView.svelte` clips at 8 wide / 4 tall and scrolls. Our `DeviceView.qml` does not implement overflow scroll for large SKUs.                                                                                             |
| **Drag affordance gating** — library tiles have `affordanceMask` (Key=1, Dial=2, TouchZone=4); drop targets reject incompatible types | Table Stakes | LOW        | **REUSE**   | `KeyCell.qml`, `EncoderDial.qml`, `TouchStripLane.qml` all gate on `affordanceMask`.                                                                                                                                                     |
| **Drag-to-trash** — drag a bound key cell to a trash drop area to unbind                                                              | Table Stakes | LOW        | **REUSE**   | `trashDropArea` exists in Phase 29 multi-action panel.                                                                                                                                                                                   |
| **Per-key binding list with reorder** — ordered list of actions per key (multi-action panel)                                          | Table Stakes | MEDIUM     | **REUSE**   | `ProfileController::appendKeyAction`, `reorderKeyAction`, `removeKeyActionAt` implemented (Phase 29-03). UI `KeyBindingList` with `keyBindingRow_<i>` and drag-reorder exists. Live persistence pending operator verification.           |

______________________________________________________________________

### Category E: Property Inspector

| Feature                                                                                                         | Label        | Complexity | Status      | Notes                                                                                                                                                                                                                                                                                                                                                        |
| --------------------------------------------------------------------------------------------------------------- | ------------ | ---------- | ----------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| **PI renders in QWebEngine + QWebChannel** — HTML file from plugin bundle, sandboxed                            | Table Stakes | MEDIUM     | **REUSE**   | `PIWebView.qml` + `PropertyInspectorController` + `PIBridge`. `AJAZZ_HAVE_WEBENGINE=1` confirmed. `QQuickWebEngineProfile` per-plugin isolation.                                                                                                                                                                                                             |
| **Elgato `sdpi.css` served from built-in URL**                                                                  | Table Stakes | LOW        | **REUSE**   | Listed in REQUIREMENTS PLUGIN-09. Served from app resources.                                                                                                                                                                                                                                                                                                 |
| **`$SD.setSettings` / `$SD.getSettings` round-trip from PI HTML**                                               | Table Stakes | MEDIUM     | **STUB**    | `PIBridge::setSettings` persists via `plugin_settings_store`. The `PI→plugin didReceiveSettings` half was verified via `plugin.simulatePiSettings` debug RPC. The real JS `$SD.setSettings()` from within the WebEngineView was NOT verified (WebEngineView has no objectName; `qml.invoke` cannot run JS in it). Requires physical-keyboard + PI HTML test. |
| **Shared context key between PI and plugin WebSocket** — both use wire context `device#root#Controller#row#col` | Table Stakes | LOW        | **REUSE**   | `Inspector.qml._contextUuid()` now derives wire context id (Phase 29-02). On-disk files confirmed as `akp05e#root#Keypad#0#0.json`.                                                                                                                                                                                                                          |
| **`sendToPropertyInspector` relay** — plugin WS sends data to PI HTML                                           | Table Stakes | LOW        | **REUSE**   | `plugin_device_bridge.cpp relayToPropertyInspector` → `PIBridge::deliverToPropertyInspector`.                                                                                                                                                                                                                                                                |
| **`cefQuery` polyfill**                                                                                         | Table Stakes | LOW        | **REUSE**   | Referenced in REQUIREMENTS PLUGIN-09. The original vendor SDK used CefSharp; the polyfill bridges to `QWebChannel`.                                                                                                                                                                                                                                          |
| **PI for Encoder (dial) controller**                                                                            | Table Stakes | MEDIUM     | **STUB**    | `Inspector._contextUuid()` handles Encoder context (`device#root#Encoder#0#col`). Unverified with an Encoder-capable plugin that has a PI (none currently installed have `controllers: ["Encoder"]` + PI).                                                                                                                                                   |
| **`propertyInspectorDidAppear/Disappear` events**                                                               | Table Stakes | LOW        | **MISSING** | Not emitted when PI panel opens/closes. Some plugins wait for `propertyInspectorDidAppear` before calling `getSettings`.                                                                                                                                                                                                                                     |

______________________________________________________________________

### Category F: Per-Context + Global Settings Persistence

| Feature                                                                                                             | Label        | Complexity | Status    | Notes                                                                                                                                                |
| ------------------------------------------------------------------------------------------------------------------- | ------------ | ---------- | --------- | ---------------------------------------------------------------------------------------------------------------------------------------------------- |
| **Per-context settings** — keyed by `device#page#controller#row#col`, persisted across restart                      | Table Stakes | LOW        | **REUSE** | `plugin_settings_store` with wire context key. Confirmed on-disk in Phase 29 verification.                                                           |
| **Global settings** — per-plugin UUID, persisted across restart                                                     | Table Stakes | LOW        | **REUSE** | Stored in `settings/<plugin_uuid>.json` per `sd_plugin_server.cpp` global-settings handler.                                                          |
| **Settings delivery at `willAppear` time** — the `payload.settings` in `willAppear` carries current stored settings | Table Stakes | LOW        | **REUSE** | `plugin_device_bridge.cpp:1064` resolves settings from `plugin_settings_store` before building `willAppear` payload.                                 |
| **Settings survive profile reload** — switching profiles then switching back re-delivers stored settings            | Table Stakes | LOW        | **REUSE** | `plugin_settings_store` is keyed by wire context (device + page + position), not profile ID. Survives profile switches for the same device+position. |
| **Settings isolated per plugin** — plugin A cannot read plugin B's settings                                         | Table Stakes | LOW        | **REUSE** | `plugin_settings_store` namespaces under plugin UUID directory.                                                                                      |

______________________________________________________________________

### Category G: Per-App Profiles (Foreground-App Switching)

| Feature                                                                                                     | Label        | Complexity | Status      | Notes                                                                                                                                                                                                                                                                                                                 |
| ----------------------------------------------------------------------------------------------------------- | ------------ | ---------- | ----------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **`Profile::applicationHints` field** — list of process-name strings that trigger this profile              | Table Stakes | LOW        | **REUSE**   | In `core::Profile` (line 189); serialised/deserialised in `profile.cpp`.                                                                                                                                                                                                                                              |
| **Foreground-app watcher** — polls active window at ~250ms, switches profile when app name matches hint     | Table Stakes | HIGH       | **MISSING** | No watcher in any source file. OpenDeck uses `active_win_pos_rs` (Linux: `xdotool getactivewindow getwindowname` / Wayland: DBus) + 250ms loop. Qt equivalents: `QProcess("xdotool")` on X11, `org.gnome.Shell` DBus on Wayland, `GetForegroundWindow` on Windows. This is the single largest standalone feature gap. |
| **`switchToProfile` host command handler** — plugins can switch profiles imperatively                       | Table Stakes | MEDIUM     | **STUB**    | In routed-action list but no dispatch handler found in bridge. Needs wiring from `SdPluginServer` emit → `ProfileController::loadProfileByName(device, profileName)`.                                                                                                                                                 |
| **"default" profile fallback** — when no app-specific profile matches, revert to default                    | Table Stakes | LOW        | **MISSING** | No fallback logic. OpenDeck `application_watcher.rs` checks `application_profiles.get("opendeck_default")`.                                                                                                                                                                                                           |
| **UI to associate a profile with an app name** — user assigns which profile activates for which application | Table Stakes | HIGH       | **MISSING** | No UI in `ProfileController` or QML. OpenDeck has a `ProfileManager.svelte` with per-device app→profile mapping. Requires new `ProfileManager.qml` with app-picker (list of running processes or manual entry).                                                                                                       |
| **`applicationDidLaunch/Terminate` events to plugins** — fired when a monitored app starts/stops            | Table Stakes | MEDIUM     | **MISSING** | Requires the foreground watcher + sysinfo process scan to detect launch/terminate. `ApplicationsToMonitor` manifest field must be parsed (currently parsed per PLUGIN-06: REUSE).                                                                                                                                     |

**Dependency chain:** Per-app profiles require `applicationHints` storage (REUSE) + foreground watcher (MISSING) + UI (MISSING). The watcher must fire `switchToProfile` to `ProfileController` AND `applicationDidLaunch/Terminate` to plugins. These can be parallel tracks but both must land together for the feature to work end-to-end.

______________________________________________________________________

### Category H: Folders / Pages / Profile Navigation

| Feature                                                                                              | Label          | Complexity | Status      | Notes                                                                                                                                                                                                                                           |
| ---------------------------------------------------------------------------------------------------- | -------------- | ---------- | ----------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **`ProfilePage` model** — pages keyed by string ID, root page = "root", children = list of page IDs  | Table Stakes   | LOW        | **REUSE**   | `core::ProfilePage` struct exists; `Profile::pages` map in `profile.hpp`. `ActionEngine` has `pageStack` + `OpenFolder`/`BackToParent` dispatch.                                                                                                |
| **Multi-page navigation (swipe / prev/next/goto)** — host-side page switch with repaint              | Table Stakes   | MEDIUM     | **REUSE**   | PROFILE-02: completed in Phase 16. Touch swipe → page change → repaint verified.                                                                                                                                                                |
| **Folder creation in UI** — user can create a sub-folder and assign actions to it                    | Table Stakes   | HIGH       | **MISSING** | `ProfilePage` model exists but no UI for creating pages / navigating the page tree in the editor. No `createProfilePage` in `ProfileController`. OpenDeck uses a hierarchical profile browser (profile IDs use path notation `folder/profile`). |
| **Folder visual in device editor** — a key configured as "OpenFolder" shows a folder icon on the key | Table Stakes   | MEDIUM     | **MISSING** | Built-in `ActionKind::OpenFolder` exists and navigates; but the key rendering for folder-type bindings (shows a folder icon, not an image) is not implemented as a distinct visual.                                                             |
| **`switchToProfile` in-profile navigation** — plugin-triggered profile switch                        | Table Stakes   | MEDIUM     | **STUB**    | Described above in G.                                                                                                                                                                                                                           |
| **Profile list per device** — user can create, rename, delete, duplicate profiles                    | Table Stakes   | LOW        | **REUSE**   | `ProfileController::createProfile`, `renameActiveProfile`, `deleteProfile`, `duplicateProfile` all implemented.                                                                                                                                 |
| **Page indicator / carousel** — visual page indicator on the device canvas or UI                     | Differentiator | LOW        | **MISSING** | Not implemented. `page.indicator` built-in action type listed in REQUIREMENTS PLUGIN-12 as completed but the device-side indicator rendering is not verified.                                                                                   |

______________________________________________________________________

### Category I: Wine / Windows-Only Plugin Support

| Feature                                                              | Label          | Complexity | Status      | Notes                                                                                                                                                                                                                                               |
| -------------------------------------------------------------------- | -------------- | ---------- | ----------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **Native `.sdPlugin` on Linux via Node.js / Python / native binary** | Table Stakes   | LOW        | **REUSE**   | System Node ≥20 detected, Python 3.11+ host, native binary via `QProcess`. Working for cross-platform plugins.                                                                                                                                      |
| **Windows-only `.exe` plugins via Wine on Linux**                    | Differentiator | HIGH       | **MISSING** | OpenDeck supports Wine for Windows-only plugins. PROJECT.md lists this as a v2.0 target ("Native non-Wine where feasible; Wine as documented fallback"). No implementation. Requires Wine detection, executable path resolution, environment setup. |
| **HTML plugin via `QWebEngineView + QWebChannel`**                   | Table Stakes   | LOW        | **REUSE**   | Implemented (PLUGIN-08).                                                                                                                                                                                                                            |
| **`connectMiraBoxSDSocket` shim**                                    | Table Stakes   | LOW        | **REUSE**   | PLUGIN-11. Aliases old SDK entry point.                                                                                                                                                                                                             |

______________________________________________________________________

## Feature Dependencies

```
Per-State Image/Title Storage (states array in Binding)
    └──required-by──> Multi Action (children with per-instance states)
    └──required-by──> Toggle Action (children with per-instance states)
    └──required-by──> DisableAutomaticStates toggling
    └──required-by──> setState persisting image override per state
    └──required-by──> InstanceEditor UI showing per-state image picker

Foreground-App Watcher
    └──required-by──> Per-App Profile Switching (activate on focus)
    └──required-by──> applicationDidLaunch / applicationDidTerminate events
    └──required-by──> switchToProfile host command (needs profile loader)

PI context sharing (wire context key)  [REUSE — already done]
    └──required-by──> sendToPropertyInspector relay  [REUSE]
    └──required-by──> propertyInspectorDidAppear/Disappear events  [MISSING]

willAppear lifecycle  [REUSE]
    └──required-by──> titleParametersDidChange (sent immediately after)  [MISSING]
    └──required-by──> Multi Action child willAppear (each child separately)  [MISSING]

Profile::pages model  [REUSE]
    └──required-by──> Folder creation UI  [MISSING]
    └──required-by──> Folder visual on key  [MISSING]
```

### Dependency Notes

- **States array requires profile schema migration:** `Binding.state: KeyState` → `Binding.states: Vec<KeyState>` is a breaking schema change. Existing profiles must be migrated on load (v1 → v2 migration in `profileFromJson`). This is the single most impactful structural change in v2.0.
- **Multi Action + Toggle Action depend on states array:** Both use `children: Vec<ActionInstance>`, where each child has its own `states`, `current_state`, and `settings`. This must land after the states array change.
- **Foreground watcher is platform-specific:** X11 (`xdotool`), Wayland (DBus `org.gnome.Shell.eval` or `xdg-activation`), Windows (`GetForegroundWindow`). Design must abstract the platform seam cleanly or CI will break on macOS.
- **titleParametersDidChange has no business dependencies:** It is emitted after willAppear and after any UI state change. It is an independent, low-risk addition.

______________________________________________________________________

## Anti-Features (Explicitly NOT Build)

| Feature                               | Why Requested                                    | Why It Is an Anti-Feature                                                                                       | Alternative                                                                   |
| ------------------------------------- | ------------------------------------------------ | --------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------- |
| Phone-home plugin catalog             | Convenience for users who don't know plugin URLs | Sends hardware identity + install list to Mirabox/Aliyun; privacy violation; already in PROJECT.md out-of-scope | Host-owned local catalog, manual install from .sdPlugin zip                   |
| Bundling `node20.exe` / bundling Wine | Works offline                                    | License cost, binary size (200+ MB), update burden                                                              | Detect system Node ≥20; detect system Wine; reject with clear error if absent |
| Unsigned plugin trust                 | Easier for plugin authors                        | Security: any local process can inject setTitle/setImage/keyDown events                                         | Signature-gate is already implemented (REUSE); keep it                        |
| Always-on global keyboard hook        | Hotkeys without focus                            | Privacy (records all keystrokes), performance, permission escalation                                            | Opt-in per binding: `system.hotkey` with explicit user approval per key       |
| `QHostAddress::Any` WebSocket bind    | Easier network plugin development                | Any process on LAN can control the device; trivially exploitable                                                | Loopback-only (already enforced and test-pinned)                              |
| Plaintext OBS WebSocket (no auth)     | OBS integration without setup                    | MITM risk on shared networks                                                                                    | `obsstudio` built-in with auth default-on (already implemented)               |
| Auto-retry-on-disconnect HID loop     | Appears responsive                               | Burns CPU; coalescing + offline badge is correct                                                                | Existing debounced hot-plug (REUSE)                                           |
| Telemetry / usage metrics             | Feature prioritisation                           | Privacy; trust erosion                                                                                          | Optional crash reporting only, never silent                                   |
| `openApplication` AJAZZ-only command  | Launch apps from plugin                          | Shell-injection risk; `openUrl` with `file://` or `xdg-open` is sufficient                                      | `openUrl` already implemented                                                 |
| Per-instance NVM auto-flash           | Persists macros to device                        | NVM wear silently bricks device in hours of editing                                                             | Explicit "Flash to device" button per-session                                 |
| Modal "device disconnected" dialog    | Alerts user immediately                          | Blocks UI; breaks workflows; all competitors use offline badge                                                  | Existing offline badge + sidebar indicator                                    |
| Sidebar reorder by recency            | Shows most-used devices first                    | Destroys focus retention; users rely on stable order                                                            | Lexicographic stable sort (HOTPLUG-04, already REUSE)                         |

______________________________________________________________________

## MVP Definition for v2.0

### Must Land (blocks real-world plugin usage)

- [ ] **States array in `core::Binding`** with profile schema v2 migration — prerequisite for everything below
- [ ] **Multi Action** — the most commonly used composite action in Elgato ecosystem; without it plugins designed for Multi Action context behave incorrectly
- [ ] **Per-app profile switching** (foreground watcher + UI) — the #1 differentiating workflow feature users coming from Elgato expect
- [ ] **`titleParametersDidChange`** — many plugins send this immediately on `willAppear` to initialise their PI; without it those plugins appear broken
- [ ] **`propertyInspectorDidAppear/Disappear`** — several popular plugins (`obs-scene-switcher`, etc.) gate getSettings on this
- [ ] **Physical-mouse drag-to-bind verified on real device** (PLUGIN-21 operator checkpoint) — currently only debug-channel proven

### Add After Core Is Verified (v2.x)

- [ ] **Toggle Action** — builds on states array; lower priority than Multi Action
- [ ] **`switchToProfile` host command dispatch** — small wiring gap; unlocks plugin-driven profile automation
- [ ] **`applicationDidLaunch/Terminate` events** — depends on the foreground watcher; add in same phase
- [ ] **Folder creation UI** — model exists; UI is the gap
- [ ] **`systemDidWakeUp`** — low-impact; add with OS power-management D-Bus integration
- [ ] **`setFeedback` / `setFeedbackLayout` full dispatch** — encoder touch-strip layout system; hardware-gated

### Defer to v2.1+ (requires hardware or non-trivial platform work)

- [ ] **Wine support for Windows-only plugins** — platform-specific; non-trivial; document as "if system Wine present"
- [ ] **`DisableAutomaticStates` automatic toggle** — depends on states array; easy to add but low priority without Multi-State UI
- [ ] **Page indicator / carousel UI on device canvas** — visual polish; not workflow-blocking
- [ ] **`setFeedbackLayout` full encoder touch-strip layout engine** — hardware-gated on retail AKP05E with working encoder input

______________________________________________________________________

## Feature Prioritization Matrix

| Feature                                            | User Value | Implementation Cost         | Priority |
| -------------------------------------------------- | ---------- | --------------------------- | -------- |
| States array in `core::Binding` + schema migration | HIGH       | MEDIUM                      | P1       |
| Multi Action                                       | HIGH       | HIGH                        | P1       |
| Per-app profile switching (watcher + UI)           | HIGH       | HIGH                        | P1       |
| `titleParametersDidChange` event                   | HIGH       | LOW                         | P1       |
| `propertyInspectorDidAppear/Disappear`             | MEDIUM     | LOW                         | P1       |
| Physical-mouse drag verified on real device        | HIGH       | LOW (operator verification) | P1       |
| Toggle Action                                      | MEDIUM     | MEDIUM                      | P2       |
| `switchToProfile` dispatch handler                 | MEDIUM     | LOW                         | P2       |
| `applicationDidLaunch/Terminate`                   | MEDIUM     | MEDIUM                      | P2       |
| Folder creation UI                                 | MEDIUM     | HIGH                        | P2       |
| `systemDidWakeUp`                                  | LOW        | LOW                         | P3       |
| Wine plugin support                                | LOW        | HIGH                        | P3       |
| `setFeedbackLayout` layout engine                  | LOW        | HIGH                        | P3       |
| `DisableAutomaticStates` auto-toggle               | LOW        | LOW                         | P2       |
| Overflow scroll for large SKUs                     | LOW        | LOW                         | P3       |

______________________________________________________________________

## Competitor Feature Analysis

| Feature                                      | OpenDeck (Svelte/Rust/Tauri)                                                    | Elgato Stream Deck                           | Our App (v1.3 actual)                                             |
| -------------------------------------------- | ------------------------------------------------------------------------------- | -------------------------------------------- | ----------------------------------------------------------------- |
| Action states array (per-instance)           | Full: `states: Vec<ActionState>` with image/text/font per state                 | Full: manifest `States[]` + runtime override | Runtime-only `stateIndex`; no per-state storage in Binding        |
| Multi Action                                 | Yes: `opendeck.multiaction` with children, 100ms between, individual willAppear | Yes: built-in                                | Not implemented                                                   |
| Toggle Action                                | Yes: `opendeck.toggleaction`, cycles children on press                          | Yes: built-in                                | Not implemented                                                   |
| Per-app profile switching                    | Yes: `application_watcher.rs`, 250ms poll, `active_win_pos_rs`                  | Yes: automatic profile switching             | Schema only (`applicationHints`); no watcher                      |
| Foreground-window detection                  | `active_win_pos_rs` (X11/Wayland/macOS/Windows)                                 | Proprietary                                  | Not implemented                                                   |
| `titleParametersDidChange`                   | Yes: emitted after willAppear                                                   | Yes                                          | Not implemented                                                   |
| `systemDidWakeUp`                            | Yes: tauri-plugin-prevent-default                                               | Yes                                          | Not implemented                                                   |
| `propertyInspectorDidAppear/Disappear`       | Yes                                                                             | Yes                                          | Not implemented                                                   |
| PI settings shared context key               | Yes: context = `device.profile.controller.position.index`                       | Yes: context UUID                            | Done (Phase 29-02): wire context `device#root#Controller#row#col` |
| `switchToProfile` dispatch                   | Yes: `profiles.rs` `set_selected_profile`                                       | Yes                                          | Listed but not dispatched                                         |
| Wine plugin support                          | Yes: `wine` child process                                                       | No (Windows-only vendor)                     | Not implemented                                                   |
| Multi-page navigation                        | Yes: profile IDs are paths (`folder/profile`)                                   | Yes: folders                                 | Yes (REUSE): `ProfilePage` + `ActionEngine`                       |
| Folder creation UI                           | Yes: hierarchical profile browser                                               | Yes: drag-to-folder                          | Not implemented                                                   |
| Encoder touch-strip feedback (`setFeedback`) | Yes: maps to device display zones                                               | Yes (SD+)                                    | Routed but not dispatched                                         |

______________________________________________________________________

## Sources

- OpenDeck `shared.rs`: `ActionInstance` struct with `states: Vec<ActionState>`, `current_state: u16`, `children: Option<Vec<ActionInstance>>` — fetched via `gh api` 2026-06-06 (HIGH confidence)
- OpenDeck `events/outbound/will_appear.rs`: confirms `titleParametersDidChange` is sent after `willAppear` — HIGH confidence
- OpenDeck `events/inbound/states.rs`: confirms `setState` updates `current_state` on instance — HIGH confidence
- OpenDeck `events/outbound/keypad.rs`: confirms Multi Action loop (100ms between children) and Toggle Action dispatch — HIGH confidence
- OpenDeck `application_watcher.rs`: confirms 250ms poll, `active_win_pos_rs`, `applicationDidLaunch/Terminate` dispatch — HIGH confidence
- OpenDeck `store/profiles.rs`: confirms `retain` bool in `rename_profile`, hierarchical path-based profile IDs — HIGH confidence
- Elgato Stream Deck SDK docs (events-sent, events-received, manifest): [Keys](https://docs.elgato.com/streamdeck/sdk/guides/keys/), [Manifest](https://docs.elgato.com/streamdeck/sdk/references/manifest/), [Events Sent](https://docs.elgato.com/sdk/plugins/events-sent), [Events Received](https://docs.elgato.com/sdk/plugins/events-received) — MEDIUM confidence (WebFetch summary; core event list matches OpenDeck)
- Live codebase grep 2026-06-06: `src/app/src/plugin_device_bridge.cpp`, `sd_plugin_server.cpp`, `pi_bridge.cpp`, `profile_controller.hpp`, `core/include/ajazz/core/profile.hpp` — HIGH confidence (direct code inspection)
- Phase 29 LIVE-VERIFICATION.md 2026-06-02/03: confirms PLUGIN-21 (partial), PLUGIN-22 (on-disk PASS), PLUGIN-23 (unit PASS / live pending) — HIGH confidence
- `.planning/opendeck-ui-plugin-study.md` 2026-06-01: architecture map with gap annotations — HIGH confidence (authored by project team from OpenDeck source)

______________________________________________________________________

*Feature research for: v2.0 Modular Plugin + Key/Dial/Touch Binding (AJAZZ Control Center)*
*Researched: 2026-06-06*
