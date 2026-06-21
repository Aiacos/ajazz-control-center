<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Implementation Plan — Faithful Elgato Stream Deck Parity (Full, A–G)

> **Status: plan deliverable (2026-06-20), awaiting approval to implement.**
> Companion to [`ELGATO-PARITY-FEASIBILITY.md`](ELGATO-PARITY-FEASIBILITY.md).
> Covers the full A–G parity scope. **No code is written until this plan is
> approved.** Each phase lands as atomic Conventional Commits, ctest-green, and
> live-verified through the debug-control channel where the harness allows;
> harness/hardware-gated verification is flagged explicitly per phase.

## Sequencing rationale

Phases are ordered by dependency and by "fastest visible Elgato-ness on a sound
base":

1. **B** (dial live preview) and **A** (pages/folders UI) are foundational —
   B builds the encoder render infrastructure; A bridges a complete data model
   to the canvas. Features build on them.
2. **C** (multi-action switch / state visuals) reuses B's render path and A's
   context model.
3. **E** (title/state styling) complements C in the Inspector.
4. **D** (smart profiles UI) sits on the existing watcher backend.
5. **G** (marketplace icon/profile install) extends the installer.
6. **F** (visual-fidelity pass) is last, polishing a complete surface under live
   screenshot review.

Every phase obeys the constitution's definition of done: `ctest --preset
linux-release` green; clean compile on Linux GCC/Clang, MSVC `/W4 /WX`, Apple
Clang `-Werror`; new interactive controls carry `objectName`; behavioural
changes verified live (`AJAZZ_DEBUG_CONTROL=1` → drive → screenshot → read);
docs/schemas updated in the same change; pre-commit/commit-msg/pre-push pass
without bypass. Concurrency: ≤2 execute agents.

---

## Phase 1 — Delta B: on-screen dial/encoder live-feedback preview

> **Status: DONE (`000c90f`, 2026-06-21).** `LiveEncoderImageProvider` registered
> at `application.cpp:1230`; `DeviceView.qml:134` binds dials + strip segments to
> `image://liveencoder/<idx>?r=<rev>`. Real on-device dial feedback remains
> hardware-gated (retail AKP05E). Phase 2 (Delta A) is now the head of the queue.

**Goal.** The editor canvas mirrors what the device LCD shows for each dial: the
Elgato feedback layout (`$X1/$A0/$A1/$B1/$B2/$C1` + custom JSON), live, via an
`image://liveencoder` provider analogous to `image://livekey`.

**Why first.** Self-contained, low risk, immediately visible, and it builds the
encoder-render plumbing the later phases reuse. The device-side renderer
(`encoder_layout_renderer.cpp`) already exists; this phase only adds the
**editor** mirror.

**Approach.**
- Add `LiveEncoderImageStore` + `LiveEncoderImageProvider` (mirror
  `live_key_image_provider.{hpp,cpp}`), keyed by encoder index, mutex-guarded.
- In `StreamDockControlService` (or the bridge seam that already calls the
  encoder layout renderer), emit `encoderImageAssigned(index, image)` and write
  the composited 200×100 frame into the store on every `setFeedback`/`setFeedbackLayout`.
- Register the provider in `application.cpp` beside `livekey`
  (the `engine.addImageProvider` site).
- In `EncoderDial.qml` (and the `TouchStripLane.qml` segment), bind the icon to
  `image://liveencoder/<index>?r=<revision>` when a live frame exists, falling
  back to the static model icon otherwise (same revision/cache-bust pattern as
  KeyCell).

**Files.** create `src/app/src/live_encoder_image_provider.{hpp,cpp}`; modify
`stream_dock_control_service.*`, `application.cpp`,
`src/app/qml/components/EncoderDial.qml`,
`src/app/qml/components/TouchStripLane.qml`; tests in `tests/unit/`.

**Constitution / verification.** `objectName` already on dials/segments. Add a
debug RPC (e.g. `device.renderEncoderTest` or reuse `device.renderTest`'s
encoder path) to drive a synthetic feedback frame and read it back headlessly.
Unit-test the provider + store (decode, revision bump, fallback). **Live editor
preview is headlessly verifiable**; **real on-device dial feedback is
hardware-gated** (retail AKP05E) — stated, not faked.

**Effort: M.**

---

## Phase 2 — Delta A: Pages + nested Folders editor UI

**Goal.** Users can create pages, open a folder key into a sub-page, navigate
back, and see a breadcrumb — on top of the existing, complete page/folder data
model.

**Approach.**
- **ProfileController**: expose page navigation + mutation — `activePageId`,
  `pageStack()` (breadcrumb), `enterFolder(keyIndex)`, `goBack()`,
  `createPage(name)`, `renamePage`, `deletePage`, and have
  `activeKeyBindings()` / canvas state resolve against `activePageId` instead of
  always `"root"`. The schema (`Profile::pages`, `ProfilePage.children`,
  `Action::OpenFolder` carrying the target page id, `Action::BackToParent`)
  already supports all of this.
- **DeviceView.qml / DeviceCanvas.qml**: add a **page bar** beneath the canvas
  (page chips + "+" to add) and a **breadcrumb** above it; a key bound to
  `OpenFolder` navigates into its child page on activation in the editor; render
  an automatic back/return affordance on non-root pages.
- Ensure `willAppear`/`willDisappear` fire correctly on page change (the bridge
  already has `retirePage`/`populateContextsForActivePage` — wire the UI page
  switch to them).
- The context-id scheme (`device#page#controller#row#column`) already encodes
  the page, so plugin contexts stay stable across navigation.

**Files.** modify `profile_controller.{hpp,cpp}`,
`src/app/qml/DeviceView.qml`, `src/app/qml/components/DeviceCanvas.qml`,
new `src/app/qml/components/PageBar.qml` + breadcrumb; debug RPCs in
`debug_control_facade.cpp` (`profile.enterFolder`, `profile.goBack`,
`profile.createPage`, `profile.pageStack`); tests in `tests/unit/`.

**Constitution / verification.** New controls get `objectName`
(`pageBar`, `pageChip_<id>`, `addPageButton`, `breadcrumb`, `backKey`). Drive
create-page → enter-folder → bind-on-subpage → go-back end-to-end via the new
debug RPCs; screenshot-read each state. COD-031 N/A (no core change).

**Effort: M–H.**

---

## Phase 3 — Delta C: Multi-Action Switch + toggle per-state renderer

**Goal.** A key can hold a two-state Multi-Action Switch (alternating
sequences) and a toggle action renders its per-state visual; multi-action steps
can drive a multi-state child (`userDesiredState`).

**Approach.**
- **Renderer**: on state change, composite the active `ActionState` visual to
  the key via the existing livekey path (the toggle render hook already exists in
  `application.cpp`; ensure it paints `states[currentState]`).
- **Switch UI**: extend `KeyBindingList.qml` to model a two-state switch
  (two sub-chains + the alternation toggle) and per-state config.
- **userDesiredState (deferred item from gap analysis)**: add a plain per-step
  state field to `core::Action` (NOT `nlohmann::json` — **COD-031**), thread it
  through `instanceChildrenToChain`, and emit `userDesiredState` on `keyDown`
  with `isInMultiAction:true`.

**Files.** modify `src/core/include/ajazz/core/profile.hpp` (+`action.hpp` /
`action_instance.hpp`), `action_engine.*`, `plugin_device_bridge.cpp`
(`userDesiredState` emission), `src/app/qml/components/KeyBindingList.qml`,
`profile_controller.{hpp,cpp}`; tests in `tests/unit/`.

**Constitution / verification.** COD-031 grep gate stays 0. Unit-test the state
field round-trip + `userDesiredState` payload (mirror `willappear_payload_test`).
Drive a synthetic toggle and read the repainted key (screenshot). Multi-state on
real hardware input is hardware-gated; synthetic `input.*` covers routing.

**Effort: M. Risk: medium** (touches `core::Action`).

---

## Phase 4 — Delta E: title styling + per-state icon controls

**Goal.** The Inspector exposes title font/size/color/alignment and a per-state
icon tray — matching Elgato's title controls and per-state custom icons.

**Approach.**
- Extend `Inspector.qml` with title-style controls bound to the binding's
  `ActionState` fields (`TitleColor`, `TitleAlignment`, `FontFamily`,
  `FontSize`, `FontStyle`, `ShowTitle`) which already exist in the States schema.
- Apply the styling in the key title overlay (KeyCell + livekey compositor).
- Per-state icon tray (drag/drop or file pick) writing `states[n].Image`.

**Files.** modify `src/app/qml/Inspector.qml`, `src/app/qml/components/KeyCell.qml`,
the livekey title compositor in `stream_dock_control_service.*`,
`profile_controller.{hpp,cpp}`; tests.

**Constitution / verification.** New controls get `objectName`
(`titleFontCombo`, `titleColorButton`, `titleAlignGroup`, `stateIconTray_<n>`).
Drive via debug channel; screenshot-read the styled title.

**Effort: L–M.**

---

## Phase 5 — Delta D: Smart Profiles per-app assignment UI

**Goal.** Users assign a profile to auto-activate when a given application is in
the foreground, on top of the existing watcher + `applicationHints` backend.

**Approach.**
- UI to bind an application identity (the normalized `appId` token from
  `IActiveWindowWatcher` — see ACTIVE-WINDOW-IDENTITY) to a profile, editing
  `Profile::applicationHints`.
- Wire the foreground-change signal to profile activation, honoring the Elgato
  semantic that auto-switch is suppressed while the editor window is focused
  (active only when minimized/background).
- Surface the capability-warning chip when the watcher reports
  `capabilityAvailable() == false` (already designed; just present it).

**Files.** new `src/app/qml/SmartProfileAssign.qml` (or a SettingsPage section),
modify `profile_controller.{hpp,cpp}`, application focus wiring; debug RPC for
assignment; tests.

**Constitution / verification.** `objectName` on the assignment controls; drive
assignment via debug RPC + synthetic foreground change (the watcher already has
a test seam); confirm profile switch. Per-platform app-id shape differs
(documented); Linux is the verification target.

**Effort: M.**

---

## Phase 6 — Delta G: Marketplace icon-pack / profile install

**Goal.** The installer handles icon packs and profile bundles, not only
`.streamDeckPlugin` plugins.

**Approach.**
- Extend `plugin_catalog_model.*` + the extractor to recognize icon-pack and
  profile bundle types; install icons into the icon library and profiles into
  the profile store.
- Surface them in `PluginStore.qml` (type filter / tabs) with the same
  install/installed affordance.

**Files.** modify `plugin_catalog_model.{hpp,cpp}`, the sdplugin/extractor path,
`src/app/qml/PluginStore.qml`, the icon library + profile store seams; tests.

**Constitution / verification.** Reuse the existing zip-slip-guarded extractor
(QZipReader sanitization rules — see `reference_qzipreader_sanitization`). Drive
install via the existing store debug path; verify an installed icon appears in
the picker and an installed profile appears in the profile bar.

**Effort: L–M.**

---

## Phase 7 — Delta F: visual-fidelity pass

**Goal.** The editor reads as Elgato: premium dark surface, Elgato-blue accent,
rounded-square key tiles with title overlay, clear selection highlight, and the
categorized action list placed to match Elgato (right-hand column).

**Approach.**
- Theme tokens: dark palette + accent (sample exact hex from a live Elgato
  install/screenshot — not published by Elgato; do not guess in code without a
  sample).
- Tile treatment (corner radius, selection outline, hover) on KeyCell /
  EncoderDial / segments.
- Action-list placement decision: Elgato puts actions on the right; our current
  layout has them left. Move or make consistent with the Stream-Deck-shaped
  mandate (constitution III) — **decide with a live screenshot comparison**.

**Files.** `src/app/qml/**` theme + component styling; a theme singleton if one
exists.

**Constitution / verification.** This phase is **screenshot-driven by
definition** (constitution V): build → launch → screenshot → read → compare to
the Elgato reference → iterate. No behavioural change; purely visual.

**Effort: L** (but iterative).

---

## Cross-cutting concerns

- **Hardware/harness-gated verification (honest list).** Real dial feedback on
  the LCD and live modern-PI WS registration cannot be fully verified on the
  current demo unit / Wayland synthetic-input harness. These are validated as far
  as the harness allows (synthetic `input.*`, unit tests, editor preview) and the
  remainder is explicitly deferred to a manual/hardware session — never marked
  "done" on synthetic evidence alone.
- **Docs currency (VII).** Update `PLUGIN-GAP-ANALYSIS.md`,
  `plugin-event-parity.md`, and any schema docs in the same change that lands the
  behaviour. Relax `docs/schemas/plugin_manifest.schema.json` over-strictness
  (gap-analysis "Faithfulness divergences") opportunistically when touching
  manifest paths.
- **Testing layers (II).** Catch2 unit tests for every new pure helper and data
  round-trip; `tests/qml/` offscreen smoke where a QML surface is added
  (Linux+macOS; Windows registration still gated — see CLAUDE.md latent item).
- **Commits / CI (I, IX).** Atomic, Conventional, into `experiment/mirajazz`,
  PR’d via #80 toward `develop`. Never push directly to or force-push the
  long-lived branches.

## Suggested delivery order & checkpoints

Phase 1 → 2 → 3 → 4 → 5 → 6 → 7. After each phase: ctest-green + live-verified +
docs updated + committed; pause for review before the next where the prior
phase's UI is a dependency. Estimated relative size: A and B are the largest
single phases; C carries the only `core` change (COD-031 watch); F is iterative
polish.
