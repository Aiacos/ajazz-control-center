# OpenDeck UI — Architecture & settings (dual implementation)

## Layering

```
┌─ UI (selectable) ─────────────────────────────────────────────┐
│   mode=qml    →  native QML OpenDeck-style UI                  │
│   mode=webui  →  OpenDeck Svelte SPA in QWebEngineView         │
├─ Seam ────────────────────────────────────────────────────────┤
│   shared contract (01-contract.md): commands + events + types  │
│   webui: OpenDeckBridge (C++/QWebChannel) + JS Tauri shim      │
│   qml:   direct singleton calls (same mapping table)           │
├─ Services (unchanged) ─────────────────────────────────────────┤
│   ProfileController · PluginCatalog · LoadedPlugins ·          │
│   PropertyInspectorController · DeviceModel ·                  │
│   StreamDockControlService                                     │
├─ Plugin runtime ───────────────────────────────────────────────┤
│   Elgato-SDK host (+ Mirabox manifest extensions, Space store) │
├─ Device/wire core ─────────────────────────────────────────────┤
│   mirajazz sidecar (streamdock-host): set_image / input / …    │
└────────────────────────────────────────────────────────────────┘
```

Everything from "Services" down is **shared and unchanged**. The two UIs are
peers behind the same contract; neither is privileged at the backend.

## Why two implementations

- **qml** (native): cleaner integration with our stack, uses the live image
  providers, debuggable via the existing `qml.*` channel, no web engine for the
  main UI. Likely the long-term default.
- **webui** (embedded OpenDeck): pixel-faithful 1:1 OpenDeck UX, maximal reuse
  of OpenDeck's actual components, PI iframes "for free". Reference + parity
  oracle, and a fallback if a native surface lags.

The config switch lets us develop both, compare them against the OpenDeck
reference, and ship whichever is appropriate per platform/build.

## Settings / configuration file

Resolution order (first hit wins):

1. **Env override** `AJAZZ_UI_MODE=qml|webui` (dev/CI/debug-channel).
1. **Config file** `ui.mode` key in `$XDG_CONFIG_HOME/Aiacos/AJAZZ Control Center/ui.conf` (INI, human-editable). Mirrored into the in-app Settings UI.
1. **Default**: `qml`.

```ini
# $XDG_CONFIG_HOME/Aiacos/AJAZZ Control Center/ui.conf
[ui]
mode = qml        # qml | webui
# webui-only:
webui.devUrl =    # optional http://… to load a live Svelte dev server
webui.bundle = :/opendeck/build   # default: bundled static SPA (qrc)
```

- Read once at startup by `main.cpp` via a small `UiModeResolver` helper
  (env → QSettings/`ui.conf` → default). The chosen mode selects the root:
  the native QML scene, or a `WebUiHost` (QML `WebEngineView` + injected shim +
  `OpenDeckBridge` registered on the QWebChannel).
- Exposed read-only to QML as `App.uiMode`; switching mode requires a restart
  (documented in Settings). A debug-channel method `ui.mode get/set` is added so
  the verification harness can flip modes headlessly.
- The existing app Settings (theme, accent, launch-on-login, per-app profiles)
  are unchanged and shared by both UIs.

## Components per implementation

| OpenDeck surface    | webui (reuse Svelte)                           | qml (native)                                  |
| ------------------- | ---------------------------------------------- | --------------------------------------------- |
| Shell / top bar     | `+layout.svelte`, `DeviceSelector`             | `OpenDeckShell.qml`                           |
| Canvas + keys       | `DeviceView`, `Key` (canvas render)            | `DeviceCanvas.qml` + `image://livekey`        |
| Action list         | `ActionList`                                   | `ActionList.qml`                              |
| Inline inspector    | `InstanceEditor`                               | `Inspector.qml`                               |
| Multi/Toggle editor | `ParentActionView`                             | `ParentActionView.qml`                        |
| Property Inspector  | `PropertyInspectorView` (iframe)               | reuse our `PropertyInspector.qml` (WebEngine) |
| Profiles            | `ProfileManager`                               | `ProfileManager.qml`                          |
| Plugins store       | `PluginManager`/`PluginDetails`/`ListedPlugin` | `PluginStore.qml` (exists; reshape)           |
| Settings            | `SettingsView`                                 | `SettingsView.qml` (exists)                   |

The qml side reuses/reshapes existing QML where possible (PluginStore,
Settings, PropertyInspector) and rebuilds the canvas/list/inspector to the
OpenDeck layout & tokens documented in `00-research.md`.

## File layout (planned)

```
src/app/
  src/ui_mode_resolver.{hpp,cpp}      # env→config→default
  src/opendeck_bridge.{hpp,cpp}       # webui: QWebChannel command/event impl
  qml/WebUiHost.qml                   # webui: WebEngineView + shim injection
  qml/opendeck/…                      # qml: native OpenDeck-style components
  webui/                              # vendored OpenDeck frontend (GPL, NOTICE)
    (built SPA bundled to qrc :/opendeck/build at configure time)
resources/opendeck-shim/tauri-shim.js # JS shim → QWebChannel
docs/opendeck-ui/*.md                 # this documentation set
```

`webui/` vendoring keeps OpenDeck's `LICENSE.md` + a `NOTICE` recording the
upstream commit; the build step runs `vite build` and stages `build/` into the
qrc. (For early dev, `webui.devUrl` can point at `vite dev` for hot reload.)
