# Contract — Elgato Plugin WebSocket (host ↔ plugin, host ↔ Property Inspector)

**Authoritative in-repo spec**: `docs/protocols/streamdeck/elgato_plugin_protocol.md` (current);
vendor extensions in `docs/protocols/streamdeck/akp_plugin_sdk.md`. Server impl:
`src/app/src/sd_plugin_server.cpp`. This file is the *plan-level* contract the subsystem must honor
for Elgato SDK (≤6.9) + OpenDeck parity.

## Transport & registration

- Server binds **`ws://127.0.0.1:<port>` (loopback only)**.
- **Plugin launch args** (4, parse by flag, order not guaranteed): `-port <n>`,
  `-pluginUUID <token>` (opaque per-launch token, NOT the manifest UUID, NOT a secret),
  `-registerEvent <string>` (normally `registerPlugin`), `-info <json>` (RegistrationInfo, shell-escaped).
- **Plugin registers**: on WS open send exactly `{ "event": "<registerEvent>", "uuid": "<token>" }`.
- **No standard auth.** Vendor `passHello`+`sha256(password+salt)` is optional and MUST resolve
  `authenticated=true` immediately when no password is set (never gates a stock plugin).
- **Property Inspector** is a *second, independent* WS client with a **5-arg** entry point:
  `connectElgatoStreamDeckSocket(port, context, "registerPropertyInspector", info, actionInfo)` —
  `context` is the **action-instance context**, not the plugin UUID.

## Inbound (host → plugin) — REQUIRED for parity

| Event                                          | Payload                                                                                   | Status (this repo)                                                                      |
| ---------------------------------------------- | ----------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------- |
| `keyDown` / `keyUp`                            | instance payload, `controller:"Keypad"`; multi-action multi-state adds `userDesiredState` | done (verify `userDesiredState`)                                                        |
| `dialRotate`                                   | encoder payload + `pressed:bool`, `ticks:number` (signed, + = CW)                         | done; wire `ticks`/polarity **HW-gated**                                                |
| `dialDown` / `dialUp`                          | encoder payload (no `dialPress` event exists)                                             | done                                                                                    |
| `touchTap`                                     | encoder payload + `hold:bool`, `tapPos:[x,y]`                                             | **partial** — `tapPos.y` hardcoded 0 (HW-gated)                                         |
| `willAppear` / `willDisappear`                 | instance payload; page/profile/folder + startup                                           | done (shape test-locked)                                                                |
| `didReceiveSettings`                           | instance payload + optional `id`                                                          | done                                                                                    |
| `didReceiveGlobalSettings`                     | `{event,payload:{settings},id?}` — no context/action/device                               | done                                                                                    |
| `propertyInspectorDidAppear` / `…DidDisappear` | `{action,context,device,event}` — no payload                                              | **done + dedup'd (T023)** — `PiAppearGate` emits exactly one per open across both sites |
| `titleParametersDidChange`                     | single payload − `isInMultiAction` + `title` + `titleParameters{…}`                       | **partial** — default `titleParameters` values `[ASSUMED]`                              |
| `deviceDidConnect`                             | `{device,event,deviceInfo:{name,size,type}}`                                              | done (geometry from DeviceDescriptor)                                                   |
| `deviceDidDisconnect`                          | `{device,event}`                                                                          | done                                                                                    |
| `applicationDidLaunch` / `…Terminate`          | `{event,payload:{application}}`, `ApplicationsToMonitor`-filtered                         | **partial** — focus-approximated, not process lifecycle                                 |
| `systemDidWakeUp`                              | `{event}`                                                                                 | done (synthetic-wake; logind D-Bus source is a follow-up)                               |
| `sendToPlugin`                                 | `{action,context,event,payload}` — no `device` (from PI)                                  | done                                                                                    |

SD 7.x optional (additive, ignore if absent): `didReceiveDeepLink`, `didReceiveResources`,
`deviceDidChange`, `didReceiveSecrets`.

## Outbound (plugin → host) — REQUIRED for parity

| Command                                              | Shape                                                                               | Status (this repo)                                                                                |
| ---------------------------------------------------- | ----------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------- |
| `setTitle`                                           | `{context,title?,target?,state?}`                                                   | done                                                                                              |
| `setImage`                                           | `{context,image?,target?,state?}` (path or `data:image/png;base64,…`)               | done                                                                                              |
| `setState`                                           | `{context,state}` (0/1)                                                             | done (2-state auto-cycle)                                                                         |
| `setFeedback`                                        | `{context,<layout-key>:value}` (SD+)                                                | done (host-side render)                                                                           |
| `setFeedbackLayout`                                  | `{context,layout}` (`$X1`/`$A0`/`$A1`/`$B1`/`$B2`/`$C1` or path)                    | done (`encoder_layout_renderer.cpp`)                                                              |
| `setTriggerDescription`                              | `{context,rotate?,push?,touch?,longTouch?}`                                         | **done (T025)** — routed in `kRoutedActions` + ownership-gated `triggerDescriptionChanged` signal |
| `showAlert` / `showOk`                               | `{context,event}`                                                                   | **done** — `onAction` paints `makeFeedbackGlyph` (dwell-then-revert), Keypad only                 |
| `set/getSettings`                                    | per-instance; `get*` carries optional `id` echoed on `didReceiveSettings`           | done                                                                                              |
| `set/getGlobalSettings`                              | plugin-wide (context = registration uuid)                                           | done                                                                                              |
| `openUrl`                                            | `{url}` (scheme-guarded)                                                            | done                                                                                              |
| `logMessage`                                         | `{message}` (≤2048 chars)                                                           | done                                                                                              |
| `sendToPropertyInspector`                            | plugin → PI relay                                                                   | done (ownership-gated)                                                                            |
| `switchToProfile`                                    | `{context,device,payload:{profile?,page?}}` (empty = previous; plugin-shipped only) | done                                                                                              |
| vendor `setBG`/`setBackground`/`clearIcon`/`setText` | per-key bg / blank / strip text                                                     | done (`setText` partial)                                                                          |
| `sendToDevice` (vendor raw HID)                      | —                                                                                   | **MISSING by policy** (RE hard rule); logged, never executed                                      |
| ~20 other vendor actions                             | —                                                                                   | intentionally unsupported → explicit WARN (not silent)                                            |

## Property Inspector routing

- PI → plugin: `sendToPlugin` (plugin receives it without `device`).
- plugin → PI: `sendToPropertyInspector`.
- PI may also send `set/getSettings`, `set/getGlobalSettings`, `openUrl`, `logMessage`; it receives
  `didReceiveSettings`/`didReceiveGlobalSettings`. The PI **cannot** drive the key.
- Host emits `propertyInspectorDidAppear`/`…DidDisappear` to the **plugin** on PI open/close — emit
  **exactly once** per open, keyed on instance `context`.

## Contract acceptance (how this is verified)

- Per-event payload shape locked by Catch2 (`willappear_payload_test.cpp`,
  `controller_token_test.cpp`, `test_plugin_device_bridge.cpp`, `test_sd_plugin_server.cpp`).
- End-to-end driven headlessly via the debug channel (`plugin.sendEvent`, `plugin.simulateAction`,
  `plugin.simulatePiSettings`, `input.*`) — see `quickstart.md`.
