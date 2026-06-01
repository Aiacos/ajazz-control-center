# OpenDeck study — UI + plugin system to replicate correctly

> Goal (user, 2026-06-01): study `ninjadev64/OpenDeck` (Svelte + Rust/Tauri)
> and replicate its UI and complete plugin functionality correctly in our
> project. This is a track distinct from the mirajazz device-backend migration.

OpenDeck is the reference Linux/Win/macOS stream-controller app. It runs Elgato
Stream Deck SDK plugins **and** the [OpenAction](https://openaction.amankhanna.me/)
API over a local WebSocket — the same protocol family our `SdPluginServer`
already speaks. So this is mostly a **gap-closing + correctness** exercise, not
a green-field rebuild.

## Architecture map (OpenDeck → our app)

| OpenDeck (Svelte/Rust)                                                                              | Our app                                              | Status                                                            |
| --------------------------------------------------------------------------------------------------- | ---------------------------------------------------- | ----------------------------------------------------------------- |
| `plugins/webserver.rs` — OpenAction WS server                                                       | `SdPluginServer` (Elgato v6 WS)                      | have; verify event parity                                         |
| `plugins/mod.rs` — manifest read + spawn (node / Wine / native by OS)                               | `plugin_manager` + `node_runner` + `plugin_manifest` | have; **Wine path for win-only plugins = likely gap**             |
| `events/inbound/*` (setImage/setTitle/setState/setSettings/getSettings/property_inspector)          | `PluginDeviceBridge` inbound handlers                | have; audit per-event coverage                                    |
| `events/outbound/*` (willAppear, keyDown/Up, dialRotate/dialPress, touchTap, sendToPI)              | `StreamDockInputService` → bridge → WS               | have; encoder/touch event parity to confirm                       |
| `components/DeviceView.svelte` — rows×cols keys + encoder row + touchpoint row, drag-drop instances | `DeviceView.qml` (Phase 26 OpenDeck-shaped editor)   | have; reconcile layout rules below                                |
| `components/ActionList.svelte` — action palette                                                     | `ActionLibraryPane.qml`                              | have (has the known `index` ReferenceError to fix)                |
| `components/PropertyInspectorView.svelte` — HTML PI in webview                                      | `PropertyInspectorController` + WebEngine            | have                                                              |
| `components/InstanceEditor.svelte` — Multi Actions / Toggle Actions / states                        | —                                                    | **likely GAP** (multi/toggle action instances + per-state config) |
| `components/PluginManager.svelte` + `PluginDetails` + `ListedPlugin`                                | plugin catalog + install models                      | have                                                              |
| `components/ProfileManager.svelte`                                                                  | `ProfileController`                                  | have                                                              |
| `application_watcher.rs` — per-app profile switching                                                | —                                                    | **likely GAP**                                                    |
| `device_sleep.rs` — idle sleep                                                                      | (sidecar keep-alive TODO)                            | partial                                                           |
| `store/profiles.rs` + `simplified_profile.rs`                                                       | `Profile` / schema                                   | have; compare model                                               |

## Key UI layout rules worth copying (DeviceView.svelte)

- Grid rows = key `rows`, then **one row for encoders** (`sliders`) if
  `encoders > 0`, then **one row for touchpoints** if `touchpoints > 0`.
- `overflowsX` when `max(columns, encoders, touchpoints) > 8`;
  `overflowsY` when `rows + (encoders?1:0) + (touchpoints?1:0) > 4` → scroll.
- Drag types: `"action"` (copy from palette) vs `"controller"` (move existing
  instance); drop calls backend `move_instance {source, destination, retain}`.
- Encoders are the `Encoder` controller and live in `profile.sliders`; keys in
  `profile.keys`. (Our model must distinguish key vs encoder vs touch-zone
  controllers — Phase 26 already split these.)

## Proposed phases (separate from mirajazz migration)

1. **Audit** — event-by-event parity of our `SdPluginServer`/`PluginDeviceBridge`
   against OpenDeck's `events/inbound`+`outbound`; produce a coverage table.
1. **Action instances + states** — port the InstanceEditor model (Multi Action,
   Toggle Action, per-state image/title) if our profile model lacks it.
1. **DeviceView reconciliation** — align our QML editor's row/overflow rules +
   drag-drop `move_instance` semantics to OpenDeck's.
1. **Per-app profiles** — application_watcher equivalent.
1. **Win-only plugins via Wine** — optional, matches OpenDeck's reach.

Reference clone (read-only): `ninjadev64/OpenDeck` (Svelte 4 + Tauri/Rust;
plugins via OpenAction WS). Our plugin epic history: see memory
`project_plugin_install_epic`, `project_plugin_profile_epic`,
`project_phase_25_to_26_pivot`.
