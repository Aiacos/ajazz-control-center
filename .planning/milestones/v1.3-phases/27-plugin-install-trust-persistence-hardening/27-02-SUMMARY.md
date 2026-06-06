---
phase: 27-plugin-install-trust-persistence-hardening
plan: 02
subsystem: plugin-manager
tags: [plugin-lifecycle, rediscover, idempotency, install-epic]
completed: 2026-05-31
duration_minutes: 25

dependency_graph:
  requires: [27-01]
  provides: [PluginManager::rediscover, installFinished->rediscover wiring]
  affects: [plugin_manager.hpp, plugin_manager.cpp, application.cpp, test_plugin_lifecycle.cpp]

tech_stack:
  added: []
  patterns:
    - 'rediscover(): discover() + m_live diff + conditional spawn (D-27-3 idempotency)'
    - QObject::connect lambda guard on ok==true for installFinished signal
    - 'TDD RED/GREEN: failing tests first, then implementation'

key_files:
  created: []
  modified:
    - src/app/src/plugin_manager.hpp
    - src/app/src/plugin_manager.cpp
    - src/app/src/application.cpp
    - tests/unit/test_plugin_lifecycle.cpp

decisions:
  - rediscover() keys by QFileInfo(manifest.sourceDir).fileName() — same as spawn() commit 5725cb0
  - Disabled-set check deferred to Plan 27-03 (Wave 3); this plan limits diff to m_live membership only
  - connect placed inside AJAZZ_HAVE_WEBSOCKETS / server-start else-block to keep reduced builds clean
  - Lambda captures `this`; m_pluginCatalog is Application-owned and outlives the connection

metrics:
  tasks_completed: 2
  tasks_total: 2
  commits: 3
  files_modified: 4
  tests_added: 2
  tests_total_passing: 698
---

# Phase 27 Plan 02: rediscover-after-install Summary

**One-liner:** Idempotent `PluginManager::rediscover()` wired to `installFinished` so a GUI-installed .sdPlugin plugin runs live with no app restart.

## Tasks Completed

| Task      | Name                                   | Commit  | Key Files                              |
| --------- | -------------------------------------- | ------- | -------------------------------------- |
| 1 (RED)   | Failing rediscover() idempotency tests | 11b3516 | tests/unit/test_plugin_lifecycle.cpp   |
| 1 (GREEN) | Implement PluginManager::rediscover()  | 4f4b45c | plugin_manager.hpp, plugin_manager.cpp |
| 2         | Wire installFinished -> rediscover()   | 5675420 | application.cpp                        |

## What Was Built

### PluginManager::rediscover() (D-27-3)

Declared in `plugin_manager.hpp` next to `discover()`. Implemented in `plugin_manager.cpp`:

1. Calls `discover()` to get the current runnable manifest list (including archive extraction)
1. For each manifest, derives the `.sdPlugin` dir-name key using `QFileInfo(manifest.sourceDir).fileName()` — the same key `spawn()` uses to populate `m_live` (per commit `5725cb0`)
1. If the key is present in `m_live`: count as `alreadyLive`, do NOT spawn
1. If the key is absent: call `spawn(manifest)`, count as `newlySpawned`
1. Logs `rediscover: N already-live, M newly-spawned`

Never tears down, restarts, or double-spawns an already-live plugin.

### installFinished connection (Application)

In `application.cpp`, inside the existing `#ifdef AJAZZ_HAVE_WEBSOCKETS` / server-start else-block:

```cpp
QObject::connect(m_pluginCatalog.get(),
                 &PluginCatalogModel::installFinished,
                 m_pluginManager.get(),
                 [this](QString const& /*uuid*/, bool ok, QString const& /*error*/) {
                     if (ok) {
                         m_pluginManager->rediscover();
                     }
                 });
```

Guard on `ok==true` — failed or refused installs do not trigger a re-scan.

The launch-time `discover()+spawn()` loop is unchanged.

### Tests (Catch2, hermetic)

Two new tests in `test_plugin_lifecycle.cpp` tag `[plugin-manager]`:

- **PluginManagerTest rediscover spawns only newly-added plugins**: seeds plugin A, discover+spawn, then seeds B, calls `rediscover()` — verifies B's argv is populated and A's argv is unchanged (not re-spawned).
- **PluginManagerTest rediscover is idempotent under repeated calls**: same setup, calls `rediscover()` twice — verifies both A and B argv are unchanged on the second call.

Both use a fake `NodeProbe` with `/nonexistent/node` + `v26.0.0` version so `spawn()` goes through the node path and populates `m_live` (argv stored pre-process-start) without blocking on a real process.

## Verification

```
ctest --preset linux-release -E qml: 698/698 passed, 0 failed
ctest --preset linux-release -R rediscover: 2/2 passed
cmake --build --preset linux-release --target ajazz-control-center: clean
```

## Deviations from Plan

None. Plan executed exactly as written.

- The disabled-set check (Plan 27-03 coordination) is intentionally deferred: `rediscover()` limits its diff to the `m_live` membership test only, as the plan specifies.
- The optional `plugin.rediscover` debug-channel RPC was not added (plan marks it as "skip if it expands scope").

## Known Stubs

None. The `rediscover()` implementation is fully functional.

## Threat Flags

None. No new network endpoints, auth paths, file access patterns, or schema changes introduced. The `T-27-DOUBLESPAWN` threat (double-spawn via repeated rediscover) is mitigated by the `m_live` membership check, proven by the idempotency tests.

## Self-Check: PASSED

- `src/app/src/plugin_manager.hpp` contains `void rediscover();` declaration
- `src/app/src/plugin_manager.cpp` contains `PluginManager::rediscover()` implementation
- `tests/unit/test_plugin_lifecycle.cpp` contains two rediscover tests
- `src/app/src/application.cpp` contains the `installFinished` connect
- Commits 11b3516, 4f4b45c, 5675420 all exist in git log
- `ctest --preset linux-release -E qml` exits 0 (698/698)
