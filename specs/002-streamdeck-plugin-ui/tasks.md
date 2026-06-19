---
description: Task list for the Elgato-Parity Plugin UI for Keys & Dials + One-Click Install feature
---

# Tasks: Elgato-Parity Plugin UI for Keys & Dials + One-Click Install

**Input**: Design documents from `/specs/002-streamdeck-plugin-ui/`

**Prerequisites**: plan.md ✓, spec.md ✓, research.md ✓, data-model.md ✓, contracts/ ✓, quickstart.md ✓

**Tests**: Test tasks ARE included — plan.md explicitly plans them ("core model round-trip
tests + QML smoke + catalog-install unit tests planned") and constitution Principle II mandates
testing at the right layer. They are NOT strict TDD-first; each story carries its own focused
tests plus a mandatory live debug-channel verification (Principle V).

**Organization**: Tasks are grouped by user story (US1–US4) so each ships and tests
independently.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependency on an incomplete task)
- **[Story]**: US1 / US2 / US3 / US4 (setup, foundational, polish carry no story label)
- Every task names exact file paths.

## Path Conventions

Single-project desktop layout (per plan.md): QML in `src/app/qml/`, app C++ in `src/app/src/`,
core model in `src/core/include/ajazz/core/`, tests in `tests/unit/` and `tests/qml/`.

______________________________________________________________________

## Phase 1: Setup (Shared baseline)

**Purpose**: Establish a known-green baseline before changing behavior.

- [x] T001 Confirm build + test baseline on branch `002-streamdeck-plugin-ui`: run `cmake --preset linux-release && cmake --build --preset linux-release` then `ctest --preset linux-release`; record the live ctest case count in the PR description (do not hand-edit CLAUDE.md). This is the green starting point all later live-verify tasks compare against.

______________________________________________________________________

## Phase 2: Foundational (Base entities + shared gating) — BLOCKS dependent stories

**Purpose**: The base data-model and shared drop-gating changes that the user stories consume.
**⚠️ CRITICAL**: US1 consumes the catalog field (T002/T006); US3 consumes the encoder model
(T003/T004/T005). US2 needs only Setup. Complete the relevant items before the matching story.

- [x] T002 [P] Add the catalog installability surface to `src/app/src/plugin_catalog_model.hpp` (+ stub in `.cpp`): derived read-only fields `installableInApp` (bool, true iff a resolvable `https` package URL exists) and `unavailableReason` (QString, short text e.g. "not installable in-app", empty otherwise); expose to QML as roles. Data definition only — install behavior lands in T007. (US1 base; per data-model.md §2)
- [x] T003 [P] Change `EncoderBinding` in `src/core/include/ajazz/core/profile.hpp` so the dial owns its touch-strip segment: document that the segment renders this binding's bound-action feedback layout and that binding/clearing the dial binds/clears the segment; mark `TouchZoneBinding`/`Profile::touchZones` deprecated-for-dial-devices but retained in-model for read-compat. No `onTap[]` chain field unless a later task proves a built-in tap action is needed. (US3 base; per data-model.md §1)
- [x] T004 Update the hand-rolled profile serialization in `src/core/src/profile.cpp` (and `src/core/src/profile_io.cpp` if the touchZones/encoders read/write path lives there) so the **writer stops emitting** new independent `touchZones` bindings for dial devices, and the **reader** keeps parsing legacy `touchZones` losslessly. **Default behavior: retain a legacy `touchZones[N]` in-model and stop writing new ones (lossless, forward-only) — do NOT auto-fold onto encoder N unless a later task proves the fold is needed; if folded, encoder wins when both `encoders[N]` and `touchZones[N]` are present.** Gate on key presence, never a schema version. ⚠️ This is `ajazz_core` — the code MUST stay nlohmann-free (COD-031: `grep -rn nlohmann src/core/include/` must return 0). (depends on T003)
- [x] T005 [P] Add a drop-target controller-gating helper used by the canvas QML: a `Keypad`-capable action is accepted on a key and rejected on a dial; an `Encoder`-capable action (vendor `Knob`→`Encoder` normalized) is accepted on a dial (segment included) and rejected on a key; the independent `"TouchZone"` drop target is retired for dial devices. Implement in the affordance-mask path feeding the QML drop areas (`src/app/src/profile_controller.*` and/or `DragRelay`/canvas QML). (shared by US2/US3/US4; per data-model.md §3)
- [x] T006 Update docs/schema for the model change in the same change set: `docs/protocols/streamdeck/**` for the touch-zone→dial control model and the profile schema's `touchZones` deprecation note (schema doc is the wire source-of-truth, Principle VII). (depends on T003/T004)

**Checkpoint**: Base entities + gating in place — user stories can proceed.

______________________________________________________________________

## Phase 3: User Story 1 — One-click in-app install (Install replaces Open) (Priority: P1) 🎯 MVP

**Goal**: Every listed plugin installs with a single in-app **Install** action; no browser, no
side drawer. Non-installable rows are shown disabled with a reason — never "Open".

**Independent Test**: From the plugin store, click the primary action on any listed plugin and
confirm it installs in-app (inline progress → Installed, actions appear in the list) with zero
browser/side-drawer openings; a non-resolvable row is disabled with "not installable in-app".

### Implementation for User Story 1

- [x] T007 [US1] Make `PluginCatalogModel::install(uuid)` in `src/app/src/plugin_catalog_model.cpp` an **in-app install only**: download + extract via the existing `.sdPlugin` installer, emit `installProgressChanged(uuid, pct)` then `installFinished(uuid, success, error)`; remove the `openUpstream()`/`QDesktopServices::openUrl` fallback entirely from the install path; a `NotInstallable` entry → no-op returning false. Compute `installableInApp`/`unavailableReason` (T002) from package-URL resolvability. (per contracts/plugin-store-ui.md)
- [x] T008 [US1] Rewrite the catalog row primary action in `src/app/qml/PluginStore.qml` to the state machine `Installable("Install") → Installing("Installing… N%", disabled) → Installed("Installed" + uninstall affordance)` plus a disabled `NotInstallable` state labelled/tooltipped with `unavailableReason`; remove the "Open page ↗" affordance and any side-drawer requirement; make the row button `objectName`-addressable.
- [x] T009 [US1] Wire post-install refresh in `src/app/src/plugin_catalog_model.cpp` + `PluginStore.qml`: on `installFinished(success)` trigger the existing `installedCountChanged` → catalogue/action-list refresh so the plugin's actions appear in the editor action list with no extra navigation; **on uninstall, remove the plugin's actions from the editor action list (FR-005)**; on failure return the row to `Installable` with an inline error and leave nothing partially-installed usable.
- [x] T010 [P] [US1] Unit test the install availability state machine + the no-browser invariant in `tests/unit/test_catalog_offline.cpp` (or a new `tests/unit/test_plugin_catalog_install.cpp`): installable vs not-installable derivation, `install()` on a non-installable entry is a no-op returning false, and the install path never invokes the browser launcher.
- [x] T011 [P] [US1] QML smoke for the store affordance in `tests/qml/`: assert rows render Install/Installed/disabled-reason states and that no "Open"/"Open page" control exists.
- [x] T012 [US1] Live-verify Scenario 1 (quickstart.md) via the debug channel: `qml.invoke` open the store, `screenshot` and READ it (Install/Installed/disabled "Not installable in-app", no "Open page"), `plugin.installFromCatalog` an installable id, `plugin.list` shows its actions; assert zero browser launches.

**Checkpoint**: US1 is independently functional — MVP candidate.

______________________________________________________________________

## Phase 4: User Story 2 — A plugin on a KEY looks & configures like Elgato (Priority: P1)

**Goal**: A plugin-bound key renders the action image + title with live updates, and selecting
it shows an Elgato-shaped title/image + Property Inspector layout.

**Independent Test**: Drop (or profile-inject) a plugin action on a key; confirm it shows the
action image + title, updates live (System-Monitor "CPU n%" reference), and that selecting it
presents an Elgato-faithful PI + title/image layout.

### Implementation for User Story 2

- [x] T013 [US2] Polish key render in `src/app/qml/components/KeyCell.qml` to be Elgato-faithful: action image dominant with title overlay, live update via the existing `image://livekey/<index>` provider, and a sensible placeholder when a state has no image/title.
- [x] T014 [US2] Arrange the key configuration surface Elgato-style in `src/app/qml/Inspector.qml` + `src/app/qml/PropertyInspector.qml`: plugin Property Inspector together with the standard title/image controls (action visual dominant, title overlay, selected control highlighted), consistent location.
- [x] T015 [US2] Highlight the key as a valid drop target during a drag in `src/app/qml/components/DeviceCanvas.qml`/`KeyCell.qml` using the gating helper (T005); reject a non-`Keypad` action with a clear affordance, leaving the target unchanged.
- [x] T016 [US2] Ensure a Property-Inspector setting change reflects in the on-screen key preview (FR-010): verify the PI→profile→`image://livekey` round-trip in `src/app/src/property_inspector_controller.*` / `profile_controller.*` updates the preview.
- [x] T017 [P] [US2] QML smoke for key-bind render in `tests/qml/`: a plugin-bound key shows image+title and reflects a simulated live update.
- [x] T018 [US2] Live-verify Scenario 2 (quickstart.md) via the debug channel: `device.setActiveDevice akp05e`, profile-inject a key binding, `input.key`, `plugin.protocolLog` (setTitle/setImage), `screenshot` and READ the live key render. Flag the modern-PI-open-via-selection walk for a manual session (harness gap).

**Checkpoint**: US1 + US2 work independently — full P1 MVP.

______________________________________________________________________

## Phase 5: User Story 3 — A plugin on a DIAL looks & configures like Elgato (Priority: P2)

**Goal**: On a Stream Deck + class device, binding a dial-capable action to dial N renders its
feedback layout in the touch-strip segment above dial N and routes rotate/press/tap to it; the
dial PI is Elgato-shaped. Dial + its segment are one control (no independent touch-zone).

**Independent Test**: Drop (or profile-inject) a dial-capable action on a dial of an AKP05E/N4;
confirm segment N renders the feedback layout, rotate/press/segment-tap each reach the action,
and the dial PI/controls appear Elgato-shaped; there is no independent touch-zone drop target.

### Implementation for User Story 3

- [x] T019 [US3] Make `commitEncoderBinding` in `src/app/src/profile_controller.*` carry the touch-strip segment so binding/clearing dial N binds/clears segment N (one control), consuming the T003/T004 model; retire the standalone `commitTouchZoneBinding` path for dial devices.
- [x] T020 [US3] Render the touch strip in `src/app/qml/components/TouchStripLane.qml`: segment N is driven by dial N's binding via `renderEncoderLayout(layoutId, feedback, target)` (`$X1/$A0/$A1/$B1/$B2/$C1`; `title/value/icon/indicator/icon2/indicator2`) with live-preview parity to the device LCD zone; default icon+title fallback when no feedback layout; remove the independent touch-zone drop target.
- [x] T021 [US3] Make the dial a drop target + selectable in `src/app/qml/components/EncoderDial.qml` and lay out the dial lane + touch-strip lane (segment above each dial) in `src/app/qml/components/DeviceCanvas.qml`; accept `Encoder`-capable actions via the gating helper (T005), reject otherwise.
- [x] T022 [US3] Verify/wire dial input routing in `src/app/src/plugin_device_bridge.cpp`: rotate→`dialRotate`, press/release→`dialDown`/`dialUp`, tap-on-segment-N→`touchTap` routed to encoder N via the encoder context `device#<pageId>#Encoder#0#N` (the `<pageId>` slot is `ctx.pageId` per `ContextRegistry::deriveContextId`; the default page id is `root`, so the concrete id is `device#root#Encoder#0#N`) (reuse feature-001 paths); confirm segment routing is correct.
- [x] T023 [US3] Load the dial Property Inspector on dial selection in `src/app/qml/Inspector.qml` (encoder context `device#<pageId>#Encoder#0#N` — `Inspector.qml::_contextUuid` emits `device#root#Encoder#0#N` for the default page, mirroring the key path's `device#root#Keypad#row#col`) and present dial controls (title, icon, feedback layout) Elgato-style in `src/app/qml/EncoderPanel.qml`.
- [x] T024 [US3] Enforce drop-type gating affordance (FR-015): reject a `Keypad`-only action on a dial and an `Encoder`-only action on a key with a clear "not supported here" affordance, leaving the target unchanged (DeviceCanvas/EncoderDial/KeyCell using T005).
- [x] T025 [P] [US3] Unit test the profile round-trip incl. the encoder segment + legacy `touchZones` read-compat in `tests/unit/test_profile_persistence.cpp` (or `tests/unit/test_profile_serialization.cpp`): writer omits independent dial touch-zones, reader loads a legacy `touchZones[N]` without loss and resolves encoder-wins.
- [x] T026 [P] [US3] QML smoke for dial/touch-strip render + drop in `tests/qml/`: a profile-injected encoder binding renders segment N and the dial drop target accepts/rejects by controller type.
- [x] T027 [US3] Live-verify Scenario 3 (quickstart.md) via the debug channel: `device.setActiveDevice akp05e`, profile-inject an Encoder binding, `input.encoder` / `input.encoderPress` / `input.touch`, `plugin.protocolLog`, `screenshot` and READ segment-0 feedback render + routing; confirm no independent touch-zone target. Also confirm a legacy-`touchZones` profile loads (read-compat) with no crash/loss.

**Checkpoint**: US1 + US2 + US3 independently functional.

______________________________________________________________________

## Phase 6: User Story 4 — Consistent, Elgato-faithful editor layout & affordances (Priority: P3)

**Goal**: One coherent Elgato-shaped editor across keys and dials: centred canvas, action list
alongside, configuration in a consistent location, selection + drop-target highlighting, and
live-preview parity.

**Independent Test**: Walk the editor with both a key and a dial bound; confirm consistent
arrangement, selection highlight, valid/invalid drop-target highlight, and a preview that
mirrors the device.

### Implementation for User Story 4

- [ ] T028 [US4] Ensure the coherent Elgato arrangement in `src/app/qml/DeviceView.qml` + `src/app/qml/components/DeviceCanvas.qml`: device canvas centre-stage (key grid and, where present, touch strip with one segment above each dial), action list alongside, selected-control configuration in a consistent location (FR-016).
- [ ] T029 [US4] Unify selection highlight + valid/invalid drop-target highlight across keys and dials (FR-017) in `DeviceCanvas.qml`/`KeyCell.qml`/`EncoderDial.qml`, reusing the gating helper (T005).
- [ ] T030 [US4] Verify live-preview parity (FR-018): the on-screen key and dial segment mirror what the physical device renders, using the shared `renderEncoderLayout`/`image://livekey` paths; reconcile any drift.
- [ ] T031 [US4] Live-verify Scenario 4 (quickstart.md) via the debug channel: attempt a `Keypad`-only drop on a dial and an `Encoder`-only drop on a key, `screenshot` and READ the reject affordance + selection/drop highlighting + preview parity.

**Checkpoint**: All four stories functional and consistent.

______________________________________________________________________

## Phase 7: Polish & Cross-Cutting Concerns

**Purpose**: Documentation, addressability audit, and final gates spanning all stories.

- [ ] T032 [P] Update `docs/architecture/PLUGIN-GAP-ANALYSIS.md` with the install-flow + dial-UI parity status, and confirm `docs/protocols/streamdeck/**` reflects the touch-zone→dial model (complete any not landed in T006).
- [ ] T033 [P] Audit that every new interactive control (store row button, dial drop target, touch-strip segment, dial PI controls) sets `objectName` and is `qml.get/set/invoke/click`-addressable (Principle V definition-of-done).
- [ ] T034 Run `ctest --preset linux-release` green and confirm 3-compiler cleanliness expectations (GCC/Clang/Apple-Clang `-Werror`, MSVC `/W4 /WX`; ASCII-only test names).
- [ ] T035 Run the full `specs/002-streamdeck-plugin-ui/quickstart.md` validation end-to-end; flag the harness-gated modern-PI-open-via-selection walk for a manual session per the Notes section.
- [ ] T036 [P] Refresh the agent context (`/speckit-agent-context-update`) and add a CHANGELOG `[Unreleased]` entry for the install-flow + key/dial parity change.
- [ ] T037 Handle the uninstall-while-bound edge case (spec Edge Cases): when a plugin is uninstalled while one of its actions is bound to a key or a dial, the affected control MUST revert to an unbound state with a clear indication and MUST NOT crash the editor. Cross-cutting across US1 (uninstall), US2 (key binding) and US3 (dial binding) in `src/app/src/plugin_catalog_model.cpp` / `profile_controller.*` / the binding model; add a unit test (`tests/unit/`) for the revert and a QML smoke (`tests/qml/`) for no-crash, then live-verify via the debug channel (install→bind→uninstall→`screenshot` reads the reverted control).

______________________________________________________________________

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: no dependencies — start immediately.
- **Foundational (Phase 2)**: after Setup. T002/T006 unblock US1; T003→T004, T005 unblock US3; T005 also supports US2/US4.
- **US1 (Phase 3, P1)**: needs T002 (+T007 derives from it). Independent of US2/US3/US4.
- **US2 (Phase 4, P1)**: needs Setup; benefits from T005. Independent of US1/US3.
- **US3 (Phase 5, P2)**: needs T003/T004/T005 from Foundational. Independent of US1/US2 at the UX level.
- **US4 (Phase 6, P3)**: builds on US2/US3 controls; reuses T005.
- **Polish (Phase 7)**: after the desired stories are complete. T037 (uninstall-while-bound) depends on US1 + US2 + US3 (it spans install, key bindings, and dial bindings).

### Within Each User Story

- Tests marked [P] may run alongside implementation; the live-verify task is last and gates the story.
- Model/serialization (Foundational) before story UI that consumes it.
- Story complete and live-verified before moving to the next priority.

### Parallel Opportunities

- Foundational: T002, T003, T005 are [P] (different files); T004 follows T003; T006 follows T003/T004.
- US1: T010, T011 [P] (tests, different files) alongside T007–T009; T012 last.
- US2: T017 [P] alongside T013–T016; T018 last.
- US3: T025, T026 [P] alongside T019–T024; T027 last.
- Cross-story: once Foundational is done, US1 and US2 (both P1) can be built in parallel by different people; US3 in parallel once T003/T004/T005 land.
- Polish: T032, T033, T036 [P]. T037 is NOT [P] (touches the shared binding model and must run after US1/US2/US3).

______________________________________________________________________

## Parallel Example: Foundational

```bash
# Base entities + shared gating (different files, no inter-dependency):
Task: "T002 Catalog installability fields in src/app/src/plugin_catalog_model.{hpp,cpp}"
Task: "T003 EncoderBinding owns segment in src/core/include/ajazz/core/profile.hpp"
Task: "T005 Drop-target controller gating helper (profile_controller/DragRelay/canvas)"
```

## Parallel Example: User Story 1

```bash
# Tests in parallel with the install implementation:
Task: "T010 Unit test install availability + no-browser invariant in tests/unit/"
Task: "T011 QML smoke for store affordance in tests/qml/"
```

______________________________________________________________________

## Implementation Strategy

### MVP First (US1, then US2 — both P1)

1. Phase 1 Setup → green baseline.
1. Phase 2 Foundational (at least T002/T006 for US1; T005 for US2).
1. Phase 3 US1 → **STOP and live-verify Scenario 1** (the user's explicit primary request).
1. Phase 4 US2 → live-verify Scenario 2. P1 MVP complete.

### Incremental Delivery

1. Setup + Foundational → base ready.
1. US1 (install) → verify → demo (the headline change).
1. US2 (key parity) → verify → demo.
1. US3 (dial parity) → verify → demo.
1. US4 (layout polish) → verify → demo.
1. Polish phase → docs, addressability audit, full quickstart gate.

### Notes

- `ctest` green is necessary but NOT sufficient — each story's live debug-channel verify task is the real definition-of-done (Principle V; feature-001 caught wiring bugs that 700+ unit tests missed).
- No plugin wire-protocol change; reuse the existing runtime, PI mechanism, and feedback-layout renderer.
- The touch-zone→dial model change MUST stay read-compatible with old profiles (T004/T025).
- The modern-PI-open-via-selection headless walk is a known harness gap (feature-001 T030) — route PI verification via `plugin.simulateAction` and flag the WS-PI walk for a manual session.
