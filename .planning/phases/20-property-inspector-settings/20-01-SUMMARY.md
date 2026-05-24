---
phase: 20-property-inspector-settings
plan: '01'
subsystem: property-inspector
tags: [test, persistence, pi-bridge, plugin-13]
dependency_graph:
  requires: []
  provides: [PLUGIN-13-test-coverage, pi-bridge-persistence-proof]
  affects: [tests/unit/test_pi_bridge.cpp, tests/unit/CMakeLists.txt]
tech_stack:
  added: []
  patterns: [QStandardPaths::setTestModeEnabled hermetic test isolation, synchronous direct-connection signal capture]
key_files:
  created: []
  modified:
    - tests/unit/test_pi_bridge.cpp
    - tests/unit/CMakeLists.txt
decisions:
  - Use unique plugin/context UUIDs per test case for independence rather than QTemporaryDir (cannot redirect AppDataLocation cross-platform in test mode)
  - Synchronous direct-connection lambda capture is sufficient for getSettings/getGlobalSettings signals (no event loop needed)
  - qsizetype instead of int for QDir::entryList().size() comparisons (-Werror=conversion compliance)
metrics:
  duration_minutes: 7
  completed_date: '2026-05-24T16:07:12Z'
  tasks_completed: 2
  files_modified: 2
requirements: [PLUGIN-13]
---

# Phase 20 Plan 01: PLUGIN-13 Test Scaffold Summary

**One-liner:** Hermetic Catch2 restart round-trip tests proving PIBridge settings persistence (per-context + global) and path-traversal UUID rejection, with pi_bridge.cpp linked into the unit-test binary.

## What Was Built

This plan proves PLUGIN-13 (settings survive app restart) with a hermetic unit-test scaffold. No production code was changed.

**Task 1 - Link pi_bridge.cpp into unit-test binary (`tests/unit/CMakeLists.txt`):**

Added `${CMAKE_SOURCE_DIR}/src/app/src/pi_bridge.cpp` to the `ajazz_unit_tests` SOURCES block alongside `pi_url_policy.cpp`. The persistence path in `pi_bridge.cpp` uses only Qt6::Core (QSaveFile, QJsonDocument, QStandardPaths) — no WebEngine dependency is pulled in. AUTOMOC (already `ON` on the target) handles the Q_OBJECT moc pass for PIBridge.

**Task 2 - PLUGIN-13 restart round-trip + path-traversal refusal cases (`tests/unit/test_pi_bridge.cpp`):**

Added four `[pi-bridge][persistence]`-tagged Catch2 TEST_CASEs:

1. **"PI settings round-trip survives a fresh PIBridge"** (test 387): writes `{"hello":"world"}` via one PIBridge, destroys it, constructs a second with the same UUIDs, calls `getSettings()`, asserts `QJsonDocument` parses to `{"hello":"world"}`.
1. **"PI global settings round-trip survives a fresh PIBridge"** (test 388): same pattern via `setGlobalSettings`/`getGlobalSettings`, asserts `version:42` and `flag:true` survive.
1. **"PI settings are isolated per context"** (test 389): settings written under `ctx-iso-A` are NOT returned by a `ctx-iso-B` bridge (emits `"{}"`).
1. **"PI settings refuse path-traversal uuids"** (test 390): `"../evil"` pluginUuid and `"../ctx"` contextUuid each trigger no file creation (plugins dir entry count unchanged) and `getSettings()` emits `"{}"`.

Uses the existing `qtApp()` fixture (`QStandardPaths::setTestModeEnabled(true)`) for hermetic AppDataLocation; unique UUIDs per case for test independence; synchronous direct-connection lambda captures for signal inspection.

## Test Results

```
ctest --preset linux-release: 523/523 passed (0 failed)
New tests (387-390): all pass
```

## Commits

| Task | Commit  | Description                                                            |
| ---- | ------- | ---------------------------------------------------------------------- |
| 1    | 0b3ce7c | test(20-01): link pi_bridge.cpp into unit-test binary                  |
| 2    | 1bf1ebe | test(20-01): add PLUGIN-13 restart round-trip and path-traversal cases |

## Verification

- `grep -c 'pi_bridge.cpp' tests/unit/CMakeLists.txt` = 4 (>= 1)
- No `AJAZZ_HAVE_WEBENGINE` define and no `Qt6::WebEngineQuick`/`WebChannelQuick` link added to the test target
- `grep -rn nlohmann tests/unit/test_pi_bridge.cpp` = 0 (COD-031 compliant)
- All TEST_CASE titles are ASCII-only (no em-dash, no right-arrow)
- Full suite: 523/523 green on `ctest --preset linux-release`

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Fixed qsizetype-to-int conversion warnings**

- **Found during:** Task 2 first build
- **Issue:** `QDir::entryList().size()` returns `qsizetype` (alias for `long long`); assigning to `int` triggered `-Werror=conversion` (all warnings are errors on this project)
- **Fix:** Changed `int const countBefore/countAfter` to `qsizetype const countBefore/countAfter`
- **Files modified:** `tests/unit/test_pi_bridge.cpp`
- **Commit:** 1bf1ebe (part of the Task 2 commit after clang-format re-stage)

### clang-format Re-stage

clang-format reformatted `test_pi_bridge.cpp` during the first Task 2 commit attempt. The file was re-staged and the commit retried (standard pre-commit hook re-stage pattern; no `--no-verify` bypass needed).

## Known Stubs

None. This plan adds only test code; no production stubs are introduced.

## Threat Flags

None. This plan adds test-only code in `tests/unit/`. The path-traversal refusal cases exercise the existing `isSafeUuidComponent` guard (T-20-path disposition: `mitigate (assert)`) — the mitigation was pre-existing in `pi_bridge.cpp`; this plan asserts it holds.

## Self-Check: PASSED

- `tests/unit/CMakeLists.txt` exists and contains `pi_bridge.cpp`: FOUND
- `tests/unit/test_pi_bridge.cpp` exists with 4 persistence TEST_CASEs: FOUND
- Commit 0b3ce7c exists: FOUND
- Commit 1bf1ebe exists: FOUND
- 523/523 ctest green: PASSED
