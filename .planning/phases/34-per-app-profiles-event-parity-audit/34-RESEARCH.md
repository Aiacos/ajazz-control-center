# Phase 34: Per-App Profiles + Event-Parity Audit - Research

**Researched:** 2026-06-08
**Domain:** Cross-platform foreground-window detection (Wayland/X11/Win32/macOS) + Stream Deck plugin event-parity audit
**Confidence:** HIGH (Wayland + X11 + bridge wiring verified on this machine; Win/macOS APIs CITED from training + standard docs)

## Summary

Phase 34 has two largely independent tracks. The **APROF** track is greenfield: a new
`IActiveWindowWatcher` interface with four platform backends that emit debounced
foreground-app changes, feeding a profile auto-switch that matches against the *already
existing* `Profile::applicationHints` field and drives the existing bridge context lifecycle
(`willDisappear` outgoing -> `willAppear` incoming). The **EVENT** track is mostly an
audit-and-complete: the event surfaces (`SdPluginServer`, `PluginDeviceBridge`) already
exist and are rich; Phase 34 produces a coverage doc, locks the `willAppear` payload shape
with a Catch2 test, implements the two missing host paths (`systemDidWakeUp`,
`switchToProfile` dispatch), and audits the `Knob`\<->`Encoder` controller-token normalization.

The single highest-risk unknown (the pending-todo blocker) is **resolved by this research**:
the dev machine runs niri 26.04 over Wayland, and a live `wl_registry` probe confirmed niri
advertises **`zwlr_foreign_toplevel_manager_v1` v3** (and the newer `ext_foreign_toplevel_list_v1` v1).
Qt 6.11.1 ships `Qt6::WaylandClient` + `QWaylandClientExtensionTemplate<T>` + the
`qt_generate_wayland_protocol_client_sources()` CMake helper, which is the blessed, no-subprocess
way to bind the wlr protocol. `QWaylandClientExtension::isActive()` is the exact runtime signal
for graceful degradation: it is `false` when the compositor does not advertise the global, which
drives the APROF-03 capability-warning chip.

**Primary recommendation:** Mirror the existing `input_synthesizer_{linux,win,mac,stub}.cpp`
platform-split pattern *exactly* (interface in `ajazz_core` if Qt-free is feasible, else app-tier;
per-OS TUs compiled unconditionally, each self-empty behind a platform `#if`; a `makeDefault*`
factory). Bind wlr-foreign-toplevel via `qt_generate_wayland_protocol_client_sources()` over a
**vendored** `wlr-foreign-toplevel-management-unstable-v1.xml` (it is NOT installed on this system).
Auto-switch and the two missing EVENT host paths route through the existing Application
composition seam (lambda on `actionReceived` / a new watcher-fed `std::function`), never via raw
pointer coupling.

## Architectural Responsibility Map

| Capability                               | Primary Tier                                                  | Secondary Tier                          | Rationale                                                                                 |
| ---------------------------------------- | ------------------------------------------------------------- | --------------------------------------- | ----------------------------------------------------------------------------------------- |
| Foreground-window detection              | OS / platform backend (per-OS TU)                             | `ajazz_core` interface                  | Pure OS API surface; mirrors `IInputSynthesizer` split                                    |
| Debounce + app-identity normalization    | App tier (watcher impl / QTimer)                              | -                                       | Qt `QTimer`-based; precedent `HotplugDebouncer`                                           |
| Profile auto-switch decision             | App tier (`ProfileController` / Application)                  | `ajazz_core` (`applicationHints` match) | Match logic touches the pure `Profile` model; activation is a controller action           |
| Bridge context lifecycle on switch       | App tier (`PluginDeviceBridge`)                               | -                                       | Already owns willAppear/willDisappear; reuse `onActivePageChanged`/`profileChanged` paths |
| `applicationDidLaunch/Terminate` fan-out | App tier (Application seam -> `SdPluginServer::sendEvent`)    | -                                       | Host-level context-free events; mirror the openUrl/logMessage seam                        |
| `systemDidWakeUp` origin                 | OS / platform signal source                                   | App tier dispatch                       | OS sleep/wake notification per platform; fan-out is host-level                            |
| `switchToProfile` host command           | App tier (`PluginDeviceBridge::onAction` or Application seam) | `ProfileController`                     | Already a routed action string but currently a no-op; needs a handler                     |
| Assign-profile UI + warning chip         | QML (settings sub-surface)                                    | `ProfileController` Q_INVOKABLE         | Mirrors `SettingsPage.qml`; chip mirrors `LoadedPluginsPage.qml` trust-chip               |
| Event-parity coverage doc                | Docs (`docs/plugin-event-parity.md`)                          | -                                       | Verification deliverable, kept current                                                    |

## User Constraints (from CONTEXT.md)

### Locked Decisions

- **All 4 platform backends behind `IActiveWindowWatcher`:** X11/EWMH, Windows/`GetForegroundWindow`,
  macOS/`NSWorkspace` (Obj-C++ `.mm`), Wayland/`zwlr-foreign-toplevel-management-v1`. Build all;
  Linux X11 + Wayland live-verified here; Windows + macOS compile-guarded + unit-tested.
- **Wayland = `zwlr-foreign-toplevel-management-v1`** (wlroots: niri / Hyprland / sway). GNOME / KDE
  without the global expose no public foreground API -> degrade gracefully (NO crash, NO silent break)
  - UI capability-warning chip (APROF-03). Do NOT shell out to xdotool / hyprctl / swaymsg as the
    primary path.
- **Live-verify the Wayland backend on niri** (the dev machine implements wlr-foreign-toplevel).
- **Debounced foreground-app changes** (~150-250ms).
- **Auto-switch matches the foreground app against `Profile::applicationHints`** (already exists);
  default-profile fallback when no hint matches. Each switch drives `willDisappear` (outgoing) then
  `willAppear` (incoming).
- **`applicationDidLaunch` / `applicationDidTerminate`** fire from the watcher to subscribing plugins.
- **UI (APROF-03):** assign profiles to app names + the capability-warning chip; both debug-addressable
  via `objectName` (the chip's `qml.get` returns non-empty warning text).
- **Switch latency target: < 500 ms** device repaint.
- **EVENT-01:** commit `docs/plugin-event-parity.md` (every OpenDeck inbound+outbound + Elgato SDK
  event, marked supported/partial/missing/hardware-gated, with a test reference per supported event).
- **EVENT-02:** Catch2 test asserts the `willAppear` payload carries `action`, `context`, `device`,
  `event`, and `payload{row, column, controller, state, isInMultiAction}` (normalized controller).
- **EVENT-03:** `systemDidWakeUp` + `switchToProfile` host-command dispatch implemented + unit-tested.
- **EVENT-04:** audit `Knob`\<->`Encoder` controller-token normalization so no encoder/touch event is dropped.

### Claude's Discretion

- The exact `IActiveWindowWatcher` interface shape, per-backend internals, debounce timing, the
  assign-profile UI layout, and the parity-table format are at the executor's discretion, consistent
  with existing patterns. Research the wlr binding approach before committing.

### Deferred Ideas (OUT OF SCOPE)

- Windows-only plugin support -> Phase 35 (WINPLG).
- Plugin security hardening -> Phase 35 (PLGSEC).
- Final milestone modularity audit (VERIF-01/02) -> Phase 35.
- Broader Wayland coverage (GNOME shell extension, KWin D-Bus) beyond the `zwlr-foreign-toplevel` path.

## Phase Requirements

| ID       | Description                                                                                    | Research Support                                                                                                                                                                                         |
| -------- | ---------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| APROF-01 | `IActiveWindowWatcher` interface + 4 platform backends, debounced foreground changes           | Mirror `input_synthesizer_*` split (verified in-repo); Wayland via `QWaylandClientExtensionTemplate` (Qt 6.11.1 present); X11 via `_NET_ACTIVE_WINDOW`; Win32 `GetForegroundWindow`; macOS `NSWorkspace` |
| APROF-02 | Auto-switch via `Profile::applicationHints` + default fallback; drive willDisappear/willAppear | `applicationHints` exists (profile.hpp:192); reuse `PluginDeviceBridge` reconcile pass (onActivePageChanged path) + `ProfileController::activateDeviceProfile`                                           |
| APROF-03 | Assign-profile UI + Wayland/GNOME capability warning                                           | 34-UI-SPEC.md pins surfaces; `QWaylandClientExtension::isActive()==false` is the runtime degradation signal                                                                                              |
| APROF-04 | `applicationDidLaunch`/`applicationDidTerminate` to subscribing plugins                        | sendEvent has no allowlist (additive); mirror Application openUrl/logMessage seam                                                                                                                        |
| EVENT-01 | Coverage table doc                                                                             | In-repo `akp_plugin_sdk.md` already enumerates events; cross-ref OpenDeck + Elgato                                                                                                                       |
| EVENT-02 | willAppear payload completeness test                                                           | Payload builder at `plugin_device_bridge.cpp:311` (`instancePayload`) + envelope at :375 (`eventEnvelope`)                                                                                               |
| EVENT-03 | `systemDidWakeUp` + `switchToProfile` dispatch                                                 | `switchToProfile` is routed (sd_plugin_server.cpp:424) but unhandled; add handler + OS wake source                                                                                                       |
| EVENT-04 | `Knob`\<->`Encoder` token audit                                                                | manifest accepts both (`plugin_manifest.cpp:144`); wire convention uses `"Encoder"` everywhere (input + bridge)                                                                                          |

## Standard Stack

### Core

| Library              | Version          | Purpose                                                                         | Why Standard                                                                                                                                                                                                                                   |
| -------------------- | ---------------- | ------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `Qt6::WaylandClient` | 6.11.1 (present) | Bind the wlr-foreign-toplevel protocol via `QWaylandClientExtensionTemplate<T>` | Blessed Qt path for custom/unstable Wayland protocols; no raw `wl_display` plumbing, integrates with Qt event loop `[VERIFIED: /usr/lib64/cmake/Qt6WaylandClient present + header /usr/include/qt6/QtWaylandClient/qwaylandclientextension.h]` |
| `wayland-client`     | 1.24.0 (present) | Underlying client lib; `wl_registry` global enumeration                         | `[VERIFIED: pkg-config --modversion wayland-client]`                                                                                                                                                                                           |
| `qtwaylandscanner`   | 6.11.1 (present) | Generates `QtWayland::zwlr_foreign_toplevel_*` C++ base from XML                | Invoked by `qt_generate_wayland_protocol_client_sources()` `[VERIFIED: /usr/lib64/qt6/libexec/qtwaylandscanner present]`                                                                                                                       |
| Xlib (`x11`)         | 1.8.13 (present) | X11/EWMH `_NET_ACTIVE_WINDOW` + `_NET_WM_PID`/`WM_CLASS`                        | Standard EWMH path; simpler than raw xcb for property reads `[VERIFIED: pkg-config --modversion x11]`                                                                                                                                          |

### Supporting

| Library                        | Version          | Purpose                                                                   | When to Use                                                                                                                            |
| ------------------------------ | ---------------- | ------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------- |
| `xcb`                          | 1.17.0 (present) | Alternative to Xlib for property reads                                    | Only if avoiding Xlib global state is preferred; note `xcb-ewmh` is NOT installed `[VERIFIED: pkg-config xcb 1.17.0; xcb-ewmh absent]` |
| Win32 `user32`                 | system           | `GetForegroundWindow` + `GetWindowThreadProcessId`                        | Windows backend (compile-guarded); already linked for HotplugMonitor `[CITED: learn.microsoft.com/windows/win32/api/winuser]`          |
| `AppKit` (`-framework AppKit`) | system           | `NSWorkspace.frontmostApplication` / `didActivateApplicationNotification` | macOS backend in a `.mm` TU `[CITED: developer.apple.com/documentation/appkit/nsworkspace]`                                            |

### Alternatives Considered

| Instead of                         | Could Use                                                | Tradeoff                                                                                                                                                                                                                                                                                                                                         |
| ---------------------------------- | -------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| `zwlr_foreign_toplevel_manager_v1` | `ext_foreign_toplevel_list_v1` (also advertised by niri) | `ext-` is the newer staged-stable protocol and gives a cleaner toplevel list, BUT it does NOT report the *activated* state (no "which window has focus" signal) — it is list-only. wlr's `state` event carries the `activated` flag, which is exactly what foreground detection needs. **Use wlr.** (See Open Question 1 for the hybrid option.) |
| `QWaylandClientExtensionTemplate`  | Raw `wl_registry` + hand-written listener structs        | Raw works (proven by the probe in this research) but bypasses Qt's event-loop integration and re-implements global tracking; the Qt template is the maintained path.                                                                                                                                                                             |
| Xlib for X11                       | `xcb` + `xcb-ewmh`                                       | `xcb-ewmh` is not installed on this Fedora host; Xlib is present and the property reads are trivial. Avoid adding a build dep.                                                                                                                                                                                                                   |
| QtWayland binding                  | Shell out to `niri msg` / `swaymsg` / `hyprctl`          | Explicitly forbidden by CONTEXT.md as the primary path (fragile, per-compositor, subprocess).                                                                                                                                                                                                                                                    |

**Installation:** No new packages required on the dev host — all confirmed present. CI/Flatpak
must add `Qt6::WaylandClient` (qtwayland), `wayland-client`/`wayland-scanner`, and `libX11` to the
build environment. See Environment Availability.

**Version verification:** `Qt 6.11.1` `[VERIFIED: qmake6 -query QT_VERSION]`;
`wayland-scanner 1.24.0` `[VERIFIED]`; `niri 26.04` `[VERIFIED]`.

## Package Legitimacy Audit

> Not applicable in the npm/PyPI sense — this is a C++/CMake project. All dependencies are
> system libraries provided by the distro / Qt SDK, not fetched from a package registry. No
> `mirajazz` crate change (VERIF-02). No new third-party source dependencies are introduced;
> the wlr protocol XML is **vendored from the canonical swaywm/wlr-protocols repo** (a copied
> standard XML file, not an executable dependency).

| Dependency                                                       | Source                               | Disposition                                                                                                                               |
| ---------------------------------------------------------------- | ------------------------------------ | ----------------------------------------------------------------------------------------------------------------------------------------- |
| `wlr-foreign-toplevel-management-unstable-v1.xml`                | swaywm/wlr-protocols (vendored copy) | Approved — copy the canonical XML into the repo (e.g. `resources/wayland-protocols/` or `src/app/wayland/`); pin the version in a comment |
| `Qt6::WaylandClient`, `libwayland`, `libX11`, `user32`, `AppKit` | distro / Qt SDK / OS frameworks      | Approved — system libraries                                                                                                               |

## Architecture Patterns

### System Architecture Diagram

```
                          ┌─────────────────────────────────────────┐
   OS focus change        │           IActiveWindowWatcher          │
  (per platform) ───────► │  (interface, makeDefaultActiveWindow-   │
                          │   Watcher() factory)                    │
                          └───────────────┬─────────────────────────┘
        ┌───────────────────┬─────────────┼──────────────┬───────────────────┐
        ▼                   ▼             ▼              ▼                    ▼
  wayland backend     x11 backend    win backend    mac backend         stub backend
  (QWaylandClient-    (_NET_ACTIVE_   (GetFore-      (NSWorkspace        (records; tests)
   ExtensionTemplate   WINDOW +       groundWindow + frontmost +
   <wlr toplevel>,     _NET_WM_PID/   GetWindow-     didActivate-
   state.activated)    WM_CLASS)      ThreadProcess) Notification)
        │
        └── isActive()==false ► capabilityAvailable=false ───────────┐
                                                                      ▼
   foreground app id (app_id / WM_CLASS / image name / bundle id)    UI: warning chip
        │                                                            (waylandCapabilityWarningChip)
        ▼
  ┌─────────────────────┐  debounce ~150-250ms (QTimer; precedent HotplugDebouncer)
  │  debounce + dedupe  │──────────────────────────────────────────────────────────┐
  └─────────┬───────────┘                                                           │
            ▼                                                                        ▼
  match against Profile::applicationHints                          applicationDidLaunch /
  (default-profile fallback)                                       applicationDidTerminate
            │                                                                │
            ▼                                                                ▼
  ProfileController.activateDeviceProfile ──► profileChanged ──►   Application seam
            │                                                      ──► SdPluginServer::sendEvent
            ▼                                                          (subscribed plugins)
  PluginDeviceBridge reconcile pass:
   willDisappear(outgoing contexts) ──► willAppear(incoming contexts)
            │
            ▼
  device repaint  (target < 500 ms)

  ── Independent EVENT track ──
  SdPluginServer::actionReceived ("switchToProfile") ─► onAction handler ─► ProfileController
  OS sleep/wake signal ─► Application seam ─► SdPluginServer::sendEvent("systemDidWakeUp")
```

### Recommended Project Structure

```
src/core/include/ajazz/core/
  active_window_watcher.hpp      # IActiveWindowWatcher + makeDefault* factory (Qt-free IF possible)
src/core/src/   (or src/app/src/ if interface needs Qt — see Pattern 1)
  active_window_watcher_stub.cpp     # always-compiled recording stub (tests / unsupported)
  active_window_watcher_wayland.cpp  # #if linux + Qt6::WaylandClient
  active_window_watcher_x11.cpp      # #if linux (X11 session)
  active_window_watcher_win.cpp      # #if Q_OS_WIN
  active_window_watcher_mac.mm       # #if Q_OS_MACOS (Obj-C++)
src/app/wayland/                     # vendored protocol XML + generated glue
  wlr-foreign-toplevel-management-unstable-v1.xml
docs/plugin-event-parity.md          # EVENT-01 deliverable
src/app/qml/  (assign-profile section in SettingsPage.qml or a sub-component)
```

### Pattern 1: Platform-split interface + per-OS TU (CANONICAL — mirror `IInputSynthesizer`)

**What:** A pure interface header + a `makeDefault*()` factory; every per-OS TU is added to the
CMake source list *unconditionally* and is self-empty unless its platform `#if` (and any feature
gate) is satisfied. A `_stub` TU always compiles and is what tests link against.

**When to use:** Always for this watcher — it is the established repo idiom.

**Example (from the repo, abbreviated):**

```cpp
// Source: src/core/src/input_synthesizer_linux.cpp
#if defined(__linux__) && defined(AJAZZ_FEATURE_INPUT_SYNTH)
// ... real backend ...
#endif   // whole TU is empty otherwise — still compiles cleanly on every platform
```

```cmake
# Source: src/core/CMakeLists.txt:28-31
src/input_synthesizer_stub.cpp
src/input_synthesizer_linux.cpp
src/input_synthesizer_win.cpp
src/input_synthesizer_mac.cpp
```

**COD-031 caveat:** `IInputSynthesizer` lives in `ajazz_core` because its interface is Qt-free.
`IActiveWindowWatcher` likely needs Qt (`QWaylandClientExtension` is a `QObject`; debounce uses
`QTimer`; emits via Qt signals). **Recommendation:** put `IActiveWindowWatcher` in the **app tier**
(`src/app/src/`) so it can be a `QObject` and use `QTimer`/signals freely, OR keep the interface
Qt-free in core (callback `std::function<void(std::string appId)>`) with all Qt machinery in the
app-tier impls. The latter matches the "seams via std::function injection" decision (STATE.md).
Prefer **Qt-free interface in core + app-tier impls** to keep the seam pattern consistent. `[ASSUMED]`
(executor's discretion per CONTEXT.md — pick after a build spike).

### Pattern 2: Wayland wlr binding via QWaylandClientExtensionTemplate

**What:** `qt_generate_wayland_protocol_client_sources()` runs qtwaylandscanner over the vendored
XML, producing a `QtWayland::zwlr_foreign_toplevel_manager_v1` (and `_handle_v1`) C++ base class.
You subclass `QWaylandClientExtensionTemplate<T>` + the generated base. `isActive()` becomes `true`
once the global binds; `false` (with `activeChanged` signal) when the compositor does not advertise it.

**When to use:** The Wayland backend only.

**Example:**

```cmake
# Source: doc.qt.io/qt-6/qt-generate-wayland-protocol-client-sources.html [CITED]
find_package(Qt6 REQUIRED COMPONENTS WaylandClient)
qt_generate_wayland_protocol_client_sources(ajazz-control-center
    PRIVATE_CODE                                   # strongly recommended
    FILES ${CMAKE_CURRENT_SOURCE_DIR}/wayland/wlr-foreign-toplevel-management-unstable-v1.xml)
target_link_libraries(ajazz-control-center PRIVATE Qt6::WaylandClient)
```

```cpp
// Conceptual — combine generated base + template (Qt Custom Extension example shape) [CITED]
class WlrToplevelManager
    : public QWaylandClientExtensionTemplate<WlrToplevelManager>,
      public QtWayland::zwlr_foreign_toplevel_manager_v1 {
public:
    WlrToplevelManager() : QWaylandClientExtensionTemplate(/*version*/ 3) {}
    // override zwlr_foreign_toplevel_manager_v1_toplevel(handle) -> wrap each handle;
    // per-handle override _state(array) -> detect ZWLR_..._STATE_ACTIVATED;
    // _app_id(QString) / _title(QString) -> identity; _done() -> commit; _closed() -> dispose.
};
// Graceful degradation: connect(&mgr, &QWaylandClientExtension::activeChanged, ...)
//   -> if (!mgr.isActive()) capabilityAvailable = false;  // drives the warning chip
```

**Toplevel events to consume (wlr handle):** `app_id`, `title`, `state` (look for the
`activated` enum bit), `done` (commit), `closed` (dispose). `[CITED: wayland.app/protocols/wlr-foreign-toplevel-management-unstable-v1]`
The **focused** app = the handle whose latest `state` array contains `activated`.

### Pattern 3: Reuse the bridge reconcile pass for the lifecycle switch (APROF-02)

**What:** Profile auto-switch must NOT re-implement willAppear/willDisappear. The bridge already
has the exact reconcile pass — `populateContextsForActivePage` (wired to `profileChanged`,
application.cpp:~656) sends `willAppear` for the incoming page and `willDisappear`+retire for any
context no longer present. The page-change path (`onActivePageChanged`, bridge:1473) is the same
shape ("Retire old page contexts (willDisappear) then populate the new page (willAppear)").

**When to use:** APROF-02 — drive a profile activation through `ProfileController` and let the
existing `profileChanged -> populateContextsForActivePage` wire fire the lifecycle. Verify the
device-disconnect path (bridge:1451 sends willDisappear per context) is the model for "outgoing".

### Pattern 4: Host-level event fan-out via the Application seam (APROF-04, EVENT-03)

**What:** `applicationDidLaunch/Terminate` and `systemDidWakeUp` are context-free host events.
Mirror the existing openUrl/logMessage lambda wired on `SdPluginServer::actionReceived`
(application.cpp:603-642) — but in the *outbound* direction: the watcher/OS-wake source emits,
the Application seam calls `SdPluginServer::sendEvent(uuid, "applicationDidLaunch", {application})`
for each registered plugin. `sendEvent` has **no event-name allowlist** (verified — kRoutedActions
gates inbound only), so these new outbound events are additive with zero server change.

**`switchToProfile` (inbound host command):** it is already in `kRoutedActions`
(sd_plugin_server.cpp:424) so it emits `actionReceived`, but **no consumer handles it** — it is
currently a silent no-op (verified: `handleSettingsAction` and `onAction` do not branch on it;
the openUrl/logMessage seam does not either). Add a handler (either a new branch in the Application
`actionReceived` lambda, or in `PluginDeviceBridge::onAction`) that calls
`ProfileController::loadProfileById` / `activateDeviceProfile` based on the payload's profile name/uuid.

### Anti-Patterns to Avoid

- **Subprocess polling (`niri msg`, `swaymsg`, `hyprctl`, `xdotool`):** forbidden as the primary
  path (CONTEXT.md). Fragile, per-compositor, latency-prone.
- **Re-implementing willAppear/willDisappear in the watcher:** the bridge owns it; route through
  `profileChanged`.
- **Caching the resolved socket in the new outbound event paths:** `sendEvent` re-resolves per call
  (Pitfall 4 / T-17-UAF). Always call `sendEvent`, never hold a `QWebSocket*`.
- **Adding `nlohmann::json` to the core interface:** COD-031. The watcher interface must be JSON-free
  (it deals in app-id strings).
- **Putting a `Switch` on the assign/remove actions:** the debug harness `qml.invoke toggle` does not
  fire `onToggled` (CLAUDE.md). Use plain `Button`/`SecondaryButton` (UI-SPEC already mandates this).
- **Treating GNOME/KDE as permanently unsupported:** recent versions (Mutter 49.2, KWin 6.6) now
  advertise the wlr global; `isActive()` is the correct *runtime* gate, never a hardcoded
  desktop-name blocklist.

## Don't Hand-Roll

| Problem                            | Don't Build                                                | Use Instead                                                                                   | Why                                                                                                     |
| ---------------------------------- | ---------------------------------------------------------- | --------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------- |
| Bind a Wayland global from Qt      | Raw `wl_registry` listener structs + manual proxy lifetime | `QWaylandClientExtensionTemplate<T>` + `qt_generate_wayland_protocol_client_sources()`        | Qt integrates global bind/unbind, version negotiation, event-loop dispatch; gives `isActive()` for free |
| willAppear/willDisappear on switch | New lifecycle code in the watcher                          | `PluginDeviceBridge` reconcile (`profileChanged`->`populateContextsForActivePage`)            | Already battle-tested incl. CR WR-01 fix (spurious didDisappear)                                        |
| Host->plugin event delivery        | New socket write code                                      | `SdPluginServer::sendEvent`                                                                   | Re-resolves socket per call (UAF guard); emits `eventSent` for the debug console                        |
| Debounce focus changes             | Ad-hoc timestamp math                                      | `QTimer` (single-shot, restart-on-change), precedent `HotplugDebouncer` (300ms trailing-edge) | Matches the existing 300ms hot-plug debouncer idiom                                                     |
| Profile activation                 | Direct profile-store mutation                              | `ProfileController::activateDeviceProfile` / `loadProfileById` (Q_INVOKABLE)                  | Emits `profileChanged` which the bridge listens to                                                      |
| Parse the wlr XML                  | Hand-written event decode                                  | qtwaylandscanner-generated `QtWayland::zwlr_*` base                                           | Generated, version-correct, maintained                                                                  |

**Key insight:** Both tracks are mostly *wiring existing machinery*. The genuinely new code is
the four watcher backends and the Wayland protocol binding; everything downstream (lifecycle,
event delivery, profile activation) already exists and must be reused.

## Runtime State Inventory

> Phase 34 is additive/greenfield (a new watcher + new events + a doc). No rename/refactor/migration.

| Category            | Items Found                                                                                                                                         | Action Required                                  |
| ------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------ |
| Stored data         | `Profile::applicationHints` already persisted in profile JSON (writer/reader exist) — APROF UI writes to it; no schema change, no migration         | code edit only (UI writes existing field)        |
| Live service config | None — the watcher reads OS state at runtime; nothing persisted externally                                                                          | None — verified by inspection (no new datastore) |
| OS-registered state | None — the watcher only *reads* foreground state; it registers no OS hooks beyond a Wayland client bind / X11 property watch (released on teardown) | None                                             |
| Secrets/env vars    | None                                                                                                                                                | None                                             |
| Build artifacts     | qtwaylandscanner generates `qwayland-wlr-foreign-toplevel-*.{h,cpp}` into the build dir from the vendored XML                                       | None (generated, gitignored build output)        |

## Common Pitfalls

### Pitfall 1: `ext_foreign_toplevel_list_v1` does not report focus

**What goes wrong:** Choosing the newer `ext-` protocol (also advertised by niri) because it is
"more stable" — but it is a *list* protocol with no activated/focused state.
**Why it happens:** Both globals appear in the registry; the newer one looks preferable.
**How to avoid:** Use `zwlr_foreign_toplevel_manager_v1`; the per-handle `state` event carries the
`activated` flag, which IS the foreground signal. `[CITED: wayland.app]`
**Warning signs:** The watcher can list windows but never knows which is focused.

### Pitfall 2: App-identity token mismatch across platforms

**What goes wrong:** `applicationHints` are matched against inconsistent tokens (WM_CLASS on X11,
`app_id` on Wayland, image-base-name on Windows, bundle-id on macOS), so a hint set on one desktop
never matches on another.
**Why it happens:** Each OS exposes a different "app identity".
**How to avoid:** Define ONE normalization contract up front. Recommended primary token:
Wayland `app_id` and X11 `WM_CLASS` instance/class (both are app-id-like, e.g. `firefox`),
Windows process image base name (`firefox.exe` -> normalize to `firefox`), macOS bundle id or
localized name. Document the chosen token per platform in the parity/watcher doc. Match
case-insensitively and consider substring/exact policy. `[ASSUMED]` exact match policy — confirm
with the user (the SDK `applicationDidLaunch` payload is `{application:"<exe-path>"}` per
`akp_plugin_sdk.md:286`, which is a *path*, distinct from the *hint match token*).
**Warning signs:** Hints set in the UI silently never trigger a switch on a different desktop.

### Pitfall 3: `Knob` vs `Encoder` token drop (EVENT-04)

**What goes wrong:** Elgato/AJAZZ manifests declare encoder actions under `Controllers: ["Knob"]`,
but the runtime wire convention is `controller: "Encoder"` everywhere (input service, bridge
byCoord keys, willAppear payload). A mismatch silently drops dial/touch events.
**Why it happens:** Two vocabularies — manifest (`Knob`) vs runtime (`Encoder`).
**How to avoid:** The repo already normalizes at the manifest boundary (`plugin_manifest.cpp:144`:
`token == "Knob" || token == "Encoder"` -> encoder bit). EVENT-04 must *audit* that every emission
path (dialRotate/dialDown/dialUp at bridge:945-988, touch at bridge:1019-1025, registration at
bridge:1246/1293) consistently uses `"Encoder"`, and that the parity doc records the `Knob`->`Encoder`
mapping. **Verified:** every byCoord/emission site in `plugin_device_bridge.cpp` uses
`QStringLiteral("Encoder")`; the only `Knob` references are in docs/manifest acceptance. The audit
should confirm no path emits `"Knob"` on the wire.
**Warning signs:** A plugin's encoder action gets `willAppear` but never `dialRotate`.

### Pitfall 4: Wayland focus-loss has no "activated" handle

**What goes wrong:** When the user focuses a window the watcher cannot see (e.g. the compositor's
own surface, or a layer-shell panel), no handle reports `activated` — the watcher must treat
"no activated toplevel" as "keep current / fall back to default", not crash.
**How to avoid:** Track the last-activated handle; on a `state` with no `activated` among any handle,
do not emit a spurious empty change. Debounce absorbs transient gaps.

### Pitfall 5: Thread/event-loop context for the Wayland client

**What goes wrong:** `QWaylandClientExtension` must `initialize()` on the GUI thread with a live
Qt Wayland platform integration; creating it before the QGuiApplication / on a worker thread fails
silently (`isActive()` stays false).
**How to avoid:** Construct the Wayland backend after the QGuiApplication is up, on the main thread.
The factory should be invoked from Application init, not a constructor at static-init time.

### Pitfall 6: X11 backend on an XWayland/Wayland session

**What goes wrong:** Under Wayland, the X11 backend would only see XWayland clients (wrong answer).
**How to avoid:** Select the backend by `XDG_SESSION_TYPE` / `QGuiApplication::platformName()`
(`wayland` vs `xcb`), not by compile platform. The `makeDefault*` factory must pick wayland-vs-x11
at runtime on Linux. On this machine `XDG_SESSION_TYPE=wayland`, `platformName()=="wayland"`.

## Code Examples

### Confirm the foreground-toplevel global is advertised (runtime capability check)

```cpp
// Verified approach — the live probe used in this research found:
//   GLOBAL zwlr_foreign_toplevel_manager_v1 v3
//   GLOBAL ext_foreign_toplevel_list_v1 v1   (on niri 26.04)
// In Qt, you don't probe wl_registry manually — QWaylandClientExtension::isActive()
// reports the same fact after initialize():
//   if (!toplevelManager->isActive())  -> capability unavailable -> show warning chip
```

### Existing willAppear envelope + payload (EVENT-02 asserts this exact shape)

```cpp
// Source: src/app/src/plugin_device_bridge.cpp:311 (instancePayload) + :375 (eventEnvelope)
// envelope: { event, action(=ctx.actionUUID), context(=deriveContextId), device, payload }
// payload : { settings, coordinates:{row,column}, controller, state, isInMultiAction }
m_server->sendEvent(owner,
    eventEnvelope(QStringLiteral("willAppear"), ctx, instancePayload(ctx)));
// EVENT-02 test asserts: top-level action/context/device/event present AND
//   payload.coordinates.row/column, payload.controller (normalized), payload.state,
//   payload.isInMultiAction present.
```

### Outbound host event with no allowlist (APROF-04 / EVENT-03)

```cpp
// sendEvent has NO event-name gate (kRoutedActions is INBOUND-only). New outbound:
m_pluginServer->sendEvent(uuid, QStringLiteral("applicationDidLaunch"),
    QJsonObject{{QStringLiteral("application"), exePathOrAppId}});
m_pluginServer->sendEvent(uuid, QStringLiteral("systemDidWakeUp"), {}); // no payload per SDK
```

## State of the Art

| Old Approach                                                      | Current Approach                                                        | When Changed                          | Impact                                                                                                                                                                 |
| ----------------------------------------------------------------- | ----------------------------------------------------------------------- | ------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| wlr-foreign-toplevel only on wlroots; GNOME/KDE had no equivalent | Mutter 49.2 + KWin 6.6 now advertise `zwlr_foreign_toplevel_manager_v1` | GNOME 49 / KDE Plasma 6.x (2025-2026) | The "GNOME/KDE gap" is shrinking; runtime `isActive()` gate (not a desktop blocklist) future-proofs against this churn `[CITED: wayland.app compositor support table]` |
| Per-compositor IPC (swaymsg/hyprctl) for window lists             | Standard `ext_foreign_toplevel_list_v1` (list) + wlr (control/state)    | wayland-protocols staging, 2023+      | wlr remains the one that carries focus/`activated` state                                                                                                               |

**Deprecated/outdated:** `wlr-foreign-toplevel` is still "unstable" but widely deployed and NOT
deprecated (no successor carries the activated-state signal yet). Continue using it.

## Assumptions Log

| #   | Claim                                                                                                                                                                                     | Section                  | Risk if Wrong                                                                                                                                                                    |
| --- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------ | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| A1  | `IActiveWindowWatcher` interface should be Qt-free in core (std::function seam) vs a QObject in app tier                                                                                  | Pattern 1                | Low — both work; executor's discretion per CONTEXT.md; a build spike decides                                                                                                     |
| A2  | App-identity match policy is case-insensitive exact match on app_id/WM_CLASS/image-base                                                                                                   | Pitfall 2                | Medium — affects whether hints reliably match; the SDK `applicationDidLaunch` payload is a *path*, distinct from the hint token; confirm match semantics with the user           |
| A3  | Windows backend uses `GetForegroundWindow`+`GetWindowThreadProcessId`+`QueryFullProcessImageNameW`; macOS uses `NSWorkspace.frontmostApplication` + `didActivateApplicationNotification`  | Standard Stack / Map     | Low — these are the documented standard APIs, but compile-guarded (not live-testable here)                                                                                       |
| A4  | `systemDidWakeUp` OS source: Linux org.freedesktop.login1 `PrepareForSleep` D-Bus signal / Windows `WM_POWERBROADCAST`/`PBT_APMRESUMEAUTOMATIC` / macOS `NSWorkspace.didWakeNotification` | Map / EVENT-03           | Medium — exact wake source per platform unverified in this session; Linux logind path is the cleanest but needs a small D-Bus listen; could ship as a stub on non-Linux for v2.0 |
| A5  | The two `ext-` vs `wlr` toplevel protocols both advertised by niri; only wlr carries `activated`                                                                                          | Pitfall 1 / Alternatives | Low — CITED from wayland.app protocol spec                                                                                                                                       |

## Open Questions

1. **`ext_foreign_toplevel_list_v1` as a fallback identity source?**

   - What we know: niri advertises both; `ext-` is list-only (no focus), wlr carries `activated`.
   - What's unclear: whether any target compositor advertises `ext-` but NOT `wlr` (would give a
     window list but no focus — useless for auto-switch).
   - Recommendation: Use wlr exclusively. If `wlr.isActive()==false`, degrade (warning chip);
     do not attempt `ext-` for focus.

1. **`systemDidWakeUp` source on Linux** — logind `PrepareForSleep(false)` D-Bus signal is the
   standard, but adds a small D-Bus dependency. Confirm whether v2.0 wants the real wake signal on
   Linux now, or a stub everywhere with the wire path + test (EVENT-03 says "implemented + unit-tested";
   a unit test can drive the dispatch via an injected signal without a real sleep). Recommendation:
   implement the *dispatch* + test now (inject a synthetic wake), wire the Linux logind source if
   cheap, stub Win/macOS sources behind the compile guard.

1. **Where does `switchToProfile` get its target?** The Elgato payload is
   `{profile, device, page?}`. Confirm the app's profile addressing (by name vs id) — `ProfileController`
   exposes both `loadProfileById` and `activateDeviceProfile(codename)`. The handler must map the
   plugin's `profile` string to a stored profile. (Auditable against `akp_plugin_sdk.md`.)

## Environment Availability

| Dependency                                | Required By                | Available                    | Version | Fallback                                                               |
| ----------------------------------------- | -------------------------- | ---------------------------- | ------- | ---------------------------------------------------------------------- |
| niri (wlroots compositor)                 | Live Wayland verify        | yes                          | 26.04   | -                                                                      |
| `zwlr_foreign_toplevel_manager_v1` global | Wayland backend            | yes (v3, advertised by niri) | v3      | warning chip (degrade)                                                 |
| `ext_foreign_toplevel_list_v1`            | (not used)                 | yes (v1)                     | v1      | -                                                                      |
| `Qt6::WaylandClient`                      | Wayland backend            | yes                          | 6.11.1  | -                                                                      |
| `qtwaylandscanner`                        | protocol codegen           | yes                          | 6.11.1  | -                                                                      |
| `wayland-scanner` / `wayland-client`      | protocol codegen / client  | yes                          | 1.24.0  | -                                                                      |
| `libX11` (Xlib)                           | X11 backend                | yes                          | 1.8.13  | -                                                                      |
| `xcb`                                     | X11 (alt)                  | yes                          | 1.17.0  | use Xlib                                                               |
| `xcb-ewmh`                                | X11 EWMH helper (optional) | **no**                       | -       | Read `_NET_*` atoms directly via Xlib/xcb                              |
| `wlr-foreign-toplevel-*.xml`              | protocol codegen input     | **no** (not installed)       | -       | **Vendor the XML into the repo** (swaywm/wlr-protocols canonical copy) |

**Missing dependencies with no fallback:** none.
**Missing dependencies with fallback:**

- `wlr-foreign-toplevel-management-unstable-v1.xml` — not on the system; vendor it into the repo
  (standard practice; it is a small, stable, permissively-licensed protocol definition).
- `xcb-ewmh` — absent; read `_NET_ACTIVE_WINDOW` / `_NET_WM_PID` / `WM_CLASS` atoms directly.

**CI/Flatpak note:** the build environment(s) must provide `qtwayland` (Qt6::WaylandClient),
`wayland-devel`/`wayland-protocols`, and `libX11-devel`. The Flatpak manifest currently does not
build with these — adding the Wayland backend requires adding the qtwayland module / wayland +
X11 dev to the manifest. Flag for the planner as a build-env task.

## Validation Architecture

### Test Framework

| Property           | Value                                                                                                  |
| ------------------ | ------------------------------------------------------------------------------------------------------ |
| Framework          | Catch2 (unit, `tests/unit/`) + integration (`tests/integration/`) + offscreen QML smoke (`tests/qml/`) |
| Config file        | CTest preset `linux-release`                                                                           |
| Quick run command  | `ctest --preset linux-release -R active_window` (new suite)                                            |
| Full suite command | `ctest --preset linux-release` (~408 cases baseline; will grow)                                        |

### Phase Requirements -> Test Map

| Req ID   | Behavior                                                                                     | Test Type        | Automated Command                                                                                   | File Exists?                  |
| -------- | -------------------------------------------------------------------------------------------- | ---------------- | --------------------------------------------------------------------------------------------------- | ----------------------------- |
| APROF-01 | Watcher interface + factory selects backend by session type; stub records                    | unit             | `ctest --preset linux-release -R active_window`                                                     | NO — Wave 0                   |
| APROF-01 | Debounce coalesces rapid changes to one emit                                                 | unit             | `ctest ... -R active_window_debounce`                                                               | NO — Wave 0                   |
| APROF-02 | Match foreground app -> applicationHints; default fallback; drives willDisappear->willAppear | unit/integration | `ctest ... -R app_profile_switch`                                                                   | NO — Wave 0                   |
| APROF-02 | Switch latency < 500 ms device repaint                                                       | live             | debug-channel: set foreground (synthetic) -> screenshot, time the repaint                           | manual/live                   |
| APROF-03 | Warning chip visible+non-empty text when capability absent                                   | live (QML)       | `qml.get waylandCapabilityWarningChip` returns visible+warningText                                  | live                          |
| APROF-03 | Assign-profile controls addressable + write applicationHints                                 | live (QML)       | `qml.set appProfileAppNameField` / `qml.click addAppProfileMappingButton`                           | live                          |
| APROF-04 | applicationDidLaunch/Terminate delivered to subscribed plugins                               | unit/integration | `ctest ... -R app_lifecycle_events`                                                                 | NO — Wave 0                   |
| EVENT-01 | Coverage doc exists + complete                                                               | doc review       | grep `docs/plugin-event-parity.md` for every SDK event                                              | NO — Wave 0 (doc)             |
| EVENT-02 | willAppear payload completeness                                                              | unit             | `ctest ... -R willappear_payload`                                                                   | NO — Wave 0                   |
| EVENT-03 | switchToProfile dispatch activates profile                                                   | unit             | `ctest ... -R switch_to_profile`                                                                    | NO — Wave 0                   |
| EVENT-03 | systemDidWakeUp dispatch (synthetic wake)                                                    | unit             | `ctest ... -R system_did_wake`                                                                      | NO — Wave 0                   |
| EVENT-04 | No path emits "Knob" on the wire (all "Encoder")                                             | unit + grep      | `ctest ... -R controller_token` + `grep -rn '"Knob"' src/app/src/*.cpp` (expect docs/manifest only) | partial (grep verifiable now) |

### Sampling Rate (highest-risk behaviors -> minimum proof)

- **Per task commit:** the new `active_window` / `willappear_payload` / `switch_to_profile` quick suite.
- **Per wave merge:** full `ctest --preset linux-release`.
- **Phase gate (live, mandatory — VERIF-01):** the following five live checks via the debug channel
  (ctest green is necessary but NOT sufficient):
  1. **X11 live switch** — run under an X11 session (or XWayland with the X11 backend forced),
     focus app A then B, confirm profile switch + repaint.
  1. **Wayland live switch on niri** — focus app A (mapped) then B, confirm the watcher emits the
     `app_id` change, the profile switches, and the device repaints < 500 ms.
  1. **Graceful-degradation path** — simulate `isActive()==false` (or run where the global is
     absent), confirm NO crash, the warning chip shows, and manual switching still works.
  1. **willAppear payload completeness** — drive a willAppear and assert the full envelope+payload
     (Catch2 EVENT-02, plus a live `plugin.simulateAction`/debug capture).
  1. **Event dispatch** — `switchToProfile` from a (synthetic) plugin action switches the profile;
     `systemDidWakeUp` reaches a subscribed plugin (debug `eventSent` console / `log.tail`).

### Wave 0 Gaps

- [ ] `tests/unit/active_window_watcher_test.cpp` — factory/backend-selection/debounce (APROF-01)
- [ ] `tests/unit/app_profile_switch_test.cpp` — hint match + default fallback + lifecycle drive (APROF-02)
- [ ] `tests/unit/app_lifecycle_events_test.cpp` — applicationDidLaunch/Terminate fan-out (APROF-04)
- [ ] `tests/unit/willappear_payload_test.cpp` — payload/envelope completeness (EVENT-02)
- [ ] `tests/unit/switch_to_profile_test.cpp` + `system_did_wake_test.cpp` (EVENT-03)
- [ ] `tests/unit/controller_token_test.cpp` — Knob->Encoder normalization (EVENT-04)
- [ ] `docs/plugin-event-parity.md` — coverage deliverable (EVENT-01)
- [ ] A test seam to inject a synthetic foreground-app change into the watcher (mirror
  `SdPluginServer::injectAction` / `input.*` synthetic RPC idiom) so APROF is testable without
  real focus changes; expose a debug RPC (e.g. `window.setForeground`) for live verification.

## Security Domain

> `security_enforcement` is not set to `false` in config — included.

### Applicable ASVS Categories

| ASVS Category         | Applies | Standard Control                                                                                                                                                                                                                                                                                                                                                                                                |
| --------------------- | ------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| V2 Authentication     | no      | (plugin WS auth already exists, unchanged)                                                                                                                                                                                                                                                                                                                                                                      |
| V3 Session Management | no      | -                                                                                                                                                                                                                                                                                                                                                                                                               |
| V4 Access Control     | yes     | The new `switchToProfile` inbound handler must respect plugin scoping — a plugin should only switch profiles it is allowed to; reuse the cross-plugin denial pattern (`ctx.pluginUuid != pluginUuid`, bridge:557) where applicable. New outbound events (applicationDidLaunch) leak the foreground app name to plugins — only deliver to *subscribed/registered* plugins (no broadcast to arbitrary processes). |
| V5 Input Validation   | yes     | `switchToProfile` payload (`profile`/`device` strings) and the assign-profile UI app-name input must be validated/bounded; the `applicationDidLaunch` payload (exe path / app id) is host-sourced but should be length-bounded before send (mirror the logMessage 2048-char cap, application.cpp:633).                                                                                                          |
| V6 Cryptography       | no      | -                                                                                                                                                                                                                                                                                                                                                                                                               |

### Known Threat Patterns for this stack

| Pattern                                                       | STRIDE                 | Standard Mitigation                                                                                                        |
| ------------------------------------------------------------- | ---------------------- | -------------------------------------------------------------------------------------------------------------------------- |
| Foreground-app name disclosure to plugins                     | Information Disclosure | Deliver applicationDid\* only to registered plugins over the loopback-only WS (PLGSEC-03 invariant holds); never broadcast |
| Malicious plugin spams `switchToProfile` to thrash the device | Denial of Service      | Debounce/idempotent-switch (no repaint if target == active); reuse the no-op-on-identical guard pattern (CR WR-01)         |
| Untrusted app-id used as a profile key                        | Tampering              | Treat app-id strictly as a match token against `applicationHints`; never eval/exec it; no shell-out (already a hard rule)  |
| Wayland global spoof / absent                                 | (degraded capability)  | `isActive()` gate; warning chip; manual fallback — fail safe, never fail open                                              |

## Sources

### Primary (HIGH confidence)

- In-repo verified: `src/core/src/input_synthesizer_{linux,win,mac,stub}.cpp` + `src/core/CMakeLists.txt:28-31` (platform-split pattern); `src/core/include/ajazz/core/profile.hpp:192` (`applicationHints`); `src/app/src/sd_plugin_server.cpp:424,534-589` (kRoutedActions / sendEvent no-allowlist); `src/app/src/plugin_device_bridge.cpp:311,375,1138,1213,1341` (instancePayload/eventEnvelope/willAppear/willDisappear); `src/app/src/plugin_manifest.cpp:144` (Knob||Encoder); `src/app/src/application.cpp:603-642,656` (host-event seam, profileChanged wire); `src/app/src/profile_controller.hpp` (Q_INVOKABLE surface).
- Live machine probes (HIGH): niri 26.04 advertises `zwlr_foreign_toplevel_manager_v1` v3 + `ext_foreign_toplevel_list_v1` v1 (wl_registry probe); Qt 6.11.1 with `Qt6::WaylandClient` + `QWaylandClientExtension` header + `qtwaylandscanner`; wayland-scanner 1.24.0; libX11 1.8.13; xcb 1.17.0 (xcb-ewmh absent); `XDG_SESSION_TYPE=wayland`, `XDG_CURRENT_DESKTOP=niri`.
- `/usr/include/qt6/QtWaylandClient/qwaylandclientextension.h` — `QWaylandClientExtensionTemplate<T>`, `isActive()`, `activeChanged`.
- In-repo `docs/protocols/streamdeck/akp_plugin_sdk.md:263-286` — event catalogue (willAppear/dialRotate/touchTap/systemDidWakeUp/applicationDidLaunch payloads; Knob vs Encoder).

### Secondary (MEDIUM confidence — official docs)

- [Qt: qt_generate_wayland_protocol_client_sources](https://doc.qt.io/qt-6/qt-generate-wayland-protocol-client-sources.html) — CMake signature, PRIVATE_CODE, Qt6::WaylandClient.
- [Qt Wayland Custom Extension example](https://doc.qt.io/qt-6/qtwaylandcompositor-custom-extension-example.html) — QWaylandClientExtensionTemplate usage shape.
- [Wayland Explorer: wlr-foreign-toplevel-management-unstable-v1](https://wayland.app/protocols/wlr-foreign-toplevel-management-unstable-v1) — compositor support (GNOME 49.2, KWin 6.6, sway 1.11, Hyprland 0.52.1, niri 25.11), handle events (title/app_id/state/closed), not deprecated.
- [Wayland Explorer: ext-foreign-toplevel-list-v1](https://wayland.app/protocols/ext-foreign-toplevel-list-v1) — list-only (no focus state).
- [swaywm/wlr-protocols XML](https://github.com/swaywm/wlr-protocols/blob/master/unstable/wlr-foreign-toplevel-management-unstable-v1.xml) — canonical XML to vendor.

### Tertiary (LOW confidence — needs validation)

- Windows `GetForegroundWindow`/`GetWindowThreadProcessId`/`QueryFullProcessImageNameW` and macOS `NSWorkspace.frontmostApplication`/`didActivateApplicationNotification`/`didWakeNotification` — standard documented APIs, but compile-guarded and not live-testable on this machine; verify at implementation against current platform SDKs `[CITED: learn.microsoft.com, developer.apple.com — not re-fetched this session]`.
- Linux `systemDidWakeUp` source via logind `PrepareForSleep` D-Bus signal — standard but unverified in this session.

## Metadata

**Confidence breakdown:**

- Standard stack (Wayland/X11): HIGH — every Linux dependency probed live on the dev host; niri advertises the protocol; Qt tooling present.
- Architecture (watcher split, lifecycle reuse, event seam): HIGH — mirrors verified in-repo patterns; integration points read directly from source.
- Win/macOS backends: MEDIUM — standard APIs, compile-guarded, not live-testable here.
- `systemDidWakeUp` OS source: MEDIUM — dispatch path clear, exact per-OS source flagged (A4).
- Event-parity audit: HIGH — event surfaces read directly; SDK doc in-repo.

**Research date:** 2026-06-08
**Valid until:** ~2026-07-08 (stable; compositor support table is the fastest-moving item — re-check wayland.app if GNOME/KDE coverage matters at release).
