# Implementation Plan: Elgato-Parity Plugin UI for Keys & Dials + One-Click Install

**Branch**: `002-streamdeck-plugin-ui` | **Date**: 2026-06-18 | **Spec**: [spec.md](./spec.md)

**Input**: Feature specification from `specs/002-streamdeck-plugin-ui/spec.md`

## Summary

Two connected UX problems in the plugin experience: **(1)** the plugin store presents a
mixed install affordance — "Install" for rows with a resolvable package and "Open page ↗"
(an external-browser launch) for the rest — and **(2)** a plugin bound to a control,
especially a **dial**, does not look or configure like the Elgato Stream Deck software.
This plan makes **install a single in-app action ("Install" replaces "Open")**, and makes
a **plugin on a key or a dial render + configure with Elgato Stream Deck + parity** —
including the clarified decision that the **touch-strip segment above each dial belongs to
that dial** (one control = dial + segment), superseding the project's current model of 4
independently-bindable touch zones for dial devices.

The technical approach reuses the existing, proven plugin runtime end-to-end (the
Elgato/OpenDeck WebSocket contract, the mirajazz sidecar render path, the
`PluginDeviceBridge` seam, the `encoder_layout_renderer` feedback layouts, and the
`PropertyInspectorController`). **No plugin wire-protocol change.** The work is concentrated
in: the plugin-store QML + `PluginCatalogModel` install flow (remove the browser fallback,
surface a disabled "not installable in-app" state); the device-canvas QML for keys and
especially dials (touch-strip segment owned by its dial, Elgato-faithful layout, live
preview); the encoder binding model in `ajazz_core` (`EncoderBinding` carries the segment;
touch-zone-as-independent-control is retired for dial devices); and the dial Property
Inspector path. Every behavioral change is verified live through the debug-control channel
(constitution Principle V), and the Elgato dial-feedback presentation is researched
accurately first (Principle VI; spec FR-019).

## Technical Context

**Language/Version**: C++20 (app + `ajazz_core`), QML 6, with the existing Rust mirajazz
sidecar and Python plugin host **unchanged** by this feature.

**Primary Dependencies**: Qt 6.7+ (`Qml`, `Quick`, `Controls`, `Network`,
`WebEngineQuick`/`WebChannelQuick` for the Property Inspector); the existing
`PluginCatalogModel` + catalog fetcher; the self-contained `.sdPlugin` ZIP installer;
`encoder_layout_renderer` (host-side `$A0/$A1/$B1/$B2/$C1/$X1` feedback rendering);
`PluginDeviceBridge` + `SdPluginServer`. `nlohmann::json` stays PRIVATE to `ajazz_plugins`
(COD-031).

**Storage**: Profile JSON (hand-rolled, nlohmann-free `profile.cpp`); installed `.sdPlugin`
directories under the app data location; QSettings for app state.

**Testing**: Catch2 via `ctest --preset linux-release`; offscreen QML smoke (`tests/qml/`);
live verification via the `AJAZZ_DEBUG_CONTROL` channel + `scripts/ajazz-debug` (qml.get/
set/invoke/click/drag, screenshot, device/input/plugin RPCs).

**Target Platform**: Linux (primary), Windows, macOS — desktop Qt6/QML application.

**Project Type**: Desktop application (single project; QML front-end + C++ core/app +
out-of-process sidecar/plugin hosts).

**Performance Goals**: Interactive editor latency; a plugin-bound control shows its
image/title within ~1 s of binding and reflects live plugin pushes (SC-003/SC-004). No
per-keystroke plugin rescans; reuse the persistent-handle render path (Principle IV).

**Constraints**: No plugin wire-protocol change. Reuse the existing plugin runtime, PI
mechanism, and feedback-layout renderer. The touch-zone→dial data-model change MUST stay
backward-compatible when reading older profiles (a profile that still carries independent
`touchZones` must load without loss/corruption). Install scope is **UI + flow only** — no
reverse-engineering of private/secret download paths (clarified). Every new interactive
control is `objectName`-addressable (Principle V).

**Scale/Scope**: One plugin store + one device editor; the dial work targets the
Stream Deck + class (AKP05E/N4: 4 dials + a 4-segment touch strip). Key-only devices are
unaffected by the dial requirements.

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.* Evaluated against
`.specify/memory/constitution.md`.

| Principle                       | Gate                                                                                                                                               | Status                                                                                                                                                                                                                                                 |
| ------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| I. Code Quality & Boundaries    | COD-031 preserved; no C++ AKP wire backends; mirajazz untouched; atomic Conventional Commits; `qmlRegisterSingletonInstance` for any new singleton | ✅ PASS — UI/flow + core-model work; no nlohmann in core; no new singletons planned                                                                                                                                                                    |
| II. Testing Standards           | Tests at the right layer; `ctest --preset linux-release` green; 3-compiler clean; ASCII test names                                                 | ✅ PASS — core model round-trip tests + QML smoke + catalog-install unit tests planned                                                                                                                                                                 |
| III. UX Consistency             | Stream-Deck/OpenDeck-shaped editor; drag-to-bind; Material theming rules; `Rot180` hardware-true render                                            | ✅ PASS — Elgato parity is the explicit goal; live preview mirrors device render                                                                                                                                                                       |
| IV. Performance                 | No per-keystroke rescans; persistent handle; hotplug-not-poll                                                                                      | ✅ PASS — reuses existing render/dispatch paths; no new hot-path work                                                                                                                                                                                  |
| V. Auto Debug & Live Validation | Every behavioral change verified via the debug channel; new controls `objectName`-addressable                                                      | ⚠️ WATCH — the modern-PI-open headless drive is a known harness gap (feature 001 T030); quickstart routes dial/PI verification around it (synthetic `input.*`, `plugin.simulateAction`, profile-inject) and flags any walk that needs a manual session |
| VI. Research-Backed             | RE/SDK docs re-read first; hardware wins; research the Elgato dial UX accurately                                                                   | ✅ PASS — research.md documents Elgato Stream Deck + dial feedback + editor UX (FR-019) before design                                                                                                                                                  |
| VII. Documentation Currency     | Docs + schema updated in the same change; schema is wire source-of-truth                                                                           | ✅ PASS — `docs/protocols/streamdeck/**` + profile schema updated when the touch-zone→dial model changes                                                                                                                                               |
| VIII. Repository Hygiene        | No system mutations; no dead-code deletion off static-analysis FPs                                                                                 | ✅ PASS                                                                                                                                                                                                                                                |
| IX. CI/CD                       | Pipelines stay green; GitFlow; shift-left hooks                                                                                                    | ✅ PASS — lands on a topic branch via PR into `develop`                                                                                                                                                                                                |

**Verdict**: PASS. One WATCH (Principle V — the pre-existing modern-PI-open headless harness
gap, inherited from feature 001) is documented in Complexity Tracking; this feature does not
introduce it and routes verification around it.

## Project Structure

### Documentation (this feature)

```text
specs/002-streamdeck-plugin-ui/
├── plan.md              # This file
├── research.md          # Phase 0 — Elgato dial/key UX research + install-flow + migration decisions
├── data-model.md        # Phase 1 — EncoderBinding (owns segment), catalog availability state, PI-for-dial
├── quickstart.md        # Phase 1 — debug-channel validation per user story
├── contracts/
│   ├── plugin-store-ui.md       # the install affordance contract (states, no-browser rule)
│   └── dial-feedback-ui.md      # the dial + touch-strip presentation + interaction contract
└── tasks.md             # Phase 2 — created by /speckit.tasks (NOT here)
```

### Source Code (repository root) — the surfaces this feature touches

```text
src/app/qml/
├── PluginStore.qml                 # install affordance: remove "Open page", disabled+reason state
├── components/DeviceCanvas.qml     # dial lane + touch-strip lane layout (segment above each dial)
├── components/EncoderDial.qml      # dial render + drop target + selection (Elgato-faithful)
├── components/TouchStripLane.qml   # touch-strip segments now driven by their dial's binding
├── components/KeyCell.qml          # key render polish (Elgato-faithful) — already largely there
├── Inspector.qml                   # PI load on dial selection (mirror the key path)
└── EncoderPanel.qml                # dial-specific config controls

src/app/src/
├── plugin_catalog_model.{hpp,cpp}  # install(): in-app only; availability/installable state; no browser
├── property_inspector_controller.* # PI for a selected dial (reuse loadInspector)
├── profile_controller.*            # commitEncoderBinding (carry the segment); migration on load
├── plugin_device_bridge.cpp        # dial feedback render path (reused; verify segment routing)
└── encoder_layout_renderer.cpp     # $A0/$A1/$B1/$B2/$C1/$X1 feedback (reused)

src/core/include/ajazz/core/
└── profile.hpp                     # EncoderBinding owns the touch-strip segment; touchZones read-compat

docs/
├── protocols/streamdeck/**         # update where the dial/touch-zone model is documented
└── architecture/PLUGIN-GAP-ANALYSIS.md  # note the install-flow + dial-UI parity status

tests/
├── unit/ (profile round-trip incl. encoder segment; catalog install availability state)
└── qml/  (device-view dial/touch-strip smoke; plugin-store affordance smoke)
```

**Structure Decision**: Single-project desktop layout (already in place). This feature
modifies existing plugin-store + device-editor QML, the `PluginCatalogModel`, the
`EncoderBinding` core model, and the PI/encoder-render wiring. No new top-level modules; the
plugin runtime and wire protocol are untouched.

## Complexity Tracking

> Items that touch a constitution WATCH point and are deliberately accepted.

| Violation / Watch                                                                      | Why Needed                                                                                                                                                                                                      | Simpler Alternative Rejected Because                                                                                                                                                                                                                |
| -------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Modern-PI-open cannot be driven fully headless (Principle V WATCH, inherited from 001) | Verifying a dial's Property Inspector end-to-end needs a PI-bearing control SELECTED, but synthetic click-to-select does not propagate the binding to the Inspector (`inspector.binding` stays null) on Wayland | Building a synthetic-input fix for QML selection is out of scope for this feature; quickstart routes verification via `plugin.simulateAction` / profile-inject / synthetic `input.*`, and flags the residual modern-PI WS walk for a manual session |
| Touch-zone→dial data-model change with read-compat for old profiles                    | Elgato parity requires the segment to belong to its dial; old profiles may carry independent `touchZones`                                                                                                       | Hard-migrating/dropping old `touchZones` silently would lose user data — the reader must fold/ignore them losslessly and the writer stops emitting independent touch-zone bindings for dial devices                                                 |
