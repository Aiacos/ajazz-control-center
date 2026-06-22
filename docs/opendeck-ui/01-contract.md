# OpenDeck UI — IPC contract & data model

The OpenDeck frontend speaks a fixed contract. Whichever UI implementation we
ship, the **same contract is the seam**:

- **webui path**: a C++ `OpenDeckBridge` exposes these commands over QWebChannel
  and a JS shim maps `window.__TAURI_INTERNALS__.invoke` → the bridge.
- **qml path**: the native QML reimplementation calls the equivalent backend
  singleton methods directly (no serialization), reusing the same mapping table
  below so behaviour is identical.

## Data types (from OpenDeck `src/lib/*.ts`)

```ts
DeviceInfo   = { id, name, rows, columns, encoders, touchpoints, type }
Action       = { name, uuid, plugin, tooltip, icon, visible_in_action_list,
                 supported_in_multi_actions, property_inspector, controllers[],
                 states: ActionState[] }
ActionState  = { image, image_scale, background_colour, name, text, show,
                 colour, stroke_colour, alignment:"top"|"middle"|"bottom",
                 family, style, size, stroke_size, underline }
ActionInstance = { action, context, states: ActionState[], current_state,
                   settings, children: ActionInstance[] | null }
Context      = { device, profile, controller:"Keypad"|"Encoder", position }
Profile      = { device, id, keys: (ActionInstance|null)[],
                 sliders: (ActionInstance|null)[] }     // sliders = encoders
```

Context string wire format: `device.profile.controller.position[.index]`.

## Commands (35 `invoke`) → our backend

### Read / bootstrap

| Command                                                 | Args          | Our mapping                                                                                        |
| ------------------------------------------------------- | ------------- | -------------------------------------------------------------------------------------------------- |
| `get_port_base`                                         | —             | SdPluginServer base port (PI/plugins WS)                                                           |
| `get_build_info`                                        | —             | `applicationVersion()` + build string                                                              |
| `get_settings` / `set_settings`                         | `{settings}`  | UI settings store (see `02`)                                                                       |
| `get_localisations`                                     | `{locale}`    | i18n table (start: `{}`)                                                                           |
| `get_fonts`                                             | —             | bundled key-title font list                                                                        |
| `get_devices`                                           | —             | `DeviceModel.connectedDevices()` → `DeviceInfo[]`                                                  |
| `get_categories`                                        | —             | `PluginCatalog.installedActions()` grouped by plugin + OpenDeck built-ins → `{category: Action[]}` |
| `list_plugins`                                          | —             | installed plugins (LoadedPlugins/PluginCatalog)                                                    |
| `get_profiles`                                          | `{device}`    | `ProfileController` profile ids for device                                                         |
| `get_selected_profile`                                  | `{device}`    | active `Profile{keys[],sliders[]}`                                                                 |
| `set_selected_profile`                                  | `{device,id}` | `ProfileController.loadProfile`                                                                    |
| `get_applications`                                      | —             | installed apps (app-profile mapping)                                                               |
| `get_application_profiles` / `set_application_profiles` | …             | per-app profile map                                                                                |
| `make_info`                                             | `{plugin}`    | PI `info` payload (PropertyInspectorController)                                                    |

### Write / edit

| Command                                              | Args                    | Our mapping                                                                            |
| ---------------------------------------------------- | ----------------------- | -------------------------------------------------------------------------------------- |
| `create_instance`                                    | `{context,action}`      | place action at slot → `commitKeyBinding` / `commitEncoderBinding` / `appendKeyAction` |
| `move_instance`                                      | `{src,dst}`             | `swapKeyBindings` / `reorderKeyAction`                                                 |
| `remove_instance`                                    | `{context}`             | `removeKeyActionAt` / clear binding                                                    |
| `set_state`                                          | `{context,index,state}` | per-state title/icon/text edit; `commitToggleStates`                                   |
| `update_image`                                       | `{context,image}`       | decode data-URL → `set_image` (sidecar) for that key/encoder                           |
| `trigger_virtual_press`                              | `{context}`             | synthetic input (StreamDockInputService)                                               |
| `switch_property_inspector`                          | `{context,...}`         | PropertyInspectorController open/close                                                 |
| `install_plugin` / `remove_plugin` / `reload_plugin` | \`{id                   | url                                                                                    |
| `show_settings_interface`                            | `{context}`             | PI settings page                                                                       |
| `rename_profile` / `delete_profile`                  | `{device,...}`          | `ProfileController`                                                                    |

### Shell / misc

| Command                                                | Mapping                     |
| ------------------------------------------------------ | --------------------------- |
| `open_url`                                             | `QDesktopServices::openUrl` |
| `open_config_directory` / `open_log_directory`         | open the resolved dirs      |
| `backup_config_directory` / `restore_config_directory` | zip/unzip config            |
| `restart`                                              | relaunch the app            |

## Events (10 `listen`) — emitted C++ → JS (webui) / signals (qml)

| Event                    | Trigger / payload                   | Our source                         |
| ------------------------ | ----------------------------------- | ---------------------------------- |
| `devices`                | device set changed                  | `DeviceModel` hot-plug             |
| `switch_profile`         | active profile changed              | `ProfileController.profileChanged` |
| `applications`           | focused-app changed                 | active-window watcher              |
| `rerender_images`        | bindings changed → repaint keys     | profile/binding change             |
| `update_state`           | a slot's current_state changed      | `cycleInstanceState`               |
| `key_moved`              | after a move/swap                   | post-`move_instance`               |
| `show_alert` / `show_ok` | plugin `showAlert`/`showOk` overlay | plugin host                        |
| `plugin_reloaded`        | plugin (re)loaded                   | PluginCatalog/host                 |
| `device_brightness`      | brightness changed                  | `setBrightness`                    |

Plus the **Property Inspector `postMessage` channel** (connect / windowOpened /
windowClosed / openUrl / fetch↔fetchResponse) and `onOpenUrl` deep links — both
already implemented in our `PropertyInspectorController`.

## Rendering note

OpenDeck renders key images **client-side** and pushes via `update_image`.

- webui path: keep that — bridge decodes the JPEG data-URL to RGBA and calls
  the sidecar `set_image`; the canvas the user sees IS the device image.
- qml path: we already render server-side and expose `image://livekey/...`; the
  native UI uses that provider directly and `update_image` is a no-op there.

Both paths converge on the sidecar `set_image{rgba_b64}` for the physical device.
