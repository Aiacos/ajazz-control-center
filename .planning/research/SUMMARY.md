# Project Research Summary

**Project:** AJAZZ Control Center v2.0 — Modular Plugin & Binding System
**Domain:** Stream Deck-compatible plugin host + device-generic key/dial/touch binding layer (Qt6/C++20, mirajazz sidecar)
**Researched:** 2026-06-06
**Confidence:** HIGH

______________________________________________________________________

## Executive Summary

v2.0 is a targeted modular refactor of a substantial, working plugin system — not a green-field
rewrite. The v1.3 codebase already contains `SdPluginServer` (full Elgato v6 WS protocol),
`PluginDeviceBridge` (context registry, lifecycle events, inbound/outbound routing), `PluginManager`
(Node/HTML/native spawn, crash lifecycle), `PropertyInspectorController` (QWebEngine + QWebChannel
scaffolding), `SidecarStreamDockDevice` (mirajazz proxy, hardware-confirmed no-wedge), and
`ProfileController` (profile load/save, applicationHints schema field). The research verdict:
**grep before building anything**. The genuine gaps are narrowly defined, high-impact, and
amenable to targeted extensions without touching the working infrastructure.

The five real gaps that block production-quality plugin usage are: (1) the `ActionInstance` model
with `states[]` / `current_state` / `children` — required for Multi Action, Toggle Action, and
correct per-state rendering — plus the profile schema v1-to-v2 migration this necessitates;
(2) the `ApplicationWatcher` for per-app profile switching — `applicationHints` is in the schema
but no OS foreground-window watcher exists; (3) the Property Inspector end-to-end round-trip —
the scaffolding exists but four concrete wiring gaps prevent real PI HTML from working;
(4) four missing host-to-plugin lifecycle events (`titleParametersDidChange`,
`propertyInspectorDidAppear/Disappear`, `applicationDidLaunch/Terminate`, `systemDidWakeUp`);
and (5) the `profileChanged` to `populateContextsForActivePage` wiring bug that causes
drag-to-bind to never fire `willAppear` until a reconnect cycle. These gaps are individually
isolated and can be delivered in dependency order without disrupting the working parts.

The #1 project failure mode is "checked but not working": Phase 27 passed 713/713 unit tests and
the "Allow" button was a no-op for `.sdPlugin` plugins, caught only by live debug-channel
verification. Every phase in v2.0 must close with debug-channel drive
(`scripts/ajazz-debug qml.invoke / plugin.simulateAction / screenshot`) and all new QML controls
must carry `objectName:`. The mirajazz sidecar edge is stable and hardware-confirmed with no
changes needed for v2.0 — the pristine `mirajazz` crate must not be touched. Sidecar
encoder/touch input decode remains HARDWARE-GATED/PROVISIONAL until a retail AKP05E is available.

______________________________________________________________________

## Key Findings

### Recommended Stack

No new external libraries are needed for v2.0. Every feature builds on the existing stack:
Qt 6.7+ (all WebEngine/WebChannel/Core modules already linked), C++20 stdlib for core model
changes (COD-031 boundary: no nlohmann in `ajazz_core` or installed headers), the mirajazz Rust
sidecar (unchanged), and system-level Wine detection for Windows-only plugin support. The only
new system dependency is `xcb-ewmh` (`xcb-util-wm`) for Linux X11 foreground-window detection;
Wayland uses `wayland-client` (already a transitive Qt dep).

**Core technologies:**

- **Qt6 WebEngine + WebChannelQuick**: PI HTML rendering — already linked, `AJAZZ_HAVE_WEBENGINE`-gated; the `QQmlWebChannel` requirement (not bare `QWebChannel`) is a known and handled Qt gotcha
- **C++20 stdlib (no nlohmann) in `src/core/`**: `ActionInstance` / `ActionState` structs + `profile_io.cpp` hand-rolled serialization — COD-031 boundary preserved
- **XCB/EWMH + Win32 + NSWorkspace + zwlr-foreign-toplevel**: platform-split `ApplicationWatcher` backends; separate `.cpp`/`.mm` files per platform (never a single `#ifdef` chain)
- **`wine` runtime (system, not bundled)**: detected via `QStandardPaths::findExecutable("wine")`; the only viable path for compiled Windows-only `.exe` plugins; PE loader is not feasible
- **mirajazz sidecar (`streamdock-host/`)**: stable, no Cargo changes needed; the persistent HID handle pattern is the hardware-confirmed correct approach

**Critical version and boundary requirements:**

- Qt 6.7+ minimum (unchanged); `QQuickWebEngineScriptCollection` requires `Qt6::WebEngineQuickPrivate`
- `grep -rn nlohmann src/core/include/` must return 0 — enforced at CI audit time (COD-031)
- `grep -rn "QHostAddress::Any" src/` must return 0 — security invariant, CI gate
- Do NOT modify the `mirajazz` crate; sidecar protocol changes go in `streamdock-host/src/main.rs`

### Expected Features

**Must have (blocks real-world plugin usage):**

- **`ActionInstance` model with `states[]` + profile schema v2 migration** — all state-aware plugins send `setState`; without per-state storage those overrides are lost on state change; Multi Action and Toggle Action both depend on this
- **Multi Action** — the most commonly used composite action in the Elgato ecosystem; without it, plugin authors who designed for Multi Action context see broken behavior
- **`titleParametersDidChange` event** — many plugins send this immediately on `willAppear` to initialize their PI; plugins appear broken without it
- **`propertyInspectorDidAppear/Disappear` events** — several popular plugins gate `getSettings` on `propertyInspectorDidAppear`; PI appears non-functional
- **`profileChanged` to `populateContextsForActivePage` wire fix** — drag-to-bind never fires `willAppear` until reconnect; foundational wiring bug affecting the entire binding layer
- **PI end-to-end round-trip** — four concrete gaps: `cefQuery` polyfill existence, `sdpi.css` resource, `loadInspector` QML caller, `sendToPlugin`/`sendToPropertyInspector` relay stub
- **Per-app profile switching** — the #1 differentiating workflow feature for users migrating from Elgato

**Should have (competitive parity, v2.x):**

- Toggle Action — builds on states array; lower priority than Multi Action
- `switchToProfile` host command dispatch — small wiring gap; unlocks plugin-driven profile automation
- `applicationDidLaunch/Terminate` events — requires ApplicationWatcher; add in same phase
- Folder creation UI — ProfilePage model exists; only UI is missing
- `systemDidWakeUp` — Qt D-Bus / power-management signal; low implementation cost

**Defer to v2.1+:**

- Wine support for Windows-only `.exe` plugins — platform-specific, non-trivial; PE loader NOT feasible (confirmed)
- `DisableAutomaticStates` auto-toggle — depends on states array; add after Multi-State UI exists
- `setFeedbackLayout` full encoder touch-strip layout engine — hardware-gated on retail AKP05E
- Overflow scroll for large SKUs (>8 cols / >4 rows in device view)

**Anti-features (do not build):**

- Phone-home plugin catalog (privacy violation)
- Bundled `node` or Wine (binary size, Flatpak policy, license)
- `QHostAddress::Any` for WS server (security)
- `openApplication` AJAZZ-only command (shell-injection risk)
- PE loader for Windows-only plugins (not production-ready)

### Architecture Approach

v2.0 follows the "composition at application root, not in components" pattern already established
in the codebase. `Application` owns all services and wires signal/slot connections; each service
receives non-owning pointers injected at construction. `std::function` injection seams are the
mechanism for cross-ownership access, keeping each component independently unit-testable. The
mirajazz sidecar is the hardware I/O edge — all render calls go through
`StreamDockControlService::assignKeyImage` (last-write-wins coalesced drain), never direct
`setKeyImage` on the device. The architectural mandate for v2.0: extend the existing vertical
slices, never replace them.

**Major components and their v2.0 roles:**

1. **`ActionInstance` + `ActionState` structs (NEW, `src/core/`)** — core model for per-state image/title/settings and Multi/Toggle Action children; serialized via hand-rolled writer in `profile_io.cpp`; `Binding` gains `std::optional<ActionInstance> instance`
1. **`PluginDeviceBridge` (MODIFIED, targeted extensions)** — `setState` renders `instance.states[idx].image`; `profileChanged` to `populateContextsForActivePage` wire added; lifecycle events `titleParametersDidChange` and `propertyInspectorDidAppear/Disappear` added
1. **`ApplicationWatcher` (NEW, `src/app/src/`)** — platform-split foreground-app detector; emits `foregroundAppChanged(name)`; `ProfileController` connects and scans `applicationHints`; profile switch triggers bridge context lifecycle
1. **`BuiltinActionsService` (MODIFIED)** — new handlers for `opendeck.multiaction` (sequential children) and `opendeck.toggleaction` (cycle + currentState advance)
1. **`PropertyInspectorController` (MODIFIED, closes stub)** — PI HTML load + `$SD` WebChannel + settings round-trip complete; `propertyInspectorDidAppear/Disappear` wired on open/close
1. **`SdPluginServer` + `PluginManager` (REUSED)** — no changes; WS protocol, spawn lifecycle, crash window complete
1. **`SidecarStreamDockDevice` + mirajazz sidecar (REUSED)** — stable; persistent handle; hardware-confirmed; no sidecar changes for v2.0

### Critical Pitfalls

1. **"Checked but not working" — unit tests pass, integration is a silent void** — the #1 failure mode (Phase 25: `setActiveDevice` never called from QML despite 645 green tests; Phase 27: "Allow" button wired to wrong plugin list despite 713 green tests). Prevention: every new interactive control must have `objectName:` and must be driven via `scripts/ajazz-debug` + screenshot before the phase closes.

1. **`profileChanged` to `populateContextsForActivePage` wiring gap** — drag-to-bind persists to disk but `willAppear` never fires until device reconnect. Prevention: add the missing `QObject::connect` in `application.cpp` before any other binding work; this is the foundational fix all plugin-binding tests depend on.

1. **QWebEngine PI wiring has five distinct silent failure modes** — wrong WebChannel module, `cefQuery` shim injected too late, per-plugin profile isolation collapsed, `PIBridge` pointer lifetime, `page` Q_PROPERTY assigned. The cefQuery shim MUST use `QWebEngineScript::DocumentCreation`. Every plugin UUID needs its own `QQuickWebEngineProfile`.

1. **Sidecar input is hardware-gated/provisional** — demo unit (`0x0300:0x3004`) delivers zero input across five independent methods. Encoder/touch decode changes without retail AKP05E hardware capture ship wrong wire values. Synthetic injection via `input.encoder` debug RPC tests routing only, not wire values.

1. **Action-instance state model edge cases** — `DisableAutomaticStates` not parsed; Multi Action chain collapses on drag-move if commit-then-delete used instead of `swapKeyBindings`; per-state settings context must encode state index; `willAppear` must be emitted on Toggle Action state change. Prevention: Catch2 test battery before any QML multi-action UI work.

1. **Event-parity drift** — missing `action` field in `willAppear` causes silent plugin handler mismatch; `controller` token mismatch (`"Encoder"` vs `"Knob"`) causes silent event drop. Prevention: maintain explicit coverage table (our bridge vs OpenDeck `events/outbound`) as a required deliverable.

______________________________________________________________________

## Implications for Roadmap

Based on the dependency graph in FEATURES.md and the build-order analysis in ARCHITECTURE.md,
six phases in dependency order are recommended. Each phase must close with debug-channel
verification, not just `ctest` green.

### Phase 1: Foundation — ActionInstance Core Model + Profile Schema v2 Migration

**Rationale:** Everything else (Multi Action, Toggle Action, per-state PI config, correct setState rendering) depends on the `states[]` array in `Binding`. This change is isolated to `src/core/` — no app-layer wiring needed yet. The profile schema migration (v1 `state: KeyState` to v2 `states: Vec<KeyState>`) must land here with round-trip unit tests before any consumer code is written.

**Delivers:** `ActionInstance` + `ActionState` structs in `src/core/include/ajazz/core/action_instance.hpp`; `Binding::instance: std::optional<ActionInstance>`; extended `profile_io.cpp`; Catch2 round-trip tests for 0-state, 1-state, 3-state, and 2-children variants.

**Features addressed:** States array (P1); profile schema migration prerequisite

**Pitfalls to avoid:** COD-031 (no nlohmann in any new core header); hand-rolled writer only in `profile_io.cpp`

**Research flag:** No additional research needed. Standard patterns apply.

______________________________________________________________________

### Phase 2: Binding Layer Fix + Multi Action / Toggle Action Dispatch

**Rationale:** The `profileChanged` to `populateContextsForActivePage` wiring bug is the most impactful single fix — it currently silently breaks all drag-to-bind plugin flows. Must land before any UI work that relies on `willAppear` firing correctly. Multi Action and Toggle Action are the natural first consumers of Phase 1's ActionInstance model.

**Delivers:** `profileChanged` to `populateContextsForActivePage` connection in `application.cpp`; `BuiltinActionsService` handlers for `opendeck.multiaction` (sequential children, 100 ms between) and `opendeck.toggleaction` (cycles `currentState`, auto-renders state image); extended `setState` handler renders `instance.states[idx].image`; `willAppear` emitted on Toggle Action state change; `isInMultiAction` flag set true for child context registrations.

**Features addressed:** Multi Action (P1); Toggle Action (P2); `profileChanged` wire fix (P1)

**Pitfalls to avoid:** Pitfall 3 (this phase IS the fix for the profileChanged wiring gap); Pitfall 8 (action-instance edge cases — test battery before QML); `swapKeyBindings` atomic move

**Research flag:** No additional research needed. HIGH confidence from OpenDeck `events/outbound/keypad.rs`.

______________________________________________________________________

### Phase 3: Property Inspector End-to-End Round-Trip

**Rationale:** The PI scaffolding exists but four concrete gaps prevent any real PI HTML from working. Depends on Phase 1 only (context ID stability) and can be placed here to close the most user-visible stub.

**Delivers:** Verified `cefQuery` polyfill and `sdpi.css`; `Inspector.qml` to `PropertyInspectorController.loadInspector()` UI trigger wired; `sendToPlugin`/`sendToPropertyInspector` relay fully wired; `propertyInspectorDidAppear` emitted on PI open, `propertyInspectorDidDisappear` on close; `titleParametersDidChange` emitted after `willAppear`; all PI controls with `objectName:`.

**Features addressed:** PI end-to-end (P1); `propertyInspectorDidAppear/Disappear` (P1); `titleParametersDidChange` (P1)

**Pitfalls to avoid:** Pitfall 4 (all five QWebEngine PI failure modes); `QWebEngineScript::DocumentCreation` injection; per-plugin profile isolation preserved

**Verification gate:** Headless bridge half via `plugin.simulatePiSettings` RPC. Human-verify checkpoint required for real PI JS `$SD.setSettings()` round-trip — cannot be driven headlessly. Flag this explicitly in the phase plan.

**Research flag:** No additional research needed. STACK.md §2 documents the complete PI stack with known gaps.

______________________________________________________________________

### Phase 4: Event-Parity Audit + Coverage Table

**Rationale:** Systematic comparison of our event surface against OpenDeck's `events/inbound` + `events/outbound` modules. Runs after Phases 2 and 3 have extended the event surface so the audit covers the full v2.0 set.

**Delivers:** Event-parity coverage table (per-event: supported/partial/missing/hardware-gated); `willAppear` payload unit test asserting all required fields; controller token normalization audit; `systemDidWakeUp` event; `switchToProfile` host command dispatch wired; all non-hardware-gated events with unit tests.

**Features addressed:** `systemDidWakeUp` (P3); `switchToProfile` dispatch (P2); event-parity completeness

**Pitfalls to avoid:** Pitfall 5 (event-parity drift — coverage table is the prevention); missing `action` field; `controller` token mismatch

**Research flag:** No additional research needed. FEATURES.md §A has a complete annotated event table.

______________________________________________________________________

### Phase 5: Per-App Profile Switching (ApplicationWatcher)

**Rationale:** Per-app profile switching is the #1 differentiating workflow feature for users migrating from Elgato. Architecturally self-contained. Placed here to build on the verified context-registration and profile-switch paths from Phase 2.

**Delivers:** `IApplicationWatcher` interface + four platform backends (X11/XCB/EWMH, Wayland/zwlr-foreign-toplevel, Windows/GetForegroundWindow 250 ms timer, macOS/NSWorkspace in `.mm`); `ProfileController::onForegroundAppChanged` slot; profile switch triggers bridge context lifecycle; 100 ms debounce; "default" profile fallback; UI for assigning profiles to app names (`ProfileManager.qml`).

**Features addressed:** Per-app profile switching (P1); `applicationDidLaunch/Terminate` (P2, fired from watcher)

**Pitfalls to avoid:** Pitfall 7 (Wayland limitation — GNOME Wayland has no public API without a shell extension; document explicitly; surface compositor-capability warning); macOS `.mm` file must be OBJCXX; debounce required

**Research flag:** NEEDS RESEARCH. Wayland active-window detection is MEDIUM confidence. Before Phase 5 implementation: sub-agent sweep of `wlr-foreign-toplevel-management-v1` compositor coverage, Hyprland IPC fallback, confirmed GNOME gap. X11, Windows, macOS paths are HIGH confidence.

______________________________________________________________________

### Phase 6: Native Windows Plugin Support + Security Hardening

**Rationale:** Wine support is P3 (low user value vs cost) but security hardening items are safety items that must complete before release. Grouping allows a focused security audit pass.

**Delivers:** Manifest OS-filter `supportsCurrentPlatform()` helper; Wine detection + spawn path with optional `WINEPREFIX`; "Windows only — requires Wine" status chip in UI; tampered-vs-unsigned verifier split (`VerifyVerdict::Tampered` vs `Unsigned`); tampered-refused-even-with-consent unit test; persisted unsigned consent in `QSettings` per plugin UUID; pre-registration `QSet<QWebSocket*>` tracking; CI grep gate `QHostAddress::Any` to 0 hits; SIGPIPE handler grep check.

**Features addressed:** Wine plugin support (P3); security hardening (release-blocker items)

**Pitfalls to avoid:** Pitfall 6 (Wine crash-tracker pattern); Pitfall 9 (tampered-vs-unsigned; phone-home gate; loopback invariant); Pitfall 11 (mid-handshake crash isolation); PE loader is NOT feasible

**Research flag:** No additional research needed. Wine spawn pattern is OpenDeck-validated (HIGH confidence). Security items are in-tree verified.

______________________________________________________________________

### Phase Ordering Rationale

- Phase 1 is isolated (`src/core/` only) and unblocks everything — no app-layer wiring needed, lowest risk first.
- Phase 2's `profileChanged` wire fix is the foundational correctness fix for the binding layer; all phases that exercise `willAppear` depend on it.
- Phase 3 depends on Phase 1 only but is placed sequentially to avoid concurrent-agent conflicts (max 2 per CLAUDE.md).
- Phase 4 (audit) follows Phases 2 and 3 so the coverage table captures the full extended event surface.
- Phase 5 (ApplicationWatcher) is self-contained but Wayland research is required before implementation starts.
- Phase 6 (security + Wine) last because security items are not workflow-blocking for development but must be complete before release.

### Research Flags

Phases needing deeper research during planning:

- **Phase 5 (ApplicationWatcher, Wayland path):** MEDIUM confidence only. Sub-agent sweep required: `wlr-foreign-toplevel-management-v1` compositor coverage map (wlroots, Hyprland, KDE, GNOME gap), `xdotool`/`hyprctl`/`swaymsg` subprocess fallback as a lower-complexity alternative. X11/Windows/macOS paths can start without waiting for Wayland research.

Phases with standard, well-documented patterns (no research phase needed):

- **Phase 1:** ActionInstance model directly transcribed from OpenDeck `shared.rs` with HIGH confidence.
- **Phase 2:** Multi Action / Toggle Action documented from OpenDeck `events/outbound/keypad.rs` with HIGH confidence.
- **Phase 3:** PI wiring gaps are enumerated and verified in STACK.md §2 and PITFALLS.md §P4 with HIGH confidence.
- **Phase 4:** Coverage comparison requires only reading OpenDeck `events/outbound/*.rs` against our bridge.
- **Phase 6:** Wine spawn pattern OpenDeck-validated; security items in-tree verified.

______________________________________________________________________

## Confidence Assessment

| Area         | Confidence | Notes                                                                                                                                   |
| ------------ | ---------- | --------------------------------------------------------------------------------------------------------------------------------------- |
| Stack        | HIGH       | All module dependencies verified against live CMakeLists.txt; no new external libraries; XCB EWMH only new system dep                   |
| Features     | HIGH       | REUSE/STUB/MISSING labels verified by live grep on 2026-06-06; OpenDeck structs/events fetched via gh api; gaps are file:line confirmed |
| Architecture | HIGH       | Component boundaries read from live source; OpenDeck reference fetched via gh api; data flows reflect actual signal/slot connections    |
| Pitfalls     | HIGH       | All pitfalls file:line verified against post-mortems, UAT records, and CLAUDE.md hard rules; no unverified inference                    |

**Overall confidence:** HIGH

### Gaps to Address

- **Wayland foreground-window detection (Phase 5):** MEDIUM confidence. Resolve with a sub-agent sweep before Phase 5 implementation. GNOME Wayland gap must be documented in the UI as a compositor warning.

- **PI JS `$SD.setSettings()` real round-trip (Phase 3):** Cannot be verified headlessly. A human-in-the-loop verification checkpoint is required: install a real plugin with a PI HTML file, open PI, change a setting, confirm `didReceiveSettings` reaches the plugin. Flag this explicitly in the Phase 3 plan.

- **Retail AKP05E encoder/touch wire values (all phases):** HARDWARE-GATED. Routing pipeline tests via synthetic injection can proceed. Wire decode correctness waits for hardware and must not block v2.0 phases.

- **Windows-only plugin survey (Phase 6 / v2.1):** MEDIUM confidence on what fraction of real `.sdPlugin` Windows-only executables are compiled Node.js bundles vs pure Win32 PE binaries. Requires a feasibility spike before implementing native-exe-first path.

______________________________________________________________________

## Sources

### Primary (HIGH confidence)

- `src/app/src/plugin_device_bridge.hpp/.cpp` — ContextRegistry, lifecycle events, routing (live source)
- `src/app/src/sd_plugin_server.hpp/.cpp` — Elgato v6 WS protocol, loopback invariant (live source)
- `src/app/src/plugin_manager.hpp/.cpp` — spawn dispatch, crash lifecycle (live source)
- `src/app/src/property_inspector_controller.hpp/.cpp` — PI scaffolding confirmed (live source)
- `src/app/src/pi_cef_shim.hpp` — cefQuery shim injection pattern (live source)
- `src/core/include/ajazz/core/profile.hpp:189` — `applicationHints` field confirmed (live source)
- `.planning/milestones/v1.3-phases/25-UAT.md`, `27-RESEARCH.md`, `28-RESEARCH.md`, `29-LIVE-VERIFICATION.md` — post-mortem records, pitfall verification
- `.planning/opendeck-ui-plugin-study.md` — architecture gap map, authored from OpenDeck source
- `ninjadev64/OpenDeck src-tauri/src/shared.rs` — `ActionInstance`, `ActionState` structs (gh api 2026-06-06)
- `ninjadev64/OpenDeck src-tauri/src/events/outbound/` — event shapes including `will_appear.rs`, `keypad.rs`
- `ninjadev64/OpenDeck src-tauri/src/application_watcher.rs` — 250 ms poll, profile switch
- `ninjadev64/OpenDeck src-tauri/src/plugins/mod.rs` — Wine spawn pattern
- `CLAUDE.md` — COD-031, Qt gotchas, AKP05E investigation glossary
- `.planning/PROJECT.md` — v2.0 milestone goal, mirajazz role

### Secondary (MEDIUM confidence)

- Elgato Stream Deck SDK docs (events-sent, events-received, manifest) — WebFetch summary; core event list cross-checked against OpenDeck
- `dimusic/active-win-pos-rs` (crates.io) — per-platform API survey for ApplicationWatcher design
- Win32 docs: `GetForegroundWindow`, `QueryFullProcessImageName` — stable since Vista
- Apple docs: `NSWorkspace.didActivateApplicationNotification` — stable since macOS 10.13

### Tertiary (LOW confidence, needs validation)

- Windows-only `.sdPlugin` ecosystem composition (JS-bundled vs pure Win32 PE) — ecosystem observation only; needs feasibility spike before Phase 6 native-first path

______________________________________________________________________

*Research completed: 2026-06-06*
*Ready for roadmap: yes*
