---
phase: 28-akp05-plugin-action-completeness-drag-to-bind-on-keys-dials
plan: '01'
subsystem: plugin-manifest
tags: [plugin, manifest, affordance, data-model, tdd]
dependency_graph:
  requires: []
  provides:
    - PluginEncoderBlock struct (plugin_manifest.hpp)
    - PluginAction.visibleInActionsList / disableAutomaticStates / defaultSettings / encoderBlock
    - PluginActionState.name / title / showTitle
    - affordanceMask(QStringList) free function
  affects:
    - src/app/src/plugin_manifest.hpp
    - src/app/src/plugin_manifest.cpp
    - tests/unit/test_plugin_manifest.cpp
    - tests/unit/fixtures/manifests/
tech_stack:
  added: []
  patterns:
    - TDD RED/GREEN (struct fixtures then failing tests then implementation)
    - QJsonObject nested-object extraction guard pattern (isObject() before toObject())
    - nodiscard free function in namespace ajazz::app
    - Affordance bitmask enum for strict drop-target gating
key_files:
  created:
    - tests/unit/fixtures/manifests/manifest_visibility.json
    - tests/unit/fixtures/manifests/manifest_affordances.json
  modified:
    - src/app/src/plugin_manifest.hpp
    - src/app/src/plugin_manifest.cpp
    - tests/unit/test_plugin_manifest.cpp
decisions:
  - 'affordanceMask empty-list defaults to Key (Pitfall 2: prevents drop rejection on absent Controllers)'
  - Information-only controllers return mask=0 (non-draggable per Pitfall 3 / Affordance table)
  - 'Knob and Encoder are synonymous for Dial affordance (AJAZZ corpus: Knob = Elgato Encoder)'
  - affordanceMask placed in outer namespace ajazz::app (not anonymous) per free-function convention
metrics:
  duration_minutes: 17
  completed_date: '2026-05-31T19:50:42Z'
  tasks_completed: 2
  files_changed: 5
requirements_closed: [PLUGIN-18, PLUGIN-20]
---

# Phase 28 Plan 01: Plugin Manifest Full Action Model + affordanceMask Summary

**One-liner:** Extended PluginAction/PluginActionState/PluginEncoderBlock with VisibleInActionsList, Encoder block, multi-state Name/Title/ShowTitle, and affordanceMask() bitmask helper — foundation for strict drag-drop gating across all wave-1..4 plans.

## What Was Built

### Fixtures (Wave 0)

- `tests/unit/fixtures/manifests/manifest_visibility.json` — 3-action fixture covering: `VisibleInActionsList:false`, full `Encoder` object with `TriggerDescription.{Rotate,Push,Touch,LongTouch}`, `DisableAutomaticStates:true`, `Settings` object, multi-state `Name`/`Title`/`ShowTitle`.
- `tests/unit/fixtures/manifests/manifest_affordances.json` — 8-action fixture covering all controller token variants: `["Keypad"]`, absent, `["Knob"]`, `["Encoder"]`, `["Keypad","Knob"]`, `["SecondaryScreen"]`, `["Keypad","Information","SecondaryScreen"]`, `["Information"]`.

### Struct Extensions (`plugin_manifest.hpp`)

- `PluginActionState` gained: `name`, `title`, `showTitle{true}`.
- New `PluginEncoderBlock` struct: `icon`, `layout`, `triggerDescriptionRotate/Push/Touch/LongTouch`.
- `PluginAction` gained: `visibleInActionsList{true}`, `disableAutomaticStates{false}`, `defaultSettings` (compact JSON string), `encoderBlock`.
- `Affordance` enum class (`Key=1, Dial=2, TouchZone=4`) and `[[nodiscard]] int affordanceMask(QStringList const&) noexcept` free function declared.

### Parser Extensions (`plugin_manifest.cpp`)

- `parseState()` reads `Name`, `Title`, `ShowTitle` (default true).
- `parseAction()` reads `VisibleInActionsList` (default true), `DisableAutomaticStates` (default false), `Settings` (guarded by `isObject()`), `Encoder` block with nested `TriggerDescription` (all guarded by `isObject()` — T-28-01 mitigation).
- `affordanceMask()` implementation: empty list → Key only; iterates tokens; Keypad→Key, Knob/Encoder→Dial, SecondaryScreen→TouchZone; Information ignored; unknown tokens contribute nothing.

### Catch2 Tests (`test_plugin_manifest.cpp`)

18 new test cases added (tests 568-584, all passing):

| Test ID | Name                                                                        | Result |
| ------- | --------------------------------------------------------------------------- | ------ |
| 573     | parses VisibleInActionsList false and absent                                | PASS   |
| 574     | parses DisableAutomaticStates true and absent                               | PASS   |
| 575     | parses Encoder block with TriggerDescription                                | PASS   |
| 576     | parses default Settings as JSON string                                      | PASS   |
| 577     | parses state Name Title ShowTitle                                           | PASS   |
| 578     | affordanceMask empty controllers defaults to Key                            | PASS   |
| 579     | affordanceMask Knob maps to Dial only                                       | PASS   |
| 580     | affordanceMask Encoder maps to Dial same as Knob                            | PASS   |
| 581     | affordanceMask Keypad Knob maps to Key and Dial                             | PASS   |
| 582     | affordanceMask SecondaryScreen maps to TouchZone only                       | PASS   |
| 583     | affordanceMask Keypad Information SecondaryScreen maps to Key and TouchZone | PASS   |
| 584     | affordanceMask Information only maps to zero                                | PASS   |

## Test Results

```
ctest --preset linux-release -E qml: 726/726 passed, 0 failed
plugin-manifest subset: 17/17 passed, 0 failed
```

## Deviations from Plan

None — plan executed exactly as written.

The TDD RED/GREEN cycle was applied correctly:

1. **RED commit (`151d028`):** Fixtures + struct extensions + failing tests (linker error on `affordanceMask` undefined reference).
1. **GREEN commit (`0732824`):** Parser extensions + `affordanceMask` definition — all 18 new tests pass.

## COD-031 Verification

`grep -rn '#include.*nlohmann' src/core/include/` returns 0 matches. COD-031 boundary clean — all new code uses `QJsonObject`/`QJsonDocument` only.

## Known Stubs

None — all new struct fields are wired through parseAction()/parseState() and verified by tests against real fixture data.

## Threat Flags

No new network endpoints, auth paths, or schema trust boundaries introduced. All new parsing is at the existing manifest-parse trust boundary (attacker-controllable `.sdPlugin` manifest JSON). T-28-01 mitigation (isObject() guard on all nested extractions) applied. T-28-03 mitigation (unknown tokens contribute no affordance bit) applied.

## Self-Check: PASSED

| Item                               | Result |
| ---------------------------------- | ------ |
| `manifest_visibility.json` exists  | FOUND  |
| `manifest_affordances.json` exists | FOUND  |
| `plugin_manifest.hpp` exists       | FOUND  |
| `plugin_manifest.cpp` exists       | FOUND  |
| `test_plugin_manifest.cpp` exists  | FOUND  |
| commit `151d028` (RED) exists      | FOUND  |
| commit `0732824` (GREEN) exists    | FOUND  |
| 726/726 ctest pass                 | PASS   |
| COD-031 clean                      | PASS   |
| No non-ASCII TEST_CASE names       | PASS   |
