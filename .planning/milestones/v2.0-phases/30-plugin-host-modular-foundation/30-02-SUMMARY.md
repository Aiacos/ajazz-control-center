---
phase: 30-plugin-host-modular-foundation
plan: '02'
subsystem: plugin-host
tags: [host-02, sentinel-uuid, crash-isolation, tdd-green, wave-1]
dependency_graph:
  requires:
    - phase: 30-01
      provides: RED Catch2 scaffold (disconnect-before-register, pre-registration-exit)
  provides:
    - HOST-02 sentinel-UUID pre-registration isolation (GREEN)
    - m_live.find guard in onProcessFailed (pre-reg exits skip crash window)
    - seedLiveForTest() test seam for crash-window simulation
  affects:
    - src/app/src/sd_plugin_server.cpp
    - src/app/src/sd_plugin_server.hpp
    - src/app/src/plugin_manager.cpp
    - src/app/src/plugin_manager.hpp
    - tests/unit/test_plugin_host2.cpp
    - tests/unit/test_plugin_lifecycle.cpp
    - tests/unit/test_plugin_concurrency.cpp
tech-stack:
  added: []
  patterns:
    - SINGLE MAP + SENTINEL UUID for pre-registration WebSocket tracking
    - m_live.find authority for crash-window eligibility in PluginManager
    - seedLiveForTest() test seam (same pattern as setPasswordForTesting / buildChildEnvironmentForTesting)

key-files:
  created: []
  modified:
    - src/app/src/sd_plugin_server.cpp
    - src/app/src/sd_plugin_server.hpp
    - src/app/src/plugin_manager.cpp
    - src/app/src/plugin_manager.hpp
    - tests/unit/test_plugin_lifecycle.cpp
    - tests/unit/test_plugin_concurrency.cpp

key-decisions:
  - 'SENTINEL UUID on connect, rekey on registerPlugin: isSentinelUuid() helper guards connectedPluginCount + onClientDisconnected; race guard (null-check it->socket) before rekey write'
  - m_live is the authority for crash-window eligibility; onProcessFailed exits early for absent UUIDs (pre-registration or already-torn-down entries)
  - seedLiveForTest() test seam required because crash-window simulation tests were calling onProcessFailed without seeding m_live; added to maintain HOST-02 correctness while keeping pre-existing tests green
  - 'WR-02 concurrency tests updated to seed m_live: HTML plugins ARE in m_live with process==nullptr; test comments were misleading (said "not in m_live") but real code always inserts them'

requirements-completed: [HOST-02]

duration: ~30min
completed: '2026-06-06'
---

# Phase 30 Plan 02: HOST-02 Pre-Registration Crash Isolation (GREEN) Summary

**Sentinel-UUID isolation in SdPluginServer + m_live.find guard in PluginManager so pre-registration crashes never consume crash credits or emit spurious pluginDisconnected signals.**

## Performance

- **Duration:** ~30 min
- **Started:** 2026-06-06T13:13:00Z
- **Completed:** 2026-06-06T13:42:00Z
- **Tasks:** 2
- **Files modified:** 6

## Accomplishments

- Plan 30-01 RED tests (`disconnect-before-register`, `pre-registration-exit`) turned GREEN (696/696 full suite passes).
- Sentinel UUID (`__pending__` prefix) inserted on every WebSocket connect; rekeyed to real UUID on `registerPlugin`; sentinel disconnects suppressed from `pluginDisconnected` signal and excluded from `connectedPluginCount()`.
- Race guard added: `if (it->socket == nullptr) return;` in the registerPlugin rekey path prevents UAF when socket dies during rekey (T-30-rekey-race).
- `onProcessFailed` pre-registration guard: early return without `recordCrash` when UUID absent from `m_live`; CR-02 double-fire guard and WR-02 re-spawn guard are unaffected.
- Live debug-channel verification: `plugin.list` returns `connectedCount` unchanged after disconnect-before-register; app remains responsive; sentinel log visible in app output.

## Task Commits

1. **Task 1: Sentinel-UUID insertion, race-guarded rekey, sentinel-aware disconnect** - `5e3bfdd` (feat)
1. **Task 2: Pre-registration-exit guard in PluginManager crash window** - `81a65a4` (fix)

**Plan metadata:** (created in final commit below)

## Files Created/Modified

- `src/app/src/sd_plugin_server.cpp` - sentinel UUID on connect, isSentinelUuid helper, race guard in rekey, sentinel-aware disconnect
- `src/app/src/sd_plugin_server.hpp` - isSentinelUuid() private static method declaration
- `src/app/src/plugin_manager.cpp` - m_live.find guard at top of onProcessFailed; seedLiveForTest() implementation
- `src/app/src/plugin_manager.hpp` - seedLiveForTest() declaration with HOST-02 context doc
- `tests/unit/test_plugin_lifecycle.cpp` - seedLiveForTest() before onProcessFailed calls in crash 3 in 30s, 2 crashes, crash-disable session-only tests
- `tests/unit/test_plugin_concurrency.cpp` - seedLiveForTest() in WR-02 HTML tests 3 and 4

## Decisions Made

- **seedLiveForTest() as test seam:** The existing crash-window tests (`crash 3 in 30s`, WR-02) were calling `onProcessFailed` with UUIDs never in `m_live`. With the HOST-02 guard, those calls became pre-reg early-exits. Rather than loosening the guard, a test seam was added (same pattern as `setPasswordForTesting`) so tests can model post-registration crashes accurately. This also corrected a misleading WR-02 test comment: HTML plugins ARE in `m_live` with `process == nullptr` (the guard in `handleProcessFailure` prevents re-spawn; the `onProcessFailed` guard now correctly requires `m_live` membership for crash accounting).

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Existing crash-window tests broke after m_live.find guard**

- **Found during:** Task 2 (onProcessFailed guard implementation)
- **Issue:** `crash 3 in 30s disables not restarts`, `2 crashes restarts not disables`, and `crash-disable is session-only` tests called `onProcessFailed` with UUIDs never in `m_live`. The new guard correctly exits early for those UUIDs, making the tests fail (expected `pluginDisabled` was not emitted).
- **Fix:** Added `seedLiveForTest(uuid)` to `PluginManager` (test-only method, analogous to `setPasswordForTesting`). Updated the three `test_plugin_lifecycle.cpp` tests and two `test_plugin_concurrency.cpp` (WR-02) tests to call `seedLiveForTest()` before `onProcessFailed`. The fix also clarifies that HTML plugins ARE in `m_live` with `process == nullptr`; the WR-02 test comment incorrectly said "UUID not in m_live."
- **Files modified:** `src/app/src/plugin_manager.hpp`, `src/app/src/plugin_manager.cpp`, `tests/unit/test_plugin_lifecycle.cpp`, `tests/unit/test_plugin_concurrency.cpp`
- **Verification:** 696/696 ctest passes; `crash 3 in 30s`, `shutdown sends exitApp`, all SdPluginServer + PluginManagerTest + PluginConcurrency tests green.
- **Committed in:** `81a65a4` (Task 2 commit, includes test updates)

______________________________________________________________________

**Total deviations:** 1 auto-fixed (Rule 1 - Bug)
**Impact on plan:** The fix was necessary for correctness. The existing tests modeled an impossible scenario (registered-plugin crash without m_live membership); correcting them with `seedLiveForTest()` makes the tests accurately reflect the production code path. No scope creep.

## Debug-Channel Verification

Live verification performed before declaring done:

1. Built app binary with new sentinel changes.
1. Launched `AJAZZ_DEBUG_CONTROL=1 QT_QPA_PLATFORM=offscreen ./ajazz-control-center`.
1. Connected a raw WebSocket to the plugin server port, waited 200ms, disconnected WITHOUT sending `registerPlugin`.
1. App log showed: `pre-registration client disconnected (sentinel=__pending__7f4f3e22-...)`.
1. `scripts/ajazz-debug plugin.list` returned `connectedCount: 1` (unchanged; pre-existing plugin unaffected).
1. App remained fully responsive (no crash, no hang, `state` RPC also returned cleanly).

Scenario confirmed: disconnect-before-register does NOT affect `connectedCount`, does NOT emit `pluginDisconnected` with a real UUID, and does NOT crash the app.

## Known Stubs

None. All HOST-02 contract assertions are implemented and verified. No placeholder values or TODO stubs in the modified files.

## Threat Flags

No new network endpoints, auth paths, file access patterns, or schema changes introduced beyond the planned HOST-02 mitigations. The three threats in the plan threat register (pre-reg DoS, rekey-race UAF, UUID spoofing) are all mitigated as specified.

## Self-Check: PASSED

| Item                                                                  | Result                |
| --------------------------------------------------------------------- | --------------------- |
| `src/app/src/sd_plugin_server.cpp` modified                           | FOUND                 |
| `src/app/src/sd_plugin_server.hpp` modified                           | FOUND                 |
| `src/app/src/plugin_manager.cpp` modified                             | FOUND                 |
| `src/app/src/plugin_manager.hpp` modified                             | FOUND                 |
| `__pending__` count >= 2 in sd_plugin_server.cpp                      | 2 (PASS)              |
| `m_live.find` guard in onProcessFailed                                | line 632 (PASS)       |
| Commit `5e3bfdd` (Task 1)                                             | FOUND                 |
| Commit `81a65a4` (Task 2)                                             | FOUND                 |
| `disconnect.before.register` GREEN                                    | PASS                  |
| `pre.registration.exit` GREEN                                         | PASS                  |
| `crash 3 in 30s` GREEN                                                | PASS                  |
| `shutdown sends exitApp` GREEN                                        | PASS                  |
| SdPluginServer suite 9/9                                              | PASS                  |
| PluginManagerTest suite                                               | PASS                  |
| PluginConcurrency suite                                               | PASS                  |
| Full ctest 696/696                                                    | PASS                  |
| Live debug-channel connectedCount unchanged after sentinel disconnect | VERIFIED              |
| No cached QWebSocket pointer                                          | VERIFIED (grep clean) |
| COD-031: no nlohmann in modified files                                | VERIFIED              |
