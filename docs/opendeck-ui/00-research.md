# OpenDeck UI — Research synthesis

> Status: research complete (2026-06-22). Source studies were run as parallel
> read-only sub-agents over local clones; this file is the durable summary
> (the raw clones live under `/tmp/opendeck-study` and `/tmp/mirabox-study`
> and are ephemeral — treat THIS file as the source of truth).

## Goal

Reimplement the OpenDeck control-center UX **inside this project**, driving our
existing backend (mirajazz sidecar + Elgato-SDK plugin host + profile model),
cleanly. Two interchangeable UI implementations, chosen by config (see
`02-architecture.md`).

## 1. OpenDeck (target UX) — `nekename/OpenDeck`, GPL-3.0

SvelteKit (UI) + Tauri/Rust (backend). License is **GPL-3.0**, identical to
this project → we may reuse OpenDeck code/components/assets.

- Build: `vite build` with `@sveltejs/adapter-static`, `ssr=false`,
  `prerender=true` → produces a **fully static SPA** in `build/` (~15 MB,
  mostly bundled key-title fonts; relative `./_app/...` paths → embeddable in
  `qrc:/` or served locally). Validated: it builds clean with Node 26 / npm.
- Runtime IPC: the frontend talks to the Tauri backend through
  `window.__TAURI_INTERNALS__.invoke(cmd, args)` and the event plugin
  (`window.__TAURI_EVENT_PLUGIN_INTERNALS__`). These are the **only** shim
  points needed to host the frontend outside Tauri.
- **Command contract: 35 `invoke` commands + 10 `listen` events** (full list +
  our-backend mapping in `01-contract.md`).
- Key rendering is **client-side**: `rendererHelper.ts` paints each key to a
  `<canvas>` then calls `invoke("update_image", {context, image: canvas.toDataURL("image/jpeg")})`. The backend just forwards the bytes to the
  device. (This simplifies our pipeline: for the webui path the frontend
  renders; our bridge decodes the data-URL and calls the sidecar `set_image`.)
- Layout (from the 4 official screenshots in OpenDeck `.github/readme/`):
  - Top bar: device `<select>` + profile `<select>` on the left; `Plugins` and
    `Settings` buttons on the right.
  - Center: device canvas — a `rows×cols` grid of `Key` canvases, plus encoder
    - touch lanes for Stream Deck + style devices.
  - Below canvas: the inline inspector for the selected slot (target radios,
    advanced options, custom icon, fg/bg colours, "General Configuration").
  - Right: `ActionList` sidebar (`w-18rem`, `bg-neutral-900`), searchable,
    actions grouped by plugin/category, draggable onto keys.
  - Overlays: Multi/Toggle action editor (`ParentActionView`), Manage Plugins
    (`PluginManager`/`PluginDetails`/`ListedPlugin`), Profiles
    (`ProfileManager`), Settings (`SettingsView`), Property Inspector
    (`PropertyInspectorView`, an `<iframe>` per instance).
- Design tokens: Tailwind v4, **no custom palette** — default `neutral` scale as
  the dark theme. Window bg `neutral-800 #262626`; sidebar `neutral-900 #171717`; action rows `neutral-950 #0a0a0a`; cards/inputs `neutral-700 #404040` with `neutral-600 #525252` borders; text `neutral-300 #d4d4d4`,
  labels `neutral-400`, headings `neutral-200`. Accent **indigo-600/500**;
  selection outline **blue-500 #3b82f6**; semantics red-400 / yellow / green /
  fuchsia. Key geometry: 144 px native (192 for 4×8), ~112 visible / ~132
  footprint, 10 % press-shrink, encoders round.
- Context string format: `device.profile.controller.position.index`;
  controllers `Keypad` / `Encoder`; synthetic parents `opendeck.multiaction` /
  `opendeck.toggleaction`.
- Core data types (`src/lib/*.ts`): see `01-contract.md`.

## 2. Our backend (what a new UI binds to)

A UI binds **only to QML singletons** — never to HID/the sidecar directly.

| Singleton                                                     | Drives                                                                                                                                                   |
| ------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `DeviceModel`                                                 | `refresh()`, `capabilitiesFor(codename)`, `connectedDevices()`; geometry roles (keyRows/cols, encoderCount, touchZoneCount, connected)                   |
| `StreamDockControlService`                                    | `setActiveDevice`, `setBrightness`, `clearAll`, `navigatePage`; signals `deviceActivated`, `keyImageAssigned/Cleared`, `encoderImageAssigned`            |
| `ProfileController`                                           | key/encoder/touch bindings, multi-action chains, toggle states, folders/pages, profile CRUD + export/import; signals `profileChanged`, `profilesChanged` |
| `PluginCatalog`                                               | `install`/`installFromFile`/`uninstall`/`toggleEnabled`, `installedActions()`                                                                            |
| `LoadedPlugins`, `PropertyInspectorController`, `PluginDebug` | running-plugin list, HTML PI hosting, protocol transcript                                                                                                |

- Device I/O: out-of-process Rust **`streamdock-host`** sidecar, newline-JSON
  over stdio. App→sidecar: `ping`, `set_brightness`, `set_image{serial,key, touchzone,width,height,rgba_b64}`, `keep_alive`, `render_test`.
  Sidecar→app: `connected`, `ready`, `input{serial,code,state,raw}`, `pong`,
  `ok`, `error`, `device_error`. Live device frames reach QML via image
  providers `image://livekey/<key0>?r=<rev>` and `image://liveencoder/...`.
- Headless **debug-control channel** (`debug_control_*`, `AJAZZ_DEBUG_CONTROL=1`,
  `scripts/ajazz-debug`): `screenshot`, `qml.tree/get/set/invoke/click`,
  `device.*`, `input.*`, `profile.*`, `plugin.*`. This is our verification
  harness (the `screenshot` RPC grabs the QQuickWindow, including an embedded
  `WebEngineView`).

## 3. Mirabox ⇄ AJAZZ — confirmed shared OEM (white-label)

AJAZZ "AKP" decks are the **same hardware** as Mirabox "Stream Dock" decks,
rebranded with different USB VID:PID (AKP05E↔N4, AKP03↔N3, AKP153↔293/HSV293S).
Dev org: `github.com/MiraboxSpace`; docs `sdk.key123.vip`; marketplace
`space.key123.vip`. Deeper OEM: "DianJia".

**Decisive caveat:** Mirabox's **Device SDK does NOT expose an open wire
protocol** — the HID logic is sealed in a precompiled `transport.{dll,so}`
blob. Adopting it would ship a closed binary into a GPL project → rejected.
**`mirajazz` stays the device/wire core** (open MPL-2.0, byte-level, validated).

## 4. MiraboxSpace repos — plugin-layer value, not a device foundation

| Repo                      | License                    | Use to us                                                                                                                                                                                                                                                                                                             |
| ------------------------- | -------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **StreamDock-Plugin-SDK** | MIT                        | Confirms the **Elgato Stream Deck plugin protocol IS the blessed interface** for AJAZZ/Mirabox HW (it's Elgato's SDK with `ESD*`→`HSD*` renames; same `ws://127.0.0.1:<port>` `-port/-pluginUUID/-registerEvent/-info` handshake + `connectElgatoStreamDeckSocket`). Validates our `.sdPlugin`/Elgato-host direction. |
| **StreamDock-Device-SDK** | MIT wrappers + closed blob | Reference only. Open part = N4Pro input-decode table (≡ retail AKP05E: knob rotate `0xA0/0xA1`, press `0x37/0x35/0x33/0x36`, swipe `0x38/0x39`, touch header `ACK ARX`) + per-model geometry + VID/PID + `-oem Model:VID:PID` aliasing. Cross-check data only — do NOT ship.                                          |
| **StreamDock-Plugins**    | GPL-3.0                    | ~22 real + 4 example `.sdPlugin` bundles → plugin catalog source + parity fixtures.                                                                                                                                                                                                                                   |
| StreamDock-Plugin-Builder | MIT                        | Plugin scaffolding skill + 9 plain-English API docs.                                                                                                                                                                                                                                                                  |
| StreamDock-OBS-Plugin     | GPL-2.0                    | OBS-side WS server.                                                                                                                                                                                                                                                                                                   |
| Icon-Pack-Template        | MIT                        | `.sdIconPack` marketplace asset format.                                                                                                                                                                                                                                                                               |

**Adopt at the plugin layer (additive, no pivot):**

1. Teach our `.sdPlugin` loader the two Mirabox manifest extensions:
   `"Nodejs": {"Version": "20"}` and the `"Knob"` / `"Information"` controller
   names → the entire Mirabox catalog loads unmodified.
1. Add `StreamDock-Plugins` / the "Space" marketplace as a catalog source
   alongside OpenDeck.
1. Use the Device-SDK source to confirm PROVISIONAL geometry/input values.

## Bottom line

- **Device/wire core → mirajazz** (unchanged).
- **Plugin runtime → our Elgato-SDK host** + Mirabox manifest extensions +
  Space/StreamDock-Plugins catalog (MiraboxSpace is "core" *here*).
- **UX → OpenDeck**, delivered by one of two interchangeable UI
  implementations (config-selected): native QML or embedded Svelte. See
  `02-architecture.md`.
