# Requirements: AJAZZ Control Center

**Active milestone:** v2.0 — Modular Plugin & Binding System (OpenDeck/Elgato-correct)
**Defined:** 2026-06-06
**Strategy:** modular refactor reusing what already works (NOT green-field). Runs on the mirajazz sidecar device backend.
**Prior milestones (historical):** v1.3 requirements archived to `.planning/milestones/v1.3-REQUIREMENTS.md` (v1.2 in the same file); v1.1 in `.planning/milestones/v1.1-REQUIREMENTS.md`.

> Research base: `.planning/research/SUMMARY.md` (+ STACK/FEATURES/ARCHITECTURE/PITFALLS), `.planning/opendeck-ui-plugin-study.md`. Sources: `ninjadev64/OpenDeck`, OpenAction, Elgato Stream Deck SDK, local RE corpus.
>
> **Verdict that frames the whole milestone:** most of the plugin system already exists and works — v2.0 is gap-closing + modularisation, not a rebuild. **Grep before coding.** The #1 failure mode is "checked but not working": every requirement below is satisfied only when verified end-to-end via the debug-control channel (+ AKP05E hardware where applicable), not when unit tests merely pass.

______________________________________________________________________

## v2.0 Requirements (Modular Plugin & Binding System)

### Modular Plugin-Host Abstraction (HOST)

- [x] **HOST-01**: A single `IPluginHost` interface unifies the Node, HTML (QWebEngine), native, and Python plugin runtimes behind one spawn / lifecycle / IPC contract; `PluginManager` dispatch is refactored onto it with no behavioural regression vs the current per-runtime paths.
- [x] **HOST-02**: Plugin crash isolation is pre-registration-safe — a plugin that crashes mid-handshake (before its contexts register) never crashes the app; the 3-crashes-in-30s disable + notify, restart, and `exitApp` shutdown behaviours are preserved.
- [x] **HOST-03**: The plugin layer has zero compile/link coupling to specific device SKUs or to wire/sidecar internals; all device I/O is reached only through `PluginDeviceBridge` → `StreamDockControlService` → the mirajazz sidecar edge.

### Binding & Action Model — device-generic (BIND)

- [x] **BIND-01**: An `ActionInstance` / `ActionState` core model (`states[]`, `currentState`, `settings`, `children`) is added under `src/core/` with hand-rolled JSON serialization (COD-031: no `nlohmann` in `ajazz_core` or any installed header); `Binding` carries an optional instance.
- [x] **BIND-02**: The profile schema migrates v1→v2 (single `state` → `states[]`) with a lossless, backward-compatible reader; existing user profiles load unchanged; round-trip unit tests cover 0-state, 1-state, 3-state, and 2-children variants.
- [x] **BIND-03**: A drag-to-bind commit fires the plugin `willAppear` immediately — the `profileChanged` → `populateContextsForActivePage` wiring gap is fixed so binding no longer requires a device reconnect to activate.
- [x] **BIND-04**: A user can assign a **Multi Action**: its child instances run sequentially (with inter-step delay) on a single key/dial press, via a built-in `com.hotspot.streamdock.multiaction` handler.
- [x] **BIND-05**: A user can assign a **Toggle Action**: pressing it cycles `currentState`, renders the new state image, and emits the state-change `willAppear`, via the built-in `com.hotspot.streamdock.toggleaction` handler (the real dispatch prefix; an `opendeck.*` id would fail `handles()`).
- [x] **BIND-06**: The binding/action layer is device-generic across key / encoder-dial / touch-zone controllers and works on every Stream Dock SKU (AKP03 / AKP05-N4 / AKP153) through the sidecar — no SKU-specific binding code.
- [x] **BIND-07**: `setState` renders the correct per-state image/title from `instance.states[idx]`; per-state settings encode and round-trip the state index.

### Property Inspector (PI)

- [x] **PI-01**: Real plugin Property Inspector HTML renders in `QWebEngine` + `QQmlWebChannel` with the `$SD` bridge object; the `cefQuery` polyfill is injected at `QWebEngineScript::DocumentCreation`; `sdpi.css` is served from a bundled resource; each plugin UUID gets its own `QQuickWebEngineProfile` (isolation).
- [x] **PI-02**: A device-canvas affordance opens the PI for the selected action (`loadInspector` wired from QML; the control carries an `objectName`).
- [ ] **PI-03**: `sendToPlugin` / `sendToPropertyInspector` relay end-to-end; per-context `getSettings`/`setSettings` and plugin-wide global settings round-trip and survive an app restart; `didReceiveSettings` reaches the plugin. *(Real PI JS `$SD.setSettings()` round-trip requires a human-verify checkpoint — it cannot be driven headlessly.)*
- [x] **PI-04**: `propertyInspectorDidAppear` fires on PI open and `propertyInspectorDidDisappear` on close; `titleParametersDidChange` fires after `willAppear`.

### Event Parity (EVENT)

- [x] **EVENT-01**: An event-parity **coverage table** (our `SdPluginServer`/`PluginDeviceBridge` vs OpenDeck `events/inbound`+`outbound` and the Elgato SDK) is produced and kept current as a verification deliverable, marking each event supported / partial / missing / hardware-gated.
- [x] **EVENT-02**: The `willAppear` payload carries all required fields (`action`, normalized `controller`, `coordinates`, `state`, `isInMultiAction`), asserted by a unit test, so plugin handlers never silently mismatch.
- [x] **EVENT-03**: `systemDidWakeUp` and the `switchToProfile` host-command dispatch are implemented and unit-tested.
- [x] **EVENT-04**: Controller-token normalization (`Knob` ↔ `Encoder`, etc.) is audited so no encoder/touch event is silently dropped.

### Per-App Profiles (APROF)

- [x] **APROF-01**: An `IActiveWindowWatcher` interface with platform-split backends — X11/EWMH, Windows/`GetForegroundWindow`, macOS/`NSWorkspace` (Obj-C++ `.mm`), Wayland/`zwlr-foreign-toplevel` (best-effort) — emits debounced foreground-app changes.
- [x] **APROF-02**: The active profile auto-switches based on the foreground app via `Profile::applicationHints`, with a default-profile fallback; each switch drives the bridge context lifecycle (`willDisappear`/`willAppear`).
- [x] **APROF-03**: A user can assign profiles to application names in the UI; the Wayland/GNOME limitation (no public foreground API without a shell extension) is surfaced as a capability warning in the UI.
- [x] **APROF-04**: `applicationDidLaunch` / `applicationDidTerminate` events are fired from the watcher to plugins that subscribe.

### Windows-only Plugin Support — native-first (WINPLG)

- [x] **WINPLG-01**: A feasibility spike classifies win-only `.sdPlugin` plugins into **WS-only-IPC** (runnable natively cross-platform) vs **vendor-DLL** (needs Wine); the decision and detection heuristic are documented (ADR-style).
- [x] **WINPLG-02**: WS-only win-only plugins run **natively** on Linux/macOS via the existing host (manifest OS-filter + a `supportsCurrentPlatform()` helper) — **no Wine**.
- [~] **WINPLG-03** (PARTIAL — chip-only this milestone; Wine launch deferred): the "requires Wine" / "unsupported on this OS" status chip is implemented + objectName-addressable (chip done). The **Wine run-path** (Vendor-DLL win-only plugins running via **detected system Wine** `QStandardPaths::findExecutable("wine")` with per-plugin `WINEPREFIX` isolation; Wine never bundled) is the documented deferral — see `35-ADR-windows-plugin-classification.md` + `35-CONTEXT.md` (no Wine/Windows hardware to live-test a launch path this milestone).

### Plugin Security Hardening (PLGSEC)

- [x] **PLGSEC-01**: The verify-gate splits **Tampered** vs **Unsigned**; a tampered plugin is refused **even with user consent** (CR-01 invariant); an unsigned plugin requires explicit per-plugin consent.
- [x] **PLGSEC-02**: Per-plugin unsigned-consent persists in `QSettings` under a named org/app scope (so it round-trips on the Windows registry backend) and survives the launch-sweep.
- [x] **PLGSEC-03**: The plugin WebSocket server stays loopback-only (`grep QHostAddress::Any src/` → 0 hits, CI-gated); the catalog never phones home.

### Device Editor (EDIT)

- [x] **EDIT-01**: The device-view editor scrolls for SKUs whose grid exceeds 8 columns or 4 rows (counting the encoder row and touch-zone row), matching OpenDeck's overflow rules; key vs encoder vs touch-zone controllers stay visually and functionally distinct.

### Verification & Modularity gate (VERIF)

- [x] **VERIF-01**: Every new interactive control added in v2.0 is debug-addressable (has an `objectName`) and every phase closes with a live debug-control-channel drive + `screenshot` confirming real behaviour — not just `ctest` green (the v1.3 "checked but not working" lesson).
- [x] **VERIF-02**: The `mirajazz` crate is never modified; sidecar changes (if any) live only in `streamdock-host/`; the removed C++ AKP wire backends are not reintroduced. Verified by review at milestone close.

______________________________________________________________________

## Future Requirements (deferred to v2.1+)

- `DisableAutomaticStates` auto-toggle (depends on the states array + multi-state UI).
- `setFeedbackLayout` full encoder/touch-strip layout engine — **HARDWARE-GATED** on a retail AKP05E.
- Real encoder/touch **input wire decode** — **HARDWARE-GATED/PROVISIONAL** (the `0300:3004` demo unit emits zero input; needs a retail AKP05E or Frida-on-Windows capture). The routing pipeline is testable via synthetic `input.*` debug RPCs in v2.0; wire values are not.
- Broader Wayland compositor coverage (GNOME shell extension, KWin D-Bus) beyond the best-effort `zwlr-foreign-toplevel` path.

______________________________________________________________________

## Out of Scope (v2.0)

| Exclusion                                                        | Reason                                                                     |
| ---------------------------------------------------------------- | -------------------------------------------------------------------------- |
| Phone-home plugin catalog (Mirabox/Aliyun)                       | Privacy violation; host-owned catalog only (PLGSEC-03).                    |
| Bundling `node` or `wine`                                        | Binary size, Flatpak policy, licensing — both are detected, never shipped. |
| `QHostAddress::Any` for the WS server                            | Security invariant — loopback-only (PLGSEC-03).                            |
| PE loader for Windows-only `.exe` plugins                        | Confirmed not production-viable; vendor-DLL plugins use Wine (WINPLG-03).  |
| `openApplication` AJAZZ-only shell command                       | Shell-injection risk.                                                      |
| AKB980 PRO keyboards, AJ-series mice, auxiliary display surfaces | Not stream controllers / hardware-gated; out of this milestone.            |
| Modifying the `mirajazz` crate or reviving C++ AKP wire backends | Pristine dependency / removed in Slice D (VERIF-02).                       |
| `nlohmann::json` in `ajazz_core` or any installed header         | Crosses the COD-031 boundary — PRIVATE to `ajazz_plugins` only.            |

______________________________________________________________________

## Traceability

| Requirement | Phase    | Status                                    |
| ----------- | -------- | ----------------------------------------- |
| HOST-01     | Phase 30 | Complete                                  |
| HOST-02     | Phase 30 | Complete                                  |
| HOST-03     | Phase 30 | Complete                                  |
| BIND-01     | Phase 31 | Complete                                  |
| BIND-02     | Phase 31 | Complete                                  |
| BIND-03     | Phase 32 | Complete                                  |
| BIND-04     | Phase 32 | Complete                                  |
| BIND-05     | Phase 32 | Complete                                  |
| BIND-06     | Phase 32 | Complete                                  |
| BIND-07     | Phase 32 | Complete                                  |
| PI-01       | Phase 33 | Complete                                  |
| PI-02       | Phase 33 | Complete                                  |
| PI-03       | Phase 33 | Partial                                   |
| PI-04       | Phase 33 | Complete                                  |
| EVENT-01    | Phase 34 | Complete                                  |
| EVENT-02    | Phase 34 | Complete                                  |
| EVENT-03    | Phase 34 | Complete                                  |
| EVENT-04    | Phase 34 | Complete                                  |
| APROF-01    | Phase 34 | Complete                                  |
| APROF-02    | Phase 34 | Complete                                  |
| APROF-03    | Phase 34 | Complete                                  |
| APROF-04    | Phase 34 | Complete                                  |
| WINPLG-01   | Phase 35 | Complete                                  |
| WINPLG-02   | Phase 35 | Complete                                  |
| WINPLG-03   | Phase 35 | Partial (chip-only; Wine launch deferred) |
| PLGSEC-01   | Phase 35 | Complete                                  |
| PLGSEC-02   | Phase 35 | Complete                                  |
| PLGSEC-03   | Phase 35 | Complete                                  |
| EDIT-01     | Phase 32 | Complete                                  |
| VERIF-01    | Phase 35 | Complete                                  |
| VERIF-02    | Phase 35 | Complete                                  |

______________________________________________________________________

*Defined 2026-06-06 — v2.0 "Modular Plugin & Binding System". Prior milestones archived under `.planning/milestones/`.*
