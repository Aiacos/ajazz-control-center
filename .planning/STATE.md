---
gsd_state_version: 1.0
milestone: v1.1
milestone_name: Device lifecycle hardening + scaffolding-to-functional
status: executing
stopped_at: Phase 4 context revalidated against new Stream Dock research (quick 260514-h0w) — D-02 + D-04 amended
last_updated: '2026-05-14T11:39:57.576Z'
last_activity: 2026-05-14
progress:
  total_phases: 6
  completed_phases: 1
  total_plans: 8
  completed_plans: 4
  percent: 50
---

# Project State

## Project Reference

See: .planning/PROJECT.md (updated 2026-05-13)

**Core value:** Modern open cross-platform control center for AJAZZ devices with Qt 6 / QML UI + Python plugin system.
**Current focus:** Phase 4 — hot-plug-hardening

## Current Position

Phase: 4 (hot-plug-hardening) — EXECUTING
Plan: 4 of 7
Status: Ready to execute
Last activity: 2026-05-14

## Performance Metrics

**Velocity:** N/A — v1.1 just started; metrics will accumulate per phase.

| Phase                       | Plans | Total | Avg/Plan |
| --------------------------- | ----- | ----- | -------- |
| 3. Architectural Decisions  | —     | —     | —        |
| 4. Hot-plug Hardening       | —     | —     | —        |
| 5. Time-Sync Scaffolding    | —     | —     | —        |
| 6. CR-01 Win32 Env Fix      | —     | —     | —        |
| 7. WR-01 Trust-Roots Parser | —     | —     | —        |
| 8. Scaffolded-Device Wiring | —     | —     | —        |
| 3                           | 1     | -     | -        |

## Accumulated Context

### Decisions

See PROJECT.md Key Decisions table.

Recent:

- Phase 1: Out-of-process Python plugin host pattern adopted (SEC-003).
- Phase 2: `qmlRegisterSingletonInstance` is the canonical pattern for shared C++ singletons exposed to QML; bare `QML_SINGLETON` macro is the anti-pattern.
- Phase 2 (post-review): `static_assert(!std::is_default_constructible_v<T>)` co-located with the `QML_SINGLETON` declaration converts the load-bearing comment-only invariant into a build break. Applied to all 9 sites: 6 from the `e221b21` sweep + 3 sibling sites (`ThemeService`, `TrayController`, `DeviceModel`). Commits `5b1d679`, `6c785f0`.
- v1.1 roadmap (2026-05-13): Phases 3-8 derived from 28 v1.1 requirements with 100% coverage. Phase 3 is a 1-day decision-doc phase (no code). Phase 4 (hot-plug ownership migration) is critical-path and must land before Phase 5 (time-sync). Phases 6 (CR-01) and 7 (WR-01) are parallel-independent side branches. Phase 8 (devices) is opportunistic and last.

### Pending Todos

None tracked in `.planning/todos/` — see repo-root `TODO.md` for legacy checklist.

### Quick Tasks Completed

| #          | Description                                                                                           | Date       | Commit  | Directory                                                                                                           |
| ---------- | ----------------------------------------------------------------------------------------------------- | ---------- | ------- | ------------------------------------------------------------------------------------------------------------------- |
| 260513-u0b | Stream Dock plugin loading + fetch + logs + UI layout diagnostic                                      | 2026-05-13 | 4acc9c5 | [260513-u0b-streamdock-plugins-diagnostic](./quick/260513-u0b-streamdock-plugins-diagnostic/)                       |
| 260513-uy6 | Plugin Store Phase A — fetcher logging + Retry buttons + re-entry guard                               | 2026-05-13 | ec9590c | [260513-uy6-plugin-store-phase-a](./quick/260513-uy6-plugin-store-phase-a/)                                         |
| 260513-v6b | Plugin Store Phase B — PluginCatalogProxyModel + GridView refactor                                    | 2026-05-13 | 9b1589f | [260513-v6b-plugin-store-phase-b](./quick/260513-v6b-plugin-store-phase-b/)                                         |
| 260514-1je | Stream Dock features research + KeyDesigner slice (AKP153)                                            | 2026-05-14 | 043d04a | [260514-1je-stream-dock-keydesigner](./quick/260514-1je-stream-dock-keydesigner/)                                   |
| 260514-h0w | Recheck Phase 4 CONTEXT vs new Stream Dock research (D-02 + D-04 amended; all D-01..D-06 still valid) | 2026-05-14 | db88676 | [260514-h0w-recheck-phase-4-context-vs-new-stream-do](./quick/260514-h0w-recheck-phase-4-context-vs-new-stream-do/) |

### Blockers/Concerns

- **CR-01 — Win32 OOP host pollutes parent env** (`src/plugins/src/out_of_process_plugin_host_win32.cpp:463-467`). The Win32 backend calls `_putenv_s` for `PYTHONPATH` / `PYTHONDONTWRITEBYTECODE` / `PYTHONUNBUFFERED` in the **parent** process before `CreateProcessW(lpEnvironment=nullptr)`, polluting both subsequent host instances and any sibling subprocess (notably the manifest verifier). Now scoped as Phase 6. Untestable from this Linux dev box — needs Windows CI exercise. Fix paths in `milestones/v1.0-phases/01-sec-003-plugin-host/01-FIX-DEFERRED.md`.
- **WR-01 — `loadTrustRoots` parser fragility**. Architectural call (nlohmann vs in-tree scanner) deferred to Phase 3 (ARCH-01); implementation lands in Phase 7. STACK + ARCHITECTURE recommend nlohmann; PITFALLS recommends in-tree scanner — threat-model framing decides.
- **Phase 4 → Phase 5 ordering is load-bearing.** `unique_ptr<IDevice>` → `shared_ptr<IDevice>` registry migration MUST land before time-sync wires the 300 ms debounced auto-sync, or disconnect-during-use becomes a UAF.

## Deferred Items

| Category         | Item                                                                                                                                                                 | Status                                                                                                                                  | Deferred At |
| ---------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------- | ----------- |
| Security (Win32) | CR-01: OOP host env pollution → needs per-spawn UTF-16 env block + `CREATE_UNICODE_ENVIRONMENT`                                                                      | Now scoped as v1.1 Phase 6 (WIN32-01..04). Fix paths in `milestones/v1.0-phases/01-sec-003-plugin-host/01-FIX-DEFERRED.md`.             | 2026-05-12  |
| Code quality     | WR-01: `loadTrustRoots` mini-grep parser fragility — full replacement requires picking one of {add `nlohmann::json` dep, break COD-031, write 80-LoC custom scanner} | Now scoped as v1.1 Phase 3 (ARCH-01 decision) + Phase 7 (TRUST-01..04 implementation). Partial window-widening fix landed in `1fbb46b`. | 2026-05-12  |

## Session Continuity

Last session: 2026-05-14T10:18:25.036Z
Stopped at: Phase 4 context revalidated against new Stream Dock research (quick 260514-h0w) — D-02 + D-04 amended
Resume file: .planning/phases/04-hot-plug-hardening/04-CONTEXT.md

## Operator Next Steps

- `/gsd-discuss-phase 3` to scope architectural decisions (ARCH-01, ARCH-02, ARCH-03).
- After Phase 3 lands, `/gsd-discuss-phase 4` for hot-plug hardening (foundational critical-path phase).
- Phase 5 should adopt `docs/superpowers/plans/2026-05-13-time-sync.md` rather than re-decompose.
- Phases 6 and 7 can execute in any order after Phase 3; both are parallel-independent of Phase 4/5.
