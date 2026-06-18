---
description: Task list for the Stream Deck + plugin Elgato/OpenDeck parity work
---

# Tasks: Stream Deck Device + Plugin System (Elgato/OpenDeck Parity)

**Input**: Design documents from `specs/001-control-center-baseline/`

**Prerequisites**: [plan.md](./plan.md), [spec.md](./spec.md), [research.md](./research.md),
[data-model.md](./data-model.md), [contracts/](./contracts/), [quickstart.md](./quickstart.md)

**Scope**: Stream Deck device layer + plugin system ONLY. Keyboard (AK980) and mouse (AJ-series) are
out of scope — already functional. This is a **completion + hardening** effort, not a green-field
rewrite (~80% of the Elgato/OpenDeck core is landed and backed by ~822 tests).

**Tests**: INCLUDED. The constitution makes automated tests + live debug-channel verification part of
definition-of-done (Principles II and V), so test tasks are first-class here.

**Story → priority map** (from spec.md, filtered to Stream Deck):
US1 device binding loop (P1) · US5 honest capability + resilient/no-wedge (P1) · US2 plugins (P2) ·
US4 multi-action/toggle (P3).

## Format: `[ID] [P?] [Story] Description`

- **[P]**: can run in parallel (different files, no dependency on an incomplete task)
- **[Story]**: US1 / US2 / US4 / US5 (Setup/Foundational/Polish carry no story label)
- All paths are repo-relative.

______________________________________________________________________

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Establish a known-green baseline and a working verification harness before changing code.

- [x] T001 Establish and record a green baseline: `cmake --preset linux-release && cmake --build --preset linux-release && ctest --preset linux-release`; capture the live ctest count for the PR (do NOT hand-edit the figure in CLAUDE.md).
- [x] T002 [P] Build + test the mirajazz sidecar: `cargo build --release` and `cargo test` in `streamdock-host/` (expect the 10 existing cases green).
- [x] T003 [P] Confirm the verification harness: launch `AJAZZ_DEBUG_CONTROL=1 build/linux-release/src/app/ajazz-control-center`, then `scripts/ajazz-debug ping` and `scripts/ajazz-debug device.list` respond.

______________________________________________________________________

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Source-of-truth hygiene + the test/verification seams that EVERY later story depends on.

**⚠️ CRITICAL**: No user-story work begins until this phase is complete.

- [x] T004 Close the KeyCell verification gap: per-cell `objectName` (`key_N`) already present on the `KeyCell` delegate (commit `028eeca`); live-verified addressable via `qml.get {objectName:"key_0",property:"occupied"}` once the editor is populated (see T005). The canvas only instantiates cells when `ProfileEditor.codename` is set — which the headless path below now drives.
- [x] T005 Close the modal headless-open gap: editor now follows the backend active device for EVERY caller of `setActiveDevice` (new `StreamDockControlService.deviceActivated` → `editor.codename`/`capabilities` binding in `src/app/qml/Main.qml`), so a headless `device.setActiveDevice` RPC populates the canvas. Added `objectName` to `ProfileEditor` (`profileEditor`) and `debugDrawer`; the editor drawers (`pluginStoreDrawer`/`loadedPluginsDrawer`/`settingsDrawer`/`debugDrawer`) open via `qml.invoke open`, the PI dock is `objectName:"inspector"`, and the PI round-trips via the `plugin.simulatePiSettings` RPC. Live-verified.
- [x] T006 Create the missing `SidecarStreamDockDevice` unit-test target `tests/unit/test_sidecar_stream_dock_device.cpp` (register in `tests/unit/CMakeLists.txt`) with RED scaffolds for: QProcess handshake, `connected`/`input` event parse, `mapSidecarInput`, and `keepAlive()` — behavior filled in US1/US5.
- [x] T007 [P] Relax the over-strict manifest schema `docs/schemas/plugin_manifest.schema.json` (accept `Knob`/`SecondaryScreen` controllers, `SDKVersion:1`, OpenDeck `CodePathLin`/`CodePaths`; drop `additionalProperties:false` where vendor keys appear) and update `tests/unit/test_plugin_manifest.cpp` to validate a real vendor manifest (research C2).
- [x] T008 [P] Refresh the stale `docs/plugin-event-parity.md` to match current code (`switchToProfile`/`systemDidWakeUp`/encoder-layouts have landed) and reconcile `docs/architecture/PLUGIN-GAP-ANALYSIS.md` — explicitly retire **B9** (`applicationDidLaunch/Terminate` is already wired via `app_event_dispatch.cpp`) (research C1, D4).

**Checkpoint**: Harness can drive key cells + modals headlessly; docs/schema tell the truth; the
device-level test target exists.

______________________________________________________________________

## Phase 3: User Story 1 — Device binding loop: render + input routing (Priority: P1) 🎯 MVP

**Goal**: Every Stream Dock control (key / encoder-dial / touch zone) renders bound imagery and
routes its input to the bound action — including the encoder-release the app currently drops.

**Independent Test**: Bind a built-in action to a key, render it, drive `input.key`/`input.encoder`/
`input.encoderPress`/`input.touch`, and confirm the action fires — no plugins, no profiles (quickstart
Scenarios 1 + 5).

### Tests for User Story 1

- [x] T009 [P] [US1] Encoder-release routing test in `tests/unit/test_stream_dock_input_service.cpp`: assert a sidecar `EncoderReleased` reaches `EncoderBinding::onRelease` (RED before T011/T012).
- [x] T010 [P] [US1] Profile round-trip test for the new `EncoderBinding::onRelease` field in `tests/unit/test_profile_serialization.cpp` (serialize → parse → equal).

### Implementation for User Story 1

- [x] T011 [US1] Add the `onRelease` action chain to `EncoderBinding` in `src/core/include/ajazz/core/profile.hpp` and serialize it (nlohmann-free) in `src/core/src/profile.cpp` (WR-05); gate the reader on key presence, never a schema version.
- [x] T012 [US1] Route the real `EncoderReleased` event to `onRelease` in `src/app/src/stream_dock_input_service.cpp` (resolve the drop-TODO at ~:306).
- [x] T013 [US1] Isolate the PROVISIONAL input/geometry constants (encoder polarity, `zoneForX`, touch `tapPos.y`, DRA/ENC 200×100) behind the synthetic-event path in `src/app/src/sidecar_stream_dock_device.cpp` (~:42) and `stream_dock_control_service.cpp`; comment them PROVISIONAL and do NOT hard-commit demo-unit values (constitution Principle VI).
- [x] T014 [US1] Live-verify quickstart Scenarios 1 (render + brightness) and 5 (encoder/touch routing) via `scripts/ajazz-debug`; read the screenshot.

**Checkpoint**: The device binding loop is correct and the dropped encoder-release is captured.

______________________________________________________________________

## Phase 4: User Story 5 — Honest capability + resilient device (no idle wedge) (Priority: P1)

**Goal**: The device never wedges on idle — the keep-alive path actually keeps the handle alive (today
`keepAlive()` is a silent no-op while a timer fires into the void). This phase also covers US5's other
two dimensions for Stream Dock: **honest capability** (FR-018 — an unsupported/provisional capability
is never advertised as working; reinforced by T013 provisional isolation + T035 deferred-item honesty)
and **resilient hot-plug** (FR-019 — removal preserves the user's selection + list position; FR-020
coalescing).

**Independent Test**: Select a device, leave it idle past the keep-alive interval, then render — it
still renders (no wedge); a unit test asserts `keepAlive()` emits a `keep_alive` command (quickstart
Scenario 2).

### Tests for User Story 5

- [x] T015 [P] [US5] Keep-alive regression test in `tests/unit/test_sidecar_stream_dock_device.cpp`: assert `keepAlive()` writes a `keep_alive` command to the sidecar stdin (RED before T017 — the no-op is the bug that shipped because nothing tested it).

### Implementation for User Story 5

- [x] T016 [US5] Add a `keep_alive {serial}` command (sends mirajazz `CRT CONNECT`) to the sidecar dispatch in `streamdock-host/src/main.rs` + a cargo test for the codec.
- [x] T017 [US5] Wire `SidecarStreamDockDevice::keepAlive()` to emit the `keep_alive` command via `sidecar_protocol` in `src/app/src/sidecar_stream_dock_device.cpp` (~:293), replacing the empty no-op.
- [x] T018 [US5] Confirm `StreamDockControlService` keep-alive timer drives it (`src/app/src/stream_dock_control_service.cpp` ~:102) and the registry evicts a stale handle on hot-plug Removed while preserving the user's device selection + list position (FR-019); add an assertion/test pinning both the eviction and selection retention.
- [x] T019 [US5] Live-verify quickstart Scenario 2 (idle, then render — no wedge) via `scripts/ajazz-debug`.

**Checkpoint**: P1 MVP complete (US1 + US5) — the device renders, routes all input, and never wedges.

______________________________________________________________________

## Phase 5: User Story 2 — Plugins: install, use, Property Inspector, full contract (Priority: P2)

**Goal**: Stream Deck `.sdPlugin` plugins install and run with Elgato/OpenDeck parity — the modern
Property Inspector registers (emitting `propertyInspectorDidAppear` exactly once), the outbound
contract is complete, and the secondary correctness bugs are fixed.

**Independent Test**: Install a plugin from the catalog, bind its action, drive a synthetic press and
see live output on the key; open its PI and round-trip a setting (quickstart Scenarios 3 + 4).

### Tests for User Story 2

- [x] T020 [P] [US2] `propertyInspectorDidAppear` single-emit test in `tests/unit/test_pi_bridge.cpp`: exactly one emit per PI open (locks the dedup before T023/T024).
- [ ] T021 [P] [US2] `setTriggerDescription` routing test in `tests/unit/test_plugin_device_bridge.cpp` (RED before T025).
- [ ] T022 [P] [US2] Secondary-bug regression tests: B5 dispatch event-name in `tests/unit/test_plugin_host2.cpp`, B6 HTML page teardown in `tests/unit/test_plugin_lifecycle.cpp`, B7 non-empty `passHello.deviceInfo` in `tests/unit/test_sd_plugin_server.cpp` (there is no `test_plugin_manager.cpp`; `PluginManager` is covered across the lifecycle/host2/concurrency suites).

### Implementation for User Story 2 — Property Inspector chain (ORDERED, top priority D1)

- [x] T023 [US2] De-duplicate `propertyInspectorDidAppear`: emit once keyed on the instance `context`, reconciling the two live emit sites at `src/app/src/application.cpp:736` (F3 `propertyInspectorRegistered`) and `:948` (PI-04 `inspectorOpened`). **MUST precede T024.**
- [ ] T024 [US2] Inject the modern PI WebSocket bootstrap `connectElgatoStreamDeckSocket(port, context, "registerPropertyInspector", info, actionInfo)` at PI DocumentReady in `src/app/src/property_inspector_controller.cpp` (mirror the HTML-plugin self-bootstrap at `plugin_manager.cpp:606`). **Depends on T023.**

### Implementation for User Story 2 — Outbound contract completion (D3)

- [ ] T025 [P] [US2] Wire `setTriggerDescription` into `kRoutedActions` + handler in `src/app/src/sd_plugin_server.cpp` and `src/app/src/plugin_device_bridge.cpp` (manifest field is parsed; runtime command is missing).
- [ ] T026 [P] [US2] Render a visual surface for `showAlert`/`showOk` (currently host-ack only) in `src/app/src/plugin_device_bridge.cpp`.

### Implementation for User Story 2 — Secondary correctness bugs (D4)

- [ ] T027 [P] [US2] Fix B5: `IPluginHost2::dispatch` forwards `actionId` as the event name — correct the contract in `src/app/src/plugin_manager.cpp` (~:1047).
- [ ] T028 [P] [US2] Fix B6: tear down `m_htmlPages` `QWebEnginePage` instances on plugin disable/uninstall in `src/app/src/plugin_manager.cpp` (page leak for app lifetime).
- [ ] T029 [P] [US2] Fix B7: populate `passHello.deviceInfo` geometry in `src/app/src/sd_plugin_server.cpp` (~:324) so plugins reading geometry at hello get real values.

### Verification for User Story 2

- [ ] T030 [US2] Live-verify quickstart Scenarios 3 (press → plugin → key repaint) and 4 (PI round-trip with a single `propertyInspectorDidAppear`) via `scripts/ajazz-debug`; read the screenshot + protocol log.

**Checkpoint**: Plugins install and run with PI round-trip and a complete, correct outbound contract.

______________________________________________________________________

## Phase 6: User Story 4 — Multi-action and toggle bindings (Priority: P3)

**Goal**: A single control runs a multi-action sequence or advances a multi-state toggle (each state
its own image/title/settings), persisting across restart. Mostly landed — verify live and close any
gap the deferred human-UAT never confirmed.

**Independent Test**: Bind a 2-step multi-action and a 3-state toggle; one press runs both steps / one
press advances + repaints the state; the state survives restart (quickstart Scenario 6).

### Tests for User Story 4

- [ ] T031 [P] [US4] Extend `tests/unit/test_multiaction_dispatch.cpp` to cover `userDesiredState` on a multi-action multi-state press (the one unverified inbound field).

### Implementation for User Story 4

- [ ] T032 [US4] Live-verify multi-action sequencing + toggle cycling via quickstart Scenario 6; close any gap found (e.g. emit `userDesiredState` in `src/app/src/plugin_device_bridge.cpp` if absent).
- [ ] T033 [US4] Confirm toggle `currentState` persistence across an app restart (relaunch + read the bound state); add a persistence assertion if missing in `tests/unit/test_profile_persistence.cpp`.

**Checkpoint**: All in-scope stories independently functional.

______________________________________________________________________

## Phase 7: Polish & Cross-Cutting Concerns

- [ ] T034 [P] Update `docs/protocols/streamdeck/**` wherever a wire fact changed this milestone; refresh `docs/architecture/PLUGIN-GAP-ANALYSIS.md` status.
- [ ] T035 [P] Record the deferred / hardware-gated items as explicit DEFERRED (not silently dropped): retail-AKP05E encoder/touch wire values (T013 provisionals), Windows VendorDll Wine launch (WINPLG-03), optional per-device `SupportedDevices` SKU enforcement (research D6).
- [ ] T036 Run the full quickstart.md (Scenarios 1–6) end-to-end through `scripts/ajazz-debug`; read every screenshot.
- [ ] T037 Final gate: `ctest --preset linux-release` green + confirm the change compiles clean on Linux GCC/Clang, Apple Clang (`-Werror`), and MSVC (`/W4 /WX`).

______________________________________________________________________

## Dependencies & Execution Order

### Phase dependencies

- **Setup (Phase 1)**: no dependencies.
- **Foundational (Phase 2)**: depends on Setup; **blocks all stories** (harness + docs/schema + test target).
- **US1 (Phase 3)** and **US5 (Phase 4)**: both P1, depend only on Foundational; together they are the MVP. They touch mostly different files (US1: `profile.*`, `stream_dock_input_service.cpp`; US5: `streamdock-host`, `sidecar_stream_dock_device.cpp`, `stream_dock_control_service.cpp`) and can proceed in parallel under the 2-agent cap.
- **US2 (Phase 5)**: depends on Foundational (esp. T004/T005 harness + T007 schema). Independent of US1/US5.
- **US4 (Phase 6)**: depends on Foundational; benefits from US2's bridge work but is independently testable.
- **Polish (Phase 7)**: depends on all desired stories.

### Critical intra-story ordering

- **US2 PI chain**: T023 (dedup) → T024 (modern bootstrap). Reversing fires `propertyInspectorDidAppear` twice — hard ordering (research D1).
- **US1**: T011 (add field) → T012 (route to it). T009/T010 are RED before T011/T012.
- **US5**: T016 (sidecar command) → T017 (wire proxy). T015 is RED before T017.

### Parallel opportunities

- Setup: T002, T003 in parallel.
- Foundational: T007, T008 in parallel (T004/T005/T006 touch QML/test infra and are best serialized).
- US1 tests T009/T010 in parallel; US2 tests T020/T021/T022 in parallel.
- US2 independent fixes T025/T026/T027/T028/T029 in parallel (different concerns/files) AFTER the T023→T024 chain.
- US1 (Phase 3) and US5 (Phase 4) in parallel (cap concurrent execute agents at 2 — CLAUDE.md).

______________________________________________________________________

## Parallel Example: User Story 2 (after the PI chain)

```bash
# RED tests first, in parallel:
Task: "propertyInspectorDidAppear single-emit test in tests/unit/test_pi_bridge.cpp"      # T020
Task: "setTriggerDescription routing test in tests/unit/test_plugin_device_bridge.cpp"     # T021
Task: "B5/B6/B7 regression tests in test_plugin_host2.cpp + test_plugin_lifecycle.cpp + test_sd_plugin_server.cpp"  # T022

# Then the independent fixes in parallel (after T023->T024 PI chain lands):
Task: "Wire setTriggerDescription into kRoutedActions + handler"                            # T025
Task: "Render showAlert/showOk visual surface in plugin_device_bridge.cpp"                  # T026
Task: "Fix B5 dispatch event-name in plugin_manager.cpp"                                    # T027
Task: "Fix B6 HTML page teardown in plugin_manager.cpp"                                     # T028
Task: "Fix B7 passHello.deviceInfo geometry in sd_plugin_server.cpp"                        # T029
```

______________________________________________________________________

## Implementation Strategy

### MVP (P1: User Story 1 + User Story 5)

1. Complete Phase 1 (Setup) + Phase 2 (Foundational — CRITICAL, blocks everything).
1. Complete Phase 3 (US1) and Phase 4 (US5) — they are co-P1 and the highest-impact device fixes
   (encoder-release + the dead keep-alive).
1. **STOP and VALIDATE**: quickstart Scenarios 1, 2, 5; read the screenshots. This is the MVP — a
   device that renders, routes all input, and never wedges.

### Incremental delivery

1. Setup + Foundational → harness + truthful docs ready.
1. - US1 + US5 → device binding loop correct & wedge-free (MVP) → demo.
1. - US2 → plugins install/run with PI round-trip + full contract → demo.
1. - US4 → multi-action/toggle verified live → demo.
1. Polish → docs, deferred-item honesty, full quickstart, 3-compiler gate.

### Definition of done (every task)

`ctest --preset linux-release` green · 3-compiler clean · the matching quickstart scenario driven via
`scripts/ajazz-debug` with the screenshot read · new controls `objectName`-addressable · affected docs
updated in the same change.

______________________________________________________________________

## Notes

- [P] = different files, no incomplete dependency. The PI chain (T023→T024) is the one hard sequential
  dependency — never parallelize or reverse it.
- Provisional encoder/touch **wire values** are hardware-gated (retail AKP05E); all routing is verified
  with synthetic `input.*` events — build everything except the raw values headless.
- Do NOT modify the `mirajazz` crate; sidecar changes go in `streamdock-host/`. Do NOT reintroduce the
  removed C++ AKP wire backends. COD-031 holds (no `nlohmann::json` in `ajazz_core`).
- Cap concurrent execute agents at 2 (CLAUDE.md retrospective lesson).
