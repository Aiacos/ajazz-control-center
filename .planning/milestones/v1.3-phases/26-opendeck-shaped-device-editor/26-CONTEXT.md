# Phase 26: OpenDeck-shaped Device Editor - Context

**Gathered:** 2026-05-28
**Status:** Ready for planning
**Discuss session:** 2026-05-28 (autonomous-mode, after SPEC.md landed)

<domain>
## Phase Boundary

Phase 26 replaces the generic `KeyDesigner.qml` NxN tile grid with an
**Elgato Stream Deck / OpenDeck-pattern device-shaped editor** that
renders the **physical geometry of each LCD-key AJAZZ / Mirabox SKU**
(key grid + optional encoder-dial row + optional touch-strip-zone row),
drives drag-drop from an action library onto every drop target, and
wires `StreamDockControlService::setActiveDevice()` on sidebar
selection — closing GAP-25A + GAP-25B from the Phase 25 partial
walkthrough so Phase 25 25-UAT.md Tests 1 + 6 convert FAIL/NO_AFFORDANCE
→ PASS on the live AKP05E.

</domain>

\<spec_lock>

## Requirements (locked via SPEC.md)

**5 requirements are locked.** See `26-SPEC.md` for full requirements,
boundaries, and acceptance criteria. Downstream agents MUST read
`26-SPEC.md` before planning or implementing. Requirements are not
duplicated here.

**In scope (from SPEC.md):**

- Replacement of `src/app/qml/KeyDesigner.qml` with a new
  `DeviceView.qml` (or equivalent) + companion components
- `setActiveDevice` wiring in `Main.qml:onDeviceSelected` (closes GAP-25A)
- `DeviceDescriptor` extended with 4 additive geometry fields
- Per-SKU geometry data on all ~15-16 LCD-key descriptor rows
- Compile-time / unit-test geometry presence check (REQ-26-D)
- Action library pane with built-in `ActionKind` tiles (v1)
- Drag-drop wire from library tile → key/encoder/strip-zone drop targets
- Phase 25 25-UAT.md re-walk: Tests 1 + 6 → PASS recording

**Out of scope (from SPEC.md):**

- AKP815's 800×480 wide single-strip editor (deferred sentinel
  `keyRows = 0` per Area D below)
- AK820/AK980 keyboards + AJ-series mouse SKUs (untouched; specialised
  panels stay reachable)
- Plugin-contributed action drag sources (Phase 20/21 follow-up)
- Wire-protocol changes (Phase 14 already ships them)
- Demo-unit input-streaming gap (Tests 2-5, 15-16 stay BLOCKED)
- Real third-party `.sdPlugin` round-trip (Tests 13-14 gated on Phase 17-22)

\</spec_lock>

<decisions>
## Implementation Decisions

### Layout grammar (Area A — action library pane)

- **D-01:** **Library pane is LEFT** of the device editor; the existing
  Inspector stays on the right. Three-column layout — library | editor |
  inspector. Matches Elgato Stream Deck's own desktop app and preserves
  the Phase 16 Inspector pattern. The visual reading flow is L→R: pick
  action → drop on device → inspect/configure.
- **D-02:** **Library content for v1 is the 5 built-in `ActionKind`
  entries** currently exposed in `Inspector.qml:215-221` (`OpenUrl`,
  `OpenPath`, `RunCommand`, `Multimedia`, etc.). Plugin-contributed
  action sources are explicitly deferred to Phase 20/21 follow-up (per
  SPEC out-of-scope). The library tiles are the same drag sources that
  Inspector's current picker dispatches; plugin actions plug into the
  same drop-target API later without UI rework.
- **D-03:** **Library tile icons come from the Qt / Material Symbols
  icon theme.** Each `ActionKind` maps to a named Material icon (e.g.,
  `Multimedia → music_note`, `OpenUrl → link`, `OpenPath → folder_open`,
  `RunCommand → terminal`). No custom SVG asset commitment; tiles render
  text + small Material icon. Consistent visual style; extensible to
  plugin actions when those come online (each plugin can declare its
  icon name in the manifest).

### Device-chassis visual (Area B — Elgato-pattern photo background)

- **D-04:** **Per-SKU device-photo background** is the v1 visual
  treatment (Elgato pattern, NOT OpenDeck's abstract grid). Each LCD-key
  SKU ships its own photo asset; the editor renders the photo and
  overlays drop-targetable cells positioned per-SKU.
- **D-05:** **Photo source = vendor product-page photos**, downloaded
  from AJAZZ / Mirabox vendor sites and lightly processed (background
  removal, consistent angle) for the fastest path to v1 ship.
  Committed under `resources/device-photos/<codename>.png` (or `.webp`).
  Attribution + license posture documented in
  `resources/device-photos/README.md` (new file): vendor marketing
  imagery used under fair use for control-software UI; project owns
  the layout JSON; vendor retains image copyright. If a vendor objects,
  a per-SKU fallback to outline-frame (D-07) preserves functionality.
- **D-06:** **Cell-position-on-photo coordinates** live in **per-SKU
  JSON layout files** at `resources/device-layouts/<codename>.json`
  (NOT in C++ `DeviceDescriptor`). Schema:
  ```json
  {
    "codename": "akp05e",
    "photo": "akp05e.png",
    "viewBox": { "w": 600, "h": 400 },
    "keys":       [{ "i": 1, "x": 60, "y": 50, "w": 80, "h": 80 }, ...],
    "encoders":   [{ "i": 0, "x": 80, "y": 280, "w": 50, "h": 50 }, ...],
    "touchZones": [{ "i": 0, "x": 50, "y": 340, "w": 120, "h": 40 }, ...]
  }
  ```
  Coordinates are in the photo's viewBox space; the QML renders at any
  display size via uniform scaling. JSON is hot-editable for design
  iteration without recompile. Reading happens in QML via
  `XMLHttpRequest` against the `qrc:/` URL OR a small C++ loader exposed
  as Q_INVOKABLE — picker decided in plan-phase (see Open at end).
- **D-07:** **Outline-frame fallback** for SKUs without a photo asset
  or layout JSON yet. The same `DeviceView.qml` checks for asset
  presence and falls back to a rounded-rect outline frame (rounded
  corners + subtle border + drop shadow) rendering the cell grid
  geometry from `DeviceDescriptor` only. Never broken — always renders
  something useful. The Phase 26 SPEC acceptance criteria are met as
  long as the in-scope SKUs have photos; SKUs added after Phase 26
  start with outline-frame and gain photos over time.

### Cell sizing + drag interaction (Area C)

- **D-08:** **Cell visual size is device-scaled** from the layout JSON
  (`w` / `h` in viewBox coordinates), NOT a fixed 96×96 or 144×144.
  Cell sizes naturally vary by SKU (AKP05E LCD key vs AKP03 LCD key vs
  AKP05E touch-strip zone). The layout JSON already declares per-cell
  dimensions — reuse them. Cells align with the photo background by
  construction.
- **D-09:** **Drag-drop grammar is FULL** for v1:
  - **Library → cell**: dragging an action tile onto a key / encoder
    / touch-zone cell sets the binding (fires
    `ProfileController::commitKeyBinding` / `commitEncoderBinding` /
    `commitTouchZoneBinding`).
  - **Cell → cell**: dragging a configured cell onto another cell
    swaps or moves the binding (OpenDeck pattern with
    `MIME["controller"] + MIME["position"]` data).
  - **Cell → trash**: a trash drop zone (icon-button at editor corner
    OR drag-out-of-editor cell-clear) deletes the binding.
    This is wider than the SPEC minimum (which only required
    library→cell) — explicit user decision in discuss-phase.
- **D-10:** **Drop-target visual feedback is scale-up 1.05×** on
  drag-over via a QML `transform: Scale { ... }` so neighbouring cells
  don't reflow (layout stability across 3×5 grids). The scale animation
  uses a 100ms `Behavior on transform.xScale` for a playful but not
  jarring feel.

### Profile schema + AKP815 deferral (Area D)

- **D-11:** **New `Profile::touchZones` map** added to
  `src/core/include/ajazz/core/profile.hpp` (NOT extending `keys` at
  offset). Cleaner type signatures; explicit semantic separation
  between LCD keys and touch zones.
  ```cpp
  struct TouchZoneBinding {
      KeyState state;
      std::vector<Action> onTap;
  };

  struct Profile {
      std::string id;
      std::string deviceCodename;
      std::unordered_map<std::uint16_t, Binding>          keys;
      std::unordered_map<std::uint8_t,  EncoderBinding>   encoders;
      std::unordered_map<std::uint8_t,  TouchZoneBinding> touchZones;  // NEW
      std::unordered_map<std::string,   ProfilePage>      pages;
  };
  ```
- **D-12:** **On-disk JSON schema version bumps to 2**, with
  auto-migrate on load. The JSON writer adds `"_schemaVersion": 2` and a
  `"touchZones"` key alongside `"keys"` / `"encoders"`. The reader
  detects v1 profiles (no `_schemaVersion` OR `_schemaVersion: 1`) and
  silently treats `touchZones` as empty. Round-trip a v1 profile and
  it lands as v2 with empty `touchZones`. Test coverage: a v1 → v2
  migration test under `tests/unit/test_profile_serialiser.cpp`.
- **D-13:** **AKP815 deferral sentinel = `keyRows = 0`** in the
  `DeviceDescriptor` row. The geometry-check regression test (REQ-26-D)
  carries a named allow-list:
  ```cpp
  static constexpr std::array kDeferredLcdSkus = {
      "akp815",  // Phase 26 SPEC out-of-scope: 800x480 wide single-strip;
                 // deferred to follow-up phase. Row keeps keyRows = 0 as
                 // sentinel. Removing this entry requires populating the
                 // descriptor + updating the SPEC.
  };
  ```
  Test iterates all descriptors, skips entries with `codename` in
  `kDeferredLcdSkus`, asserts `keyRows > 0` for the rest. Adding the
  SKU to the allow-list is an explicit code change that a PR reviewer
  sees.

### Claude's Discretion (planner / executor)

- **Wave structure** — the planner decides the wave split. Suggested
  ordering (smallest blast radius first):
  1. **Wave 1 (closes GAP-25A immediately):** the one-line
     `Main.qml:onDeviceSelected` fix (REQ-26-A). Lands as an atomic
     commit before the bigger UI work; immediately retest Phase 25
     Test 1 against the live AKP05E.
  1. **Wave 2:** `DeviceDescriptor` extension (REQ-26-C) + populate
     ~15 LCD-key rows + geometry-check test (REQ-26-D). Compile-time
     safety net before the QML editor lands on top.
  1. **Wave 3:** Profile schema bump (D-11/D-12) + migration test.
     Independent of QML; can land in parallel with Wave 2.
  1. **Wave 4:** The new `DeviceView.qml` + companion components +
     layout JSON loader + drag-drop grammar (REQ-26-B). Deletes
     `KeyDesigner.qml` atomically with the new editor's first
     in-place render.
  1. **Wave 5:** Per-SKU layout JSONs + photo assets for the in-scope
     SKUs (start with AKP05E so the operator can re-walk Phase 25
     UAT Test 1 + 6 immediately).
  1. **Wave 6:** Phase 25 25-UAT.md Tests 1 + 6 operator re-walk;
     update `25-UAT.md` results to PASS (REQ-26-E).
- **JSON parser for layout files** — QML's `XMLHttpRequest` is the
  zero-dependency path; alternatively a Q_INVOKABLE on `DeviceLayoutLoader`
  (new C++ class) gives type-checked errors. Planner picks.
- **Trash drop zone UI** — D-09 says cells can drag-to-trash. The
  visual treatment of the trash zone (corner icon-button? dedicated
  drop strip? drop-outside-cell?) is planner's call. Suggested: an
  icon-button in the editor's top-right corner that highlights during
  drag (Elgato pattern).

</decisions>

\<canonical_refs>

## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Phase 26 in-tree

- `.planning/phases/26-opendeck-shaped-device-editor/26-SPEC.md` —
  **LOCKED requirements** (5 reqs, 11+ acceptance criteria). MUST read
  before planning.
- `.planning/phases/26-opendeck-shaped-device-editor/26-RESEARCH.md` —
  Source-grounded analysis of OpenDeck, opendeck-akp05, mirajazz.
  Includes the schema proposal (§6), QML component architecture (§7),
  risk register (§8), and the 6 open questions (§9) that this CONTEXT
  resolves.

### Phase 25 partial (the upstream that motivated Phase 26)

- `.planning/phases/25-hardware-verification-real-plugin/25-02-SUMMARY.md`
  — Plan 25-02 PARTIAL summary; GAP-25A + GAP-25B descriptors.
- `.planning/phases/25-hardware-verification-real-plugin/25-UAT.md` —
  The operator UAT runbook with Test 1 FAIL + Test 6 NO_AFFORDANCE
  rows that Phase 26 must convert to PASS.

### OpenDeck / mirajazz reference (patterns-only — no direct code copy)

- `https://github.com/nekename/OpenDeck` @ `f7d9e07` (GPL-3.0) —
  `src/lib/components/DeviceView.svelte` (246 lines, three-stacked-rows
  pattern); `src/lib/components/Key.svelte` (the cell delegate
  discriminated by `controller` + `isTouchPoint`); `src-tauri/src/devices.rs`
  (device-type registry).
- `https://github.com/naerschhersch/opendeck-akp05` @ `d77e9e2`
  (GPL-3.0) — `src/main.rs:register_device(rows=2, cols=5, encoders=4, type=7)`
  — the minimal-data pattern for SKU registration.
- `https://github.com/4ndv/mirajazz` @ `37a9512` (MPL-2.0) — per-SKU
  geometry constants; init sequences cross-referenced with our
  `docs/protocols/streamdeck/akp05_init_sequence.md`.

### Existing in-tree code that Phase 26 touches

- `src/app/qml/KeyDesigner.qml` — to be DELETED (hard replacement; D-04).
- `src/app/qml/Inspector.qml` — REUSED (right pane); the FileDialog
  picker stays; `bindingFieldChanged` signal stays the integration
  point.
- `src/app/qml/components/KeyCell.qml` — REUSED as the LCD-key cell
  delegate inside `DeviceView.qml`.
- `src/app/qml/Main.qml:127-129` — `onDeviceSelected` handler; the
  one-line `StreamDockControlService.setActiveDevice(codename)` call
  lands here (closes GAP-25A, REQ-26-A).
- `src/app/qml/ProfileEditor.qml` — its capability-based routing
  decides which editor to instantiate for the current SKU; the
  LCD-key branch flips from `KeyDesigner.qml` to `DeviceView.qml`.
- `src/app/qml/MousePanel.qml`, `RgbPicker.qml`, `SettingsRow.qml`,
  `FirmwarePanel.qml` — UNTOUCHED; non-LCD SKUs stay on these.
- `src/app/src/profile_controller.{hpp,cpp}` — extend with
  `commitTouchZoneBinding(quint8 idx, QString iconPath, QString label, int actionKind, QString settingsJson)`
  Q_INVOKABLE.
- `src/app/src/stream_dock_control_service.{hpp,cpp}` — already ships
  `setActiveDevice(QString codename)` (line 116/153); just needs to be
  called from QML. `assignMainImage` / `assignEncoderImage` /
  `assignTouchStripZone` wire layer untouched (Phase 14 / Phase 23
  deliverable).
- `src/core/include/ajazz/core/profile.hpp` — add `TouchZoneBinding`
  struct + `Profile::touchZones` map (D-11). Bump schema version (D-12).
- `src/core/src/profile.cpp` — extend serialiser/deserialiser for
  `_schemaVersion` + `touchZones`. Add v1→v2 migration test.
- `src/core/include/ajazz/core/device.hpp` — extend `DeviceDescriptor`
  with `keyRows`, `touchZoneCount`, `mainScreenWidthPx`, `mainScreenHeightPx`.
- `src/devices/streamdeck/src/register.cpp` — populate the 4 fields on
  every LCD-key SKU row except AKP815 (D-13).

### Project rules

- `/home/aiacos/workspace/ajazz-control-center/CLAUDE.md` — Qt 6 / QML
  gotchas, conventional-commit allowlist (no `spec`; use `docs(N):` for
  SPEC commits), pre-commit dance (mdformat re-add), AKP05E glossary
  §7.1 (demo unit input-streaming gap stays BLOCKED), no system-level
  mutations, atomic commits, never `--no-verify`.
- `.planning/REQUIREMENTS.md` — VERIFY-05 anchor; the existing
  PROFILE-01/02 + DISPLAY-08/10 requirements that Phase 26 builds on.

\</canonical_refs>

\<code_context>

## Existing Code Insights

### Reusable assets

- **`Inspector.qml` + `iconPicker FileDialog`** — the right-pane
  binding editor stays. Drag-drop from the new library is an
  ADDITION to the picker; both paths fire `bindingFieldChanged`.
  The URL-handling fix from commit `24651a3` (L1+L2) keeps the picker
  working end-to-end once GAP-25A wiring lands.
- **`KeyCell.qml`** — the LCD-key cell delegate is reused as the key
  cell inside `DeviceView.qml`. Its `required property url iconSource`
  stays; the cell is wrapped in a DropArea + draggable handle for
  the new drag-drop grammar.
- **`ProfileController` + `Profile` schema** — the binding storage +
  Q_INVOKABLE persistence stays; only EXTEND with a new
  `commitTouchZoneBinding` Q_INVOKABLE and a `touchZones` map field
  (D-11).
- **`StreamDockControlService::assignMainImage` /
  `assignEncoderImage` / `assignTouchStripZone`** — wire layer is
  already shipped + tested (26 tests). Phase 26 only consumes it.
- **`Theme` system** — `Theme.accent`, `Theme.tile`, `Theme.spacingMd`,
  `Theme.radiusMd` etc. used for cell-highlight + outline-frame
  fallback rendering.

### Established patterns

- **`QML_SINGLETON` requires `qmlRegisterSingletonInstance`** per
  CLAUDE.md "Qt 6 / QML gotchas". If `DeviceLayoutLoader` becomes a
  QML singleton, use the non-default-constructible static-assert
  pattern.
- **Hard-replacement atomic commits** — `KeyDesigner.qml` deletion
  lands in the SAME commit as `DeviceView.qml` first compile. No
  interim broken-editor state.
- **mdformat pre-commit dance** — staging a markdown file, hook
  reformats, re-stage + re-commit. Documented in CLAUDE.md.
- **Conventional commit allowlist** — `feat, fix, docs, style, refactor, perf, test, build, ci, chore, revert`. NO `spec` —
  use `docs(N):` for SPEC commits (verified during Phase 26 SPEC
  commit `9e9733f`).

### Integration points

- **`Main.qml:127-129 onDeviceSelected`** — the GAP-25A wire-up.
  Single source of truth for "user picked a different device";
  fires `StreamDockControlService.setActiveDevice(codename)`,
  updates `editor.codename`, updates `editor.capabilities`.
- **`ProfileEditor.qml` capability dispatch** — checks
  `_showKeys / _showEncoders / _showDials etc.` from `DeviceModel.capabilitiesFor(codename)`.
  The LCD-key branch flips from `KeyDesigner` to `DeviceView`.
  Non-LCD branches (mouse / keyboard / etc.) stay routing to existing
  specialised panels (`MousePanel`, `RgbPicker`, `SettingsRow`,
  `FirmwarePanel`).
- **`ProfileController::commitKeyBinding / commitEncoderBinding / commitTouchZoneBinding`** — the QML→C++ binding-update boundary.
  New method added for touch zones; existing methods reused for keys +
  encoders.
- **`DeviceLayoutLoader` (new)** — a small Q_INVOKABLE class (or QML
  helper) that reads `resources/device-layouts/<codename>.json` and
  exposes `layoutFor(codename) → JSObject`. Loads at editor mount.

\</code_context>

<specifics>
## Specific Ideas

- **"Come Elgato StreamDeck / OpenDeck"** (user directive 2026-05-28
  13:10) — D-04 lands on Elgato-style per-SKU device-photo background
  with cells positioned over the photo. OpenDeck-style abstract grid
  is the FALLBACK (D-07), not the primary v1 visual.
- **"Tutti i dispositivi AJAZZ che opendeck supporta"** — the v1
  scope is ALL LCD-key SKUs (AKP05/153/03 + Mirabox N3/N3E/N4 +
  AKP153R); AKP815 deferred (D-13); AK820/AK980 keyboards + AJ-series
  mouse untouched (SPEC out-of-scope).
- **"Trovarci già pronti in futuro"** — the schema is designed to
  scale: layout JSONs are per-SKU and easy to author; the geometry-check
  test (REQ-26-D) catches missing data when a new SKU lands;
  `Profile::touchZones` map accepts arbitrary `touchZoneCount` values
  without code changes.
- **Drag-drop "playful" feedback (D-10)** — the user explicitly
  picked the 1.05× scale-up over the more conservative
  border-highlight. Editor wants to feel responsive, not static.

</specifics>

<deferred>
## Deferred Ideas

- **AKP815 wide-strip editor** — 800×480 single rect-addressable LCD
  strip. Geometrically different from the grid SKUs (no rows/cols
  matrix). Needs a `MainScreenEditor.qml` companion component +
  DRA-zone partial upload UI. Phase 26.x or v2.
- **Plugin-contributed action library tiles** — Phase 20/21 follow-up
  per SPEC. The library pane already has the architectural shape to
  accept plugin actions (`ActionLibraryPane.qml` reads a model;
  plugin actions get added to the model when those phases land).
- **Per-SKU touch-strip layouts beyond "4 zones below 4 encoders"** —
  if a future SKU has a wide single strip OR a vertical strip, the
  layout JSON already supports arbitrary `touchZones` rects, but the
  visual treatment of the strip lane will need refinement.
- **Cell drag-reorder cross-controller** (e.g., drag from an encoder
  cell to a key cell) — D-09 allows cell-to-cell within the same
  controller surface; cross-controller swaps need binding-type
  conversion and are deferred.
- **Designer mode for layout JSON authoring** — a future QML editor
  that lets a contributor drag a photo + place cells visually,
  writing the layout JSON for them. Manual JSON editing is fine for
  v1; designer mode is a follow-up nice-to-have.
- **Light/dark theme polish on the photo background** — vendor photos
  often have white backgrounds that clash with the project's dark
  theme. v1 ships with whatever the vendor provides + the outline
  frame fallback; theme-aware photo treatment is a follow-up.

</deferred>

______________________________________________________________________

*Phase: 26-opendeck-shaped-device-editor*
*Context gathered: 2026-05-28*
*Next step: `/gsd-plan-phase 26` — produces executable plans, wave-parallelised per the wave structure in `<decisions> → Claude's Discretion`.*
