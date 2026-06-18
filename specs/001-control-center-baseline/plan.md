# Implementation Plan: Stream Deck Device + Plugin System (Elgato/OpenDeck Parity)

**Branch**: `experiment/mirajazz` | **Date**: 2026-06-18 | **Spec**: [spec.md](./spec.md)

**Input**: Feature specification from `specs/001-control-center-baseline/spec.md`

**Scope note**: The baseline spec covers the whole product. **This plan deliberately scopes to the
Stream Deck device layer + the plugin system only** — User Stories 1, 2, 4 and the Story-5
resilience guarantees *as they apply to Stream Dock families*. Keyboard (AK980) and mouse
(AJ-series) are **explicitly out of scope here**: they are already functional (FR-001/004/005 for
those families are satisfied). Non-Stream-Dock device work and packaging (US6) are out of scope for
this plan.

## Summary

The Stream Deck subsystem already implements most of the Elgato Stream Deck SDK + OpenDeck plugin
contract on top of the out-of-process mirajazz Rust sidecar (~822 ctest green; F1–F4 plugin
foundations landed). The user-reported problem is **comprehensibility and the last-mile correctness
gaps**, not a missing implementation. This plan therefore does **not** green-field rewrite a working
system. It (a) **decomposes and documents** the subsystem as an explicit contract (so the
architecture is understandable and the next change is obvious), then (b) **closes the genuine
remaining parity gaps** in priority order, each verified live through the debug-control channel.

The technical approach: keep the proven **driver/core split** (mirajazz sidecar = dumb driver: raw
input in, rendered images out; C++ core = `(device,position)→bound-action` resolution + Elgato event
synthesis, mediated by the single clean `PluginDeviceBridge`/`ContextRegistry` seam). Fix the
device-layer correctness bugs (dead `keepAlive` no-op, dropped encoder-release, provisional
constants), complete the plugin contract (modern Property Inspector WebSocket bootstrap with
`propertyInspectorDidAppear` de-duplication, `setTriggerDescription`, `showAlert`/`showOk` surfacing),
and harden the parts that have no dedicated tests (`SidecarStreamDockDevice`). Hardware-gated input
wire values (retail AKP05E) are isolated behind the synthetic-event test path so all non-wire work
proceeds headless.

## Technical Context

**Language/Version**: C++20 (app + `ajazz_core`), Rust (mirajazz sidecar `streamdock-host/`),
Python 3.10+ (out-of-process plugin host child), QML 6.

**Primary Dependencies**: Qt 6.7+ (`Qml`, `Network`, `WebEngineCore`, `WebChannelQuick`); the
pristine `mirajazz` crate (git dependency — **do NOT modify**); `hidapi_hidraw` (sidecar side, via
mirajazz); zlib (self-contained `.sdPlugin` ZIP reader); Ed25519/Sigstore (manifest verify gate);
`nlohmann::json` PRIVATE-linked to `ajazz_plugins` only (COD-031).

**Storage**: Profile JSON files (hand-rolled, nlohmann-free serialization in `profile.cpp`); QSettings
for app settings; on-disk installed `.sdPlugin` directories.

**Testing**: Catch2 via `ctest --preset linux-release` (~822 cases); cargo tests in `streamdock-host/`
(10); pytest for the Python host; offscreen QML smoke (`tests/qml/`, not registered on Windows);
synthetic input via `scripts/ajazz-debug input.*`; live verification via the `AJAZZ_DEBUG_CONTROL`
channel.

**Target Platform**: Linux (primary), Windows, macOS. Stream Dock I/O is sidecar-only; the
`hidapi_libusb` backend is disabled.

**Project Type**: Desktop application (Qt6/QML) composed of three cooperating processes — the app,
the Rust device sidecar, and per-plugin host processes (Node/HTML/native/Python).

**Performance Goals**: Interactive latency on real hardware. Persistent per-session HID handle (one
`CRT DIS`, no per-interaction open/close churn → no wedge). Coalesced last-write-wins key-image
drain. No per-keystroke plugin re-scan. Encoder-rotation 16 ms coalescer.

**Constraints**: COD-031 (`nlohmann::json` PRIVATE to `ajazz_plugins`, zero hits in
`src/core/include/`). Stream Dock backend is the mirajazz sidecar ONLY — the removed C++
AKP03/05/153 wire backends MUST NOT be reintroduced (AKP815 keeps its custom C++ backend). The
mirajazz crate is not modified in-tree. Plugin WebSocket server binds `QHostAddress::LocalHost`
only. Wire/protocol changes cross-checked against `docs/protocols/streamdeck/**` and verified
against hardware where possible; provisional values are not hard-committed. Every new interactive
control is `objectName`-addressable.

**Scale/Scope**: 3 Stream Dock families (~16 SKUs: AKP05/N4, AKP03/N3, AKP153) via the sidecar +
the AKP815 carve-out. ~822 existing tests in the subsystem. The plugin contract targets the full
Elgato SDK ≤6.9 + OpenDeck event sets.

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.* Evaluated against
`.specify/memory/constitution.md` v1.0.0.

| Principle                       | Gate                                                                                                            | Status                                                                                                                          |
| ------------------------------- | --------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------- |
| I. Code Quality & Boundaries    | COD-031 preserved; no C++ AKP wire backends reintroduced; mirajazz crate untouched; atomic Conventional Commits | ✅ PASS — plan reuses the existing boundaries; no new nlohmann in core                                                          |
| II. Testing Standards           | Every change lands with tests at the right layer; `ctest --preset linux-release` green; 3-compiler clean        | ✅ PASS — plan adds the missing `SidecarStreamDockDevice` + keep-alive + dedup tests                                            |
| III. UX Consistency             | Stream-Deck/OpenDeck-shaped; drag-onto-canvas; `Rot180` hardware-true render                                    | ✅ PASS — parity with Elgato/OpenDeck is the explicit goal                                                                      |
| IV. Performance                 | No per-keystroke rescans; persistent handle; hotplug-not-poll                                                   | ⚠️ WATCH — the input service currently polls on an 8 ms timer; documented, not regressed by this plan (see Complexity Tracking) |
| V. Auto Debug & Live Validation | Every behavioral change verified via the debug channel; new controls `objectName`-addressable                   | ✅ PASS — quickstart.md encodes the debug-channel verification per gap; closing the KeyCell/Drawer harness gap is in scope      |
| VI. Research-Backed             | RE corpus re-read before wire changes; hardware wins; SDK documented                                            | ✅ PASS — this plan is the product of two cross-verified research sweeps; provisional values isolated                           |
| VII. Documentation Currency     | Docs + schema updated in the same change; schema is wire source-of-truth                                        | ✅ PASS — plan fixes the over-strict manifest schema doc + refreshes the stale `plugin-event-parity.md`                         |
| VIII. Repository Hygiene        | No system mutations; no dead-code deletion off static-analysis FPs                                              | ✅ PASS                                                                                                                         |
| IX. CI/CD                       | Pipelines stay green; GitFlow; shift-left hooks                                                                 | ✅ PASS — work lands on the topic branch via PR                                                                                 |

**Verdict**: PASS. One WATCH item (Principle IV — pre-existing 8 ms input poll) is documented in
Complexity Tracking; it is not introduced or worsened by this plan and a move to event-driven input
is noted as a follow-up, not a gate.

## Project Structure

### Documentation (this feature)

```text
specs/001-control-center-baseline/
├── plan.md              # This file
├── research.md          # Phase 0 — architecture decomposition + parity decisions
├── data-model.md        # Phase 1 — entities (ActionInstance, Binding, RegistrationInfo, manifest…)
├── quickstart.md        # Phase 1 — debug-channel validation scenarios per parity gap
├── contracts/
│   ├── elgato-plugin-ws.md      # host↔plugin + host↔PI WebSocket event contract
│   ├── sidecar-stdio.md         # app↔mirajazz sidecar JSON-over-stdio contract
│   └── debug-control-rpc.md     # the AJAZZ_DEBUG_CONTROL methods that drive/verify this subsystem
└── tasks.md             # Phase 2 — created by /speckit.tasks (NOT here)
```

### Source Code (repository root) — the subsystem touched by this plan

```text
streamdock-host/                         # Rust mirajazz sidecar (driver: raw in / rendered out)
├── src/main.rs                          #   stdio dispatch, per-device input reader, keep-alive (NEW)
└── src/kind.rs                          #   VID/PID→family + per-family image format/geometry

src/app/src/
├── sidecar_stream_dock_device.{hpp,cpp} # QProcess proxy; IDisplay/IEncoder/ITouchStrip capable
│                                        #   FIX: keepAlive() no-op; PROVISIONAL input map; EncoderReleased
├── sidecar_protocol.{hpp,cpp}           # JSON codec (the only currently-unit-tested piece)
├── stream_dock_input_service.{hpp,cpp}  # device input → Binding chain → ActionEngine + deviceEvent
├── stream_dock_control_service.{hpp,cpp}# coalesced key-image writes; keep-alive timer (currently dead)
├── plugin_device_bridge.{hpp,cpp}       # THE device↔plugin seam + ContextRegistry (ownership)
├── sd_plugin_server.{hpp,cpp}           # Elgato v6 WebSocket server (plugin + PI connections)
├── property_inspector_controller.{hpp,cpp} # WebEngine PI host; modern WS bootstrap (NEW)
├── unified_plugin_host.{hpp,cpp}        # aggregates PluginManager + Python host (IPluginHost2)
├── plugin_manager.{hpp,cpp}             # .sdPlugin discover/spawn; buildInfoJson; HTML page lifetime (FIX B6)
├── encoder_layout_renderer.cpp          # host-side $A0/$A1/$B1/$B2/$C1/$X1 → QImage
├── sdplugin_extractor.{hpp,cpp}         # self-contained ZIP64/data-descriptor reader (zip-slip guard)
├── plugin_verify_gate.{hpp,cpp}         # Ed25519 4-way verdict
├── app_event_dispatch.cpp               # switchToProfile / systemDidWakeUp / app-lifecycle fan-out
└── application.cpp                       # composition root; PI didAppear dedup (FIX two emit sites)

src/core/include/ajazz/core/
├── action_instance.hpp                  # ActionInstance/ActionState (nlohmann-free)
└── profile.hpp                          # Binding/EncoderBinding/Profile (+ onRelease, FIX WR-05)

docs/
├── protocols/streamdeck/elgato_plugin_protocol.md   # the authoritative contract (current)
├── protocols/streamdeck/akp_plugin_sdk.md           # vendor extensions
├── architecture/PLUGIN-GAP-ANALYSIS.md              # current gap tracker
├── plugin-event-parity.md                           # REFRESH (stale rows)
└── schemas/plugin_manifest.schema.json              # RELAX (over-strict vs real manifests)

tests/
├── unit/ (test_plugin_device_bridge, test_sd_plugin_server, test_pi_bridge, test_action_instance,
│         test_multiaction_dispatch, test_sidecar_protocol, …)  + NEW sidecar-device + keepalive + dedup
├── integration/
└── qml/  (device-view smoke; close the KeyCell/Drawer headless harness gap)
```

**Structure Decision**: Single-project layout already in place; this plan modifies the existing
Stream Deck + plugin files in `streamdock-host/`, `src/app/src/`, `src/core/include/`, `docs/`, and
`tests/`. No new top-level modules. The driver/core split and the single `PluginDeviceBridge` seam
are preserved as the load-bearing architecture.

## Complexity Tracking

> Items that touch a constitution WATCH point and are deliberately accepted.

| Violation / Watch                                                                                       | Why Needed                                                                                                                                    | Simpler Alternative Rejected Because                                                                                                                                                                               |
| ------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| Pre-existing 8 ms input poll in `StreamDockInputService` (Principle IV)                                 | Existing, hardware-validated; the sidecar already pushes input events asynchronously, but the app-side service still polls the handle         | Converting to fully event-driven input is a separate, larger refactor; this plan does not regress it, and notes event-driven input as a follow-up rather than bundling an unrelated change (Principle I atomicity) |
| `applicationDidLaunch/Terminate` sourced from a foreground-focus watcher, not a true OS process monitor | Real cross-platform process monitoring (esp. Wayland) is high-cost; focus approximation already satisfies the common per-app-profile use case | True process lifecycle is deferred; the plan documents the semantic gap rather than shipping a heavy process-monitor that US3 does not require                                                                     |
| Provisional encoder/touch wire constants kept behind the synthetic-event path                           | A retail AKP05E/N4 is unavailable; routing must still be built and verified                                                                   | Hard-committing demo-unit values would violate Principle VI (provisional ≠ fact); isolation behind synthetic events lets all non-wire work land headless                                                           |
