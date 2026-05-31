---
phase: 27
plan: '05'
subsystem: plugin-manager
tags: [plugin-concurrency, regression-test, crash-lifecycle, wr-02]
dependency_graph:
  requires:
    - '27-01: plugin trust split (verifier context only)'
    - '27-02: rediscover idempotency (plugin_manager.cpp in AJAZZ_HAVE_WEBSOCKETS block)'
  provides:
    - PLUGIN-17 concurrency regression net
    - 5725cb0 shared-key collision guard
    - WR-02 HTML-no-respawn regression guard
  affects: []
tech_stack:
  added: []
  patterns:
    - Catch2 TEST_CASE with injected synthetic clock (no real sleeps)
    - QSignalSpy count assertion for exactly-one-disable invariant
    - In-memory PluginManifest (no sourceDir) so pluginId == codePath
key_files:
  created:
    - tests/unit/test_plugin_concurrency.cpp
  modified:
    - tests/unit/CMakeLists.txt
decisions:
  - Used /nonexistent/node probe so plugins enter m_live synchronously (argv stored, QProcess launch async) — allows asserting m_live isolation without pumping the event loop
  - Cleared disabledSpy after spawn() so only crash-path signals are counted
  - WR-02 tested via m_live-absent UUID (never registered via spawn) — directly mirrors HTML plugin case where process==nullptr
  - Gated new test file inside existing AJAZZ_HAVE_WEBSOCKETS CMake block (plugin_manager.cpp compiled there)
metrics:
  duration: ~8 minutes
  completed: '2026-05-31T15:32:29Z'
  tasks_completed: 1
  tasks_total: 1
  files_created: 1
  files_modified: 1
---

# Phase 27 Plan 05: Plugin Concurrency Regression Test Summary

**One-liner:** Four Catch2 tests pin one-crash-disables-only-itself across distinctly-keyed siblings using an injected clock, guarding the 5725cb0 shared-key collision and the WR-02 HTML-no-respawn invariant.

## What Was Built

Added `tests/unit/test_plugin_concurrency.cpp` with four `[plugin-concurrency]` TEST_CASEs and registered the file in `tests/unit/CMakeLists.txt` inside the `AJAZZ_HAVE_WEBSOCKETS` block.

### Test Cases

| Test # | Name                                                                 | Asserts                                                                                                             |
| ------ | -------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------- |
| 624    | PluginManager crash disables only itself across siblings             | 3 crashes on A -> exactly 1 pluginDisabled signal with uuid==pluginA.js; isDisabled(B)==false, isDisabled(C)==false |
| 625    | PluginManager two crashes do not disable, siblings unaffected        | 2 crashes -> disabledSpy.count()==0, neither plugin disabled                                                        |
| 626    | PluginManager WR-02 HTML plugin not re-spawned on failure            | 1 crash on m_live-absent UUID -> no disable signal, sibling unaffected                                              |
| 627    | PluginManager WR-02 HTML UUID disabled at threshold without re-spawn | 3 crashes on m_live-absent UUID -> exactly 1 disable signal, count stays at 1                                       |

### Key Design Choices

- **Injected synthetic clock**: `qint64 fakeNow = 0` captured by reference; test advances it between `onProcessFailed` calls. All three crashes land at t=0, t=1000, t=2000 ms — inside `kWindowMs=30000`. No real sleeps.
- **Distinct codePath keys**: `pluginA.js`, `pluginB.js`, `pluginC.js` — in-memory manifests with empty `sourceDir` so `pluginId == codePath` (the 5725cb0 key-distinctness property under test).
- **Non-existent node path** (`/nonexistent/node`): puts plugins into `m_live` synchronously (argv stored before QProcess starts) without requiring event-loop pumping. Async failure fires later and is irrelevant before assertions.
- **disabledSpy.clear() after spawn()**: isolates crash-path signals from the node-absent-or-no-node pre-disable signals that spawn() may fire.

## Verification

```
ctest --preset linux-release -R "Plugin|Crash|Lifecycle" -E qml
# 125/125 passed (includes 4 new test_plugin_concurrency cases: 624-627)

ctest --preset linux-release -E qml
# 702/702 passed — 0 regressions
```

## Deviations from Plan

None — plan executed exactly as written. The WR-02 guard was covered without needing `AJAZZ_HAVE_WEBENGINE` gating (the guard is tested via UUID-absent-from-m_live, not via an actual HTML spawn path), which is the correct hermetic approach.

## Threat Surface Scan

No new network endpoints, auth paths, file access patterns, or schema changes introduced. Test-only file.

## Self-Check: PASSED

- `tests/unit/test_plugin_concurrency.cpp` exists and is registered in CMakeLists.txt
- Commit `adeaf47` exists: `git log --oneline | grep adeaf47` confirms
- All 4 new tests passed (ctest IDs 624-627)
- Full suite 702/702 green
