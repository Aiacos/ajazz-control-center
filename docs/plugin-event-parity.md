<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Plugin Event Parity Coverage (EVENT-01)

> **Living verification deliverable.** This document is the source-of-truth coverage
> map for the Stream Deck plugin event surface. It cross-references the AJAZZ Control
> Center host (`SdPluginServer` + `PluginDeviceBridge`) against the OpenDeck
> `events/inbound` + `events/outbound` sets and the Elgato Stream Deck SDK event list.
> Keep it current: when an event's status or implementing site changes, update the
> matching row AND its test reference in the same PR. ASCII-only (CLAUDE.md: test
> names and wire tags must survive the Win32 CMD codepage).

**Phase 34 audit (2026-06-08).** The event surfaces already existed and are rich;
this audit classifies every event and pins a test reference per supported event.

## Legend

| Status           | Meaning                                                                                                   |
| ---------------- | --------------------------------------------------------------------------------------------------------- |
| `supported`      | A real host code path emits/handles this event AND a test (or grep gate) covers it.                       |
| `partial`        | A code path exists but is incomplete (e.g. shape locked, values [ASSUMED]; or dispatch-only).             |
| `missing`        | Not implemented in this milestone (tracked for a future phase).                                           |
| `hardware-gated` | Code path exists but real wire values cannot be confirmed without retail hardware (demo unit zero-input). |

**Direction:** `inbound` = plugin -> host (action the plugin sends); `outbound` =
host -> plugin (event the host delivers to the plugin's WebSocket).

**Source vocabularies:**

- OpenDeck `events/inbound` + `events/outbound` (the OSS reference set the app targets).
- Elgato Stream Deck SDK event list (the canonical superset most plugins are written against).
- In-repo event catalogue: `docs/protocols/streamdeck/akp_plugin_sdk.md` Sections 4.3 (inbound)
  and 4.4 (outbound), derived from the vendor `SDLibrary1.dll` / `streamdock.exe` decompile.

## Knob \<-> Encoder controller-token normalization (cross-link to EVENT-04)

Elgato / AJAZZ manifests declare encoder actions under `Controllers: ["Knob"]` (the
manifest vocabulary), but the runtime **wire** vocabulary is `controller: "Encoder"`
everywhere (input service, bridge `byCoord` keys, every outbound payload). The single
sanctioned acceptance of the `"Knob"` token is the manifest boundary
(`src/app/src/plugin_manifest.cpp:144`, `affordanceMask`):
`token == "Knob" || token == "Encoder"` -> `Affordance::Dial`.

**Invariant:** No wire-emission site emits the literal `"Knob"`; every encoder
controller is normalized to `"Encoder"` before it reaches a plugin. This is enforced
by `controller_token` (Catch2) plus a grep gate asserting that the only `"Knob"`
literal under `src/app/src/*.cpp` is the manifest acceptance line. See **EVENT-04**
(`tests/unit/controller_token_test.cpp`).

## Outbound events (host -> plugin)

| Event                             | OpenDeck/Elgato | Status         | Implementing site (file:symbol)                                                                                                                                       | Test reference                                                                              |
| --------------------------------- | --------------- | -------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------- |
| `willAppear`                      | yes             | supported      | `plugin_device_bridge.cpp` `populateContextsForActivePage` (:1213/:1261/:1314) via `eventEnvelope`+`instancePayload`                                                  | `ctest -R willappear_payload`; `-R "plugin-device-bridge"` (E2E willAppear)                 |
| `willDisappear`                   | yes             | supported      | `plugin_device_bridge.cpp` `retirePageContexts` (:1341/:1369) via `eventEnvelope`+`instancePayload`                                                                   | `-R "plugin-device-bridge"` (retire/page-change E2E)                                        |
| `keyDown`                         | yes             | supported      | `plugin_device_bridge.cpp` `onDeviceEvent` (:935)                                                                                                                     | `-R "plugin-device-bridge"` (outbound keyDown E2E)                                          |
| `keyUp`                           | yes             | supported      | `plugin_device_bridge.cpp` `onDeviceEvent` (:935)                                                                                                                     | `-R "plugin-device-bridge"` (outbound keyDown/keyUp E2E)                                    |
| `dialRotate`                      | yes (SD+)       | supported      | `plugin_device_bridge.cpp` `onDeviceEvent` (:959), controller `"Encoder"`                                                                                             | `-R "plugin-device-bridge"` (dialRotate E2E); `-R controller_token` (Encoder normalization) |
| `dialDown`                        | yes (SD+)       | supported      | `plugin_device_bridge.cpp` `onDeviceEvent` (:975), controller `"Encoder"`                                                                                             | `-R "plugin-device-bridge"`; `-R controller_token`                                          |
| `dialUp`                          | yes (SD+)       | supported      | `plugin_device_bridge.cpp` `onDeviceEvent` (:994), controller `"Encoder"`                                                                                             | `-R "plugin-device-bridge"`; `-R controller_token`                                          |
| `keyDownCord` / `keyUpCord`       | AJAZZ-only      | supported      | `plugin_device_bridge.cpp` `onDeviceEvent` (:978/:996) legacy encoder alias                                                                                           | `-R "plugin-device-bridge"` (encoder press E2E)                                             |
| `touchTap`                        | yes (SD+)       | hardware-gated | `plugin_device_bridge.cpp` `onDeviceEvent` (:1034); routing tested via synthetic `input.*`                                                                            | `-R "plugin-device-bridge"` (touch routing); real wire values need retail AKP05E            |
| `titleParametersDidChange`        | yes             | partial        | `plugin_device_bridge.cpp` (:1218/:1265/:1318) via `titlePayload`; default `titleParameters` values [ASSUMED]                                                         | `-R "PI-04"` (titleParametersDidChange completeness; shape locked)                          |
| `willAppear` payload completeness | yes             | supported      | `instancePayload` (:311) + `eventEnvelope` (:374): `action/context/device/event` + `payload{coordinates.row, coordinates.column, controller, state, isInMultiAction}` | `ctest -R willappear_payload` (EVENT-02)                                                    |
| `didReceiveSettings`              | yes             | supported      | `plugin_device_bridge.cpp` (:582/:1505); `onPropertyInspectorSettings`                                                                                                | `-R "plugin-device-bridge"`; `-R "PI"` (settings round-trip)                                |
| `didReceiveGlobalSettings`        | yes             | supported      | `pi_bridge.cpp` (:340/:343); global-settings reply path                                                                                                               | `-R "pi_bridge"` / `-R "PI"` (global settings)                                              |
| `deviceDidConnect`                | yes             | supported      | `plugin_device_bridge.cpp` `onDeviceConnected` (:1434)                                                                                                                | `-R "plugin-device-bridge"` (device connect E2E)                                            |
| `deviceDidDisconnect`             | yes             | supported      | `plugin_device_bridge.cpp` `onDeviceDisconnected` (:1464)                                                                                                             | `-R "plugin-device-bridge"` (device disconnect E2E)                                         |
| `propertyInspectorDidAppear`      | yes             | supported      | `application.cpp` PI seam (:751) via `SdPluginServer::sendEvent`                                                                                                      | `-R "PI-04"` (inspectorOpened -> event)                                                     |
| `propertyInspectorDidDisappear`   | yes             | supported      | `application.cpp` PI seam (:763) via `SdPluginServer::sendEvent`                                                                                                      | `-R "PI-04"` (inspectorClosed -> event)                                                     |
| `sendToPlugin`                    | yes             | supported      | `sd_plugin_server.cpp` relay (PI -> plugin); `application.cpp` route                                                                                                  | `-R "sd_plugin_server"` / `-R "PI"`                                                         |
| `sendToPropertyInspector`         | yes             | supported      | `plugin_device_bridge.cpp` `relayToPropertyInspector` signal -> PI relay                                                                                              | `-R "PI"` (relay E2E)                                                                       |
| `passHello`                       | AJAZZ-only      | supported      | `sd_plugin_server.cpp` post-registration handshake                                                                                                                    | `-R "sd_plugin_server"` (registration handshake)                                            |
| `applicationDidLaunch`            | yes             | missing        | Outbound fan-out planned via the Application seam (APROF-04, Plan 04)                                                                                                 | `-R app_lifecycle_events` (registered RED scaffold, Plan 01; turns GREEN in Plan 04)        |
| `applicationDidTerminate`         | yes             | missing        | Outbound fan-out planned via the Application seam (APROF-04, Plan 04)                                                                                                 | `-R app_lifecycle_events` (registered RED scaffold, Plan 01; turns GREEN in Plan 04)        |
| `systemDidWakeUp`                 | yes             | missing        | Dispatch + synthetic-wake test planned (EVENT-03, Plan 03/04)                                                                                                         | `-R system_did_wake` (lands EVENT-03)                                                       |
| `deleteAction`                    | AJAZZ-only      | missing        | Not implemented this milestone                                                                                                                                        | n/a                                                                                         |
| `lockScreen` / `unLockScreen`     | AJAZZ-only      | missing        | Not implemented this milestone                                                                                                                                        | n/a                                                                                         |

## Inbound events (plugin -> host)

| Event                                                                                                                                                                                                                                                                                                                             | OpenDeck/Elgato | Status    | Implementing site (file:symbol)                                                                                   | Test reference                                     |
| --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------- | --------- | ----------------------------------------------------------------------------------------------------------------- | -------------------------------------------------- |
| `registerPlugin`                                                                                                                                                                                                                                                                                                                  | yes             | supported | `sd_plugin_server.cpp` registration + handshake                                                                   | `-R "sd_plugin_server"` (registration)             |
| `registerPropertyInspector`                                                                                                                                                                                                                                                                                                       | yes             | supported | `sd_plugin_server.cpp` PI registration                                                                            | `-R "sd_plugin_server"` / `-R "PI"`                |
| `setImage`                                                                                                                                                                                                                                                                                                                        | yes             | supported | `plugin_device_bridge.cpp` `onSetImage`                                                                           | `-R "plugin-device-bridge"` (setImage E2E)         |
| `setTitle`                                                                                                                                                                                                                                                                                                                        | yes             | supported | `plugin_device_bridge.cpp` `onSetTitle`                                                                           | `-R "plugin-device-bridge"` (setTitle E2E)         |
| `setState`                                                                                                                                                                                                                                                                                                                        | yes             | supported | `plugin_device_bridge.cpp` setState path                                                                          | `-R "plugin-device-bridge"` / `-R toggle_dispatch` |
| `setBG`                                                                                                                                                                                                                                                                                                                           | AJAZZ-only      | supported | `plugin_device_bridge.cpp` `onSetBG`                                                                              | `-R "plugin-device-bridge"` (setBG E2E)            |
| `setFeedback`                                                                                                                                                                                                                                                                                                                     | yes             | supported | routed (`kRoutedActions`); bridge feedback path                                                                   | `-R "plugin-device-bridge"`                        |
| `setText`                                                                                                                                                                                                                                                                                                                         | AJAZZ-only      | partial   | routed (`kRoutedActions`); text-overlay render                                                                    | `-R "plugin-device-bridge"`                        |
| `getSettings` / `setSettings`                                                                                                                                                                                                                                                                                                     | yes             | supported | `plugin_device_bridge.cpp` `handleSettingsAction`; per-context store                                              | `-R "plugin-device-bridge"` (settings round-trip)  |
| `getGlobalSettings` / `setGlobalSettings`                                                                                                                                                                                                                                                                                         | yes             | supported | `plugin_device_bridge.cpp` `handleSettingsAction`; plugin-wide store                                              | `-R "plugin-device-bridge"` (global settings)      |
| `sendToPropertyInspector`                                                                                                                                                                                                                                                                                                         | yes             | supported | `plugin_device_bridge.cpp` relay signal                                                                           | `-R "PI"` (relay)                                  |
| `sendToPlugin`                                                                                                                                                                                                                                                                                                                    | yes             | supported | `sd_plugin_server.cpp` route                                                                                      | `-R "sd_plugin_server"` / `-R "PI"`                |
| `openUrl`                                                                                                                                                                                                                                                                                                                         | yes             | supported | `application.cpp` actionReceived seam (:610), scheme-guarded                                                      | `-R "sd_plugin_server"` / `-R "application"`       |
| `logMessage`                                                                                                                                                                                                                                                                                                                      | yes             | supported | `application.cpp` actionReceived seam (:626), 2048-char cap                                                       | `-R "sd_plugin_server"` / `-R "application"`       |
| `showAlert` / `showOk`                                                                                                                                                                                                                                                                                                            | yes             | partial   | routed (`kRoutedActions`); host-side ack only                                                                     | `-R "sd_plugin_server"` (routed)                   |
| `switchToProfile`                                                                                                                                                                                                                                                                                                                 | yes             | missing   | Routed (`kRoutedActions`, sd_plugin_server.cpp:424) but unhandled downstream; handler lands EVENT-03 (Plan 03/04) | `-R switch_to_profile` (lands EVENT-03)            |
| `sendToDevice`                                                                                                                                                                                                                                                                                                                    | AJAZZ-only      | missing   | Routed; raw HID forwarding is a hard-rule RE boundary (not implemented)                                           | n/a                                                |
| `openTouchbarSecondaryMenu` / `exitTouchbarSecondaryMenu`                                                                                                                                                                                                                                                                         | AJAZZ-only      | missing   | Routed; not implemented this milestone                                                                            | n/a                                                |
| `getUserInfo` / `sendUserInfo`                                                                                                                                                                                                                                                                                                    | AJAZZ-only      | missing   | Routed; Mirabox account integration out of scope                                                                  | n/a                                                |
| Other AJAZZ-only routed actions (`clearIcon`, `setBackground`, `getScreenshot`, `getSystemAudioVolume`, `enterGatheringEvent`, `registrationScreenSaverEvent`, `setAcImgTop`, `onSwitch*FolderProfile`, `deleteAction`, `stopBackground`, `lockScreen`/`unLockScreen`, `getDetectedSensorsData`, `exitFullScreen`, audio capture) | AJAZZ-only      | missing   | Routed in `kRoutedActions` (emit `actionReceived`) but no downstream consumer this milestone                      | n/a                                                |

## Notes

- **`willAppear` envelope shape (EVENT-02, locked).** Top-level: `event`, `action`
  (= `ctx.actionUUID`), `context` (= `ContextRegistry::deriveContextId`), `device`,
  `payload`. The nested `payload` (from `instancePayload`): `settings`,
  `coordinates:{row, column}`, `controller` (normalized -> `"Keypad"` | `"Encoder"`,
  never `"Knob"`), `state`, `isInMultiAction`. Asserted against the REAL bridge
  builders (not a hand-rolled duplicate) by `tests/unit/willappear_payload_test.cpp`.
- **`titleParametersDidChange`** is emitted INLINE after each `willAppear` (PI-04);
  its `titleParameters` default values are [ASSUMED] (the Catch2 test locks the
  shape; the end-of-phase human-verify confirms values if a real plugin reads them).
- **Input wire decode (`touchTap`, encoder ticks/polarity)** is `hardware-gated`: the
  demo unit `0x0300:0x3004` delivers zero input across all five verification methods
  (CLAUDE.md / STATE.md). The routing pipeline IS tested via synthetic `input.*`
  debug RPCs; only the raw wire VALUES wait for a retail AKP05E.
- **`switchToProfile` + `systemDidWakeUp`** are EVENT-03 (host-command dispatch +
  synthetic-wake test); **`applicationDidLaunch` / `applicationDidTerminate`** are
  APROF-04 (registered-plugin fan-out via the Application seam). Their RED scaffolds
  are registered in Plan 01 (`tests/unit/test_app_lifecycle_events.cpp`); they turn
  GREEN in Plan 04. Outbound `sendEvent` has no event-name allowlist
  (`kRoutedActions` gates inbound only), so these are additive on the wire.

## ACTIVE-WINDOW-IDENTITY — foreground app-id normalization contract (APROF-01)

The per-app profile auto-switch (Plan 04) matches the foreground application
against `Profile::applicationHints`. The four `IActiveWindowWatcher` backends each
emit `ActiveWindowInfo.appId` as a **single normalized match token**, defined here
so a hint set on one desktop matches on another (34-RESEARCH Pitfall 2). Matching
is **case-insensitive**; every backend lowercases its token at the source.

| Platform      | Source signal                                                        | `appId` token                                                        | Normalization                                                                  |
| ------------- | -------------------------------------------------------------------- | -------------------------------------------------------------------- | ------------------------------------------------------------------------------ |
| Wayland (wlr) | `zwlr_foreign_toplevel_handle_v1.app_id` of the `activated` toplevel | app_id (e.g. `firefox`)                                              | as-reported (already app-id form); compositor string is untrusted (T-34-03-03) |
| X11/EWMH      | `WM_CLASS` instance of `_NET_ACTIVE_WINDOW`                          | instance/class (e.g. `firefox`)                                      | lowercased ASCII                                                               |
| Windows       | `QueryFullProcessImageNameW` of `GetForegroundWindow`'s process      | image base name minus `.exe` (e.g. `FIREFOX.EXE` -> `firefox`)       | base name, strip `.exe`, lowercase                                             |
| macOS         | `NSWorkspace.frontmostApplication`                                   | `bundleIdentifier` (e.g. `org.mozilla.firefox`), else localized name | lowercased                                                                     |

Notes:

- The macOS token (reverse-DNS bundle id) differs in **shape** from the Wayland/X11
  app_id; a hint authored on Linux will not auto-match on macOS and vice-versa. This
  is acceptable for v2.0 (hints are per-install); a future cross-platform alias map
  is out of scope (deferred).
- The token is consumed strictly as a match string (Plan 04) — never eval'd, exec'd,
  or shelled out (threat T-34-03-03; no `xdotool`/`swaymsg`/`wmctrl` subprocess).
- `capabilityAvailable()` is the runtime degradation signal (APROF-03): Wayland maps
  it to `QWaylandClientExtension::isActive()`; X11 to a successful display open;
  Win/macOS are always available. When `false`, the watcher emits no automatic
  changes and the UI shows the capability-warning chip (Plan 05) — manual switching
  still works (fail safe, never fail open).
