# Technology Stack — v2.0 Modular Plugin & Binding System

**Project:** AJAZZ Control Center v2.0
**Researched:** 2026-06-06
**Scope:** Delta stack — new or changed additions needed for v2.0 on top of the existing v1.3
base. The existing stack (C++20, Qt 6.7+, CMake/Ninja, Rust/Cargo mirajazz sidecar,
nlohmann::json PRIVATE to `ajazz_plugins`, Catch2 v3, Python 3.11+) is NOT re-litigated.
**Overall confidence:** HIGH for Qt/C++ stack; MEDIUM for per-app profile OS APIs; HIGH for
native-Windows-exe feasibility verdict.

______________________________________________________________________

## Context: What Already Exists

The v1.3 codebase has more implemented than the GSD planning state suggests. Before building
anything, verify against these in-tree files:

| Capability                                    | In-tree file                                          | Status     |
| --------------------------------------------- | ----------------------------------------------------- | ---------- |
| WS plugin server (Elgato v6)                  | `src/app/src/sd_plugin_server.{hpp,cpp}`              | DONE       |
| Plugin manifest parser                        | `src/app/src/plugin_manifest.{hpp,cpp}`               | DONE       |
| Plugin manager (Node/HTML/native dispatch)    | `src/app/src/plugin_manager.{hpp,cpp}`                | DONE       |
| Node.js spawn + version resolve               | `src/app/src/node_runner.{hpp,cpp}`                   | DONE       |
| Mirabox shim (`connectMiraBoxSDSocket` alias) | `src/app/src/plugin_mirabox_shim.hpp`                 | DONE       |
| PI controller + profile + channel             | `src/app/src/property_inspector_controller.{hpp,cpp}` | DONE       |
| PI bridge (`$SD` JS surface, settings I/O)    | `src/app/src/pi_bridge.{hpp,cpp}`                     | DONE       |
| PI URL interceptor + policy                   | `src/app/src/pi_url_policy.{hpp,cpp}` + interceptor   | DONE       |
| `cefQuery` polyfill (`pi_cef_shim`)           | `src/app/src/pi_cef_shim.{hpp,cpp}`                   | check grep |
| `sdpi.css` bundled resource                   | `resources/streamdeck/sdpi.css`                       | check grep |
| `applicationHints` in profile schema          | `src/core/include/ajazz/core/profile.hpp:189`         | DONE       |
| Crash tracker (3-in-30s window)               | `src/app/src/plugin_crash_tracker.{hpp,cpp}`          | DONE       |
| Zip-slip-guarded extractor                    | `src/app/src/sdplugin_extractor.{hpp,cpp}`            | DONE       |
| Ed25519 signature verify-gate                 | `src/plugins/`                                        | DONE       |
| OOP Python plugin host                        | `src/plugins/src/out_of_process_plugin_host.cpp`      | DONE       |
| `PerAppProfiles` capability flag              | `src/core/include/ajazz/core/capabilities.hpp`        | DONE       |

Run `grep -rn <symbol> src/` before building anything this research describes.

______________________________________________________________________

## 1. Modular Plugin-Host Abstraction

### Current State

`PluginManager` already dispatches by CodePath extension: `.js/.mjs/.cjs` → Node.js via
`QProcess`; `.html/.htm` → headless `QWebEnginePage` in a shared `QQuickWebEngineProfile` with
the Mirabox shim; everything else → native `QProcess`. Python plugins go through the separate OOP
host in `src/plugins/` (the pre-existing IPC channel over fork+exec with line-delimited JSON).

The v1.3 dispatch is a `switch`-on-extension inside `PluginManager::spawn()`. It works but is
not abstracted behind an interface — each host type is a `QProcess`+`LivePlugin` struct or a raw
`QWebEnginePage*` stored in separate collections. For v2.0 a **`IPluginHost` interface** is the
correct refactor target: one virtual `start()`, `stop()`, `isRunning()` per host type, with
`NodePluginHost`, `HtmlPluginHost`, `NativePluginHost`, and `PythonPluginHost` as the four
concrete implementations. The existing `PluginManager` becomes the orchestrator that holds
`std::map<QString, std::unique_ptr<IPluginHost>>`.

### Stack Additions Needed

**No new external libraries.** All four host types are implemented with:

| Component                     | Qt module / runtime                      | Purpose                   | Integration point                                        |
| ----------------------------- | ---------------------------------------- | ------------------------- | -------------------------------------------------------- |
| `IPluginHost` (new interface) | C++20 pure virtual                       | Unified start/stop/status | `PluginManager` holds `unique_ptr<IPluginHost>`          |
| `NodePluginHost`              | `Qt6::Core` (`QProcess`)                 | Spawn `node >=20` child   | Reuse `node_runner.hpp` + `buildNodeArgv()`              |
| `HtmlPluginHost`              | `Qt6::WebEngineCore` (`QWebEnginePage`)  | Headless Chromium page    | Reuse shared `m_htmlProfile` + `makeMiraboxShim()`       |
| `NativePluginHost`            | `Qt6::Core` (`QProcess`)                 | Spawn arbitrary binary    | No change — Wine path added here (see §4)                |
| `PythonPluginHost`            | `src/plugins/out_of_process_plugin_host` | OOP IPC + sandbox         | Reuse existing OOP host; wire into `IPluginHost` wrapper |

**Key constraint (COD-031):** All of `src/app/src/plugin_*.cpp` must stay Qt-JSON only. The OOP
Python host in `src/plugins/` has its own wire format (not nlohmann). Nothing changes here.

**Key constraint (CLAUDE.md `QML_SINGLETON`):** `PluginManager` is already registered; do not
add a default constructor. `static_assert(!std::is_default_constructible_v<PluginManager>)` if
you promote it to a singleton.

______________________________________________________________________

## 2. Property Inspector via QWebEngine + QWebChannel

### Current State

The PI stack is substantially done:

- `PropertyInspectorController` creates a `QQuickWebEngineProfile` per plugin UUID, installs
  `QQmlWebChannel` with `PIBridge` as `"$SD"`, wires URL interceptor/policy.
- `PIWebView.qml` binds the `{profile, webChannel, url}` trio correctly (the `no page Q_PROPERTY` gotcha is handled).
- `PIBridge` has `setSettings`/`getSettings`/`setGlobalSettings`/`getGlobalSettings` as
  `Q_INVOKABLE` slots with atomic `QSaveFile` persistence under `AppDataLocation/plugins/<uuid>/`.
- `PIUrlRequestInterceptor` + `pi_url_policy` gate sub-resource loads.

**Verified gaps (grep the tree):**

1. `cefQuery` polyfill — does `src/app/src/pi_cef_shim.{hpp,cpp}` exist? If not, it must be
   added: a `QWebEngineScript` at `DocumentCreation/MainWorld/runsOnSubFrames` defining
   `window.cefQuery` that delegates to `channel.objects.$SD.invoke(json)` (§8 of the SDK).
1. `sdpi.css` — does `resources/streamdeck/sdpi.css` exist? If not, vendor a copy from
   `elgatosf/sdpi-css` and add a qrc-redirect in the interceptor.
1. `Inspector.qml` → `PropertyInspectorController.loadInspector(...)` wire — grep for
   `loadInspector` callers in `src/app/qml/`; if zero, the UI trigger is the remaining gap.
1. `sendToPlugin`/`sendToPropertyInspector` relay — `PIBridge::sendToPlugin` is a logging stub;
   must be wired to `SdPluginServer::sendEvent(uuid, "sendToPlugin", payload)`.

### Stack (No New Libraries)

| Module                 | Purpose                                                              | Already Linked                    |
| ---------------------- | -------------------------------------------------------------------- | --------------------------------- |
| `Qt6::WebEngineQuick`  | Chromium-based PI rendering (`QWebEnginePage`, `QWebEngineProfile`)  | Yes, `AJAZZ_HAVE_WEBENGINE`-gated |
| `Qt6::WebChannelQuick` | `QQmlWebChannel` — `WebEngineView.webChannel` accepts ONLY this type | Yes, same gate                    |
| `Qt6::WebEngineCore`   | `QWebEngineScript` injection at `DocumentCreation`                   | Yes                               |
| `Qt6::Core`            | `QSaveFile`, `QStandardPaths`, `QJsonDocument` for settings          | Yes                               |

**Known Qt API gap:** `QQuickWebEngineScriptCollection` is forward-declared only in the public
`qquickwebengineprofile.h`; the complete type requires the private header
`<QtWebEngineQuick/private/qquickwebenginescriptcollection_p.h>`. This is already handled in
`plugin_manager.cpp` with a `static_assert(QT_VERSION >= QT_VERSION_CHECK(6, 7, 0))` guard and
`Qt6::WebEngineQuickPrivate` link target in CMakeLists. Do NOT use the public-API workaround
(it is incomplete). If Qt bumps this API to public in 6.8+, update the CMakeLists.

**Minimum Qt version:** 6.7 (project minimum, unchanged).

______________________________________________________________________

## 3. Per-App Profile Switching (Foreground-App Detection)

### What Exists

The profile schema already has `applicationHints: std::vector<std::string>` in
`src/core/include/ajazz/core/profile.hpp:189`. The `PerAppProfiles = 1u << 14` capability flag
is declared. No `ApplicationWatcher` class exists in `src/app/src/`.

### Reference: OpenDeck's Approach

OpenDeck (`ninjadev64/OpenDeck`) implements `application_watcher.rs` using:

- **`active-win-pos-rs` crate** — abstraction over `GetForegroundWindow` (Windows), `NSWorkspace`
  (macOS), and XCB EWMH `_NET_ACTIVE_WINDOW` + Wayland compositor protocols (Linux).
- **250 ms polling loop** with `tokio::time::sleep`.
- **`sysinfo` crate** for process launch/terminate events (500 ms poll).
- On foreground-app change: emit `"switch_profile"` event, match against the profile's app hints.

OpenDeck delegates this entirely to the `active-win-pos-rs` crate. This is a viable approach for
the Rust sidecar but NOT for our C++ Qt app layer. We need a C++ implementation.

### Platform-by-Platform Analysis

#### Linux X11

**APIs available:** XCB + `xcb-ewmh` library (`xcb_ewmh_get_active_window`). Subscribe to
`PropertyNotify` events on the root window watching `_NET_ACTIVE_WINDOW`. When it fires, call
`xcb_get_property` to read the new window XID, then `xcb_ewmh_get_wm_pid` to get the PID, then
`/proc/<pid>/comm` or `readlink /proc/<pid>/exe` for the process name.

**System packages:** `libxcb-ewmh-dev` / `xcb-util-wm` (Arch). Already indirectly present on
most Linux desktops running X11.

**Qt integration:** Qt 6 uses XCB internally on Linux/X11 — access via
`QNativeInterface::QX11Application` (Qt 6.2+). Alternatively, open a separate XCB connection
(simpler for a watcher). Do NOT use `QX11Info` (it's Qt5/X11Extras-only).

**Confidence:** HIGH — `_NET_ACTIVE_WINDOW` is the EWMH standard; every ICCCM-compliant WM
exposes it.

#### Linux Wayland

**The hard truth:** There is no unified Wayland protocol for active-window querying from
outside the compositor. Each compositor does it differently:

| Compositor                            | Method                                               | Complexity                                                                           |
| ------------------------------------- | ---------------------------------------------------- | ------------------------------------------------------------------------------------ |
| wlroots-based (Sway, Hyprland, labwc) | `zwlr_foreign_toplevel_manager_v1` protocol          | MEDIUM — compositor must support it; Hyprland also supports via `hyprctl` IPC socket |
| KDE Plasma/KWin                       | KWin scripting via `qdbus6` + parse journal          | HIGH complexity, fragile                                                             |
| GNOME/Mutter                          | `window-calls-extended` GNOME Shell extension + DBus | HIGH complexity; requires user to install extension                                  |

**Recommended approach:** Implement in priority order:

1. `zwlr_foreign_toplevel_manager_v1` (covers wlroots ecosystem — largest Wayland share on
   non-GNOME desktops). Parse `app_id` from toplevel state change events.
1. Hyprland IPC socket fallback (`/tmp/hypr/<sig>/`.hyprland.his socket; `hyprctl activewindow -j`).
1. Accept GNOME Wayland as not supported in v2.0 (document explicitly; GNOME Wayland without a
   shell extension has no publicly accessible active-window API).

**System packages:** `wayland-client` (almost always present). `zwlr_foreign_toplevel_manager_v1`
requires `wayland-protocols` (for `wlr-foreign-toplevel-management-unstable-v1.xml`) or use the
pre-generated C bindings from `wlroots` headers.

**Confidence:** MEDIUM — the protocol is well-documented but compositor coverage is incomplete;
GNOME is a known gap.

**Qt-native approach for Wayland:** There is no `QNativeInterface` for foreign-toplevel on Qt
6.7. You must use the raw Wayland protocol API (`wl_registry_bind`, `zxdg_foreign_*`). This is
not a Qt limitation — the Wayland security model intentionally restricts this information.

#### Windows

**APIs:** `GetForegroundWindow()` (Win32) → `GetWindowThreadProcessId()` → `OpenProcess()` +
`QueryFullProcessImageName()` (Vista+). Map the exe path to a basename for matching against
profile hints. Use a `QTimer` polling at 250 ms (matching OpenDeck) in the `ApplicationWatcher`
class.

**System packages:** None — pure Win32 (user32.dll + kernel32.dll), always available.

**Confidence:** HIGH — these APIs are stable across Windows Vista → 11.

#### macOS

**APIs:** `NSWorkspace.shared.frontmostApplication` (AppKit) via
`NSWorkspace.didActivateApplicationNotification` (push-based, no polling). Use the notification
as the trigger; read `NSRunningApplication.localizedName` / `bundleIdentifier`. Must be wrapped
in an Objective-C++ compilation unit (`.mm` file) compiled with `OBJCXX`.

**System packages:** None — AppKit is the system framework, linked via `-framework AppKit`.
Already used by Qt on macOS.

**Confidence:** HIGH — `NSWorkspace` API is stable across macOS 10.13+.

### Recommended Implementation Shape

```
src/app/src/
├── application_watcher.hpp          # IApplicationWatcher Q_OBJECT interface
├── application_watcher_x11.cpp      # XCB/EWMH implementation (Linux X11)
├── application_watcher_wayland.cpp  # zwlr_foreign_toplevel (Wayland, guarded)
├── application_watcher_win32.cpp    # GetForegroundWindow poll (Windows)
└── application_watcher_macos.mm     # NSWorkspace notification (macOS, OBJCXX)
```

Factory function in CMakeLists selects the right implementation via `Q_OS_LINUX` + env detection
(`WAYLAND_DISPLAY` at runtime, or CMake `Qt6_QPA_DEFAULT_PLATFORM`). Do NOT write a single file
with `#ifdef` chains — the platform files are already separate in `src/plugins/src/`.

**Qt module addition needed:** None for Windows/macOS. For Linux, if the XCB connection is
opened via `QNativeInterface::QX11Application`, add `Qt6::Gui` (already linked) or `xcb` raw
link (no new Qt module; system `xcb` is already a transitive dep of Qt6-xcb). For Wayland:
`wayland-client` system library + `Qt6::WaylandClient` if using the Qt wayland abstraction, but
raw `wl_*` calls only need `wayland-client` directly.

**Polling vs event-driven:** Push-based on macOS (NSWorkspace notification) and Wayland
(toplevel-changed event). Polling on X11 (subscribe PropertyNotify; still event-driven via XCB
file descriptor in a `QSocketNotifier`). Polling on Windows (`QTimer` at 250 ms, same as
OpenDeck).

**No new CMake `find_package` calls for macOS or Windows.** For Linux, add:

```cmake
find_package(XCB COMPONENTS EWMH)  # provides xcb::ewmh target (Fedora: xcb-util-wm-devel)
```

Only needed when `AJAZZ_BUILD_APP_WATCHER_X11` is enabled (auto-detected from platform).

______________________________________________________________________

## 4. Native Execution of Windows-Only `.sdPlugin` Plugins on Linux/macOS

### The Question

The manifest supports `CodePathWin` (Windows `.exe`), `CodePathMac` (macOS binary), and generic
`CodePath`. A plugin that ships only `CodePathWin` has no Linux/macOS binary. Can we run it
natively without Wine?

### Feasibility Assessment

**Option A: PE Loader (running Windows EXE natively on Linux/macOS)**

Verdict: **NOT FEASIBLE for production use.**

PE loaders (e.g. `polycone/pe-loader`) parse and map Windows `.exe` files into memory and
attempt to resolve Win32 API calls. They are research-grade projects, not production-ready:

- They require re-implementing substantial portions of the Win32 API surface.
- `.sdPlugin` native EXEs call Win32 GDI, registry, COM, DirectX (UI/rendering), and
  `HidD_SetFeature` — none of which have open PE-loader implementations.
- `active-win-pos-rs` (OpenDeck's own dependency) cannot be mapped through a PE loader.
- This approach is roughly equivalent to writing a second Wine from scratch.
- Confidence that a PE loader covers any real `.sdPlugin` exe: near zero.

**RECOMMENDATION: Do not pursue PE loaders. Not worth engineering time.**

**Option B: Re-host the Plugin as Node.js or HTML**

Most real-world Windows-only `.sdPlugin` executables with `CodePathWin: "plugin.exe"` are
compiled from the **Node.js SDK** using `pkg`, `nexe`, or a bundler. The original source is
JavaScript. The compiled `.exe` simply bundles the Node runtime with the JS code.

**Viable native path for these:** Ship the JS source alongside the exe (many plugins do), or
detect that the `CodePathWin` extension matches a known bundler pattern and refuse to run it
with a clear user message: "This plugin requires Windows. Contact the plugin author for a
cross-platform version."

**Confidence (finding):** MEDIUM — based on examining real `.sdPlugin` packages. Some do ship
JS source; many ship only the compiled `.exe`. No authoritative survey exists.

**Option C: Shimming `connectElgatoStreamDeckSocket` via a Stub Node.js or Python wrapper**

If a plugin's Windows `.exe` is a thin wrapper that calls `connectElgatoStreamDeckSocket` over
a loopback WebSocket (the Elgato SDK standard), in principle one could intercept that call via
a hook. However:

- The hook site is inside the compiled binary, requiring Windows binary patching.
- This is Wine's job — Wine intercepts Win32 API calls at the OS layer.
- Without Wine, there is no reasonable interception point.

**RECOMMENDATION: Not feasible without Wine.**

**Option D: Wine (the practical fallback)**

OpenDeck's implementation (`src-tauri/src/plugins/mod.rs`) shows the production-validated path:

- Detect Wine availability: `which wine` / `flatpak-spawn --host wine --version`.
- For a plugin where manifest `OS` contains only `"windows"` but the current platform is
  Linux/macOS, invoke: `wine <CodePathWin> -port <p> -pluginUUID <u> -registerEvent registerPlugin -info <json>`.
- Optional: per-plugin isolated `WINEPREFIX` at `<pluginDir>/wineprefix/` (OpenDeck's
  `"separatewine"` setting).
- The plugin connects back over the loopback WebSocket — Wine does not intercept that
  (TCP is native even under Wine).

**This is the ONLY viable path for compiled Windows-only native `.exe` plugins.**

**Confidence:** HIGH — OpenDeck has shipped this in production; real users run Windows-only
Stream Deck plugins via Wine on Linux.

### Recommendation

Implement in this priority order:

1. **Detect `CodePathWin`-only manifest on non-Windows.** Do not silently skip — surface a
   user-visible "Wine required" notice in the plugin catalog UI.
1. **Wine spawn path in `PluginManager::spawn()`** (the `NativePluginHost` / native else-branch):
   when `!manifest.codePathWin.isEmpty() && manifestHasOnlyWindowsOS(manifest)` and
   `Q_OS_LINUX || Q_OS_MAC`, check `resolveWineExe()` (analogous to `resolveNode20Plus`) and
   spawn `wine <codePathWin> <standard-argv>` via `QProcess`. If Wine is absent, call
   `disableWithNotice(pluginId, "Wine not found")`.
1. **WINEPREFIX isolation** (optional, v2.0 stretch): set `WINEPREFIX=<pluginDir>/wineprefix`
   in `buildChildEnv()` when the setting is enabled.

**Stack addition:** No new C++ library. Wine is a system package (`wine` on all Linux/macOS).
Detect with `QStandardPaths::findExecutable("wine")` — zero new CMake deps.

**What NOT to implement:**

- PE loader — infeasible.
- Shimming `connectElgatoStreamDeckSocket` inside the Windows binary — requires Wine.
- Bundling Wine — extreme binary size, Flatpak policy violation, user license concerns.

______________________________________________________________________

## 5. Action Instances + States (Multi Action / Toggle Action)

### What Exists

`Profile`, `Binding`, and `ActionEngine` support a single action per binding. No `MultiAction`
or `ToggleAction` instance type exists in-tree (confirmed by `grep -rn MultiAction src/`).

### Stack

**No new libraries.** The data model changes are all in `src/core/` profile types:

- Add `ActionInstanceType { Single, Multi, Toggle }` enum to `profile.hpp`.
- Add `per_state_config: vector<StateConfig>` where `StateConfig` has optional `imagePath`,
  `title`, `settings` fields.
- `ActionEngine` grows a `currentState[bindingId]` map (in-memory, reset on profile load).

Toggle state lives in memory (transient) — do not persist it; OpenDeck resets on launch.
Multi Action chaining lives in the profile as a `vector<ActionRef>` on the binding.

______________________________________________________________________

## 6. Complete Stack Table (Additions Only)

### New Qt Modules

| Module                        | Version | Purpose                                          | CMake target   | Gate                   |
| ----------------------------- | ------- | ------------------------------------------------ | -------------- | ---------------------- |
| `Qt6::WebEngineQuick`         | 6.7+    | PI HTML render (Chromium)                        | already linked | `AJAZZ_HAVE_WEBENGINE` |
| `Qt6::WebChannelQuick`        | 6.7+    | `QQmlWebChannel` (`$SD` bridge)                  | already linked | `AJAZZ_HAVE_WEBENGINE` |
| `Qt6::WebEngineCore`          | 6.7+    | `QWebEngineScript` injection                     | already linked | `AJAZZ_HAVE_WEBENGINE` |
| `Qt6::WebEngineQuickPrivate`  | 6.7+    | `QQuickWebEngineScriptCollection` complete type  | already linked | `AJAZZ_HAVE_WEBENGINE` |
| `Qt6::Gui` (XCB native iface) | 6.7+    | `QNativeInterface::QX11Application` on Linux X11 | already linked | Linux X11 only         |

**No new Qt module find_package calls needed** for the plugin-host abstraction, PI completion,
action instances, or the Wine path. The existing CMakeLists.txt gates are sufficient.

### New System Libraries / OS APIs

| Library / API                       | Platform      | Purpose                                            | CMake                                               |
| ----------------------------------- | ------------- | -------------------------------------------------- | --------------------------------------------------- |
| `xcb-ewmh` (`xcb-util-wm`)          | Linux X11     | `_NET_ACTIVE_WINDOW` query                         | `find_package(XCB COMPONENTS EWMH)`                 |
| `wayland-client`                    | Linux Wayland | `zwlr_foreign_toplevel_manager_v1`                 | system dep, already transitive via Qt               |
| Win32 `user32.dll` (no link change) | Windows       | `GetForegroundWindow` + `GetWindowThreadProcessId` | already linked via Qt                               |
| AppKit (`-framework AppKit`)        | macOS         | `NSWorkspace` notification                         | already linked via Qt                               |
| `wine` (runtime, not a compile dep) | Linux/macOS   | Windows-only native plugin execution               | detect via `QStandardPaths::findExecutable("wine")` |

### New C++ Source Files (New, Not Replacements)

| File                              | Location       | Why                                                           |
| --------------------------------- | -------------- | ------------------------------------------------------------- |
| `i_plugin_host.hpp`               | `src/app/src/` | Interface for the unified host abstraction                    |
| `node_plugin_host.{hpp,cpp}`      | `src/app/src/` | `IPluginHost` wrapper around existing `NodeRunner`/`QProcess` |
| `html_plugin_host.{hpp,cpp}`      | `src/app/src/` | `IPluginHost` wrapper around existing `QWebEnginePage` path   |
| `native_plugin_host.{hpp,cpp}`    | `src/app/src/` | `IPluginHost` wrapper; adds Wine detection+spawn              |
| `python_plugin_host.{hpp,cpp}`    | `src/app/src/` | `IPluginHost` wrapper around existing OOP host                |
| `application_watcher.hpp`         | `src/app/src/` | Platform-neutral interface + factory                          |
| `application_watcher_x11.cpp`     | `src/app/src/` | XCB/EWMH implementation                                       |
| `application_watcher_wayland.cpp` | `src/app/src/` | `zwlr_foreign_toplevel_manager_v1`                            |
| `application_watcher_win32.cpp`   | `src/app/src/` | `GetForegroundWindow` poll                                    |
| `application_watcher_macos.mm`    | `src/app/src/` | `NSWorkspace` (Objective-C++)                                 |

All under `src/app/src/` — never `ajazz_core` or installed public headers (COD-031 unaffected).

### Sidecar (`streamdock-host/`)

**No new Cargo dependencies needed for v2.0.** The sidecar is a hardware I/O relay; the
plugin/binding logic lives in the C++ app layer. The only potential sidecar changes in v2.0 are:

- Encoder/touch input decode corrections (HARDWARE-GATED; provisional values remain until a
  retail AKP05E is available).
- Any new sidecar command types the binding layer needs to push (currently `set_brightness`,
  `set_image` are the relevant ones — check the existing JSON protocol before adding new fields).

Do NOT add `active-win-pos-rs` or `sysinfo` to the sidecar — those belong in the C++ app layer.

______________________________________________________________________

## 7. Alternatives Considered

| Category                                 | Recommended                                        | Alternative                                                          | Why Not                                                                                          |
| ---------------------------------------- | -------------------------------------------------- | -------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------ |
| Foreground app (Linux Wayland)           | `zwlr_foreign_toplevel_manager_v1`                 | DBus + per-compositor hacks (qdbus6 KWin scripting, GNOME extension) | Per-compositor hacks are fragile; the protocol is the composited standard for wlroots            |
| Foreground app (Linux X11)               | XCB + `xcb-ewmh` + `QSocketNotifier`               | Polling via Qt                                                       | Event-driven is cleaner; XCB fd is `QSocketNotifier`-ready                                       |
| Foreground app (cross-platform shortcut) | Platform-split `.cpp`/`.mm` files                  | `active-win-pos-rs` Rust crate via FFI                               | FFI to Rust from C++ adds complexity; the platform APIs are straightforward to call directly     |
| Windows-only native plugins              | Wine (documented fallback)                         | PE loader                                                            | PE loader is not production-ready; Wine is the proven path                                       |
| PI WebChannel                            | `QQmlWebChannel`                                   | `QWebChannel` (C++ class)                                            | `WebEngineView.webChannel` accepts only `QQmlWebChannel*`; using `QWebChannel` fails silently    |
| Plugin host abstraction                  | `IPluginHost` interface + 4 concrete impls         | Keep monolithic `PluginManager::spawn()`                             | The monolithic version works but is not testable without the full `PluginManager` rig            |
| cefQuery polyfill delivery               | `QWebEngineScript` at `DocumentCreation/MainWorld` | `runJavaScript` after page load                                      | `runJavaScript` races the plugin's own script; `DocumentCreation` is guaranteed to run before it |

______________________________________________________________________

## 8. What NOT to Add

| Item                                                  | Reason                                                                                    |
| ----------------------------------------------------- | ----------------------------------------------------------------------------------------- |
| `nlohmann::json` in `ajazz_core` or installed headers | COD-031 boundary — hard rule, release blocker                                             |
| `QCefView` or any CEF binding                         | Explicitly forbidden (CLAUDE.md); `Qt6::WebEngineQuick` is the replacement                |
| Bundled `node` executable                             | Anti-feature (CONTEXT.md); require system Node ≥20 or disable with notice                 |
| Bundled Wine                                          | Binary size, Flatpak policy, user license. Require system Wine; document                  |
| `trompeloeil`, `FakeIt`, or other mocking frameworks  | In-tree mock seams (`injectEvent`, `NodeProbe`) are sufficient                            |
| New OOP IPC protocol for Node/HTML plugins            | They already use the loopback WebSocket; a second IPC channel is over-engineering         |
| `active-win-pos-rs` Rust crate in the sidecar         | App-profile switching is C++ app-layer concern; sidecar is a hardware relay               |
| `sysinfo` Rust crate                                  | Same reason — process tracking for per-app profiles belongs in `ApplicationWatcher` (C++) |
| `QX11Info`                                            | Qt5/X11Extras — does not exist in Qt6; use `QNativeInterface::QX11Application`            |
| `QHostAddress::Any` for plugin server                 | Security invariant — always `LocalHost`; test-pinned; no setter allowed                   |
| TCP dual-stack alongside WebSocket server             | Not in locked scope; no concrete need from real plugins                                   |
| GNOME Wayland active-window support                   | No public API without a shell extension; document as unsupported in v2.0                  |

______________________________________________________________________

## 9. Installation

```bash
# Linux — per-app profile switching (X11)
sudo dnf install xcb-util-wm-devel        # Fedora (provides xcb-util-wm, xcb-ewmh)
sudo apt install libxcb-ewmh-dev           # Debian/Ubuntu

# Linux — Wayland foreign-toplevel protocol (usually already present via Qt)
# wayland-protocols package provides the XML; wlroots headers for pre-generated C bindings
sudo dnf install wayland-protocols-devel

# Wine (runtime, for Windows-only plugin execution on Linux/macOS)
sudo dnf install wine                       # Fedora
sudo apt install wine64                     # Debian/Ubuntu
brew install wine-stable                    # macOS

# No npm, pip, or cargo packages for the C++ app layer additions
# No changes to streamdock-host/Cargo.toml for v2.0
```

______________________________________________________________________

## 10. Sources

| Source                                                              | Confidence | Notes                                                      |
| ------------------------------------------------------------------- | ---------- | ---------------------------------------------------------- |
| `src/app/src/plugin_manager.{hpp,cpp}` (in-tree)                    | HIGH       | Verified dispatch logic, spawn-by-extension, env isolation |
| `src/app/src/property_inspector_controller.{hpp,cpp}` (in-tree)     | HIGH       | PI stack confirmed complete except UI trigger              |
| `src/app/src/plugin_mirabox_shim.hpp` (in-tree)                     | HIGH       | `connectMiraBoxSDSocket` alias via `QWebEngineScript`      |
| `.planning/milestones/v1.3-phases/17-RESEARCH.md`                   | HIGH       | WS protocol, auth, routing                                 |
| `.planning/milestones/v1.3-phases/20-RESEARCH.md`                   | HIGH       | PI gap analysis, `cefQuery` shim, `sdpi.css`               |
| `.planning/milestones/v1.3-phases/18-RESEARCH.md`                   | HIGH       | Plugin manifest, spawn, lifecycle                          |
| `src/core/include/ajazz/core/profile.hpp:189`                       | HIGH       | `applicationHints` already in schema                       |
| `ninjadev64/OpenDeck src-tauri/src/application_watcher.rs` (GitHub) | MEDIUM     | `active-win-pos-rs` + 250 ms poll pattern                  |
| `ninjadev64/OpenDeck src-tauri/src/plugins/mod.rs` (GitHub)         | HIGH       | Wine spawn pattern, CodePath selection                     |
| `dimusic/active-win-pos-rs` (crates.io)                             | MEDIUM     | XCB EWMH + Wayland + Win32 + NSWorkspace per-platform      |
| `docs.elgato.com/sdk/plugins/manifest`                              | HIGH       | `CodePath`, `CodePathWin`, `CodePathMac`, `OS` array       |
| Win32 docs: `GetForegroundWindow`, `QueryFullProcessImageName`      | HIGH       | Stable since Vista                                         |
| Apple docs: `NSWorkspace.didActivateApplicationNotification`        | HIGH       | Stable since macOS 10.13                                   |
| dev.to/plexescor C++23 Wayland window tracking article              | MEDIUM     | Per-compositor method survey                               |
| `CLAUDE.md` + `PROJECT.md`                                          | HIGH       | Constraints, COD-031, anti-features                        |
