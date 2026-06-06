---
phase: 27-plugin-install-trust-persistence-hardening
plan: '03'
subsystem: plugin-manager
tags: [plugins, persistence, qsettings, lifecycle, tdd]
completed: 2026-05-31T15:40:47Z

dependency_graph:
  requires: [27-02]
  provides: [setPluginEnabled, persisted-disabled-set, spawn-skip-predicate]
  affects: [src/app/src/plugin_manager.hpp, src/app/src/plugin_manager.cpp, tests/unit/test_plugin_lifecycle.cpp]

tech_stack:
  added: [QSettings (default-scope, plugins/disabled/<id>)]
  patterns: [QStandardPaths::setTestModeEnabled hermetic test isolation, shouldSkipSpawn single-predicate spawn-bypass]

key_files:
  created: []
  modified:
    - src/app/src/plugin_manager.hpp
    - src/app/src/plugin_manager.cpp
    - tests/unit/test_plugin_lifecycle.cpp

decisions:
  - Spawn-skip in spawn() boundary (not discover()) so both launch loop and rediscover() share one predicate without code duplication
  - pluginId key = .sdPlugin dir-name (same as m_live key from commit 5725cb0) -- consistent identity, no second identifier
  - User-disable and crash-disable kept completely separate: QSettings write ONLY in setPluginEnabled/shouldSkipSpawn; disableWithNotice/m_disabled NEVER touch QSettings
  - shouldSkipSpawn is a static method (not lambda/free function) so it is testable at the call site and documented in the header with the threat-register cross-reference

metrics:
  duration_minutes: 20
  tasks_completed: 1
  tasks_total: 1
  files_modified: 3
  tests_added: 4
  test_suite_before: 702
  test_suite_after: 706
---

# Phase 27 Plan 03: Persisted Per-Plugin User-Disable Set Summary

**One-liner:** QSettings-backed disabled-set (`plugins/disabled/<pluginId>`) with `setPluginEnabled(id,bool)`, single `shouldSkipSpawn` predicate shared by launch loop and `rediscover()`, and four hermetic restart-survival Catch2 tests.

## What Was Built

### setPluginEnabled(pluginId, bool)

New public method on `PluginManager` (declared in `plugin_manager.hpp`, implemented in `plugin_manager.cpp`):

- **enabled=false**: writes `plugins/disabled/<pluginId> = true` to QSettings, then tears down the live plugin if present (sends `exitApp`, calls `terminate(1s)` then `kill`, erases from `m_live`).
- **enabled=true**: removes the QSettings key, then if the plugin is not already live runs `discover()` to find its manifest and calls `spawn()`.

### shouldSkipSpawn(pluginId) — single shared predicate

Private static method that reads `QSettings::value(plugins/disabled/<pluginId>, false).toBool()`. Called at the top of `spawn()` so BOTH the launch-time discover+spawn loop (in `Application::startBackgroundServices`) and `rediscover()` (Plan 27-02) honor the user-disable without any additional check in `rediscover()`.

This is the T-27-DISABLE-BYPASS mitigation: one predicate, zero bypass paths.

### QSettings isolation pattern

Uses the same default-scope `QSettings settings;` pattern as `plugin_catalog_model.cpp:135,499` (`plugins/onlineCatalogEnabled`). `QStandardPaths::setTestModeEnabled(true)` in tests isolates settings to a temp directory, providing hermetic restart-survival tests without polluting real user settings.

### Crash-disable vs user-disable separation (T-27-DISABLE-LEAK)

`disableWithNotice()` and `m_disabled` are untouched — no QSettings write ever appears in those paths. The grep-verified separation is:

```
grep -n "QSettings" plugin_manager.cpp
```

Returns hits ONLY in `shouldSkipSpawn`, `setPluginEnabled`, and the `kDisabledGroup` helper — never in `disableWithNotice` or `onProcessFailed`.

## Tests Added (4 new Catch2 cases)

| Test ID | Name                                               | What it proves                                                                            |
| ------- | -------------------------------------------------- | ----------------------------------------------------------------------------------------- |
| D27-01  | setPluginEnabled persists disable across restart   | Disable in mgr1; fresh mgr2 over same dir+QSettings does NOT spawn the plugin             |
| D27-02  | setPluginEnabled re-enable spawns on next launch   | Disable then re-enable in mgr1; fresh mgr2 spawns the plugin normally                     |
| D27-03  | crash-disable is session-only and does not persist | 3-in-30s crash-disable in mgr1; fresh mgr2 finds and would-spawn the plugin (not blocked) |
| D27-04  | rediscover skips user-disabled plugins             | setPluginEnabled(A, false) then rediscover(); A not spawned, B spawned                    |

All tests use `QStandardPaths::setTestModeEnabled(true)` for hermeticity and clean up with `settings.remove(QStringLiteral("plugins/disabled"))` before each case.

## TDD Gate Compliance

- **RED commit** (`da9bfd7`): `test(27-03)` — 4 failing tests (setPluginEnabled missing)
- **GREEN commit** (`8dd9e82`): `feat(27-03)` — implementation; all 4 new tests pass + 706/706 total

## Verification

```
ctest --preset linux-release -R "Plugin|Lifecycle" -E qml  -> 129/129 passed
ctest --preset linux-release -E qml                         -> 706/706 passed
grep -rn nlohmann src/core/include/                         -> 0 actual includes (COD-031 clean)
grep "QSettings" src/app/src/plugin_manager.cpp | grep disableWithNotice -> (empty — separation verified)
```

## Deviations from Plan

None — plan executed exactly as written. The spawn-skip is placed at the `spawn()` boundary (as specified), using a single `shouldSkipSpawn` predicate (as specified). `disableWithNotice`/`m_disabled` are untouched.

## Threat Flags

None — no new network endpoints, auth paths, or schema changes beyond the QSettings key documented in the plan threat model.

## Known Stubs

None — the persisted disabled-set is fully wired: QSettings write in `setPluginEnabled`, read in `shouldSkipSpawn`, consulted at every `spawn()` call site.

## Self-Check: PASSED

- `src/app/src/plugin_manager.hpp` contains `setPluginEnabled` — FOUND
- `src/app/src/plugin_manager.cpp` contains `plugins/disabled` — FOUND
- `tests/unit/test_plugin_lifecycle.cpp` contains `setPluginEnabled` — FOUND
- RED commit `da9bfd7` exists — FOUND
- GREEN commit `8dd9e82` exists — FOUND
- 706/706 tests pass — VERIFIED
