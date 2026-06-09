---
plan: 33-03
status: deferred
requirements: [PI-03]
---

# 33-03 — PI Human-Verify Checkpoint (DEFERRED by user)

**Criterion 5** (real PI JS `$SD.setSettings()` round-trip + survives restart, plus the
PI-renders-not-blank and `propertyInspectorDidAppear`-on-open live checks) is a `checkpoint:human-verify`
task. The user chose to DEFER it and continue the autonomous run to Phase 34 (2026-06-08).

## Live-verified headless (by the orchestrator, AKP05E connected, WebEngine build)

- PI-02 affordance objectNames `openPiButton` / `closePiButton` / `piPanelLoader` / `piWebView`
  present + debug-addressable (`qml.get`).
- Criterion 2: `plugin.simulatePiSettings {pluginUuid, contextId, settings}` → `delivered: true`;
  `plugin.protocolLog` shows `OUT didReceiveSettings {...}` reaching the plugin.
- PI-03 relay + persistence live: full `IN setSettings {savedFor} → OUT didReceiveSettings {settings:{savedFor}}`
  round-trip observed.
- A plugin WITH a PI exists for the walk-through: `com.test.demo` → `pi/index.html` (the "PI Demo" page).

## Why headless could not complete criterion 5 / render / didAppear-on-open

Opening the PI requires SELECTING a bound key, but KeyCells lack `objectName`s (known harness gap)
and `qml.invoke` cannot reach the C++ QML singleton `PropertyInspectorController`. So the
PI-renders-not-blank + didAppear/didDisappear-on-open + real-`$SD.setSettings` checks all require a
real windowed selection — recorded in 33-HUMAN-UAT.md.

## Walk-through (for when the user runs it)

1. Launch `build/linux-release/src/app/ajazz-control-center` (windowed).
1. Select AKP05E, bind `com.test.demo.toggle` to a key, click that key.
1. PI panel renders the "PI Demo" HTML (not blank); `propertyInspectorDidAppear` in protocolLog.
1. Edit a PI setting → `$SD.setSettings({...})` → value sticks.
1. Restart, reselect → setting survives.
1. Close PI → `propertyInspectorDidDisappear` in protocolLog.
