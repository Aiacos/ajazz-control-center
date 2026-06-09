---
phase: 30-plugin-host-modular-foundation
plan: 03
subsystem: plugins
tags: [ipluginhost2, unified-plugin-host, plugin-manager, dispatch, qt, cpp20]

requires:
  - phase: 30-01
    provides: RED contract tests (test_plugin_host2.cpp), UNIFY ADR
  - phase: 30-02
    provides: sentinel-UUID pre-registration tracking + onProcessFailed crash-window guard
provides:
  - IPluginHost2 — single Qt-typed app-layer spawn/lifecycle/IPC contract
  - UnifiedPluginHost — aggregator owning PluginManager (.sdPlugin) + OutOfProcessPluginHost (Python), routing dispatch() by UUID internally
  - Application::pluginHost2() returns the aggregator; Application::dispatch() no longer branches by runtime
affects: [31-actioninstance-core-model, 32-binding-layer, 33-property-inspector, 34-per-app-profiles, 35-windows-plugin-security]

tech-stack:
  added: []
  patterns:
    - 'Aggregator-behind-interface: one IPluginHost2 surface, two concrete sub-hosts routed by UUID internally'
    - Qt types at the app-layer interface; Qt->STL conversion at the Python (OutOfProcessPluginHost) boundary

key-files:
  created:
    - src/app/src/i_plugin_host2.hpp
    - src/app/src/unified_plugin_host.hpp
    - src/app/src/unified_plugin_host.cpp
  modified:
    - src/app/src/plugin_manager.hpp
    - src/app/src/plugin_manager.cpp
    - src/app/src/application.hpp
    - src/app/src/application.cpp
    - tests/unit/test_plugin_host2.cpp
    - tests/unit/CMakeLists.txt
    - src/app/CMakeLists.txt

key-decisions:
  - FULL UNIFICATION via a UnifiedPluginHost aggregator (RESEARCH A4 / ADR) rather than routing in Application — the single contract is genuine, not a facade
  - IPluginHost2 uses Qt types (QString/QJsonObject); the Python wrapper converts to STL at the OutOfProcessPluginHost boundary
  - IPluginHost2 + UnifiedPluginHost live in src/app/src/ (app layer) so COD-031 holds — no nlohmann in ajazz_core or installed headers

patterns-established:
  - 'Pattern: a runtime-agnostic plugin host contract (IPluginHost2) with an aggregator that hides per-runtime routing from all callers'

requirements-completed: [HOST-01, HOST-03]

duration: ~30min (interrupted by Sonnet rate limit after task commits; closed out by orchestrator)
completed: 2026-06-06
---

# Phase 30 / Plan 03: IPluginHost2 + UnifiedPluginHost Summary

**A single Qt-typed `IPluginHost2` contract with a `UnifiedPluginHost` aggregator that owns the `.sdPlugin` (PluginManager) and Python (OutOfProcessPluginHost) sub-hosts and routes `dispatch()` by UUID internally — full runtime unification, verified by a contract test that reaches Python through an `IPluginHost2*` pointer.**

## Performance

- **Duration:** ~30 min (executor was interrupted by the Sonnet daily rate limit immediately AFTER committing all 3 task commits but BEFORE writing this SUMMARY / tracking; the orchestrator verified and closed it out)
- **Completed:** 2026-06-06
- **Tasks:** 3
- **Files modified:** 10 (3 created, 7 modified; 867 insertions)

## Accomplishments

- `IPluginHost2` — the single app-layer spawn/lifecycle/IPC contract (Qt types), with `discover()`/`spawn()`/`dispatch()`/`plugins()` as the unified surface.
- `UnifiedPluginHost` — implements `IPluginHost2`, owns both concrete sub-hosts, and routes `dispatch(QString pluginUuid, ...)` by UUID internally (`.sdPlugin` → PluginManager/WS path; Python → `OutOfProcessPluginHost::dispatch()` with Qt→STL conversion at that boundary). `plugins()` folds both inventories.
- `Application::pluginHost2()` now returns the `UnifiedPluginHost` (constructed at application.cpp:981 as `m_pluginHost2`); runtime/UUID routing was removed from `Application::dispatch()` so every caller of the contract reaches all four runtimes.
- Four-runtime no-regression coverage plus the keystone contract test "unified host dispatch reaches Python through the IPluginHost2 pointer".

## Task Commits

1. **Task 1: IPluginHost2 contract + UnifiedPluginHost aggregator** — `222aa40` (feat)
1. **Task 2: wire Application onto UnifiedPluginHost aggregator** — `5b63101` (feat)
1. **Task 3: four-runtime no-regression + Python-via-IPluginHost2 dispatch contract** — `af3b929` (test)

## Files Created/Modified

- `src/app/src/i_plugin_host2.hpp` (139 lines) — the unified contract (Qt-typed; COD-031 boundary documented).
- `src/app/src/unified_plugin_host.{hpp,cpp}` (163 + 153 lines) — the aggregator + internal UUID routing.
- `src/app/src/plugin_manager.{hpp,cpp}` — PluginManager adapted to the IPluginHost2 surface (+55 cpp / +44 hpp).
- `src/app/src/application.{hpp,cpp}` — owns `m_pluginHost2`; `pluginHost2()` accessor; dispatch delegates to the aggregator.
- `tests/unit/test_plugin_host2.cpp` (+292) — 4 runtime tests + the IPluginHost2\*-pointer Python contract test.
- `tests/unit/CMakeLists.txt`, `src/app/CMakeLists.txt` — build wiring for the new sources.

## Decisions Made

- Routed dispatch inside the aggregator (not Application) so the IPluginHost2 contract is honest for every caller — this was the plan-checker Warning-2 fix and aligns with RESEARCH Assumption A4 and the ADR's UNIFY decision.

## Deviations from Plan

None functional — plan executed as written. Operational note: the executing Sonnet agent exhausted its daily quota after the third task commit; the orchestrator (Opus) verified the build, ran the full suite, ran the COD-031 + HOST-03 audits, and authored this SUMMARY + tracking updates as the close-out.

## Issues Encountered

- **Sonnet rate limit mid-plan.** All three task commits had landed and the working tree was clean, so close-out was a safe manual step (no partial/uncommitted code). Resolved by orchestrator verification rather than re-execution.

## Verification (orchestrator-run, post-interruption)

- `cmake --build --preset linux-release --target ajazz_unit_tests` — compiles + links the new sources.
- `ctest --preset linux-release` — **701/701 passed** (four-runtime PluginManager tests, OutOfProcess Python test, the IPluginHost2\*-pointer contract test, and the two HOST-02 tests from Wave 1 all green; zero regression).
- COD-031: `grep -rn "#include.*nlohmann" src/core/include/ src/app/src/i_plugin_host2.hpp src/app/src/unified_plugin_host.hpp` → no actual includes (only boundary-assertion comments).
- HOST-03 SKU audit: `grep -rn "akp05e|akp03|akp153" src/app/src/plugin_manager.cpp src/app/src/unified_plugin_host.cpp` → 0.
- The clangd "file not found / unknown type" diagnostics on these files are IDE-without-build-flags false positives (the files compile and the suite runs).

## Outstanding (deferred to phase verification)

- Live debug-channel check (`AJAZZ_DEBUG_CONTROL=1` → `scripts/ajazz-debug plugin.list` shows the merged .sdPlugin + Python inventory via the unified host) was NOT run during close-out — recommended during phase verification per CLAUDE.md.

## Next Phase Readiness

- The unified IPluginHost2 surface is the foundation Phase 31 (ActionInstance core model) and Phase 33 (Property Inspector) build on. No blockers.

______________________________________________________________________

*Phase: 30-plugin-host-modular-foundation*
*Completed: 2026-06-06*
