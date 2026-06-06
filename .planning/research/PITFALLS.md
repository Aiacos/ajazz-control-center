# Pitfalls Research

**Domain:** Stream Deck-compatible plugin system + device-generic key/dial/touch binding layer (Qt6/QML/C++20, mirajazz sidecar backend)
**Researched:** 2026-06-06
**Confidence:** HIGH — all pitfalls are file:line verified against this codebase's own post-mortems, v1.3 UAT failures, Phase 28/29 research, and CLAUDE.md documented hard lessons. No unverified inference.

______________________________________________________________________

## Critical Pitfalls

### Pitfall 1: "Checked but Not Working" — Unit Tests Pass, Integration Is Silent Void

**What goes wrong:**
A requirement is ticked complete because `ctest` passes and a code review finds no obvious flaw. At the first live run the feature is a no-op: the wire is simply never connected. This project's canonical example (Phase 25 UAT, 2026-05-28): `StreamDockControlService::setActiveDevice()` was declared in C++, called from _no_ QML file, so `m_activeDevice` stayed null and `repaintPage()` returned at the first guard. All 645 unit tests passed. The image-push never fired.

A second live example (Phase 27, 2026-05-31): the Phase-27 "Allow" button for unsigned plugins passed 713/713 unit tests and a full code review, but live debug-channel drive showed it was wired to the Python-host plugin list and was a no-op for `.sdPlugin` plugins. Caught exclusively by `qml.invoke` + `screenshot`.

**Why it happens:**
Unit tests exercise isolated classes. QML binding wires and Qt `connect()` calls that span three objects are invisible to unit tests. The codebase has many places where a signal fires into space because the receiving slot was never connected in `application.cpp`.

**How to avoid:**

1. Every new interactive control MUST set `objectName:` so it is reachable by `scripts/ajazz-debug qml.get/set/invoke/click`.
1. Every new C++ signal/slot wire MUST be driven via the debug channel and verified with `screenshot` before declaring done.
1. The definition-of-done checklist item for any new control: "Can I drive this from `scripts/ajazz-debug`?"
1. Add a `plugin.installedActions` / `plugin.protocolLog` debug RPC alongside any new bridge entry point so autonomous verification covers the full path.
1. For binding-drop → plugin event pipes specifically: verify `willAppear` reaches the plugin log after the drop, not just after device reconnect.

**Warning signs:**

- Unit tests green, no live test run attempted.
- A function exists in C++ but no `QObject::connect()` call references it as a slot.
- `m_activeDeviceId.isEmpty()` at any bridge entry that queries the active device.
- QML calls a `Q_INVOKABLE` but the return value / side effect is never observed.

**Phase to address:** Every execution phase. This is a process rule, not a one-time fix. The ADR phase (architectural decisions) should mandate the debug-channel verification contract as a written requirement, not a convention.

______________________________________________________________________

### Pitfall 2: Sidecar Input Is Hardware-Gated and Provisional — Do Not Patch Blind

**What goes wrong:**
The encoder/touch input decode (delta polarity, touch gesture codes `0x30..0x3F`, zone index byte at `frame[10]`) is documented in `docs/protocols/streamdeck/akp05_input_corrections.md` and in `akp05.md §5` as **PROVISIONAL** — marked "Unverified from a real capture." The demo unit (`0x0300:0x3004`) provably delivers zero input across five independent verification methods including the reference library `4ndv/mirajazz`. Any code that assumes those provisional values are correct and patches around observed mismatch is guessing.

The specific trap: `akp05_input_corrections.md §7.1` documents the input-unreachable proof chain. Developers re-running this investigation (hidraw raw read, `GET_REPORT` polling, evdev, usbmon, mirajazz) will replicate the same zero-capture result and may then "fix" the decode to match a wrong guess, shipping wrong wire values that will fail on a retail unit.

**Why it happens:**
The demo unit is indistinguishable at the USB enumeration level from a retail AKP05E. It looks connected, it responds to output commands (render, brightness, clear), but its input firmware path is disabled. Investigators mistake "no input observed" for "my decode is wrong."

**How to avoid:**

1. Read CLAUDE.md "AKP05E / streamdeck investigation glossary" before ANY sidecar input experiment.
1. Input decode changes require a retail AKP05E or Mirabox N4 (or Frida-on-Windows vendor app capture per `methods-and-tooling.md §2`). Gate these explicitly in the phase requirements as HARDWARE-GATED.
1. Synthetic `input.encoder` / `input.key` / `input.touch` via the debug channel tests the routing pipeline without requiring real hardware. Use this for pipeline integration tests; mark them clearly as "routing test, not wire decode test."
1. When the RE docs and hardware disagree, the hardware wins — and the RE doc must be updated in the same session. Never update only the code.
1. The sidecar (`streamdock-host/`) is the single I/O path for AKP03/AKP05-N4/AKP153. Do NOT reintroduce the removed C++ AKP wire backends (Slice D). Wire-byte coverage lives in the sidecar's cargo tests.

**Warning signs:**

- A proposed commit changes encoder delta polarity, touch gesture byte offsets, or zone-index byte position without citing a retail-unit hardware capture.
- A test asserting input decode values is being added using values derived only from `akp05_input_corrections.md` provisional section.
- The phrase "the device did not respond to input so I adjusted the decode" in a commit message.

**Phase to address:** Hardware-gated sidecar input decode phase. Must be flagged HARDWARE-GATED in the ROADMAP and not allowed to block other phases. Routing pipeline (signal path, action dispatch, context registry) can proceed on synthetic injection. Actual wire values wait for hardware.

______________________________________________________________________

### Pitfall 3: ActionContext Not Registered After Drag-Drop Bind

**What goes wrong:**
A plugin action is dragged from the action library onto an encoder dial or touch-strip zone. The binding persists to the profile on disk. But the next synthetic `input.encoder` event fires into `ContextRegistry::byCoord()` and returns `std::nullopt`. The plugin never receives `dialRotate`. The app logs "unbound encoder — silent drop."

Root cause (verified in Phase 28 research at `application.cpp:450-483`): `profileChanged` is connected to `StreamDockControlService::repaintEncodersFromProfile` and `repaintFromProfile`, but NOT to `PluginDeviceBridge::populateContextsForActivePage`. Contexts are only registered at plugin connect time (`onPluginRegistered`), device connect time (`onDeviceConnected`), or page navigation (`onActivePageChanged`). A QML drag-drop fires `profileChanged` without triggering any of those.

A second layer: `populateContextsForActivePage` does not enumerate `prof.touchZones` at all — touch zone plugin invocation piggybacks on encoder contexts via `controller="Encoder"`. A touch zone binding committed via `commitTouchZoneBinding` produces a context registered under the wrong controller string if the convention is not honoured.

**Why it happens:**
The signal-slot wiring in `application.cpp` was built incrementally. Each new surface (keys, encoders, touch zones) added its own `repaint*` slot but forgot to extend the context-registration trigger.

**How to avoid:**

1. Add `QObject::connect(profileController, &ProfileController::profileChanged, pluginBridge, [&]() { if (!bridge->activeDeviceId().isEmpty()) bridge->populateContextsForActivePage(bridge->activeDeviceId()); })` in `application.cpp`. Guard with `!isEmpty()` — do not call with empty device id.
1. Touch zone contexts must be registered under `controller="Encoder"`, `column=zoneIndex` to match the `onDeviceEvent` line 628 lookup. Document this in code comments. Never change `onDeviceEvent` lookup key unilaterally.
1. After any binding commit path is added, immediately write a unit test: construct bridge + mock server, set profile with the new binding type, call `populateContextsForActivePage`, assert `willAppear` was sent and `registry().byCoord(...)` has value.
1. The live verification sequence for every new binding surface: drop → check `plugin.protocolLog` for `willAppear` → fire `input.encoder` → check log for `dialRotate`.

**Warning signs:**

- A new `commit*Binding` method in `profile_controller.cpp` without a corresponding integration test checking `willAppear`.
- `populateContextsForActivePage` not called in the `profileChanged` connection list.
- `input.encoder` reaches the plugin only after a device disconnect/reconnect cycle, not immediately after the drop.

**Phase to address:** Plugin-host abstraction / binding layer phase. The `profileChanged` → `populateContextsForActivePage` wire is the foundational fix that all subsequent plugin-binding tests depend on.

______________________________________________________________________

### Pitfall 4: QWebEngine + QWebChannel Property Inspector Wiring

**What goes wrong:**
Multiple distinct failure modes, each with its own silent symptom:

(a) **`WebEngineView` has no `page` Q_PROPERTY.** Assigning a `QWebEnginePage*` to `WebEngineView` from QML is a silently inert binding (and a `qmllint missing-property` warning). The page is never displayed. Use `profile:`, `webChannel:`, and `url:` on `WebEngineView` — the three QML-bindable handles. This was already the v1.3 trap; the current `PropertyInspectorController` correctly uses `QQuickWebEngineProfile*` + `QQmlWebChannel*` exposed as Q_PROPERTYYs.

(b) **Wrong WebChannel module.** `Qt6::WebChannel` (the C++ module) and `Qt6::WebChannelQuick` (the QML-bindable module) are distinct CMake targets. The QML `webChannel:` property requires `QQmlWebChannel` from `Qt6::WebChannelQuick`. Using the bare C++ `QWebChannel` produces a type mismatch that silently refuses to bind.

(c) **cefQuery polyfill race condition.** Elgato-compatible PI HTML calls `window.cefQuery(...)` in its inline `<script>`. Injecting the polyfill via `runJavaScript()` after page load races this call — the PI's own script fires before the polyfill is defined, producing an uncatchable `ReferenceError`. The fix is `QWebEngineScript::DocumentCreation` injection (already implemented in `pi_cef_shim.hpp` via `makeCefQueryShim()`). Removing or deferring this injection breaks all standard Elgato PIs silently.

(d) **sdpi.css / local resource serving.** PI HTML files reference `sdpi.css` relative to their location. Without a URL request interceptor scoped to the plugin bundle directory, `file://` resources outside the app bundle are blocked by Chromium's same-origin rules. The `PIUrlRequestInterceptor` per plugin UUID handles this; removing it per-profile breaks PI styling silently.

(e) **Per-plugin profile isolation.** Using a single shared `QQuickWebEngineProfile` across all plugins lets Cookie/localStorage from plugin A bleed into plugin B. Each plugin UUID needs its own profile (already the pattern at `profilesByPluginUuid`). A refactor that collapses this to one profile is a security regression.

(f) **`PIBridge` pointer lifetime.** `PIBridge` is owned by the active `QWebChannel`, which is owned by the controller. On the next `loadInspector` or `closeInspector` call the old bridge is destroyed. Any cached pointer in `Application` or elsewhere becomes dangling. Never cache `PIBridge*` beyond the current inspector session.

**Why it happens:**
Qt WebEngine has a uniquely indirect QML API (three separate properties instead of one page handle) and several non-obvious requirements (module naming, injection timing, interceptor setup). Each omission fails silently at the UI level.

**How to avoid:**

1. Never expose `QWebEnginePage*` directly. Use the three-property pattern: `profile:`, `webChannel:`, `url:`.
1. CMakeLists must link `Qt6::WebChannelQuick` not `Qt6::WebChannel` for QML bindings.
1. The cefQuery shim MUST use `QWebEngineScript::DocumentCreation`. Never defer to `runJavaScript`. Never remove `makeCefQueryShim()`.
1. Every new plugin UUID that opens a PI must get its own `QQuickWebEngineProfile` with its own `PIUrlRequestInterceptor` scoped to the plugin directory.
1. Connect `activeBridgeChanged` in `Application` to wire `toPluginRequested` → `SdPluginServer::sendEvent` every time a new inspector loads.
1. The live verification that requires physical interaction: the real PI JS `$SD.setSettings` round-trip cannot be tested headlessly (WebEngine has no objectName; `qml.invoke` cannot run arbitrary JS). Use `plugin.simulatePiSettings` RPC to prove the bridge half; accept that the actual `$SD.setSettings` call needs a human + a real PI plugin.

**Warning signs:**

- `qmllint` warning `missing-property: page` on `WebEngineView`.
- PI page blank or styling broken with no console error (interceptor missing).
- `window.cefQuery is not a function` in browser console (injection timing wrong).
- PI settings not persisted to `~/.local/share/.../plugins/<uuid>/settings/*.json` (bridge not connected).

**Phase to address:** Property Inspector phase. Flag the physical-mouse / human-verify checkpoint explicitly in the phase plan — autonomous verification can prove only the bridge half.

______________________________________________________________________

### Pitfall 5: Event-Parity Drift vs OpenDeck/Elgato

**What goes wrong:**
The plugin event protocol has many fields. Subtle omissions compile cleanly, pass unit tests, and only surface when a real plugin checks the field and silently ignores the action. Confirmed gaps from this codebase:

- **Missing `action` field in `willAppear`:** OpenDeck and the Elgato SDK require `willAppear` payload to include `"action": actionId`. Without it, plugins that match on `action` to route their state receive the event but do nothing. The plugin appears registered but its `keyDown` handler never fires for the right action.
- **Owner-match UUID prefix check:** `PluginDeviceBridge` checks that the action UUID starts with the plugin's own root UUID before routing events to it (the T-19 leak guard). Silent no-`willAppear` if the context was registered with the root UUID prefix but the plugin registered a UUID subtly different from its manifest top-level UUID. See memory `project_plugin_install_demo_working.md` — "owner-match needs root UUID prefix (silent no-willAppear)."
- **`dialRotate` vs `keyDown` for encoder press:** Encoder press on AKP05E emits `keyDown` (then synthesised `keyUp`) per the press-only-then-release pattern. A plugin expecting `dialDown`/`dialUp` receives nothing. Current behavior is documented but may diverge from a future retail unit that uses `dialDown`.
- **Coordinates:** `willAppear` and `keyDown` carry `row`/`column` coordinates. If the context is registered with a wrong row/column (e.g., using page-relative index when 0-based column is expected) the plugin can match the event but display on the wrong cell.
- **`SecondaryScreen` / `Encoder` controller semantics:** The `controller` field in event payloads must match what the plugin's manifest declares. AJAZZ uses `"Knob"` where Elgato uses `"Encoder"`. Sending `"Encoder"` to a plugin that declares `"Knob"` will cause it to ignore the event silently.

**Why it happens:**
The event schema is long and the reference is spread across `docs/schemas/plugin_manifest.schema.json`, `docs/protocols/streamdeck/akp_plugin_sdk.md`, and OpenDeck source. Incremental additions miss fields. "The plugin loaded" is mistaken for "the plugin received the right event."

**How to avoid:**

1. Maintain an explicit event-parity coverage table (OpenDeck `events/inbound` + `events/outbound` vs our `PluginDeviceBridge` implementation) as a required deliverable in the event-parity audit phase.
1. Every event payload MUST include all fields the Elgato SDK and OpenDeck send. Use OpenDeck's `events/outbound/*.rs` as the authoritative field list.
1. The `willAppear` payload check should be a unit test: construct a bridge, register a plugin action, call `populateContextsForActivePage`, assert the captured WS message JSON has `"action"`, `"context"`, `"device"`, `"event"`, and `"payload"` with `"row"`, `"column"`, `"controller"`, `"settings"`.
1. For controller token normalization: the affordance normalizer (`affordanceMask()`) and the event `controller` field must use the same canonical string ("Encoder" or "Knob"). Pick one and enforce consistently.
1. Use `plugin.protocolLog` debug RPC to capture the exact JSON the running plugin receives, and compare manually against the Elgato SDK spec.

**Warning signs:**

- A plugin registers (appears in `plugin.list`) but never shows an image or reacts to key presses.
- `plugin.protocolLog` shows `willAppear` was sent but the plugin's own log shows no reaction.
- Event payload JSON is missing `"action"` or `"controller"` keys.

**Phase to address:** Event-parity audit phase. This must precede any phase that declares plugin event handling complete.

______________________________________________________________________

### Pitfall 6: Native Win-Only Plugin Execution

**What goes wrong:**
Windows-only `.sdPlugin` plugins (those with `manifest.json` `"OS": [{"Platform": "windows"}]` and a Windows executable as the plugin process) cannot be spawned on Linux/macOS without Wine. The pitfall is in the approach: OpenDeck's `plugins/mod.rs` spawns via Wine when the OS filter says `windows` and the host is Linux/macOS. Without implementing this gate, either (a) the plugin is spawned natively and immediately crashes (ELF vs PE), or (b) the plugin is silently skipped with no user-visible reason.

The secondary Wine trap: Wine requires a full Windows environment. The plugin's bundled DLLs may need `WINEPREFIX` initialization, the WS connection back to the app must use `ws://127.0.0.1:<port>` (not a Unix socket), and Wine's process isolation means the crash tracker's `QProcess::finished` signal may not fire when Wine exits abnormally. Plugin crash tracking breaks for Wine-spawned plugins.

**Why it happens:**
The OS filter field in manifests is parsed but the spawn path in `plugin_manager.cpp` does not check it against the current platform. The developer tests on Linux with a Linux-compatible plugin, the manifest check path is never exercised.

**How to avoid:**

1. Parse `"OS"` platform filters in `plugin_manifest.cpp` during `parseManifest()`. Surface as a `supportsCurrentPlatform()` helper.
1. `plugin_manager.cpp::spawn()` must check `supportsCurrentPlatform()` before any process spawn. On mismatch: if Wine is available, route to Wine spawn path; otherwise surface a distinct "Windows-only plugin" status in `LoadedPluginsPage.qml` — never silently skip.
1. Wine spawn path gets its own `QProcess` wrapper with its own crash tracker entry. The crash tracker must handle the Wine parent-process exit pattern (Wine exits cleanly even when the guest crashes).
1. The WS connection back to the app: Wine-spawned plugins must be given the same `ws://127.0.0.1:<port>` connection string as native plugins. No special IPC path needed.
1. Do NOT use `wine` if the target executable is a Node.js plugin (`manifest.json` `"CodePath"` points to a `.js` file) — those are cross-platform by design. Only native PE binaries need Wine.
1. Document Wine as a fallback, not the primary path. Investigate native cross-compilation or Proton as alternatives before defaulting to Wine.

**Warning signs:**

- A Windows-only plugin is installed, `plugin.list` shows it connected, but it immediately drops off.
- `QProcess::exitCode` is always 0 for a Wine-spawned plugin that should have crashed.
- Plugin manifest has `"OS": [{"Platform": "windows"}]` and the spawn path does not produce a distinct log line.

**Phase to address:** Plugin host abstraction phase. The OS-filter gate must be in the base modular interface, not added post-hoc per plugin type.

______________________________________________________________________

### Pitfall 7: Per-App Profile Switching Across Wayland/X11/Windows/macOS

**What goes wrong:**
Per-app (foreground-window-driven) profile switching requires knowing which application has focus. This is platform-specific at each layer:

- **Linux/Wayland:** No standard API. `wlroots`-based compositors (Sway, Niri) expose `wlr-foreign-toplevel-management-unstable-v1` or D-Bus `org.gnome.Shell` `FocusedApp` for GNOME. KDE Plasma exposes `org.kde.KWin`. These are compositor-specific, fragile, and unavailable on all compositors. A generic "active window process name" query simply does not exist on Wayland.
- **Linux/X11:** `_NET_ACTIVE_WINDOW` + `XGetWindowProperty` gives the active window ID; `_NET_WM_PID` gives the PID; `/proc/<pid>/exe` gives the executable path. Works but requires `xcb` or `Xlib` linkage.
- **Windows:** `GetForegroundWindow()` + `GetWindowThreadProcessId()` + `QueryFullProcessImageNameW()`. Well-defined but requires the Win32 API.
- **macOS:** `NSWorkspace.shared.frontmostApplication` (AppKit). Requires the `accessibility` entitlement in sandboxed builds.

OpenDeck's `application_watcher.rs` uses `active-win` (a native system call wrapper) per platform. This is an external dependency not yet present in this codebase.

The cross-platform trap: implementing this feature behind a single C++ interface that compiles on all platforms requires four separate backends or a carefully guarded `#ifdef` tree. Missing a platform leaves profile switching silently broken.

**Why it happens:**
Developers prototype on Linux/X11 where `_NET_ACTIVE_WINDOW` is easy, ship it without a Wayland or Windows path, and the feature is broken on 80% of real user systems.

**How to avoid:**

1. Define a `IActiveWindowWatcher` interface with `activeProcessName()` and a `focusChanged(QString processName)` signal. Ship stub implementations on all platforms first.
1. Implement the X11 backend using `xcb` (already a transitive dep via Qt). Gate with `QX11Info::isPlatformX11()` at runtime.
1. For Wayland: implement a D-Bus query to `org.gnome.Shell` and `org.kde.KWin` with a 2-second poll fallback. Document the compositor limitations explicitly. Do NOT promise per-app switching on all Wayland compositors.
1. For Windows: implement via `GetForegroundWindow` in the Win32 backend, separate compilation unit, `#ifdef Q_OS_WIN`.
1. For macOS: use `NSWorkspace` in an Objective-C++ compilation unit, `#ifdef Q_OS_MAC`.
1. The QSettings key for per-app profiles must be stable across renames; use the executable basename, not the full path.
1. Profile switching must be debounced (at least 100 ms) to avoid thrashing during task switching.

**Warning signs:**

- Per-app switching works on X11 but is silent on Wayland.
- Profile switching on Windows fires `GetForegroundWindow` for the app's own window and immediately reverts.
- macOS build fails because `NSWorkspace` header is included in a C++ (not ObjC++) translation unit.

**Phase to address:** Per-app profile phase. This phase needs a HARDWARE-GATED / PLATFORM-GATED verification checkpoint: full verification requires testing on X11, Wayland, Windows, and macOS.

______________________________________________________________________

### Pitfall 8: Action-Instance and State Model Edge Cases

**What goes wrong:**
Multi Action and Toggle Action have per-state images, titles, and settings. The edge cases:

(a) **`DisableAutomaticStates` not parsed:** When false (default), the host automatically advances the action's state index on each `keyDown`. If this field is absent from the `PluginAction` struct, the host never honours it and either always auto-advances (breaking plugins that manage state themselves) or never advances (breaking plugins that expect host-driven state toggle).

(b) **Multi Action onPress chain collapses on move:** When a key binding with a 2+ action `onPress` chain is moved to another key via a drag-and-drop, a naive "copy then delete" implementation commits each action individually via `commitKeyBinding`, which replaces the entire `Binding` for the destination key on the first action and loses it on the second. The fix is `ProfileController::swapKeyBindings` which moves the entire `core::Binding` atomically (landed in Phase 29).

(c) **Per-state settings context:** Each action state should be independently settable by the PI. The context UUID must encode the state index. If the context key does not include the state, two states share a settings file and overwrite each other.

(d) **`willAppear` on state change:** Elgato SDK requires a `willAppear` event when the state index changes (e.g., Toggle Action cycles to state 1). Without this, the plugin is never notified that the visible state changed and cannot update its image.

(e) **QML `index` re-declaration in list delegates:** The `ActionLibraryPane.qml` has a known `index` `ReferenceError` (noted in `opendeck-ui-plugin-study.md`). Any new list delegate that uses `index` inside a `Repeater` must use `model.index` or a `required property int index` — never redeclare `index` as a local property inside a delegate that also receives it as an attached property.

**Why it happens:**
The state model is implemented incrementally. Each state-related field is added when a plugin that needs it is encountered. `DisableAutomaticStates` is easy to miss because no vendor plugin in the standard corpus requires it. The QML `index` trap is a Qt ambiguity that produces a runtime ReferenceError, not a build error.

**How to avoid:**

1. Parse ALL fields from `plugin_manifest.schema.json` `States[]` and the `DisableAutomaticStates` flag upfront, not on-demand. Use the schema as the authoritative field list.
1. `swapKeyBindings` is the atomic move primitive. No commit-then-delete patterns.
1. Context UUID for per-state settings: `deviceId#page#controller#row#col#state<N>`. Verify the format is stable across profile load/save round-trips.
1. Unit test: Toggle Action → assert `willAppear` emitted on state change.
1. QML list delegates: use `required property int index` in every new delegate that uses the index; never rely on the implicit attached property inside a `Repeater`/`ListView`.

**Warning signs:**

- A Toggle Action plugin updates its image on the first press but not subsequent presses.
- A Multi Action key silently drops the second action on drag-move.
- PI settings for state 0 overwrite state 1 settings.
- `ReferenceError: index is not defined` in QML console from a list delegate.

**Phase to address:** Action instance + states phase. Add a Catch2 test battery for all state-model edge cases before any QML multi-action UI work.

______________________________________________________________________

### Pitfall 9: Security — Loopback Binding, Signature Gate, Unsigned-Consent Persistence

**What goes wrong:**
Four distinct security pitfalls, each already partially addressed but each with a specific regression surface:

(a) **WS server bound to `0.0.0.0`:** The vendor implementation uses `QHostAddress::Any`. Our implementation correctly uses `QHostAddress::LocalHost` (127.0.0.1) enforced at `sd_plugin_server.cpp:92`. A refactor that changes the listen call must not reintroduce `Any`.

(b) **Unsigned-consent leaks into tampered path:** `verifyStagedPlugin` maps both "no signature" and "signature present but invalid" to `VerifyVerdict::Refused`. A plugin with a forged/tampered signature must be refused even when `userConfirmedUnsigned=true`. Letting the `userConfirmedUnsigned` flag bypass tampered-signature rejection is a critical security regression (CR-01 in Phase 27 research). The verifier must distinguish `Unsigned` from `Tampered` as separate verdict states.

(c) **One-shot unsigned consent is wiped on restart:** Current behavior (memory `project_plugin_install_demo_working.md`): "unsigned consent is one-shot (launch-sweep deletes)." A per-plugin persisted consent record (`QSettings`: `plugins/consented/<uuid>`) must survive restarts. But it must NOT grant consent for tampered plugins.

(d) **No phone-home:** The plugin catalog must never contact external servers for validation or telemetry. The trust anchor is the local `trust_roots.json`. Any new "cloud catalog" feature must be explicitly opt-in and must not transmit plugin UUIDs or usage data. The existing `onlineCatalogEnabled` toggle (default: on for discovery, not for telemetry) is the correct model.

(e) **Plugin post-install re-verify:** After cross-filesystem copy during install, the manifest must be re-verified (path at `plugin_catalog_model.cpp:846`). If the re-verify path is ever made conditional or skipped for "already-verified" plugins, a TOCTOU window opens.

**Why it happens:**
Security properties are easy to regress in refactors that change the call site without re-reading the security invariant at the call site. The tampered-vs-unsigned distinction is subtle; most devs conflate "refused" with "bad." The phone-home trap appears when adding convenience catalog features.

**How to avoid:**

1. CI grep gate: `grep -rn "QHostAddress::Any" src/` must return 0. Add this to `.github/workflows/ci.yml` alongside the existing `hid_open` grep gate.
1. The verifier unit test for tampered-refused-even-with-consent is the non-negotiable gate (Phase 27 D-1). Never ship without it.
1. Persisted unsigned consent in QSettings, per plugin UUID, distinct from crash-disable (crash-disable is NOT persisted; user-consent IS persisted).
1. Code review checklist item for any catalog feature: "Does this call an external URL without an explicit user opt-in gate?"
1. The re-verify path after install must be unconditional — never skip it for "just-installed" files.

**Warning signs:**

- `sd_plugin_server.cpp` listen call changed from `LocalHost` to any other address family.
- `userConfirmedUnsigned` read inside a code path that also checks `signatureState == Invalid`.
- A network call in `plugin_catalog_model.cpp` without an `onlineCatalogEnabled` guard.
- The re-verify call at `:846` wrapped in a condition.

**Phase to address:** Security hardening phase (plugin host abstraction). The verifier-split unit test is the gate before any unsigned-consent UI is built.

______________________________________________________________________

### Pitfall 10: Cross-Platform QSettings Persistence — Empty Org/App Name

**What goes wrong:**
`QSettings` on Windows uses the registry under `HKCU\Software\<OrganizationName>\<ApplicationName>`. If either is empty, QSettings silently writes to `HKCU\Software\\` (or a root key), which is shared across all applications and creates key collisions. On Linux/macOS the equivalent is a file at `~/.config//<appname>.ini` or `~/Library/Preferences/.plist`, which fails silently with a permission error.

`main.cpp` correctly calls `QApplication::setOrganizationName(AJAZZ_VENDOR_NAME)` and `QApplication::setApplicationName(AJAZZ_PRODUCT_NAME)`. The risk is any test or secondary entry point that constructs `QSettings` before these calls, or any new executable (e.g., a helper CLI or test runner) that omits them.

The secondary trap: `QStandardPaths::setTestModeEnabled(true)` redirects QSettings to a temp location in tests, but only if called before any `QSettings` constructor. A test that constructs a `QSettings` in a static initializer bypasses `setTestModeEnabled` and writes to the real user config.

**Why it happens:**
New test files copy an existing pattern but omit the `setTestModeEnabled(true)` call. The failure is silent on Linux (writes to a temp path anyway under XDG) but corrupts user settings on Windows during a test run.

**How to avoid:**

1. Any test that touches `QSettings` (directly or via a service that wraps it) must call `QStandardPaths::setTestModeEnabled(true)` as the first line of the test fixture setup.
1. No `QSettings` construction in static initializers or global objects — only in functions called after `QApplication` is constructed.
1. CI check: grep for `QSettings` in test files, assert each test fixture that uses it also has `setTestModeEnabled`. Can be a Python pre-commit hook.
1. The `AJAZZ_VENDOR_NAME` and `AJAZZ_PRODUCT_NAME` macros must be defined via CMake (already the case), not hardcoded per translation unit.

**Warning signs:**

- Windows CI produces spurious registry entries under `HKCU\Software\` with no company name prefix.
- A test passes locally but fails on Windows CI because settings from a previous test bleed through.
- `QSettings::fileName()` returns a path with `//` double-slash in the organization segment.

**Phase to address:** Plugin persistence phase (first phase that adds new QSettings keys). Add a static analysis check or grep gate to CI.

______________________________________________________________________

### Pitfall 11: Crash Isolation — Plugin-Kill-Mid-Handshake App Crash

**What goes wrong:**
When a plugin process is killed between the WebSocket `onNewConnection` event and the `registerPlugin` message (i.e., before the UUID is known), `SdPluginServer` holds a `QWebSocket*` with no corresponding plugin entry. On subsequent cleanup (device disconnect, app shutdown), code that iterates `m_connections` by UUID will not find the entry. Depending on the implementation, this produces either a null dereference or a dangling `QWebSocket*` that was already freed by Qt's connection lifecycle.

This is the "app crashes on plugin-kill mid-handshake" gap documented in memory `project_plugin_install_demo_working.md`. It is distinct from the 3-in-30s crash policy managed by `plugin_crash_tracker.cpp` (which only fires after a plugin has successfully registered).

A related crash: if a Python plugin host child writes to a pipe after the app closes the read end, `SIGPIPE` kills the app. `main.cpp` correctly installs `signal(SIGPIPE, SIG_IGN)`. Removing this call without a replacement (e.g., `SO_NOSIGPIPE` per-socket) is a regression.

**Why it happens:**
The pre-registration window is a time interval, not a state machine. The server does not track "connections awaiting registration" separately from "registered connections." Cleanup code written for the registered state is wrong for the pre-registration state.

**How to avoid:**

1. Track pre-registration connections in a separate `QSet<QWebSocket*>` cleared on `registerPlugin` or on disconnect (whichever comes first). On disconnect without registration, log and discard — no plugin-manager notification needed.
1. All cleanup paths (device disconnect, app shutdown, server stop) must iterate only the registered-connection map, never raw `QWebSocket*` pointers that may have been freed.
1. The `PluginCrashTracker` 3-in-30s window starts AFTER successful registration. Pre-registration exits are tracked separately as "failed to connect" (not a crash, not a disable trigger).
1. The SIGPIPE handler must remain. Any new pipe-based IPC (future Python or native plugin host) must be added to the same `SIG_IGN` umbrella.
1. Catch2 test: construct a `SdPluginServer`, open a WebSocket connection, do NOT send `registerPlugin`, then call `server.stop()` — assert no crash, no dangling pointer.

**Warning signs:**

- App crashes with a null dereference in `SdPluginServer::onClientDisconnected()` when no plugin has registered.
- `plugin.list` shows 0 connected plugins but the crash count increments.
- A new IPC mechanism (stdout/stdin pipe to a child) is added without verifying SIGPIPE disposition.

**Phase to address:** Plugin host abstraction modular refactor. The pre-registration lifecycle must be explicit in the new modular interface, not left as an implicit time window.

______________________________________________________________________

## Technical Debt Patterns

| Shortcut                                        | Immediate Benefit                                     | Long-term Cost                                                                                                          | When Acceptable                                                              |
| ----------------------------------------------- | ----------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------- |
| Skip `objectName:` on new QML controls          | Faster QML authoring                                  | Control is invisible to debug channel; autonomous verification impossible; the CLAUDE.md definition-of-done is violated | Never. Every interactive control must have `objectName:`.                    |
| Use `QHostAddress::Any` for WS server           | Simplifies test setup (reachable from other machines) | Exposes plugin API to LAN; any process on the network can register as a plugin                                          | Never in production. Test isolation via loopback only.                       |
| Single `QQuickWebEngineProfile` for all plugins | One profile to manage                                 | Cookie/localStorage bleed across plugins; security regression                                                           | Never. Per-UUID profiles are required.                                       |
| Skip the cefQuery shim injection                | Simpler startup code                                  | All standard Elgato PIs fail silently (cefQuery is undefined)                                                           | Never.                                                                       |
| Verify by unit test only, skip debug channel    | Faster iteration                                      | "Checked but not working" — the #1 failure mode. Produces false confidence.                                             | Never for interactive features. Unit tests are necessary but not sufficient. |
| One unified crash-disable for all exit reasons  | Simpler tracker                                       | Pre-registration exits disable the plugin permanently without a legitimate crash                                        | Never. Distinguish pre-registration exits from post-registration crashes.    |
| Touch zone context under "TouchZone" controller | Semantically cleaner                                  | `onDeviceEvent` line 628 looks up `controller="Encoder"`. Mismatched key = silent drop.                                 | Never without also updating `onDeviceEvent`. The convention is locked.       |
| Seed `defaultSettings` only at first PI open    | Defer complexity                                      | If the PI opens before `willAppear`, `getSettings` returns empty and the plugin uses wrong defaults                     | Acceptable only for plugins with no `Settings` object in manifest.           |

______________________________________________________________________

## Integration Gotchas

| Integration                    | Common Mistake                                                                                      | Correct Approach                                                                                                                                            |
| ------------------------------ | --------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `profileChanged` signal        | Connecting only repaint slots, not bridge context refresh                                           | Connect `populateContextsForActivePage` in the same `application.cpp` block, guarded by `!activeDeviceId().isEmpty()`                                       |
| QWebChannel `$SD` object name  | Registering the bridge under a different object name than `"$SD"`                                   | Always register as `channel->registerObject(QStringLiteral("$SD"), bridge)`. The cefQuery shim hard-codes `"$SD"`.                                          |
| mirajazz sidecar JSON protocol | Adding C++ AKP wire calls directly in `SidecarStreamDockDevice` instead of JSON commands            | All wire commands go via JSON over stdout to the sidecar. Never reintroduce `akp05.cpp`-style direct HID writes for AKP03/05/153.                           |
| `hid_open()` call sites        | Calling `hid_open` outside `hid_transport.cpp` in a new device backend                              | CI grep gate enforces this. The gate must be extended to cover any new transport file.                                                                      |
| `nlohmann::json` includes      | Adding `#include <nlohmann/json.hpp>` in a new app-layer file that is also included by `ajazz_core` | Run `grep -rn nlohmann src/core/include/` — must return 0. The COD-031 boundary is a release-blocker.                                                       |
| QML list delegate `index`      | Using bare `index` in a `Repeater` delegate where `index` is also an attached property              | Use `required property int index` or `model.index`. Never rely on implicit attached `index` in delegates.                                                   |
| `QML_SINGLETON` macro          | Using `QML_SINGLETON` alone without `qmlRegisterSingletonInstance`                                  | Always pair with `qmlRegisterSingletonInstance` and `static_assert(!std::is_default_constructible_v<T>)`. Macro alone creates a second instance per import. |

______________________________________________________________________

## Performance Traps

| Trap                                                                            | Symptoms                                                             | Prevention                                                                                                                                          | When It Breaks                               |
| ------------------------------------------------------------------------------- | -------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------- |
| `installedActions()` called on every frame                                      | UI jank when action library is open; disk I/O on every repaint       | `installedActions()` scans disk. Cache the result; only invalidate on `rediscover()` or plugin install/remove                                       | > 20 installed plugins                       |
| Per-plugin `QQuickWebEngineProfile` cold start                                  | PI first open takes 2-3 s on slow systems (Chromium profile init)    | Profile creation is one-time per UUID. Keep profiles alive for the session even after PI close; only destroy on plugin uninstall                    | Immediately noticeable; no scale threshold   |
| Calling `populateContextsForActivePage` on every `profileChanged` with no guard | Excess `willAppear`/`willDisappear` churn during rapid profile edits | Debounce or guard with `activeDeviceId().isEmpty()`. The bridge's reconcile diff approach (Phase 29) already handles idempotency but not throttling | > 5 bound actions + rapid drag-drop sequence |
| `rediscover()` spawning already-live plugins                                    | Duplicate plugin processes; shared key collision in `m_connections`  | `rediscover()` must diff `m_live` keyset before spawning. Unit test: assert spawn count = 1 after two `rediscover()` calls with same plugin on disk | Any call frequency                           |

______________________________________________________________________

## Security Mistakes

| Mistake                                         | Risk                                                                                       | Prevention                                                                                                                     |
| ----------------------------------------------- | ------------------------------------------------------------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------ |
| WS server on `0.0.0.0` instead of `127.0.0.1`   | Any LAN host can register a fake plugin and receive key events or inject actions           | CI grep gate: `grep -rn "QHostAddress::Any" src/` must return 0                                                                |
| Tampered plugin bypassing unsigned-consent gate | A modified plugin with a forged signature slip through as "unsigned" and gets user consent | Verifier must return `Tampered` (never allowable) vs `Unsigned` (consent-gated). Unit test: tampered-refused-even-with-consent |
| PI page JS accessing arbitrary `file://` paths  | A malicious PI could read local files via `XMLHttpRequest("file:///etc/passwd")`           | `PIUrlRequestInterceptor` restricts to the plugin bundle directory. Never widen this to the full filesystem.                   |
| Phone-home in catalog code                      | Plugin UUID / install counts leak to external server                                       | Every network call in `plugin_catalog_model.cpp` must be gated by `onlineCatalogEnabled` and must not transmit plugin UUIDs    |
| Crash-disable bypass on pre-registration exits  | A plugin that always fails to register is respawned indefinitely                           | Pre-registration exits do NOT count toward the 3-in-30s crash window. They are tracked separately with their own backoff.      |

______________________________________________________________________

## UX Pitfalls

| Pitfall                                                          | User Impact                                                                                              | Better Approach                                                                                                                              |
| ---------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------- |
| Plugin trust status not visually distinct (Unsigned vs Tampered) | User cannot tell whether a plugin is merely unsigned (consent-able) or actively tampered (refuse always) | Three distinct chip states in `LoadedPluginsPage.qml`: trusted (green), unsigned (amber + consent button), tampered (red, no consent button) |
| `Information`-only actions draggable but all drops rejected      | User drags an action, every target rejects with red flash; no explanation                                | `enabled: affordanceMask !== 0` on the drag handler; tooltip "This action is for informational display only"                                 |
| No affordance for win-only plugins on Linux                      | Plugin installed, appears in list, never spawns; no message                                              | Surface `"Windows only — requires Wine"` in the plugin status chip                                                                           |
| Per-app profile switching silently broken on Wayland             | User sets up per-app switching, nothing happens                                                          | Show a compositor-capability warning on Wayland when the watcher cannot detect the active window                                             |
| Unsigned consent disappears on restart                           | User consents once, restarts app, plugin is blocked again                                                | Persist consent in QSettings per plugin UUID (distinct from crash-disable which is intentionally session-only)                               |

______________________________________________________________________

## "Looks Done But Isn't" Checklist

- [ ] **Plugin bind on encoder dial:** Verify `willAppear` appears in `plugin.protocolLog` immediately after the drop — not only after the next device reconnect.
- [ ] **Touch zone plugin bind:** Verify context registered under `controller="Encoder"` (not "TouchZone") matching `onDeviceEvent` line 628 lookup.
- [ ] **`willAppear` payload completeness:** Assert `"action"` field is present in `willAppear` JSON — its absence causes silent plugin handler mismatch.
- [ ] **Tampered-vs-unsigned verifier split:** Run the unit test asserting tampered plugin refused even with `userConfirmedUnsigned=true`.
- [ ] **WS loopback invariant:** `grep -rn "QHostAddress::Any" src/` returns 0 after any `SdPluginServer` refactor.
- [ ] **cefQuery shim injection point:** Verify `QWebEngineScript::DocumentCreation` (not `DocumentReady` or deferred `runJavaScript`) on any PI infrastructure change.
- [ ] **`objectName:` coverage:** Every new Button, Switch, Drawer, KeyCell, EncoderDial, TouchZone cell, or list delegate has `objectName` set before the phase closes.
- [ ] **`profileChanged` → `populateContextsForActivePage` wire:** Check `application.cpp` after any restructuring that the connection is present and guarded by `!activeDeviceId().isEmpty()`.
- [ ] **Multi-action chain survives move:** Use `swapKeyBindings` (atomic whole-binding move), never commit-then-delete for drag-move operations.
- [ ] **Sidecar input values marked PROVISIONAL:** Any encoder/touch input decode change cites a retail AKP05E hardware capture, not an inference from `akp05.md` provisional section.
- [ ] **SIGPIPE handler preserved:** `signal(SIGPIPE, SIG_IGN)` present in `main.cpp` after any main-entry restructuring.
- [ ] **QSettings test isolation:** Every new test file that touches QSettings has `QStandardPaths::setTestModeEnabled(true)` as its first fixture line.
- [ ] **COD-031 boundary:** `grep -rn nlohmann src/core/include/` returns 0 after any new dep addition.

______________________________________________________________________

## Recovery Strategies

| Pitfall                                              | Recovery Cost   | Recovery Steps                                                                                                                                                    |
| ---------------------------------------------------- | --------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| "Checked but not working" discovered at UAT          | MEDIUM          | Add the missing `QObject::connect()` or QML binding. Identify the debug-channel verification that would have caught it earlier. Add that RPC to the debug facade. |
| Hardware-provisional decode shipped wrong            | HIGH            | Immediately revert the decode change. File as HARDWARE-GATED blocker. Do not patch further without retail unit.                                                   |
| ActionContext registration gap (willAppear not sent) | MEDIUM          | Add `profileChanged` → `populateContextsForActivePage` connection. Write the unit test proving the path. Verify live via `plugin.protocolLog`.                    |
| cefQuery shim broken (all PIs fail)                  | LOW             | Restore `QWebEngineScript::DocumentCreation` injection. Test with a real PI HTML file that calls `cefQuery`.                                                      |
| WS server accidentally on 0.0.0.0                    | HIGH (security) | Immediately revert to `QHostAddress::LocalHost`. Tag the fix as a security patch. Add CI grep gate to prevent recurrence.                                         |
| Unsigned-consent leaks into tampered path            | HIGH (security) | Revert the verifier change. Restore the `Unsigned` vs `Tampered` distinction. The tampered-refused unit test is the gate.                                         |
| Per-app profile switching broken on Wayland          | MEDIUM          | Implement the D-Bus compositor watcher and document limitations. Surface a UI warning where the API is unavailable.                                               |
| Plugin kills app mid-handshake                       | HIGH            | Fix the pre-registration lifecycle tracking (separate `QSet<QWebSocket*>`). Add a Catch2 test for the disconnect-before-register scenario.                        |

______________________________________________________________________

## Pitfall-to-Phase Mapping

| Pitfall                                        | Prevention Phase                        | Verification                                                                                             |
| ---------------------------------------------- | --------------------------------------- | -------------------------------------------------------------------------------------------------------- |
| "Checked but not working" (P1)                 | Every phase — process rule              | Debug channel drive + screenshot for every new interactive control before phase close                    |
| Sidecar input provisional/hardware-gated (P2)  | Hardware-gated sidecar input phase      | Phase plan explicitly marked HARDWARE-GATED; retail AKP05E required for close                            |
| ActionContext not registered after drop (P3)   | Plugin-host abstraction / binding layer | Unit test: drop → `willAppear` asserted; live: `plugin.protocolLog` after synthetic `input.encoder`      |
| QWebEngine PI wiring (P4)                      | Property Inspector phase                | Headless: bridge half via `plugin.simulatePiSettings`; human-verify checkpoint for real PI JS round-trip |
| Event-parity drift (P5)                        | Event-parity audit phase                | Coverage table deliverable; `willAppear` payload unit test with all required fields                      |
| Native win-only plugin execution (P6)          | Plugin host abstraction                 | Manifest OS-filter parse unit test; Wine spawn path documented; `LoadedPluginsPage` shows status         |
| Per-app profile switching (P7)                 | Per-app profiles phase                  | PLATFORM-GATED verification checkpoint; Wayland limitation documented in UI                              |
| Action-instance/state edge cases (P8)          | Action instances + states phase         | Catch2 test battery for all state-model edge cases before QML UI work                                    |
| Security (loopback / signature / consent) (P9) | Security hardening phase                | CI grep gate for `QHostAddress::Any`; tampered-refused unit test; phone-home review in code audit        |
| QSettings persistence (P10)                    | Plugin persistence phase                | Windows CI run; grep gate for `setTestModeEnabled` in test files                                         |
| Crash isolation / mid-handshake (P11)          | Plugin host abstraction                 | Catch2 test: disconnect-before-register, assert no crash; SIGPIPE handler grep in CI                     |

______________________________________________________________________

## Sources

- `CLAUDE.md` — project hard rules, Qt gotchas, debug-channel verification mandate, AKP05E investigation glossary (HIGH confidence — project-owned, file:line verified)
- `.planning/milestones/v1.3-phases/25-hardware-verification-real-plugin/25-UAT.md` — 3-layer image-upload regression (L1/L2/L3), 16 UAT results (HIGH confidence — documented live session)
- `.planning/milestones/v1.3-phases/25-hardware-verification-real-plugin/25-VERIFICATION.md` — demo-unit input-streaming proof chain (HIGH confidence)
- `.planning/milestones/v1.3-phases/27-plugin-install-trust-persistence-hardening/27-RESEARCH.md` — verifier split, rediscover idempotency, concurrency guard (HIGH confidence — file:line verified)
- `.planning/milestones/v1.3-phases/28-akp05-plugin-action-completeness-drag-to-bind-on-keys-dials/28-RESEARCH.md` — ActionContext gap, 5-arg drop bug, affordance normalization (HIGH confidence — file:line verified)
- `.planning/milestones/v1.3-phases/29-plugin-gui-parity-real-drag-drop-pi-config-multi-action/29-LIVE-VERIFICATION.md` — PI bridge live results, PLUGIN-21/22/23 status (HIGH confidence — autonomous debug-channel session)
- `.planning/opendeck-ui-plugin-study.md` — OpenDeck architecture map, gap table (HIGH confidence — code-level study)
- `.planning/RETROSPECTIVE.md` — cross-milestone failure patterns: agent concurrency, `--no-transition` footguns, integration-audit value (HIGH confidence)
- `src/app/src/property_inspector_controller.{hpp,cpp}` — WebEngine wiring patterns, `QQmlWebChannel` vs `QWebChannel`, per-plugin profile isolation (HIGH confidence — live source)
- `src/app/src/pi_cef_shim.hpp` — cefQuery shim source, injection point documentation (HIGH confidence — live source)
- `src/app/src/sd_plugin_server.cpp:88-100` — loopback binding, SECURITY-CRITICAL comment (HIGH confidence — live source)
- `src/app/src/main.cpp:49-55` — SIGPIPE handler (HIGH confidence — live source)
- User memory `project_plugin_install_demo_working.md` — "app crashes on plugin-kill mid-handshake", owner-match UUID prefix gap, unsigned-consent one-shot (HIGH confidence — documented live session)

______________________________________________________________________

*Pitfalls research for: v2.0 modular plugin + key/dial binding reimplementation (AJAZZ Control Center)*
*Researched: 2026-06-06*
