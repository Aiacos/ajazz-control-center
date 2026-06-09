---
status: partial
phase: 33-property-inspector-end-to-end
source: [33-VERIFICATION.md, 33-03-SUMMARY.md]
started: 2026-06-08
updated: 2026-06-08
---

## Current Test

[deferred by user 2026-06-08 — run in a windowed session when convenient]

## Tests

### 1. PI HTML renders (not blank) + propertyInspectorDidAppear on open (criteria 1+3)

expected: select a key bound to `com.test.demo.toggle`; the PI panel renders the "PI Demo" HTML (not blank); `plugin.protocolLog` shows propertyInspectorDidAppear.
result: [pending — blocked headless by the KeyCell-selection harness gap (KeyCells lack objectNames; qml.invoke can't reach the C++ singleton). Needs a windowed click.]

### 2. propertyInspectorDidDisappear on close (criterion 3)

expected: close the PI (closePiButton) → protocolLog shows propertyInspectorDidDisappear.
result: [pending]

### 3. Real PI JS $SD.setSettings round-trip + survives restart (criterion 5)

expected: in the open PI, edit a setting → its JS calls $SD.setSettings({...}) → value sticks; restart the app, reselect → setting survives.
result: [pending — the one criterion that cannot be driven headlessly by design]

## Summary

total: 3
passed: 0
issues: 0
pending: 3
skipped: 0
blocked: 0

## Gaps

- All 3 require opening the PI via a real windowed key selection. Headless-confirmed already: PI-02
  objectNames addressable; criterion 2 (plugin.simulatePiSettings -> didReceiveSettings) live; the
  full setSettings->didReceiveSettings round-trip + persistence (PI-03 headless half). Walk-through
  in 33-03-SUMMARY.md. A plugin with a PI exists: com.test.demo -> pi/index.html.
- PI-03 stays Partial in REQUIREMENTS.md until criterion 5 passes.
