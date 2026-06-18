# Phase 0 Research — Stream Deck Device + Plugin System

**Feature**: Stream Deck + plugin Elgato/OpenDeck parity (scoped from [spec.md](./spec.md))
**Date**: 2026-06-18
**Method**: Two cross-verified read-only research sweeps over the live code + `docs/protocols/**`
and `docs/architecture/**`, plus Elgato SDK / OpenDeck domain knowledge. Where the two sweeps
disagreed, the direct code-reading verdict won (noted inline).

This document resolves all NEEDS CLARIFICATION and records the decisions that shape the design. Each
entry: **Decision → Rationale → Alternatives considered**.

______________________________________________________________________

## A. Architecture decomposition (the mental model the rewrite must preserve)

### A1. Keep the driver/core split

- **Decision**: The mirajazz sidecar (`streamdock-host/`, Rust, JSON-over-stdio) stays a *dumb
  driver* — raw input out (`{serial,code,state,raw}`), rendered images + brightness in. All
  `(device,position)→bound-action` resolution and Elgato-event synthesis lives in the C++ core/app.
- **Rationale**: This is OpenDeck's best architectural decision and it already works here (persistent
  HID handle, one `CRT DIS`, no wedge — hardware-confirmed 2026-06-01). It keeps wire complexity in
  the pristine mirajazz crate and keeps the app testable without hardware.
- **Alternatives considered**: (a) Re-introduce in-tree C++ wire backends — rejected, removed in
  Slice D, forbidden by constitution. (b) Push action resolution into the sidecar — rejected,
  couples driver to plugin semantics and defeats headless testing.

### A2. Keep the single `PluginDeviceBridge` + `ContextRegistry` seam

- **Decision**: Device↔plugin coupling stays mediated by exactly one component:
  `PluginDeviceBridge` (inbound `DeviceEvent`→Elgato event; outbound plugin command→`assignKeyImage`),
  with `ContextRegistry` owning the `deviceId#page#controller#row#col ↔ owning-plugin` map and
  enforcing per-plugin ownership.
- **Rationale**: The audit confirms neither the device backends nor the WS server reference each
  other — everything crosses via signals + the bridge. This clean seam is why the subsystem is
  extendable; the "rewrite" should harden it, not dissolve it.
- **Alternatives considered**: Direct device→server calls (faster to write, but reintroduces the
  cross-coupling the v2.0 modularity audit explicitly removed).

### A3. Composition via lazy lambda-setter seams at the `Application` root

- **Decision**: Keep wiring behavior through `Application`-installed resolver lambdas
  (`setProfileAccessor`, `setActionOwnerResolver`, `setDeviceGeometryResolver`,
  `setEncoderLayoutResolver`, `setContextOwnerResolver`, …) rather than raw cross-pointers.
- **Rationale**: Preserves the modularity boundary (bridge/server stay free of each other's types)
  and matches the existing init order (control→input→server→bridge→manager→host2).
- **Alternatives considered**: `std::function` constructor injection — marginally cleaner but a large
  mechanical churn for no behavioral gain; not justified under Principle I atomicity.

______________________________________________________________________

## B. The Elgato/OpenDeck target contract (what "parity" means)

### B1. Standard Elgato registration is the baseline; vendor `passHello` is additive

- **Decision**: A standards `.sdPlugin` registers with `{event:registerEvent, uuid:token}` over
  `ws://127.0.0.1:<port>` after launch with `-port/-pluginUUID/-registerEvent/-info`. The vendor
  `passHello`+salt handshake stays optional and MUST resolve `authenticated=true` immediately when no
  password is set, never gating a standards plugin.
- **Rationale**: OpenDeck is a *clean subset* of Elgato; real packages target Elgato and never need
  vendor extensions. Confirmed: `sd_plugin_server.cpp:378` makes auth a no-op without a password.
- **Alternatives considered**: Requiring vendor auth — rejected, breaks every stock Elgato plugin.

### B2. `-info` RegistrationInfo must be fully populated, especially `devices[]`

- **Decision**: Emit the complete `RegistrationInfo`: `application{font,language,platform, platformVersion,version}`, `colors{5}`, `devicePixelRatio`, per-plugin `plugin{uuid,version}`, and a
  **populated** `devices[]` of `{id,name,size:{columns,rows},type}` where `size` is the real
  `DeviceDescriptor` grid (never a constant) and `type` maps AKP05E/N4→7 (SD+), AKP153/AKP03→a
  key-grid type.
- **Rationale**: Empty `devices[]` is the single most damaging omission (breaks device/version-gated
  plugins). Already landed F1/F2 (`522dddf`/`095b199`); the plan keeps and verifies it.
- **Alternatives considered**: Minimal `{application,devices:[]}` — rejected (proven to break gating).

### B3. Property Inspector is a second, independent WebSocket connection

- **Decision**: The PI registers separately with a **5-arg** entry point
  `connectElgatoStreamDeckSocket(port, context, "registerPropertyInspector", info, actionInfo)` where
  `context` is the **action-instance context**, not the plugin UUID. PI→plugin via `sendToPlugin`
  (no `device`); plugin→PI via `sendToPropertyInspector`. The host emits
  `propertyInspectorDidAppear/DidDisappear` to the plugin.
- **Rationale**: This is the Elgato model and the F3 server side is done (`6a32191`). The remaining
  work is bootstrapping a *modern* WS-only PI (see D1).
- **Alternatives considered**: QWebChannel `$SD` cefQuery bridge only — works for vendor/legacy PIs
  but a modern stock PI doing `new WebSocket(...)` never registers.

### B4. `Target` is numeric; `Controllers` normalize vendor names

- **Decision**: Treat `setImage/setTitle` `target` as numeric (`0` both, `1` hardware, `2` software).
  Normalize manifest `Controllers` vendor values (`Knob`→`Encoder`; accept `SecondaryScreen`,
  `Information` as additive) and accept `SDKVersion:1`.
- **Rationale**: Matches real vendor manifests; the runtime parser (`plugin_manifest.cpp:144`) is
  already permissive.
- **Alternatives considered**: Enforcing the strict schema — rejected; it rejects real manifests
  (the schema doc, not the parser, is what's wrong — see C2).

______________________________________________________________________

## C. Documentation corrections (source-of-truth hygiene, Principle VII)

### C1. `docs/plugin-event-parity.md` is stale — refresh, don't trust

- **Decision**: Refresh the parity table; it marks `switchToProfile`/`systemDidWakeUp` "missing" and
  omits encoder-layout work that has since landed (`bf5e417`, `a9dfe5e`, `e5a81ef`). The current
  source of truth is the code + `docs/architecture/PLUGIN-GAP-ANALYSIS.md`.
- **Rationale**: A stale parity doc actively misleads (Principle VII); it caused the two research
  sweeps to initially disagree.
- **Alternatives considered**: Leave it — rejected; it will keep producing wrong conclusions.

### C2. `docs/schemas/plugin_manifest.schema.json` is over-strict — relax it

- **Decision**: Relax the manifest schema to validate real-world manifests: allow `Knob`/
  `SecondaryScreen` controllers, `SDKVersion:1`, OpenDeck `CodePathLin`/`CodePaths`, and additional
  top-level keys (drop `additionalProperties:false` where vendor keys appear).
- **Rationale**: The runtime parser already accepts these; the schema doc diverges from reality and
  would fail a faithful validation step.
- **Alternatives considered**: Tighten the parser to the schema — rejected, breaks real plugins.

______________________________________________________________________

## D. Gap-resolution decisions (the genuine remaining work, prioritized)

### D1. Modern PI WebSocket bootstrap — *blocked on* didAppear dedup (TOP PRIORITY)

- **Decision**: First **de-duplicate `propertyInspectorDidAppear`** — it is emitted from TWO live
  sites (`application.cpp:736` via the F3 `propertyInspectorRegistered` path AND `application.cpp:948`
  via the pre-existing PI-04 `inspectorOpened` path). Key on the instance `context`, emit once. THEN
  inject the PI-side `connectElgatoStreamDeckSocket(...)` bootstrap at PI DocumentReady (mirroring the
  HTML-plugin self-bootstrap at `plugin_manager.cpp:606`).
- **Rationale**: Enabling the bootstrap without dedup fires `propertyInspectorDidAppear` twice per
  open. This ordering is mandatory. (Direct code read; the doc-only sweep missed the second emit
  site — code wins.)
- **Alternatives considered**: Bootstrap first — rejected (double-emit regression). Rely on QWebChannel
  only — rejected (modern PIs never register).

### D2. Device-layer correctness: revive keep-alive, capture encoder-release, isolate provisionals

- **Decision**: (a) Implement a real `keep_alive` in the sidecar (`CRT CONNECT`) and wire
  `SidecarStreamDockDevice::keepAlive()` to send it — today it is an **empty no-op**
  (`sidecar_stream_dock_device.cpp:293`) while `StreamDockControlService` arms a ~1 s timer that fires
  into the void. (b) Add `EncoderBinding::onRelease` and route the real `EncoderReleased` the sidecar
  emits but the app currently drops (`stream_dock_input_service.cpp:306`, WR-05). (c) Keep all
  provisional input/geometry constants (encoder polarity, `zoneForX`, touch `tapPos.y=0`, DRA/ENC
  200×100) behind the synthetic-event path; do not hard-commit demo-unit values.
- **Rationale**: The dead keep-alive is the single most important device-layer correctness bug (the
  idle-wedge guard does nothing on the shipping path). Provisional isolation honors Principle VI.
- **Alternatives considered**: Add a `CRT CONNECT` keep-alive in the app proxy — rejected; the sidecar
  owns the HID handle, so the keep-alive must originate there.

### D3. Complete the outbound contract: `setTriggerDescription`, `showAlert`/`showOk` surfacing

- **Decision**: Wire `setTriggerDescription` (SD+ dial trigger hints — manifest field parsed but the
  runtime command is not in `kRoutedActions`) and render a visual surface for `showAlert`/`showOk`
  (currently host-side ack only, no overlay).
- **Rationale**: Required for full SD+ parity and standard plugin feedback semantics.
- **Alternatives considered**: Defer — acceptable only if US2 acceptance does not require dial hints;
  scheduled as P2 within this plan, not dropped.

### D4. Secondary correctness bugs (B5–B9) — fix the real ones, retire the false one

- **Decision**: Fix B5 (`IPluginHost2::dispatch` forwards `actionId` as the *event name* —
  mislabeled contract, `plugin_manager.cpp:1047`), B6 (HTML `QWebEnginePage` in `m_htmlPages` never
  torn down → leak for app lifetime), B7 (`passHello.deviceInfo` is empty `{}` → plugins reading
  geometry at hello get nothing). **B9 is now FALSE** — `applicationDidLaunch/Terminate` *is* wired
  via `app_event_dispatch.cpp` (focus-approximated); update the gap doc rather than "fix" it.
- **Rationale**: B5–B7 are real, untested defects; B9 was fixed since the gap note was written
  (verify-before-trust, Principle VI).
- **Alternatives considered**: Treat B9 as open — rejected; code disproves it.

### D5. Test the untested: `SidecarStreamDockDevice` + keep-alive + dedup

- **Decision**: Add dedicated unit tests for `SidecarStreamDockDevice` (QProcess handshake, event
  parse, input mapping), a keep-alive/idle-wedge regression test, and a PI didAppear single-emit test.
  Close the QML `KeyCell`/`Drawer` headless harness gap (per-cell `objectName`, headless modal open)
  so the deferred human-UAT walks become debug-channel-drivable.
- **Rationale**: `SidecarStreamDockDevice` has **no** dedicated test (only the QML smoke target
  compiles it); the keep-alive no-op shipped precisely because nothing tested it (Principle II + V).
- **Alternatives considered**: Rely on the QML smoke target — rejected; it compiles but does not
  exercise the device behavior.

### D6. Deferred / hardware-gated (explicitly NOT in this plan's critical path)

- **Decision**: Retail-AKP05E wire values (encoder ticks/polarity, `touchTap.tapPos`), Windows
  VendorDll Wine launch (WINPLG-03), and optional per-device `SupportedDevices` SKU enforcement are
  tracked but **deferred**; all non-wire routing is verified headless via synthetic events.
- **Rationale**: Unavailable hardware / OS must not block the achievable parity work. Honest scoping
  per Principle VI and the spec's Assumptions section.
- **Alternatives considered**: Block the milestone on hardware — rejected; defeats headless progress.

______________________________________________________________________

## E. Resolved unknowns (no NEEDS CLARIFICATION remain)

| Question                                       | Resolution                                                                                                                                            |
| ---------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------- |
| Is this a rewrite or a completion?             | **Completion + decomposition.** ~80% of the Elgato/OpenDeck core is landed and tested; green-field rewrite rejected (constitution: reuse what works). |
| Where does the sidecar end and the core begin? | Sidecar = raw input/rendered output only; core owns action resolution + event synthesis (A1).                                                         |
| What is the top blocking item?                 | Modern PI WS bootstrap, gated on `propertyInspectorDidAppear` dedup (D1).                                                                             |
| What is the worst device bug?                  | Dead `keepAlive()` no-op on the sidecar path (D2).                                                                                                    |
| What can proceed without hardware?             | Everything except raw encoder/touch wire values — synthetic events cover routing (D6).                                                                |
| Which docs are authoritative?                  | `elgato_plugin_protocol.md` + `PLUGIN-GAP-ANALYSIS.md` (current); `plugin-event-parity.md` + manifest schema need fixing (C1/C2).                     |
