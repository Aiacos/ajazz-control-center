# Roadmap: AJAZZ Control Center — v2.0 Modular Plugin & Binding System

> **Prior milestones archived:** v1.0–v1.3 phases (1–29) are in `.planning/milestones/v1.3-ROADMAP.md`.
> This file contains ONLY v2.0 phases (30–35).

## Overview

v2.0 closes the gap between "checked" and "working" for the plugin and binding systems. The
codebase already contains `SdPluginServer`, `PluginDeviceBridge`, `PropertyInspectorController`,
and `SidecarStreamDockDevice` — all working in isolation. What is missing is (1) the modular
host abstraction that decouples runtime-specific spawn from the shared lifecycle contract, (2) the
`ActionInstance` model that unlocks Multi Action / Toggle Action and per-state image rendering,
(3) the four concrete PI wiring gaps, (4) the `profileChanged` wire fix that silently broke all
drag-to-bind flows, (5) per-app profile switching, and (6) security hardening before release.

Every phase closes with a live debug-channel drive (`scripts/ajazz-debug`) and a `screenshot`
confirming real behaviour — not just `ctest` green. This is the #1 lesson from v1.3.

Phases are numbered 30–35, continuing from the v1.3 sequence (Phases 14–29).

## Phases

- [x] **Phase 30: Plugin-Host Modular Foundation** - Ratify the host-abstraction ADR; close the mid-handshake crash gap; enforce device-coupling decoupling; establish the modularity contract all later phases build on (completed 2026-06-06)
- [ ] **Phase 31: ActionInstance Core Model + Profile Schema v2** - Add `ActionInstance`/`ActionState` to `src/core/` with hand-rolled serialization; v1→v2 profile schema migration; COD-031 preserved throughout
- [ ] **Phase 32: Binding Layer Fix + Multi/Toggle Action + Device Editor** - Fix `profileChanged`→`populateContextsForActivePage` wire; implement Multi Action and Toggle Action dispatch; extend setState rendering; device-view overflow scroll for large SKUs
- [ ] **Phase 33: Property Inspector End-to-End** - Complete PI HTML rendering (cefQuery polyfill, sdpi.css, $SD WebChannel bridge, per-plugin profile isolation); settings round-trip; PI lifecycle events
- [ ] **Phase 34: Per-App Profiles + Event-Parity Audit** - Implement `ApplicationWatcher` (X11/Wayland/Win32/macOS); profile auto-switch on foreground-app change; full event-parity coverage table; fix all non-hardware-gated event gaps
- [ ] **Phase 35: Windows Plugin Support + Security Hardening + Milestone Verification** - Wine detection and native-first plugin classification; tampered-vs-unsigned verifier split; persisted unsigned consent; CI security gates; full modularity audit closing VERIF-01/02

## Phase Details

### Phase 30: Plugin-Host Modular Foundation

**Goal**: The plugin layer has a clean, modular contract — all runtimes share one spawn/lifecycle/IPC interface; crash mid-handshake never brings down the app; device SKU details never bleed into plugin code
**Depends on**: Nothing (first phase of v2.0)
**Requirements**: HOST-01, HOST-02, HOST-03
**Success Criteria** (what must be TRUE):

1. A plugin process killed between WebSocket connection and `registerPlugin` message causes zero app crash and zero dangling pointer — verified by a Catch2 test (disconnect-before-register) and confirmed by `scripts/ajazz-debug plugin.list` returning 0 connected (not a crash)
1. `PluginManager` dispatch routes Node, HTML, native, and Python runtimes without any SKU-specific branch — verified by `grep -rn "akp05e\|akp03\|akp153" src/app/src/plugin_manager.cpp` returning 0 hits
1. `grep -rn "QHostAddress::Any" src/` returns 0 and SIGPIPE handler is confirmed present in `main.cpp` — both CI-gated
1. The 3-in-30s disable + notify, restart, and `exitApp` plugin lifecycle behaviours all pass in `ctest --preset linux-release`; pre-registration exits do NOT count toward the crash window
1. ADR committed to `.planning/phases/30-*/`: IPluginHost2 unification decision (keep `.sdPlugin` WS path and Python OOP path separate or unify) documented with rationale, so downstream phases do not re-derive it

**Plans**: 3 plans

- [x] 30-01-PLAN.md — Wave 0 scaffold: RED pre-registration tests, QHostAddress::Any+SIGPIPE CI gate, IPluginHost2 UNIFY ADR
- [x] 30-02-PLAN.md — Sentinel-UUID pre-registration crash isolation (HOST-02): rekey + crash-window guard
- [x] 30-03-PLAN.md — IPluginHost2 unified contract + PluginManager/OOP refactor + four-runtime no-regression + SKU audit (HOST-01/03)

### Phase 31: ActionInstance Core Model + Profile Schema v2

**Goal**: The `ActionInstance` / `ActionState` data model exists in `src/core/` with hand-rolled serialization; existing profiles load unchanged; the schema supports Multi Action children and per-state images
**Depends on**: Phase 30
**Requirements**: BIND-01, BIND-02
**Success Criteria** (what must be TRUE):

1. `grep -rn nlohmann src/core/include/` returns 0 after the new `action_instance.hpp` is merged — COD-031 boundary preserved
1. Catch2 round-trip tests pass for 0-state, 1-state, 3-state, and 2-children `ActionInstance` variants — verified by `ctest --preset linux-release -R action_instance`
1. An existing v1.3 profile file (with the singular `state:` key) loads without error and round-trips correctly as a `states:` array of one — verified by a dedicated Catch2 migration test
1. `Binding` and `EncoderBinding` in `profile.hpp` carry `std::optional<ActionInstance> instance`; profiles without an `"instance"` key parse cleanly with `instance = std::nullopt`
   **Plans**: TBD

### Phase 32: Binding Layer Fix + Multi/Toggle Action + Device Editor

**Goal**: Drag-to-bind fires `willAppear` immediately without a reconnect; Multi Action and Toggle Action dispatch correctly cycle state and render per-state images; the device editor scrolls for large SKU grids; the binding layer is device-generic across all Stream Dock SKUs
**Depends on**: Phase 31
**Requirements**: BIND-03, BIND-04, BIND-05, BIND-06, BIND-07, EDIT-01
**Success Criteria** (what must be TRUE):

1. After dragging a plugin action onto a key and dropping it, `scripts/ajazz-debug plugin.protocolLog` shows `willAppear` arriving at the plugin process within the same QML interaction — without a device disconnect/reconnect cycle (Pitfall 3 closed)
1. A Multi Action binding on a key: synthetic `scripts/ajazz-debug input.key` on that key causes child actions to fire sequentially in `plugin.protocolLog` with the correct inter-step delay
1. A Toggle Action binding: repeated synthetic `input.key` presses cycle `currentState`; each cycle triggers a `setState`-driven key image update confirmed by `scripts/ajazz-debug screenshot` showing the key face changes between presses
1. `scripts/ajazz-debug input.encoder` and `input.touch` route to their registered plugin contexts without a reconnect — binding works across key / encoder-dial / touch-zone on all sidecar-backed SKUs (AKP03/AKP05-N4/AKP153); no SKU-specific dispatch code
1. The device editor for a SKU grid exceeding 8 columns or 4 rows renders a scrollable view; `scripts/ajazz-debug qml.get` on the scroll container's `objectName` returns scroll-position properties
   **Plans**: TBD
   **UI hint**: yes

### Phase 33: Property Inspector End-to-End

**Goal**: Real plugin PI HTML renders in QWebEngine with the $SD bridge; settings round-trip end-to-end; PI lifecycle events fire correctly — the v1.3 stub is fully closed
**Depends on**: Phase 31
**Requirements**: PI-01, PI-02, PI-03, PI-04
**Success Criteria** (what must be TRUE):

1. A plugin's PI HTML page loads in the `QWebEngineView` panel when the user selects a bound action key — confirmed by `scripts/ajazz-debug screenshot` showing PI content rendered (not a blank frame); the PI panel open control has an `objectName` and is reachable via `qml.invoke`
1. `scripts/ajazz-debug plugin.simulatePiSettings` causes `didReceiveSettings` to appear in `plugin.protocolLog` — headless bridge half verified
1. `propertyInspectorDidAppear` is logged in `plugin.protocolLog` when the PI panel opens; `propertyInspectorDidDisappear` when it closes — both driven via the PI panel's `objectName`-addressed controls
1. `titleParametersDidChange` appears in `plugin.protocolLog` immediately after `willAppear` is registered for a context — verified by a Catch2 unit test asserting payload completeness
1. **HUMAN-VERIFY CHECKPOINT (cannot be headless)**: real PI JS `$SD.setSettings({...})` call from a live plugin causes `didReceiveSettings` to reach the plugin and the setting survives an app restart — requires a human to open the PI panel and interact with it
   **Plans**: TBD
   **UI hint**: yes

### Phase 34: Per-App Profiles + Event-Parity Audit

**Goal**: Profiles auto-switch when the foreground application changes; a full event-parity coverage table is produced; all non-hardware-gated event gaps are closed
**Depends on**: Phase 32, Phase 33
**Requirements**: APROF-01, APROF-02, APROF-03, APROF-04, EVENT-01, EVENT-02, EVENT-03, EVENT-04
**Success Criteria** (what must be TRUE):

1. On Linux/X11: switching the foreground application to one matching a profile's `applicationHints` causes the device to repaint with that profile's keys within 500 ms — confirmed by `scripts/ajazz-debug screenshot` before and after the switch, with `log.tail` showing `willDisappear` + `willAppear` in the correct order
1. **PLATFORM-GATED (Wayland/GNOME)**: When the compositor does not expose a foreground-window API, the UI shows a capability warning chip — `scripts/ajazz-debug qml.get` on the warning control's `objectName` returns non-empty warning text; the absence of this API does NOT crash or silently break the watcher
1. The event-parity coverage table committed to `docs/plugin-event-parity.md` lists every event in OpenDeck `events/outbound` as supported / partial / missing / hardware-gated, with a test reference for each supported event
1. A `willAppear` payload captured from `plugin.protocolLog` contains `"action"`, `"context"`, `"device"`, `"event"`, and `"payload"` with `"row"`, `"column"`, `"controller"`, `"state"`, `"isInMultiAction"` — asserted by a Catch2 unit test; controller token normalization is audited so no encoder/touch event is silently dropped
1. `systemDidWakeUp` and `switchToProfile` host-command dispatch appear in `plugin.protocolLog` when triggered; `applicationDidLaunch`/`applicationDidTerminate` fire from the watcher — all exercised by unit tests
   **Plans**: TBD
   **UI hint**: yes

### Phase 35: Windows Plugin Support + Security Hardening + Milestone Verification

**Goal**: Windows-only plugins are correctly classified and surfaced to the user; security invariants are CI-enforced; the milestone closes with a modularity audit confirming all VERIF-01/02 criteria
**Depends on**: Phase 34
**Requirements**: WINPLG-01, WINPLG-02, WINPLG-03, PLGSEC-01, PLGSEC-02, PLGSEC-03, VERIF-01, VERIF-02
**Success Criteria** (what must be TRUE):

1. A tampered plugin (valid-looking manifest, corrupted signature) is refused even when `userConfirmedUnsigned=true` — a Catch2 unit test asserts `VerifyVerdict::Tampered` and `installFromFile` aborts; an unsigned plugin shows an amber "Unsigned — requires consent" chip addressable via `objectName`
1. Persisted unsigned consent survives an app restart — verified by: granting consent, restarting the app, then confirming `scripts/ajazz-debug plugin.list` shows the plugin loaded without re-prompting; consent is stored under a named org/app QSettings scope (Windows registry safe)
1. `grep -rn "QHostAddress::Any" src/` returns 0 in CI (grep gate active in `.github/workflows/ci.yml`); `scripts/ajazz-debug` confirms the WS server is loopback-only via `state` or `ping` response
1. A Windows-only `.sdPlugin` installed on Linux shows either a "runs natively" status (WS-IPC-only) or a "requires Wine" chip (PE binary); if Wine is absent, the chip reads "unsupported on this OS" — all chip states are `objectName`-addressed and reachable via `scripts/ajazz-debug qml.get`
1. Milestone modularity audit (VERIF-01/02): `grep -rn mirajazz src/core/ src/app/src/plugin_manager.cpp src/app/src/plugin_device_bridge.cpp` returns 0; `grep -rn nlohmann src/core/include/` returns 0; `grep -rn "akp05\.cpp\|makeAkp05\|makeAkp03\|makeAkp153" src/` returns 0; every new interactive control added in v2.0 has `objectName:` (verified by a grep scan of new QML files); all phases have a `screenshot` in their VERIFICATION artifact
   **Plans**: TBD

## Progress

| Phase                                                                    | Plans Complete | Status      | Completed  |
| ------------------------------------------------------------------------ | -------------- | ----------- | ---------- |
| 30. Plugin-Host Modular Foundation                                       | 3/3            | Complete    | 2026-06-06 |
| 31. ActionInstance Core Model + Profile Schema v2                        | 0/TBD          | Not started | -          |
| 32. Binding Layer Fix + Multi/Toggle Action + Device Editor              | 0/TBD          | Not started | -          |
| 33. Property Inspector End-to-End                                        | 0/TBD          | Not started | -          |
| 34. Per-App Profiles + Event-Parity Audit                                | 0/TBD          | Not started | -          |
| 35. Windows Plugin Support + Security Hardening + Milestone Verification | 0/TBD          | Not started | -          |
