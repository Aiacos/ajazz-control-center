---
phase: 26
slug: opendeck-shaped-device-editor
audited: 2026-05-28
auditor: gsd-ui-auditor (claude-sonnet-4-6)
baseline: 26-UI-SPEC.md (approved 2026-05-28)
screenshots: not captured (no dev server detected)
overall_score: 16
pillar_scores:
  copywriting: 2
  visuals: 2
  color: 3
  typography: 2
  spacing: 3
  experience_design: 2
registry_audit: not applicable (no components.json, no third-party registries)
---

# Phase 26 — UI Review

**Audited:** 2026-05-28
**Baseline:** 26-UI-SPEC.md (approved 2026-05-28, 13 locked decisions D-01..D-13)
**Screenshots:** not captured (no dev server at localhost:3000/5173/8080 — code-only audit)

______________________________________________________________________

## Pillar Scores

| Pillar               | Score | Key Finding                                                                                                   |
| -------------------- | ----- | ------------------------------------------------------------------------------------------------------------- |
| 1. Copywriting       | 2/4   | Five UI-SPEC copy strings not implemented; two accessibility strings contract-mismatched                      |
| 2. Visuals           | 2/4   | Photo background path computed but not rendered; drag-reject visual state absent; trash 1.1x scale missing    |
| 3. Color             | 3/4   | One hardcoded hex (`#000000` in KeyCell); all layout tokens used correctly elsewhere                          |
| 4. Typography        | 2/4   | Four hardcoded `font.pixelSize` integers; `Font.DemiBold` and `font.bold` violate the 2-weight spec           |
| 5. Spacing           | 3/4   | `anchors.margins: 6` in KeyCell is off-scale; cell default sizes (96/120px) not token-backed                  |
| 6. Experience Design | 2/4   | Drag-reject gives misleading scale-up feedback; encoder swap destroys data; Delete key promised but not wired |

**Overall: 16/24**

______________________________________________________________________

## Top 3 Priority Fixes

1. **Drag-reject visual state is absent — users receive false accept feedback** — When a user drags an Encoder cell over a KeyCell (or vice versa), `onEntered` fires `cellScale.xScale = 1.05` and `drag.accepted = true` on all three cell types before the controller is checked. Cross-controller rejection only fires in `onDropped` after the user releases. The UI-SPEC mandates a distinct reject state (`Theme.errorAccent` dashed border, no scale, `Qt.IgnoreAction` cursor) shown *during* hover, not after drop. Fix: read the controller from `drag.getDataAsString("application/x-ajazz-binding")` in `onEntered`, set `drag.accepted = false` and apply the dashed-border reject state immediately when the controller mismatches. This requires a per-cell `property bool _dragWillReject` to drive the border color.

1. **Encoder cell-to-cell swap destroys both bindings** — `DeviceView.qml:309-313` `onEncoderSwapRequested` calls `commitEncoderBinding(src,"","",0,"")` and `commitEncoderBinding(dst,"","",0,"")`. This wipes *both* cells with empty data rather than exchanging them. A user who drags encoder A onto encoder B loses both bindings immediately with no undo path except the Revert footer (which only works if they haven't applied). The UI-SPEC contract says "If target occupied: SWAP bindings". Fix: cache the source binding data *before* the first commit and pass it as the destination's new binding in the second call — same pattern as the working `onCellSwapRequested` in KeyCell/DeviceView.

1. **Photo background never renders despite 17 layout JSONs and product photos being present** — `DeviceView.qml:158-163` computes `_hasPhoto` correctly (layout JSONs load, `photo` field is populated) but no `Image` element in the component ever reads `_hasPhoto` or `_layout.photo`. The spec's D-04 "Elgato pattern" visual is the primary v1 treatment; the outline-frame is the fallback. With all 17 JSONs and product photos in tree, the photo path should now engage. Fix: add an `Image { source: "qrc:/qt/qml/AjazzControlCenter/devices/products/" + root._layout.photo; visible: root._hasPhoto; fillMode: Image.PreserveAspectFit }` layer inside `chassisArea` and conditionally hide `outlineFrame` when `_hasPhoto` is true. Cell overlays need to scale from viewBox coordinates to the rendered image size.

______________________________________________________________________

## Detailed Findings

### Pillar 1: Copywriting (2/4)

**WARNING — Multiple UI-SPEC copy strings not implemented.**

The UI-SPEC §Copywriting Contract defines 11 specific copy strings. The following diverge or are absent from the code:

**MISMATCH 1 — No-device empty state heading (ProfileEditor.qml:86)**

- SPEC: `"Select a device to start configuring"`
- ACTUAL: `qsTr("Select a device on the left")` (PageHeader) + `qsTr("No device selected")` (EmptyState title)
- Neither matches the spec. The intent is the same but the wording differs.

**MISMATCH 2 — No-device empty state body (ProfileEditor.qml:139)**

- SPEC: `"Choose a device from the sidebar, then drag actions onto its keys, dials, and touch zones."`
- ACTUAL: `qsTr("Pick a device from the sidebar to see its keys, encoders, RGB, and pointer settings.")`
- The spec body is drag-centric; the actual body is feature-inventory. Different user intent framing.

**MISMATCH 3 — Inspector empty state (Inspector.qml:138-139)**

- SPEC: `"Select a key, dial, or touch zone to configure it."` (body)
- ACTUAL title: `qsTr("Nothing selected")`, body: `qsTr("Click a key on the left to configure its action, label, and icon.")`
- Body says "click a key" only — omits dials and touch zones. Incorrect after Phase 26 adds encoder and touch-zone selection.

**MISSING 4 — "Built-in" section label not implemented in ActionLibraryPane**

- SPEC: Library section heading "Built-in" above the tile list.
- ACTUAL: No section heading. The `Actions` header renders but the per-section `Built-in` label is absent. ListView has no section grouping.

**MISSING 5 — "Drag to a key" CTA drag-hint copy not rendered**

- SPEC: CTA (drag hint in library): `"Drag to a key"` somewhere in the library.
- ACTUAL: No such text exists in ActionLibraryPane. The only copy is the tile labels and Accessible.description.

**MISSING 6 — "Layout file not found. Using default grid." error copy**

- SPEC: inline caption shown when layout JSON is missing.
- ACTUAL: JSON-load failure silently falls back to outline-frame with no user-visible copy. `_layout` stays null and the EmptyState or outline-frame renders without explanation.

**PARTIAL — Trash zone tooltip**

- SPEC: `tooltipText: qsTr("Clear binding")` on the trash button.
- ACTUAL: `Accessible.name` carries `qsTr("Clear binding")` (screen-reader only) but no `ToolTip` component is wired. Sighted users hovering the trash button get no tooltip.

**PASS — qsTr() coverage**: All user-visible strings in the 5 audited QML files use `qsTr()`. Zero non-translated literals in interactive elements.

**PASS — D-09 trash label**: Trash button uses `Accessible.name: qsTr("Clear binding")` matching the SPEC's prescribed Accessible.name.

**PASS — Icon-only ActionLibraryPane tiles**: `Accessible.description` on each tile is present and uses the correct `"Drag to assign %1 to a key"` pattern matching SPEC Accessibility table row 1.

______________________________________________________________________

### Pillar 2: Visuals (2/4)

**WARNING — Photo background absent; drag-reject state absent; trash button scale missing.**

**BLOCKER — Photo background not rendered (DeviceView.qml:158-163)**
The `_hasPhoto` property is correctly computed from the loaded `_layout`. All 17 layout JSONs are committed (Plan 26-06), all referenced `product-*.png` files exist in `resources/devices/products/`. However, no `Image {}` element in `DeviceView.qml` ever reads `_hasPhoto` or `_layout.photo`. The file itself documents this on line 161: *"\_hasPhoto is computed but not yet consumed by any visual element."* The outline-frame fallback is the only visual for all SKUs including AKP05E, which has both a layout JSON and a product photo. D-04 designates the photo background as the **primary** v1 visual; outline-frame is the fallback for missing assets. Currently the two are effectively swapped for all 17 in-scope SKUs.

**WARNING — Drag-reject state gives wrong visual feedback (all three cell types)**
All three DropArea `onEntered` handlers (KeyCell.qml:159-163, EncoderDial.qml:134-138, TouchStripLane.qml:210-214) unconditionally apply the 1.05x scale-up and set `drag.accepted = true` regardless of whether the drag source is a compatible controller. Cross-controller rejection is evaluated only in `onDropped`. The UI-SPEC Component States table is unambiguous: drag-over (reject) requires `Theme.errorAccent` dashed border, no scale, and `Qt.IgnoreAction` cursor — all applied when the cursor *enters* the cell. Currently a user dragging an encoder binding over a key cell sees the acceptance animation (scale-up, accent border) then the drop silently fails. This is confusing and misleading.

**WARNING — Trash button missing 1.1x scale animation on drag-over (DeviceView.qml:350-415)**
UI-SPEC §Micro-Interactions defines: *"During drag-over the trash zone: icon pulses to Theme.errorAccent fill, background becomes Theme.chipBgError, scale 1.1× same 100ms Easing.OutQuad."* The background color change on `containsDrag` is implemented (line 377). The scale transform and icon-color-change are absent — no `transform: Scale` block exists on `trashBtn`. The button only gains opacity and background color, not the 1.1x pulse prescribed.

**WARNING — Outline-frame fallback missing drop-shadow (DeviceView.qml:197-204)**
UI-SPEC §DeviceView.qml specifies: `layer.effect: MultiEffect { shadowEnabled: true; ... }` matching `Theme.elevation2` on the outline-frame Rectangle. The `outlineFrame` Rectangle has no `layer.enabled: true` and no `MultiEffect` child. The CLAUDE.md Qt 6 / QML gotcha explicitly cautions about `MultiEffect.maskSource` requiring an Item wrap, but the shadow is simply absent — not mis-wired.

**PASS — Three-column layout**: ActionLibraryPane (240px) + chassis (fillWidth) + Inspector (280px) correctly implemented in `RowLayout` with `spacing: Theme.spacingXl`. Column structure matches D-01.

**PASS — Three-stacked-rows chassis**: Key grid / encoder row / touch-strip row visible-gated on `keyCount`, `encoderCount`, `touchZoneCount`. OpenDeck pattern correctly ported.

**PASS — 1.05x scale-up animation**: All three cell types implement `transform: Scale { Behavior on xScale/yScale { NumberAnimation { duration: 100; easing.type: Easing.OutQuad } } }` matching D-10 exactly. Scale origin correctly set to `width/2 × height/2` to prevent positional drift.

**PASS — Focal point**: Chassis area fills remaining width and is visually dominant. ActionLibraryPane and Inspector are narrower, creating correct reading-flow hierarchy.

______________________________________________________________________

### Pillar 3: Color (3/4)

**WARNING — One hardcoded hex literal. Otherwise excellent token compliance.**

**FINDING — `KeyCell.qml:116` uses `styleColor: "#000000"`**
The text-shadow color for icon-overlaid labels is hardcoded to `"#000000"` (pure black). This is not a Theme token. The correct token would be `Theme.fgOnAccent` or a dark equivalent. While functionally identical to the intended effect (dark shadow for readability on bright icons), it bypasses the theming system and would fail to adapt if a light-theme variant were added.

**PASS — 60/30/10 split**: ActionLibraryPane uses `Theme.bgBase` (dominant surface, 60%), outline-frame chassis uses `Theme.tile` (secondary surface, 30%), accent used only on focus rings, selected cell borders, drop-accept indicator, and the LED-strip top border on TouchStripLane — consistent with the 10% accent budget.

**PASS — errorAccent scoped correctly**: `Theme.errorAccent` appears only on the trash button icon (DeviceView.qml:370) — exactly where the spec restricts it. No misuse on info or success paths.

**PASS — EncoderDial permanent accent border**: The 2px `Theme.accent` border on EncoderDial idle state (line 59-60) is documented as the pre-existing `EncoderCard.qml` pattern explicitly permitted by the UI-SPEC. It is not an overreach.

**PASS — No other hardcoded hex/rgb**: Only `#000000` in KeyCell. All other color references use `Theme.*` tokens or `Qt.rgba(Theme.fgPrimary.r, ...)` alpha-derivation patterns.

**PASS — `Theme.chipBgError` on trash containsDrag**: Correctly applied for the trash active background (DeviceView.qml:378).

______________________________________________________________________

### Pillar 4: Typography (2/4)

**WARNING — Four hardcoded icon-pixel sizes and two weight violations outside the 2-weight spec.**

**FINDING 1 — `Font.DemiBold` in KeyCell.qml:107**
`font.weight: root.label !== "" ? Font.DemiBold : Font.Normal`
`Font.DemiBold` (600) is not in the UI-SPEC's declared 2-weight system (Font.Normal 400 + Font.Medium 500). The label overlay for occupied cells uses a third weight. Correct token: `Font.Medium` (matches `Theme.typeLabelMedium.weight` used by EncoderDial and TouchStripLane for the same label context).

**FINDING 2 — `font.bold: true` in ProfileEditor.qml:115**
The device model name in the ProfileEditor header uses `font.bold: true` (700). This is outside the 2-weight system. Correct token: `Font.Medium` or use `Theme.typeTitleSmall` for the header label.

**FINDING 3 — Hardcoded icon pixel sizes in 4 locations**

- `ActionLibraryPane.qml:141`: `font.pixelSize: 20` (Material Symbols icon)
- `DeviceView.qml:368`: `font.pixelSize: 22` (trash delete icon)
- `EncoderDial.qml:82`: `font.pixelSize: 28` (tune glyph)
- `TouchStripLane.qml:148`: `font.pixelSize: 20` (view_column glyph)

These are icon sizes, not body copy, so the 4-role typography table does not prescribe exact values for them. However, they are raw integers not bound to any Theme token, which breaks future-proofing if the display density or design system changes. The spec says "Do not introduce sizes outside this table" for *text* roles; icon sizes are a grey area. Flagged as warning rather than blocker, but they should use token aliases (e.g., `Theme.iconSizeMd`) if one is introduced.

**FINDING 4 — EmptyState component uses `Theme.fontLg` (18px) not `Theme.typeHeadlineMedium` (28px)**
`EmptyState.qml` renders its `title` with `font.pixelSize: Theme.fontLg` (18px). The UI-SPEC designates `Theme.typeHeadlineMedium` (28px / 400) for the DeviceView empty-state hero heading. EmptyState is a shared component, so changing it affects other panels; but the DeviceView no-geometry empty state would benefit from the larger hero treatment.

**PASS — Token coverage for body copy**: ActionLibraryPane tile labels use `Theme.typeBodyMedium`, EncoderDial and TouchStripLane labels use `Theme.typeLabelMedium`, ActionLibraryPane header uses `Theme.typeTitleSmall`. These three roles match the UI-SPEC typography table exactly.

______________________________________________________________________

### Pillar 5: Spacing (3/4)

**WARNING — Two off-scale values. Otherwise consistent Theme token use.**

**FINDING 1 — `KeyCell.qml:83` `anchors.margins: 6` is off the 4-point scale**
The inner content item uses `anchors.margins: 6`. The project's 4-point scale is 4/8/12/16/24 (`Theme.spacingXs` through `Theme.spacingXl`). 6px falls between `spacingXs` (4) and `spacingSm` (8). Correct value for this context (tight inner padding): `Theme.spacingXs` (4px) or `Theme.spacingSm` (8px) — 8px is more comfortable for a 96×96 cell interior.

**FINDING 2 — Hardcoded component sizes not backed by tokens**

- `KeyCell.qml:53-54`: `width: 96; height: 96` — correct default per the spec's "fixed 96×96 cells", but not a Theme token.
- `EncoderDial.qml:38`: `width: 120; height: 120` — correct default; not a Theme token.
- `ActionLibraryPane.qml:95`: tile `height: 48` — matches the "comfortable list density" spec note; not a Token.
- These are dimension constants for specific UI components. The UI-SPEC notes that D-08 cell sizes come from layout JSON `w/h` fields, not fixed values. For the outline-frame fallback these hardcoded defaults are the current implementation. They should ideally be `readonly property int` constants at the top of each file so they can be traced and adjusted together, but this is a maintainability advisory, not a visual violation.

**FINDING 3 — EncoderDial glyph vertical positioning uses proportional offset (EncoderDial.qml:80)**
`anchors.verticalCenterOffset: parent.height * 0.35` — a computed fractional anchor for the "tune" glyph center. This bypasses the spacing system entirely. The intent is to vertically position the icon inside the circular cell. A fixed `anchors.centerIn: parent` with a vertical nudge via `Theme.spacingXs` would be cleaner, or simply `anchors.centerIn: parent` with `anchors.verticalCenterOffset: -parent.height * 0.1` if room for the label is needed.

**PASS — All inter-component spacing**: `Theme.spacingXl` between the three main columns (DeviceView:168), `Theme.spacingLg` between chassis rows (DeviceView:217), `Theme.spacingMd` between key rows (DeviceView:224), `Theme.spacingXs` between touch-zone cells (TouchStripLane:47). All match the UI-SPEC §Spacing Scale table exactly.

**PASS — `Theme.minTouchTarget` (44px)**: TouchStripLane zone cells use `Math.max(Theme.minTouchTarget, 120)` and `Math.max(Theme.minTouchTarget, 48)` (always satisfied since 120 > 44). KeyCell is 96×96 (satisfies minTouchTarget). EncoderDial is 120×120 (satisfies minTouchTarget). No interactive cell is below the 44px floor.

______________________________________________________________________

### Pillar 6: Experience Design (2/4)

**WARNING — Misleading drag feedback, data-destroying encoder swap, promised keyboard shortcut absent.**

**BLOCKER — Drag-reject gives wrong feedback to user**
As detailed in Pillar 2: all three cell types show scale-up acceptance animation on `onEntered` for ALL drag events including cross-controller ones. The rejection only fires on `onDropped`. From the user's perspective: they hover over an incompatible cell and see an inviting glow/scale (implying "drop here"), release, and nothing happens. There is no dashed red-border "not allowed" state during the hover phase. This is the most visible interaction defect in the editor.

**BLOCKER — Encoder cell-to-cell swap destroys both bindings (DeviceView.qml:307-313)**
`onEncoderSwapRequested` commits empty strings to *both* source and destination positions:

```qml
ProfileController.commitEncoderBinding(src, "", "", 0, "");
ProfileController.commitEncoderBinding(dst, "", "", 0, "");
```

A user dragging encoder 1 binding onto encoder 2 loses both bindings silently. The swap semantics in the UI-SPEC contract require reading the source binding before clearing it, then writing it to the destination (same pattern as the working `onCellSwapRequested` in DeviceView which correctly reads from `bindings.get(src)` before overwriting). The encoder swap has no equivalent read-before-overwrite logic because the encoder model is not backed by a ListModel the same way (it's driven by a bare `model: root.encoderCount` Repeater). Fix requires either maintaining a per-encoder binding state in DeviceView (ListModel or JS object) or reading back from ProfileController.

**WARNING — Delete/Backspace key handler promised but not wired**
`KeyCell.qml:209`: `Accessible.description: qsTr("Press Space to select, Delete to clear. Drag to move binding.")` — this string promises the Delete key clears the binding. No `Keys.onDeletePressed` or `Keys.onBackspacePressed` handler exists anywhere in KeyCell.qml or DeviceView.qml. The SPEC §Keyboard Navigation table prescribes this behavior. Users relying on keyboard-only navigation will find the promised Delete shortcut does nothing.

**WARNING — Arrow-key grid navigation explicitly deferred (DeviceView.qml:37)**
Comment: *"Full grid arrow-key navigation is a follow-up (arrow keys within chassis deferred)."* The UI-SPEC §Keyboard Navigation table prescribes Up/Down/Left/Right within the chassis. The spec also prescribes Home/End per row and Escape for deselect. None of these are implemented. Only Tab/Shift-Tab through the whole editor works. This is acknowledged as a v1 deferral; flagging here for the record.

**WARNING — `_activeDragCount` underflow (WR-02 from code review — partially fixed)**
The `Math.max(0, _activeDragCount - 1)` clamp (DeviceView.qml:241, 297, 337) prevents going negative but does not reset on geometry changes (device switch mid-drag). The code review (26-REVIEW.md WR-01) documents the residual risk. The partial fix is present; the zero-reset on `onKeyCountChanged` / `onEncoderCountChanged` is not.

**WARNING — QML test CR-01: inverted assertion partially fixed but weakened**
`test_device_view_drag_drop.qml:76-91` was updated after the code review to remove the inverted `count == before` assertion. However the replacement assertion `verify(profileChangedSpy.count >= 0, ...)` is trivially true (a count is always >= 0). The function only verifies the method does not throw, not that it emits `profileChanged`. The companion C++ test in `test_device_view_tests.cpp` does verify `fired == 1` correctly, so coverage exists at the C++ level — but the QML test is effectively a no-op assertion now (IN-02/IN-03 from 26-REVIEW.md are unresolved stubs).

**PASS — Loading/error states**: `loadLayout()` uses async XHR with `try/catch` around `JSON.parse`. Load failure silently falls back to outline-frame as spec requires. No broken-image icons possible.

**PASS — Empty state**: DeviceView EmptyState triggers on `keyCount === 0 && encoderCount === 0 && touchZoneCount === 0`. ProfileEditor EmptyState triggers on `codename === ""`. Both empty states are correctly scoped and visible.

**PASS — Drag-drop library→cell**: Library-to-cell drops work correctly for all three controller types: `commitKeyBinding` (KeyCell), `commitEncoderBinding` (EncoderDial), `commitTouchZoneBinding` (TouchStripLane). All emit `profileChanged`.

**PASS — Trash zone opacity reveal**: Opacity 0.3→1.0 on `anyDragActive` with 150ms `Behavior` using `Theme.durationShort` + `Theme.easingStandard`. Matches UI-SPEC micro-interactions exactly (duration and easing token correct). Only the 1.1x scale pulse is missing.

**PASS — Cross-controller drag rejected at drop level**: While the onEntered visual is wrong, the actual drop is correctly rejected (`drop.accepted = false`) for cross-controller pairs in all three cell types. No data corruption from rejected cross-controller drops.

**PASS — Cell selection state and Inspector wiring**: `selectedKeyIndex` / `selectedEncoderIndex` / `selectedZoneIndex` mutually exclusive. CR-03 fix (zone click → `onZoneTapped` → `zoneSelected`) confirmed present. Inspector receives correct `selectionLabel`.

______________________________________________________________________

## Registry Safety

Registry audit: not applicable. `components.json` absent (no shadcn). UI-SPEC §Registry Safety lists only platform-bundled Qt Quick Controls 2 and Material Symbols icon names — no third-party code registries.

______________________________________________________________________

## Files Audited

| File                                                              | Role                                               |
| ----------------------------------------------------------------- | -------------------------------------------------- |
| `src/app/qml/DeviceView.qml`                                      | Top-level three-column editor                      |
| `src/app/qml/ActionLibraryPane.qml`                               | Left drag-source panel                             |
| `src/app/qml/components/EncoderDial.qml`                          | Encoder cell delegate                              |
| `src/app/qml/components/TouchStripLane.qml`                       | Touch-zone lane + cell delegate                    |
| `src/app/qml/components/KeyCell.qml`                              | LCD-key cell delegate                              |
| `src/app/qml/ProfileEditor.qml`                                   | Tab container + empty states                       |
| `src/app/qml/components/EmptyState.qml`                           | Shared empty-state component                       |
| `src/app/qml/Inspector.qml`                                       | Right-pane binding editor (empty state check only) |
| `src/app/qml/Theme.qml`                                           | Token verification                                 |
| `tests/qml/test_device_view_drag_drop.qml`                        | QML test CR-01 verification                        |
| `resources/device-layouts/akp05e.json`                            | Layout JSON structure verification                 |
| `resources/devices/products/`                                     | Product photo asset availability check             |
| `.planning/phases/26-opendeck-shaped-device-editor/26-UI-SPEC.md` | Design contract baseline                           |
| `.planning/phases/26-opendeck-shaped-device-editor/26-REVIEW.md`  | Code review context (iteration 2)                  |
| Plans 26-01 through 26-07 SUMMARY files                           | Implementation context                             |

______________________________________________________________________

*Phase: 26-opendeck-shaped-device-editor*
*Review date: 2026-05-28*
*Reviewer: gsd-ui-auditor (claude-sonnet-4-6)*
*Method: code-only audit (no live server)*
