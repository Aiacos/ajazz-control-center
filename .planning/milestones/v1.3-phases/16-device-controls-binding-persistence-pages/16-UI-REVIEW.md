---
phase: 16
slug: device-controls-binding-persistence-pages
status: advisory
screenshots: none (static-source audit only)
baseline: abstract 6-pillar + codebase conventions
pillar_scores:
  copywriting: 4
  visuals: 3
  color: 4
  typography: 3
  spacing: 4
  experience_design: 3
overall: 21/24
audited: 2026-05-24
---

# Phase 16 — UI Review (Advisory)

**Audited:** 2026-05-24
**Scope:** Phase 16 additions only — brightness Slider + "Clear all keys" button in
`ProfileEditor.qml`; `ProfileController.commitKeyBinding` call in `KeyDesigner.qml`;
Apply/Revert/Restore Defaults wiring in `Main.qml`.
**Baseline:** Existing codebase conventions (`Theme.qml`, `SettingsRow.qml`,
`SecondaryButton.qml`, `PrimaryButton.qml`).
**Screenshots:** Not captured — no dev server; static QML source audit only.
**Status:** ADVISORY — non-blocking findings; no BLOCKERs identified.

______________________________________________________________________

## Pillar Scores

| Pillar               | Score | Key Finding                                                                                                                      |
| -------------------- | ----- | -------------------------------------------------------------------------------------------------------------------------------- |
| 1. Copywriting       | 4/4   | All strings are specific and translated via qsTr; CTAs and descriptions match established patterns                               |
| 2. Visuals           | 3/4   | Brightness slider lacks the value readout chip that SettingsRow's equivalent slider provides                                     |
| 3. Color             | 4/4   | All new elements use Theme tokens exclusively; no hardcoded colors introduced                                                    |
| 4. Typography        | 3/4   | "Brightness" Label uses bare `Theme.fontSm` without `font.weight`; inconsistent with SettingsRow section-label pattern           |
| 5. Spacing           | 4/4   | All new spacing is from Theme scale; controls row uses consistent Theme.spacingMd gap                                            |
| 6. Experience Design | 3/4   | ListModel not seeded from loaded Profile on startup (known follow-up); no confirmation guard on "Restore defaults" (destructive) |

**Overall: 21/24**

______________________________________________________________________

## Top 3 Priority Fixes

1. **Brightness slider has no value readout** — User cannot see the current brightness
   percentage without squinting at slider position. `SettingsRow.qml` (line 303-318)
   solved this identically for the key-response slider: a 36x28 `Rectangle` chip
   containing a `Label { text: Math.round(brightnessSlider.value).toString() }`.
   Add the same pattern to the right of the brightness `Slider` in `ProfileEditor.qml`
   (after line 313). Severity: WARNING.

1. **"Restore defaults" has no confirmation dialog** — `resetActiveProfile()` clears
   all key bindings and immediately saves to disk (irreversible without a backup).
   The codebase has no precedent for confirmation dialogs yet, so this is not a
   regression, but the action meets the standard threshold for a guard (destructive +
   non-undoable). Add a `Dialog` with "Are you sure? This will clear all key bindings
   and save immediately." before calling `root.restoreDefaultsRequested()`.
   Severity: WARNING.

1. **KeyDesigner ListModel not seeded from Profile on app restart** — After
   Apply + restart, the Keys tab renders all cells blank even though bindings exist
   in the saved profile. Users who restart and land on the Keys tab see empty cells
   that do not reflect their saved work, which looks like data loss. This is documented
   in the Phase 16-02 SUMMARY as a follow-up item (Phase 16-03/later). Track it
   explicitly; the UX cost is medium — bindings ARE saved and do repaint to the device,
   but the editor grid does not reflect them. Severity: WARNING.

______________________________________________________________________

## Detailed Findings

### Pillar 1: Copywriting (4/4)

All Phase 16 strings pass. Specific findings:

- `qsTr("Brightness")` — clear, no generic label.
- `qsTr("Panel brightness")` — Accessible.name is more specific than the visible label,
  which is correct practise for screen-reader disambiguation.
- `qsTr("Clear all keys")` — specific, matches the action. `accessibleDescription: qsTr("Blank all LCD keys on the device")` adds correct extra context.
- `qsTr("Restore defaults")` / `qsTr("Revert")` / `qsTr("Apply")` — these are
  pre-existing labels, now wired to real actions; no copywriting regression.
- `ProfileEditor.qml:229/235/240` `accessibleDescription` strings are precise and
  action-oriented. No "OK", "Cancel", "Submit" generic patterns found.

### Pillar 2: Visuals (3/4)

**WARNING — Missing value readout on brightness slider
(`ProfileEditor.qml` ~line 279-313)**

`SettingsRow.qml` lines 303-319 provide a 36x28 px chip displaying
`Math.round(responseSlider.value).toString()` next to the key-response slider.
The new brightness slider (`from:0, to:100`) has no equivalent. The user has no
way to confirm the exact percentage value they've set without interpreting the
slider track position. Since brightness is a 0-100 range (not a 1-5 ordinal scale),
a numeric readout is more important here, not less.

No other visual issues in Phase 16 additions:

- "Clear all keys" SecondaryButton correctly placed to the right of the slider row
  (logical grouping of live-device controls).
- Footer layout (pre-existing 56 px bar) is unchanged and consistent.
- No icon-only buttons introduced; all buttons have visible text.

### Pillar 3: Color (4/4)

No regressions. All color references in Phase 16 additions:

- `ProfileEditor.qml:275` — `color: Theme.fgFaint` on the "Brightness" Label.
  Consistent with `SettingsRow.qml` section-label pattern (lines 155, 201, 247, 267,
  284 all use `Theme.fgFaint`).
- No hardcoded hex literals introduced in Phase 16 QML changes.
- No accent overuse — the new row uses neutral tokens only.
- `Main.qml` Phase 16 changes add no color tokens; three signal handler bindings only.

### Pillar 4: Typography (3/4)

**WARNING — "Brightness" Label uses legacy scalar token without weight
(`ProfileEditor.qml:274-276`)**

```
Label {
    text: qsTr("Brightness")
    color: Theme.fgFaint
    Layout.alignment: Qt.AlignVCenter
}
```

There is no `font.pixelSize` set explicitly on this `Label`. It will inherit the
default Qt font size (system-dependent) rather than being pinned to `Theme.fontSm`.
`SettingsRow.qml` section labels at lines 153, 198, 245, 264, 282 all set
`color: Theme.fgFaint` but also omit explicit `font.pixelSize` — so the pattern is
consistent with the rest of the codebase, and the visual output is likely the same as
siblings. However, compared with `SettingsPage.qml` which uses the full M3 `typeTitleMedium`
token object (pixelSize + weight + letterSpacing), the brightness label and its SettingsRow
siblings are using a thinner convention (color only). This is a pre-existing codebase-wide
minor inconsistency rather than a Phase 16 regression; flagging for awareness.

No new font-size or font-weight values introduced in Phase 16 that are outside the
existing theme scale.

### Pillar 5: Spacing (4/4)

Phase 16 additions are fully token-compliant:

- Controls row: `spacing: Theme.spacingMd` (12 px) — matches sibling rows.
- Outer `ColumnLayout` inside `keyDesignerComp`: `spacing: Theme.spacingSm` (8 px).
- No arbitrary `[Npx]` or raw integer spacing values introduced.
- Footer height of 56 px (`Layout.preferredHeight: 56`, `ProfileEditor.qml:216`) is
  pre-existing (introduced in commit 685a4a2) and not in Phase 16 scope.

### Pillar 6: Experience Design (3/4)

**WARNING 1 — "Restore defaults" lacks confirmation guard**

`ProfileEditor.qml:227-230` wires `onClicked: root.restoreDefaultsRequested()` directly.
`Main.qml:147` routes this to `ProfileController.resetActiveProfile()` which clears all
key bindings and saves immediately. The operation is irreversible from the UI (no undo, no
backup-before-reset). No `Dialog` component exists anywhere in the QML codebase yet, so
this is not a Phase 16 regression — but the destructive-action guardrail is absent.

**WARNING 2 — KeyDesigner ListModel not populated from Profile on load**

`KeyDesigner._ensureBindings()` (lines 50-68) grows/shrinks the ListModel to match
`keyCount` but only appends empty rows (`iconSource:"", label:"", ...`). There is no path
from `ProfileController.profileChanged` or `ProfileController.loadActiveProfile()` into
`KeyDesigner.bindings`. After a restart where `loadActiveProfile` fires during
`ProfileEditor` initialization, the device gets correct repaint (bindings → device via
`repaintFromProfile`), but the editor grid shows blank cells. Explicitly noted as a
follow-up in Phase 16-02 SUMMARY ("KeyDesigner populates bindings from loaded Profile on
init — Phase 16-03/later"). Track with `WARNING` until resolved.

**Passing items:**

- Brightness slider is correctly gated: `visible: root._showKeys && root.codename !== ""`
  prevents it from appearing on non-LCD devices.
- "Clear all keys" `enabled` guard matches the `visible` gate redundantly
  (`enabled: root.codename !== "" && root._showKeys`) — belt-and-suspenders, no issue.
- Debounce Timer (`interval: 80, repeat: false`) plus the `onPressedChanged(!pressed)`
  final-write cover the two UX states (drag-in-progress and drag-complete) correctly;
  no intermediate-value ghost state.
- `Main.qml` Connections blocks surface `profileSaved`, `saveFailed`, and `loadFailed`
  as toasts — adequate feedback for Apply/Revert outcomes.
- `Accessible.role: Accessible.Slider` + `Accessible.name` on the brightness slider
  satisfies the basic AT-SPI contract. `Accessible.valueText` is absent (same omission
  as the pre-existing responseSlider in SettingsRow), so a screen reader will announce
  the numeric value from the Qt default binding — acceptable but not ideal.

______________________________________________________________________

## Qt Gotcha Compliance (CLAUDE.md)

- `QML_SINGLETON` pattern: Phase 16-01 added the full `create()/registerInstance()/ static_assert(!is_default_constructible_v<StreamDockControlService>)` machinery,
  mirroring `LightingService`. The CLAUDE.md gotcha ("bare QML_SINGLETON macro silently
  creates a second instance") is explicitly avoided. PASS.
- Material attached properties in Popups: Phase 16 adds no new `Popup`/`Drawer`/`Dialog`
  elements. All three existing Drawers in `Main.qml` already carry explicit
  `Material.theme`, `Material.accent`, `Material.primary` inside the Drawer body.
  No regression introduced. PASS.
- `MultiEffect.maskSource`: no MultiEffect usage in Phase 16 additions. PASS.

______________________________________________________________________

## Registry Safety

No `components.json` present. shadcn not initialized. Audit skipped.

______________________________________________________________________

## Files Audited

- `src/app/qml/ProfileEditor.qml` — full file; Phase 16 additions: lines 253-323 (keyDesignerComp expansion)
- `src/app/qml/KeyDesigner.qml` — full file; Phase 16 addition: lines 91-107 (commitKeyBinding call)
- `src/app/qml/Main.qml` — full file; Phase 16 additions: lines 138-147 (signal handler wiring)
- `src/app/qml/Theme.qml` — reference for token definitions
- `src/app/qml/components/SecondaryButton.qml` — reference for button conventions
- `src/app/qml/components/PrimaryButton.qml` — reference for button conventions
- `src/app/qml/SettingsRow.qml` — reference for Slider + section-label patterns
- `src/app/qml/SettingsPage.qml` — reference for typography token usage
- `src/app/qml/Inspector.qml` — reference for form field and state handling patterns
