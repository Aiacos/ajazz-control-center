---
phase: 28-akp05-plugin-action-completeness-drag-to-bind-on-keys-dials
plan: '02'
subsystem: plugin-catalog
tags: [plugin, catalog, affordance, action-library, debug-rpc, tdd]
dependency_graph:
  requires:
    - 28-01 (affordanceMask helper + PluginAction new fields)
  provides:
    - VisibleInActionsList filter in installedActions()
    - lastScanDiagnostics() Q_INVOKABLE accessor (installedCount / hiddenByVisibility / skippedUuidName / skippedParseFailure)
    - skippedActionsChanged() Q_SIGNAL on PluginCatalogModel
    - Extended QVariantMap from installedActions() with affordanceMask + 5 new fields
    - controllers + affordanceMask wired through ActionLibraryPane model rows and Drag.mimeData
    - LibraryTile objectName for debug-channel addressability
    - plugin.installedActions debug RPC (AJAZZ_DEBUG_CONTROL-gated)
  affects:
    - src/app/src/plugin_catalog_model.cpp
    - src/app/src/plugin_catalog_model.hpp
    - src/app/qml/ActionLibraryPane.qml
    - src/app/src/debug_control_facade.cpp
    - tests/unit/test_catalog_offline.cpp
tech_stack:
  added: []
  patterns:
    - TDD RED/GREEN (failing tests first, implementation second)
    - mutable member + const method for diagnostic caching
    - QJsonObject::fromVariantMap for debug-RPC QVariant bridge
    - AJAZZ_DEBUG_CONTROL-gated server.registerMethod lambda shape
key_files:
  created: []
  modified:
    - src/app/src/plugin_catalog_model.cpp
    - src/app/src/plugin_catalog_model.hpp
    - src/app/qml/ActionLibraryPane.qml
    - src/app/src/debug_control_facade.cpp
    - tests/unit/test_catalog_offline.cpp
decisions:
  - emit skippedActionsChanged() removed from const installedActions() -- signal declared for future use from non-const contexts; diagnostic stored via mutable members
  - affordanceMask:1 for builtin rows (Key-capable), affordanceMask:0 for hint rows (non-draggable)
  - 'objectName: "libraryTile_" + index for LibraryTile per CLAUDE.md debug-addressability rule'
  - totalScanned stored as m_lastTotalScanned but not exposed in lastScanDiagnostics() -- available for future signal use
metrics:
  duration_minutes: 25
  completed_date: '2026-05-31T22:01:00Z'
  tasks_completed: 2
  files_changed: 5
requirements_closed: [PLUGIN-18, PLUGIN-20]
---

# Phase 28 Plan 02: VisibleInActionsList Filter + Diagnostic Counters + Affordance MIME Payload Summary

**One-liner:** Filtered installedActions() hiding VisibleInActionsList=false vendor actions, exposed countable diagnostic (hidden vs error-skip vs parse-failure), extended QVariantMap with affordanceMask + 5 fields, wired affordanceMask through ActionLibraryPane MIME payload, added plugin.installedActions debug RPC.

## What Was Built

### Task 1: VisibleInActionsList Filter + Diagnostic Counters + Extended QVariantMap (TDD)

**RED commit (`8672aee`):** Added 4 failing Catch2 test cases + `lastScanDiagnostics()` declaration + `skippedActionsChanged()` signal + mutable member variables. Build failed at link (undefined reference to `lastScanDiagnostics()`).

**GREEN commit (`8672aee`):** Implemented all changes — tests pass.

**`plugin_catalog_model.cpp` changes:**

- `installedActions()` now declares `hiddenCount`, `errorSkipCount`, `parseFailureCount`, `totalScanned` as locals.
- `++parseFailureCount` added at manifest-parse-failure `continue` (was silent).
- `VisibleInActionsList` gate added **before** the UUID/Name check: `if (!action.visibleInActionsList) { ++hiddenCount; continue; }` — T-28-05 mitigation, Pitfall 7 compliance.
- `++errorSkipCount` replaces the silent `continue` on empty UUID/Name.
- QVariantMap extended with 6 new fields after the existing `controllers` insert:
  - `affordanceMask` — computed from `affordanceMask(action.controllers)` (C++ helper from 28-01)
  - `visibleInActionsList` — bool (for QML filter completeness)
  - `stateCount` — `static_cast<int>(action.states.size())`
  - `disableAutomaticStates` — bool
  - `defaultSettings` — compact JSON string from encoder block settings
  - `encoderLayout` — from `encoderBlock.layout`
- After the scan loop: `m_lastInstalledCount`, `m_lastHiddenByVisibility`, `m_lastSkippedUuidName`, `m_lastSkippedParseFailure`, `m_lastTotalScanned` updated via `mutable` members.
- New `lastScanDiagnostics()` implementation returns the 4-counter QVariantMap.

**`plugin_catalog_model.hpp` changes:**

- `lastScanDiagnostics()` Q_INVOKABLE declaration added.
- `skippedActionsChanged(int, int, int, int)` Q_SIGNAL declared.
- 5 `mutable int` members added for last-scan diagnostic storage.

**4 new Catch2 TEST_CASEs (`test_catalog_offline.cpp`):**

| Test Name                                                | Assertion                                                  |
| -------------------------------------------------------- | ---------------------------------------------------------- |
| CatalogOffline VisibleInActionsList false filters action | 3-action fixture (1 hidden) -> size==2; hidden UUID absent |
| CatalogOffline diagnostic hidden count vs error count    | hiddenByVisibility==1, skippedUuidName==1, distinct values |
| CatalogOffline controllers in output map                 | controllers field matches manifest Controllers array       |
| CatalogOffline affordanceMask in output map              | Keypad=1, Knob=2, Keypad+Knob=3                            |

### Task 2: ActionLibraryPane Model + MIME Payload + plugin.installedActions RPC

**Commit `902cc19`.**

**`ActionLibraryPane.qml` changes:**

- `_rebuild()` plugin rows append `controllers: a.controllers || []` and `affordanceMask: a.affordanceMask || 0`.
- Builtin rows append `controllers: []`, `affordanceMask: 1` (Key-capable).
- Hint row appends `controllers: []`, `affordanceMask: 0` (non-draggable).
- `LibraryTile` required property block gains `required property var controllers` and `required property int affordanceMask`.
- `LibraryTile` gains `objectName: "libraryTile_" + index` for debug-channel addressability (CLAUDE.md rule).
- `Drag.mimeData` JSON payload gains `affordanceMask: tile.affordanceMask`.
- Header comment updated to document the new MIME field.

**`debug_control_facade.cpp` changes:**

- `plugin.installedActions` method registered inside `#ifdef AJAZZ_HAVE_WEBSOCKETS` immediately after `plugin.list`.
- Null-guards `app.pluginCatalog() == nullptr`.
- Calls `cat->installedActions()`, converts each QVariant to `QJsonObject::fromVariantMap`.
- Returns `{count, actions[], diagnostics{}}` where diagnostics come from `cat->lastScanDiagnostics()`.

## Test Results

```
ctest --preset linux-release -E qml: 730/730 passed, 0 failed
(baseline was 726; +4 new catalog tests)
```

## Deviations from Plan

**1. [Rule 1 - Bug] `emit skippedActionsChanged()` removed from `const` method**

- **Found during:** Task 1 GREEN compile
- **Issue:** `installedActions()` is declared `const` (it is a `Q_INVOKABLE` with no side effects); `emit` on a signal from a `const` method is a compile error (`-fpermissive`).
- **Fix:** The diagnostic state is stored via `mutable` members (already added to the plan). The `skippedActionsChanged` signal is declared for completeness and future use (e.g. from a non-const wrapper or a public scan trigger method). The `lastScanDiagnostics()` accessor is the primary interface for tests and the debug RPC.
- **Files modified:** `plugin_catalog_model.hpp`, `plugin_catalog_model.cpp`
- **Impact:** None — `lastScanDiagnostics()` is fully testable and the debug RPC uses it correctly.

## COD-031 Verification

`grep -rn '#include.*nlohmann' src/core/include/` returns 0 matches. COD-031 boundary clean — all new code uses `QJsonObject::fromVariantMap`, `QJsonArray`, `QVariantMap` only.

## Known Stubs

None — all new fields are computed from real manifest data or carry genuine affordance semantics.

## Threat Flags

| Flag              | File                     | Description                                                                                                                               |
| ----------------- | ------------------------ | ----------------------------------------------------------------------------------------------------------------------------------------- |
| T-28-05 mitigated | plugin_catalog_model.cpp | VisibleInActionsList filter at :386 (before UUID/Name check) prevents hidden actions from entering the output list; they cannot be routed |
| T-28-06 mitigated | plugin_catalog_model.cpp | affordanceMask computed server-side from controllers (C++ helper); QML reads from trusted model, not from an untrusted manifest value     |

## Self-Check: PASSED

| Item                                                     | Result |
| -------------------------------------------------------- | ------ |
| plugin_catalog_model.cpp exists                          | FOUND  |
| plugin_catalog_model.hpp exists                          | FOUND  |
| ActionLibraryPane.qml exists                             | FOUND  |
| debug_control_facade.cpp exists                          | FOUND  |
| test_catalog_offline.cpp exists                          | FOUND  |
| commit 8672aee (Task 1) exists                           | FOUND  |
| commit 902cc19 (Task 2) exists                           | FOUND  |
| 730/730 ctest -E qml pass                                | PASS   |
| COD-031 clean                                            | PASS   |
| plugin.installedActions in debug_control_facade.cpp      | FOUND  |
| affordanceMask in ActionLibraryPane.qml MIME payload     | FOUND  |
| lastScanDiagnostics in plugin_catalog_model.hpp          | FOUND  |
| skippedActionsChanged signal in plugin_catalog_model.hpp | FOUND  |
| objectName on LibraryTile                                | FOUND  |
