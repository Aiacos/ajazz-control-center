---
quick_id: 260514-1je
slug: stream-dock-keydesigner
date: 2026-05-14
status: complete
---

# Quick Task 260514-1je: Stream Dock features research + KeyDesigner slice — Summary

**Goal:** Research the AJAZZ Stream Dock product family + implement the smallest coherent KeyDesigner slice that makes the per-key editor real.

## What landed

### Research (3700-word FINDINGS.md)

Six-section report at `260514-1je-FINDINGS.md` covering: (1) per-device feature matrix for AKP153/AKP153E/AKP03/AKP05/0x3004; (2) canonical Stream-Deck-class feature taxonomy with T1-T4 cost tiers; (3) gap analysis vs current codebase with file:line citations — **headline: the data model is largely correct, the gap is overwhelmingly UI**; (4) the AKP153-specific KeyDesigner slice with component tree + data flow diagram + ~330 LoC budget + explicit defer list; (5) eight D1-D8 design decisions with recommendations; (6) 30+ sources (OpenDeck, Elgato SDK, mirajazz, reverse-eng writeups).

### Implementation (pure QML, ~280 LoC delta)

| File                                 | Change                                                                                                                                                                                                                                                                                                                                                       | Net LoC                           |
| ------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | --------------------------------- |
| `src/app/qml/components/KeyCell.qml` | Text-only tile → Image + overlay label. `index`/`iconSource`/`label` promoted to `required` at type level so the Repeater's role auto-binding works (the previous instance-level-redeclare hit a default-shadow trap and rendered all cells as "1").                                                                                                         | +35                               |
| `src/app/qml/Inspector.qml`          | Live-bound shape: takes `binding` prop, emits `bindingFieldChanged(field, value)` on every TextField / ComboBox edit. Added `Icon` row with `FileDialog` (filesystem-only per FINDINGS D1). Removed dead Apply-button surface. Action-type ComboBox maps the user-meaningful subset of `ActionKind` (Plugin / KeyPress / RunCommand / OpenUrl / OpenFolder). | +35                               |
| `src/app/qml/KeyDesigner.qml`        | Rewrite. Internal `bindings` `ListModel` (one row per key, roles `iconSource`/`label`/`actionKind`/`actionParams`), grows/shrinks to match `keyCount`. RowLayout: centred GridView on left, embedded `Inspector` (320 px) on right. `updateSelectedBinding(field, value)` is the single mutation entry point.                                                | +85 net (rewrite of 57-line stub) |
| `src/app/qml/Main.qml`               | Removed the top-level placeholder `Inspector` from the main RowLayout (it now lives inside KeyDesigner per FINDINGS §4). ProfileEditor absorbs the 320 px column.                                                                                                                                                                                            | -8                                |

### Decisions applied (from FINDINGS §5)

- **D1**: Filesystem icon picker, no qrc library bundled. ✓
- **D2**: Single-state `Binding::state`; defer multi-state schema bump. ✓
- **D3**: Deferred multi-action editor (chain is still in the schema). ✓
- **D6**: BrightnessRow **deferred** (a dead slider would be worse than no slider — needs `setBrightness` C++ which is its own slice). ✓
- **D7**: Live-binding shape (no Apply button) inside the per-key Inspector. ✓
- **D8**: Send-to-device debounce moot until C++ `setKeyImage` lands. ✓

### Build + smoke test

- `cmake --build build/linux-debug --target ajazz-control-center` — clean.
- qmllint clean on the new files (one unused-import auto-fixed; the pre-existing `Theme.materialTheme` / `fgSecondary` / `bgRow` warnings in `LoadedPluginsPage.qml` are out of scope).
- Manual smoke test: temporary auto-select of the connected Stream Dock 0x3004 → 6-cell grid renders with cells labelled 1..6 (after the required-property fix), Inspector shows "Nothing selected" until a cell is clicked, Encoders tab visible (AKP03 has 1 encoder), Apply/Revert/Restore footer intact. Temp auto-select patch reverted before commit.

## What's deferred

Per FINDINGS §4 explicit defer list — these all become follow-up quick tasks:

- **C++ `setBrightness` + BrightnessRow** — the obvious next slice, ~20 LoC C++ + ~30 LoC QML.
- **C++ `setKeyImage(index, QUrl)`** — JPEG-encode at 85×85 + push to LCD with 250 ms debounce. ~50 LoC.
- **Profile persistence** — wire the QML `bindings` ListModel to `ProfileController.saveProfile()`. Cross-session keep.
- **Multi-action chain editor** — inline list-of-rows inside Inspector. ~200 LoC.
- **Folders / page navigation** — needs page-stack + auto-injected "back" key.
- **Per-state icons** (toggles) — requires schema bump (D2 Option A).
- **EncoderPanel buildout** (AKP03/AKP05) — separate quick task with similar shape.
- **Touch-strip UI** (AKP05) — own slice.

## What this resolves (from FINDINGS §3 gap analysis)

| FINDINGS gap                                  | Status                                                    |
| --------------------------------------------- | --------------------------------------------------------- |
| KeyCell renders text-only, no live preview    | ✅ Image + overlay label live-previews binding state      |
| No icon picker in Inspector                   | ✅ `FileDialog` for any image format                      |
| KeyDesigner is a 57-line stub                 | ✅ Real editor: grid + embedded Inspector + binding store |
| Per-key form not live                         | ✅ Edits propagate instantly into the cell preview        |
| Inspector placeholder shows "Key 1" hardcoded | ✅ Tracks actual selected cell index                      |

## Verification

```bash
wc -l src/app/qml/KeyDesigner.qml src/app/qml/Inspector.qml src/app/qml/components/KeyCell.qml
# KeyDesigner ~145, Inspector ~170, KeyCell ~75

cmake --build build/linux-debug --target ajazz-control-center
# clean

grep "SCREENSHOT-TEMP" src/app/qml/Main.qml
# (no match — temp patch reverted)
```

## Routing

Out of v1.1 milestone scope (still focused on hot-plug, time-sync, carry-overs). Tracked as quick task. This is the **first** of an anticipated series of UI-completeness quick tasks; FINDINGS §4 deferred list is the source-of-truth for what follows.
