# Phase 26: OpenDeck-shaped Device Editor — Specification

**Created:** 2026-05-28
**Ambiguity score:** 0.076 (gate: ≤ 0.20)
**Requirements:** 5 locked

## Goal

Replace the generic `KeyDesigner.qml` NxN tile grid with a device-shaped
editor that renders the physical geometry of each LCD-key AJAZZ /
Mirabox SKU (key grid + optional encoder-dial row + optional touch-
strip-zone row), drives drag-drop from an action library onto every
drop target, and wires `StreamDockControlService::setActiveDevice` on
sidebar selection — closing GAP-25A + GAP-25B from the Phase 25 partial
walkthrough so Phase 25 25-UAT.md Tests 1 + 6 convert FAIL/NO_AFFORDANCE
→ PASS on the live AKP05E.

## Background

The Phase 25 operator UAT 2026-05-28 13:00 surfaced two structural gaps
in the current editor:

- **GAP-25A** — `StreamDockControlService::setActiveDevice()` is declared
  at `src/app/src/stream_dock_control_service.hpp:153` and defined at
  `.cpp:116`, but **never called from any QML file** (verified by grep).
  `Main.qml:127-129` `onDeviceSelected` handler updates only
  `editor.codename` and `editor.capabilities`. As a result `m_activeDevice`
  stays null, `repaintPage()` early-returns at the `!m_activeDevice` guard
  (`stream_dock_control_service.cpp:348`), and no image-write fires even
  after the URL-handling fixes that landed in commit `24651a3`. Operator
  observation 2026-05-28 13:08: "Inspector preview shows the picked
  image but device key stays blank."

- **GAP-25B** — `KeyDesigner.qml` (lines 40-176) is a generic NxN
  `GridLayout columns=root.gridColumns`. No encoder-dial visualisation,
  no touch-strip drop target. Operator observation: "nella UI
  dell'applicazione non compare nessuna voce LCD, solo Dial."

The OpenDeck reference analysis in `26-RESEARCH.md` shows a clean
three-stacked-row pattern (`DeviceView.svelte`, 246 lines, geometry-
driven from six integers: `rows, columns, encoders, touchpoints, type, id`). The plugin (`opendeck-akp05`) contributes only the integers
via `register_device(rows=2, cols=5, enc=4, type=7)`; OpenDeck auto-
renders. Our wire layer (`assignMainImage` / `assignEncoderImage` /
`assignTouchStripZone`) is already shipped and tested (26 tests under
`StreamDockControlService`); Phase 26 is **UI + wiring only**.

## Requirements

1. **REQ-26-A — setActiveDevice wiring (closes GAP-25A)**:
   The sidebar selection handler calls `StreamDockControlService.setActiveDevice(codename)`.

   - Current: `src/app/qml/Main.qml:127-129` `onDeviceSelected` updates only
     `editor.codename` and `editor.capabilities`. `setActiveDevice` is
     never invoked from QML. `m_activeDevice` stays null.
   - Target: `onDeviceSelected` invokes `StreamDockControlService.setActiveDevice(codename)`
     before assigning `editor.codename`, so subsequent
     `assignMainImage` / `assignEncoderImage` / `assignTouchStripZone`
     / `repaintFromProfile` calls find a non-null handle.
   - Acceptance: After selecting the AKP05E in the sidebar, journalctl
     logs `[akp05] device opened: AJAZZ AKP05E (Stream Dock Plus)` AND
     `[stream-dock-control] repaintPage` does not early-return; the
     next binding edit produces wire writes (BAT header + chunks +
     ULEND) on hidraw.

1. **REQ-26-B — Device-shaped editor renders per-SKU geometry**:
   A new `DeviceView.qml` (replacing `KeyDesigner.qml`) renders three
   stacked rows discriminated by `DeviceDescriptor` geometry fields.

   - Current: `KeyDesigner.qml:40-176` renders one generic `GridLayout`
     with `columns = root.gridColumns`. No encoder dials, no touch-
     strip zones; only LCD-key cells.
   - Target: For an LCD-key SKU, `DeviceView.qml` renders:
     (1) a `keyRows × keyColumns` grid of LCD-key cells (reusing
     `KeyCell.qml`), (2) if `hasEncoders` then a row of `encoderCount`
     encoder-dial cells, (3) if `touchZoneCount > 0` then a row of
     `touchZoneCount` touch-strip-zone cells. `KeyDesigner.qml` is
     deleted (hard replacement).
   - Acceptance: AKP05E selected → 5×2 key grid + 4 encoder dials +
     4 touch-strip zones. AKP153 selected → 3×5 key grid + 0 enc +
     0 strip. AKP03 selected → 2×3 key grid + 3 encoder dials + 0 strip.
     Each cell is a distinct drop target (validated by QML offscreen
     test with synthetic drag events).

1. **REQ-26-C — DeviceDescriptor carries per-SKU geometry**:
   `DeviceDescriptor` (in `src/core/include/ajazz/core/device.hpp` and
   sibling sites) extended with 4 additive fields, and every LCD-key
   SKU descriptor row in `src/devices/streamdeck/src/register.cpp` is
   populated. AKP815 row is **NOT** populated (deferred — see
   Boundaries).

   - Current: `DeviceDescriptor` carries `keyCount`, `hasEncoders`,
     `displayInfo`, `hasClock`. Missing: per-SKU layout (rows×cols),
     touch-strip presence/count, main-screen pixel dimensions.
   - Target: 4 additive fields with safe zero defaults:
     `std::uint8_t keyRows = 0`, `std::uint8_t touchZoneCount = 0`,
     `std::uint16_t mainScreenWidthPx = 0`, `std::uint16_t mainScreenHeightPx = 0`.
     All ~15-16 LCD-key SKU descriptor rows in
     `src/devices/streamdeck/src/register.cpp` (AKP05/05E/05E-Plus,
     AKP153/153E/153R + Mirabox V1/V2 V1.0, AKP03/03E/03R/03R-rev2,
     Mirabox N3/N3E/N4) populated with non-zero `keyRows` (and the
     other 3 where applicable; touchZoneCount=4 for the AKP05/N4
     family).
   - Acceptance: `grep -E 'keyRows = 0' src/devices/streamdeck/src/register.cpp`
     returns ONLY the AKP815 row(s) (deferred) and AK820/AK980/AJ-series
     mouse/keyboard rows (out of scope). All other LCD-key rows have
     `keyRows >= 1`.

1. **REQ-26-D — Compile-time / unit-test geometry presence check**:
   A regression test iterates every `DeviceDescriptor` with `keyCount > 0`
   and `family == "streamdeck"` (or similar LCD-key gating), asserting
   geometry data is populated. CI fails if a new LCD-key row lands
   without geometry data.

   - Current: No such test; `DeviceDescriptor` rows can land
     incomplete and downstream callers silently fall through.
   - Target: A Catch2 `TEST_CASE` (e.g.,
     `tests/unit/test_streamdeck_register_geometry.cpp`) iterates all
     registered descriptors, filters for LCD-key SKUs (excluding
     AKP815 explicitly via SKU allow-list comment), asserts
     `keyRows > 0` AND `(touchZoneCount == 0 OR (mainScreenWidthPx > 0 AND mainScreenHeightPx > 0))`.
   - Acceptance: The test passes when all in-scope LCD-key descriptor
     rows are populated; **fails** when any row lacks geometry. AKP815
     is in an explicit allow-list (deferred SKU). Added to ctest;
     visible in `ctest -N -R Geometry`.

1. **REQ-26-E — Phase 25 25-UAT.md Tests 1 + 6 convert to PASS**:
   On the live AKP05E demo unit (`0x0300:0x3004`, fw `V3.AKP05E.01.007`),
   after Phase 26 lands the operator re-walks Tests 1 and 6 of
   `.planning/phases/25-hardware-verification-real-plugin/25-UAT.md`
   and records PASS.

   - Current: 25-UAT.md Test 1 = FAIL (L3 setActiveDevice gap);
     Test 6 = NO_AFFORDANCE (no touch-strip drop target).
   - Target: Drag-drop or picker-driven image assignment causes the
     image to appear on physical key 1; image assignment to touch-
     strip zone 0..3 causes the image to appear on that physical
     zone. Both PASS recorded with operator session note referencing
     the Phase 26 commit SHA.
   - Acceptance: `grep "result: PASS" .planning/phases/25-hardware-verification-real-plugin/25-UAT.md`
     returns at least 5 PASS rows (the existing 3 + new Tests 1 + 6).
     The 25-UAT.md Summary table is updated; `passed: 5` minimum.

## Boundaries

**In scope:**

- Replacement of `src/app/qml/KeyDesigner.qml` with a new
  `DeviceView.qml` (or equivalent) + companion components
  (`EncoderDial.qml`, `TouchStripLane.qml`, `ActionLibraryPane.qml`)
  per `26-RESEARCH.md §7.1`.
- `StreamDockControlService.setActiveDevice(codename)` invocation
  from `Main.qml:onDeviceSelected` (closes GAP-25A — the one-line
  fix called out in `26-RESEARCH.md §7.3`).
- `DeviceDescriptor` extended with 4 additive zero-default geometry
  fields (`keyRows`, `touchZoneCount`, `mainScreenWidthPx`,
  `mainScreenHeightPx`).
- Per-SKU geometry data populated for ~15-16 LCD-key SKU descriptor
  rows in `src/devices/streamdeck/src/register.cpp` (AKP05/05E
  family, AKP153 family incl. AKP153R, AKP03 family, Mirabox
  N3/N3E/N4).
- A Catch2 regression test that fails CI when a future LCD-key
  descriptor row lacks geometry data (compile-or-runtime check).
- Action library pane with **built-in `ActionKind` tiles only** for
  v1 (the 5 entries currently in `Inspector.qml:215-221`).
- Drag-drop wire from action library tile → key/encoder/strip-zone
  drop target → `ProfileController.commitKeyBinding` /
  `commitEncoderBinding` / `commitTouchZoneBinding` (sig may need
  extension for touch zones; details in discuss-phase).
- Phase 25 25-UAT.md re-walk: Tests 1 + 6 PASS recording.
- All `ctest --preset linux-release -E qml` tests continue to pass
  (no regressions in the 645 baseline).

**Out of scope:**

- **AKP815's 800×480 wide single-strip editor** — geometrically odd
  (no rows/cols grid; single rect-addressable strip per Research §9
  Q5). AKP815 stays on the existing path until a follow-up phase;
  its `keyRows = 0` (sentinel for the geometry-check test allow-
  list).
- **AK820 Pro, AK980 PRO keyboard SKUs** — different surface type
  (RGB mode + per-key colour); already use specialised panels
  (`RgbPicker.qml`, `SettingsRow.qml`, `FirmwarePanel.qml`). The
  `KeyDesigner.qml` deletion does NOT affect these — they route
  through different `ProfileEditor.qml` capability branches.
- **AJ-series mouse SKUs** — already use specialised `MousePanel.qml`
  (DPI / poll / RGB). Untouched by Phase 26.
- **Plugin-contributed action drag sources** — v1 = built-in
  `ActionKind` only. Plugin actions land in a Phase 20/21 follow-up
  (the plugin protocol already supports it; just not wired into the
  action library pane yet).
- **Wire-protocol changes** to `StreamDockControlService` — the wire
  layer is shipped + tested (Phase 14, 26 tests). Phase 26 only
  consumes it.
- **Demo-unit input-streaming gap** — Tests 2-5, 15-16 in 25-UAT.md
  stay BLOCKED per CLAUDE.md AKP05E glossary §7.1. Phase 26 cannot
  change documented hardware behaviour.
- **Real third-party `.sdPlugin` round-trip** — Tests 13 + 14 of
  25-UAT.md are gated on a real `.sdPlugin` sample on disk, which
  belongs to Phase 17-22.
- **Device-chassis silhouette background image** — per Research §9
  Q2 this is left to discuss-phase. SPEC remains agnostic (could be
  in v1 or deferred); discuss-phase decides.
- **Profile schema change** for touch zones — per Research §9 Q6,
  the choice between `Profile::keys` at offset `keyCount..` (OpenDeck
  model) vs a new `Profile::touchZones` map is left to discuss-phase.
  SPEC requires the data to be persisted but not the storage shape.

## Constraints

- **Qt 6 / QML constraints from CLAUDE.md** — must avoid the
  documented gotchas: `QML_SINGLETON` requires
  `qmlRegisterSingletonInstance` (not bare macro); `WebEngineView`
  has no `page` Q_PROPERTY; `Qt6::WebChannelQuick` not `WebChannel`;
  `MultiEffect.maskSource` needs a proper Item type (raw Rectangle =
  SIGABRT); Material attached properties don't cross Popup scope.
- **License posture** — `nekename/OpenDeck` (GPL-3.0) and
  `naerschhersch/opendeck-akp05` (GPL-3.0) are compatible with this
  project's GPL-3.0-or-later, but per Research §8 the recommended
  posture is **patterns-only reuse** (no direct code copy) for
  cleaner attribution. Phase 26 implementations must be original
  Qt 6 / QML code informed by the OpenDeck pattern.
- **No regressions in existing tests** — `ctest --preset linux-release -E qml`
  must remain 645/645 (or higher with the new geometry-check test).
- **Hard-replacement constraint** — `KeyDesigner.qml` deletion is
  ATOMIC with the DeviceView.qml landing + the descriptor geometry
  data + the geometry-check test. No interim merge where the LCD-key
  editor is broken.
- **Cross-platform test names** — any new test names ASCII-only
  (Win32 CMD codepage mangling per CLAUDE.md).

## Acceptance Criteria

- [ ] `src/app/qml/Main.qml:127-129` `onDeviceSelected` handler
  invokes `StreamDockControlService.setActiveDevice(codename)`
  before assigning `editor.codename` (closes GAP-25A).
- [ ] `src/core/include/ajazz/core/device.hpp` `DeviceDescriptor`
  struct extended with `keyRows`, `touchZoneCount`,
  `mainScreenWidthPx`, `mainScreenHeightPx` — all 4 additive
  with zero defaults.
- [ ] `src/devices/streamdeck/src/register.cpp`: every LCD-key SKU
  row in scope (AKP05/05E + AKP153 family incl. AKP153R + AKP03
  family + Mirabox N3/N3E/N4) has `keyRows >= 1`. AKP05E /
  AKP05/N4 family additionally has `touchZoneCount = 4`. AKP815
  row left at `keyRows = 0` (deferred allow-list).
- [ ] `tests/unit/test_streamdeck_register_geometry.cpp` (or equiv)
  added; iterates descriptors with `keyCount > 0`, asserts
  `keyRows > 0` for LCD-key SKUs minus AKP815 allow-list, plus
  the touchZoneCount → mainScreenWidth/Height invariant.
  Visible in `ctest -N -R Geometry`; passes.
- [ ] `src/app/qml/KeyDesigner.qml` deleted; new `DeviceView.qml` +
  companion components in place; `ProfileEditor.qml` routes
  LCD-key SKUs to `DeviceView.qml`.
- [ ] AKP05E selected → DeviceView renders 5×2 grid + 4 encoder
  dials + 4 touch-strip-zones. Verified via offscreen QML test
  with a synthetic AKP05E descriptor.
- [ ] AKP153 selected → DeviceView renders 3×5 grid + 0 encoder
  dials + 0 touch zones. AKP03 selected → 2×3 grid + 3 encoder
  dials + 0 touch zones.
- [ ] AK820 Pro, AK980 PRO, AJ-series mouse SKUs continue routing
  to existing specialised panels (`MousePanel`, `RgbPicker`,
  `SettingsRow`, `FirmwarePanel`) with no UI regression.
- [ ] Drag from action library tile onto a key/encoder/strip-zone
  cell updates the binding (verified via QML offscreen test
  with synthetic drag events and a mock `ProfileController`).
- [ ] `25-UAT.md` Test 1 `result:` field updated from `FAIL` to
  `PASS` after Phase 26 commit lands + operator re-walks against
  the AKP05E demo unit. Session note references Phase 26
  completion SHA.
- [ ] `25-UAT.md` Test 6 `result:` field updated from
  `NO_AFFORDANCE` to `PASS` (touch-strip zone now has a drop
  target; image renders on the zone).
- [ ] `ctest --preset linux-release -E qml` ≥ 645 passed, 0 failed
  (no regressions).
- [ ] No new QML qWarning lines in the app log on AKP05E selection
  \+ binding edit + drag-drop cycle (re-checked against the
  2026-05-28 13:00 baseline).

## Ambiguity Report

| Dimension           | Score | Min   | Status | Notes                                                                                  |
| ------------------- | ----- | ----- | ------ | -------------------------------------------------------------------------------------- |
| Goal Clarity        | 0.95  | 0.75  | ✓      | Concrete deliverable, GAP-25A/B closure, Tests 1+6 anchor                              |
| Boundary Clarity    | 0.95  | 0.70  | ✓      | AKP815 deferred; AK980/AJ-mouse untouched; KeyDesigner deletion scoped to LCD-key path |
| Constraint Clarity  | 0.85  | 0.65  | ✓      | Qt6/QML gotchas, GPL posture, hard-replacement atomic, ASCII test names                |
| Acceptance Criteria | 0.92  | 0.70  | ✓      | 11 pass/fail items; all falsifiable via grep / ctest / 25-UAT.md inspection            |
| **Ambiguity**       | 0.076 | ≤0.20 | ✓      | Clear gate-pass after 2 rounds                                                         |

## Interview Log

| Round | Perspective           | Question summary                 | Decision locked                                                                              |
| ----- | --------------------- | -------------------------------- | -------------------------------------------------------------------------------------------- |
| 1     | Researcher+Simplifier | Per-SKU scope for v1             | All LCD-key SKUs (AKP05/153/03 + Mirabox N3/N4); AKP815 deferred                             |
| 1     | Researcher+Simplifier | KeyDesigner.qml fate             | Hard replacement (delete); ProfileEditor's capability routing for non-LCD SKUs untouched     |
| 1     | Researcher+Simplifier | Phase 25 UAT resume criterion    | Tests 1 + 6 convert FAIL/NO_AFFORDANCE → PASS on AKP05E demo unit (anchor for REQ-26-E)      |
| 2     | Failure Analyst       | AKP815 800×480 wide-strip in v1? | Deferred; AKP815 row keeps `keyRows = 0` as the sentinel; geometry-check test allow-lists it |
| 2     | Failure Analyst       | Out-of-scope SKU rendering       | Untouched — `KeyDesigner.qml` deletion only affects the LCD-key path                         |
| 2     | Failure Analyst       | Missing geometry data on new SKU | Compile-time check via Catch2 regression test; CI fails if a future row lacks data           |

______________________________________________________________________

*Phase: 26-opendeck-shaped-device-editor*
*Spec created: 2026-05-28*
*Next step: /gsd-discuss-phase 26 — surfaces the 6 HOW-questions flagged in `26-RESEARCH.md §9` (library pane left/right, chassis silhouette y/n, per-cell pixel size, action library v1 content shape, AKP815-deferral plumbing, touch-zone Profile schema choice).*
