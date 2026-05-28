# Phase 26: OpenDeck-shaped Device Editor - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or
> execution agents. Decisions are captured in `26-CONTEXT.md` — this log
> preserves the alternatives considered.

**Date:** 2026-05-28
**Phase:** 26-opendeck-shaped-device-editor
**Areas discussed:** Action library pane · Device-chassis visual ·
Cell sizing + drag interaction · Profile schema + AKP815 deferral
**SPEC.md status:** loaded (5 locked requirements; WHAT/WHY skipped per
workflow)

______________________________________________________________________

## Area A — Action library pane

### A.1 — Library pane placement

| Option                                                               | Description                                                                | Selected |
| -------------------------------------------------------------------- | -------------------------------------------------------------------------- | -------- |
| LEFT — library left, editor centre, Inspector right                  | Mirrors Elgato desktop app; preserves Phase 16 Inspector; L→R reading flow | ✓        |
| RIGHT (OpenDeck convention) — library right, Inspector below/popover | Matches OpenDeck literally; forces Inspector restructure                   |          |
| BELOW — horizontal library strip below the editor                    | Saves vertical space; less conventional                                    |          |

**User's choice:** LEFT.
**Notes:** Three-column layout makes the reading flow obvious + keeps
the Phase 16 Inspector pattern intact. Locked as D-01 in CONTEXT.

### A.2 — Library content for v1

| Option                                       | Description                                                                             | Selected |
| -------------------------------------------- | --------------------------------------------------------------------------------------- | -------- |
| Built-in `ActionKind` tiles only (5 entries) | Smallest v1 surface; matches SPEC out-of-scope (plugin actions deferred to Phase 20/21) | ✓        |
| Built-in + currently loaded plugin actions   | Needs `PluginActionRegistry` exposed to QML; risks Phase 20/21 entanglement             |          |
| Empty placeholder                            | Structural placeholder for v1; defer action sourcing                                    |          |

**User's choice:** Built-in `ActionKind` tiles only.
**Notes:** Plugin actions plug into the same drop-target API later. D-02.

### A.3 — Library tile icon strategy

| Option                                                        | Description                                                           | Selected |
| ------------------------------------------------------------- | --------------------------------------------------------------------- | -------- |
| Material Symbols / Qt icon theme (text + small Material icon) | Zero asset commitment; consistent style; extensible to plugin actions | ✓        |
| Text-only tiles                                               | Simplest; rougher visual; risks not feeling "like Elgato/OpenDeck"    |          |
| Custom SVG icons per ActionKind                               | Highest control; biggest asset cost; not ideal for v1                 |          |

**User's choice:** Material Symbols / Qt icon theme.
**Notes:** D-03. Each `ActionKind` maps to a named Material icon at the QML side.

______________________________________________________________________

## Area B — Device-chassis visual treatment

### B.1 — Chassis visual treatment

| Option                                            | Description                                                    | Selected |
| ------------------------------------------------- | -------------------------------------------------------------- | -------- |
| Outline frame only (Recommended)                  | Rounded-rect silhouette + subtle border; zero asset commitment |          |
| Per-SKU device-photo background (Elgato pattern)  | Best visual fidelity; biggest asset commitment                 | ✓        |
| Pure-geometry abstract grid (OpenDeck literal)    | Simplest; risks not feeling like a device                      |          |
| Per-SKU outline + device name + family logo glyph | Adds discoverability without full photo cost                   |          |

**User's choice:** Per-SKU device-photo background (Elgato pattern).
**Notes:** Significant scope add over the recommended option — surfaced
the implication and asked follow-ups B.2/B.3 to lock asset strategy.
Closes the gap between user's "come Elgato StreamDeck" directive and
OpenDeck's abstract aesthetic by going with Elgato. D-04.

### B.2 — Photo asset sourcing

| Option                                                               | Description                                                              | Selected |
| -------------------------------------------------------------------- | ------------------------------------------------------------------------ | -------- |
| Vendor product-page photos (downloaded → `resources/device-photos/`) | Fastest path; vendor's own marketing imagery; license caveat             | ✓        |
| Custom photography (your own units)                                  | Highest accuracy + zero license ambiguity; limits coverage to owned SKUs |          |
| Procedural rendering (QML Canvas/Shape)                              | Zero assets; loses photo realism                                         |          |
| Mix (vendor photos + procedural fallback)                            | Best of both; complex authoring                                          |          |

**User's choice:** Vendor product-page photos.
**Notes:** D-05. Attribution + license posture documented in
`resources/device-photos/README.md` (new file). Vendor objections route
to outline-frame fallback per D-07.

### B.3 — Cell-position-on-photo coordinates

| Option                                                                  | Description                                                                   | Selected |
| ----------------------------------------------------------------------- | ----------------------------------------------------------------------------- | -------- |
| Per-SKU JSON layout files at `resources/device-layouts/<codename>.json` | Easy to author + iterate; designer-friendly; mirrors OpenDeck plugin pattern  | ✓        |
| DeviceDescriptor C++ extension (`keyPositions[]` etc.)                  | Type-checked; bigger surface; needs recompile per coord tweak                 |          |
| Computed from key dimensions + photo dimensions at runtime              | Algorithmic; only works for grid-aligned SKUs; doesn't scale to AKP05E layout |          |

**User's choice:** Per-SKU JSON layout files.
**Notes:** D-06. JSON schema documented inline in CONTEXT. Hot-editable
without recompile. Plan-phase decides parser: QML XHR vs Q_INVOKABLE
loader.

### B.4 — Fallback for SKUs without a photo yet

| Option                                                           | Description                                            | Selected |
| ---------------------------------------------------------------- | ------------------------------------------------------ | -------- |
| Outline-frame rendering (same QML component, hasPhoto flag)      | Never broken; always renders something useful          | ✓        |
| Empty state with "photo coming in v2" message                    | Honest but feels broken                                |          |
| Hard requirement — build fails if any in-scope SKU lacks a photo | Strongest guarantee; ties v1 ship to asset acquisition |          |

**User's choice:** Outline-frame rendering.
**Notes:** D-07. SPEC acceptance criteria are met as long as in-scope
SKUs have photos; newly added SKUs start outline-frame and gain photos
over time.

______________________________________________________________________

## Area C — Cell sizing + drag-drop interaction

### C.1 — Per-cell pixel size

| Option                                   | Description                                                      | Selected |
| ---------------------------------------- | ---------------------------------------------------------------- | -------- |
| Device-scaled from layout JSON `w`/`h`   | Cells align with photo by construction; sizes vary by SKU        | ✓        |
| Fixed 96×96 (current KeyCell convention) | Inconsistent with photo background                               |          |
| Fixed 144×144 (OpenDeck convention)      | Conflicts with photo background; cells would float off the photo |          |

**User's choice:** Device-scaled from layout JSON.
**Notes:** D-08. Layout JSON already declares per-cell w/h — reuse it.

### C.2 — Drag-drop interaction grammar

| Option                                        | Description                                                  | Selected |
| --------------------------------------------- | ------------------------------------------------------------ | -------- |
| Library → cell only (v1 minimum)              | Smallest atomic interaction; meets SPEC acceptance criterion |          |
| Library → cell + cell-to-cell drag-reorder    | OpenDeck pattern; more feature-complete                      |          |
| Library → cell + cell-to-cell + drag-to-trash | Full grammar                                                 | ✓        |

**User's choice:** Full grammar (library → cell + cell ↔ cell + cell → trash).
**Notes:** Wider than SPEC minimum; explicit user decision in
discuss-phase. D-09. Trash drop zone UI is planner's call (suggested:
icon-button in editor's top-right corner that highlights during drag).

### C.3 — Drop-target visual feedback

| Option                                                     | Description                             | Selected |
| ---------------------------------------------------------- | --------------------------------------- | -------- |
| Subtle highlight border + colour shift on drop-target cell | Standard UX; uses existing Theme.accent |          |
| Drop-target cell scales up 1.05× on hover                  | More playful; risk of layout reflow     | ✓        |
| No visual feedback — cursor change only                    | Subtler; less discoverable              |          |

**User's choice:** Scale-up 1.05× on hover.
**Notes:** D-10. Implemented via QML `transform: Scale { ... }` so
neighbouring cells don't reflow (layout stability across 3×5 grids).
100ms `Behavior on transform.xScale` for animation easing.

______________________________________________________________________

## Area D — Profile schema for touch zones + AKP815 deferral plumbing

### D.1 — Touch-zone bindings storage

| Option                                                                                     | Description                                                                | Selected |
| ------------------------------------------------------------------------------------------ | -------------------------------------------------------------------------- | -------- |
| New `Profile::touchZones` map + on-disk JSON v2 with auto-migrate                          | Clean type signatures; explicit semantic separation; schema bump           | ✓        |
| Extend `Profile::keys` at offsets `keyCount..keyCount+touchZoneCount-1` (OpenDeck literal) | On-disk JSON unchanged; type confusion risk                                |          |
| Extend Binding with `surfaceType` enum discriminator                                       | Single map with self-describing entries; less type-safe than separate maps |          |

**User's choice:** New `Profile::touchZones` map.
**Notes:** D-11 + D-12. JSON `_schemaVersion: 2` with auto-migrate from
v1. Round-trip test under `tests/unit/test_profile_serialiser.cpp`.

### D.2 — AKP815 deferral plumbing

| Option                                                     | Description                                                            | Selected |
| ---------------------------------------------------------- | ---------------------------------------------------------------------- | -------- |
| Sentinel `keyRows = 0` + named allow-list in the test file | Explicit; PR reviewer sees each addition to the allow-list             | ✓        |
| New `DeviceDescriptor::editorScope` enum field             | Type-safe discriminator; bigger surface change                         |          |
| CMake-level exclusion (build flag)                         | Cleanest no-half-state; too aggressive — hides SKU from default builds |          |

**User's choice:** Sentinel + named allow-list in test file.
**Notes:** D-13. `kDeferredLcdSkus = {"akp815"}` in the geometry-check
test; comment cites Phase 26 SPEC out-of-scope.

______________________________________________________________________

## Claude's Discretion

The user explicitly delegated the following to the planner / executor
(documented in CONTEXT `<decisions>` → "Claude's Discretion"):

- **Wave structure for execution** — suggested 6-wave ordering provided
  (Wave 1 = GAP-25A one-liner; Wave 2 = DeviceDescriptor + populate
  rows + geometry test; Wave 3 = Profile schema bump; Wave 4 =
  DeviceView.qml + companions; Wave 5 = per-SKU layout JSONs + photos;
  Wave 6 = Phase 25 UAT re-walk).
- **JSON parser for layout files** — QML `XMLHttpRequest` (zero
  dependency) vs `Q_INVOKABLE DeviceLayoutLoader` (type-checked).
- **Trash drop zone UI** — corner icon-button (suggested) vs dedicated
  drop strip vs drop-outside-cell.
- **Photo asset acquisition workflow** — the actual fetch/process/commit
  workflow for vendor product-page photos.

## Deferred Ideas

- **AKP815 wide-strip editor** — 800×480 single rect-addressable LCD
  strip; Phase 26.x or v2.
- **Plugin-contributed action library tiles** — Phase 20/21 follow-up.
- **Per-SKU touch-strip layouts beyond "4 zones below 4 encoders"** —
  layout JSON already supports arbitrary rects; visual treatment refines
  later.
- **Cell drag-reorder cross-controller** — encoder ↔ key swaps need
  binding-type conversion; deferred.
- **Designer mode for layout JSON authoring** — visual drag-and-place
  editor for layout JSONs; follow-up nice-to-have.
- **Light/dark theme polish on photo backgrounds** — vendor photos
  often clash with dark theme; v1 ships outline-frame fallback.

______________________________________________________________________

*Audit log only — see `26-CONTEXT.md` for decisions consumed by
downstream agents.*
