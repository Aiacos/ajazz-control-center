---
gsd_state_version: 1.0
milestone: v1.1
milestone_name: Device lifecycle hardening + scaffolding-to-functional
status: planning
last_updated: '2026-05-13T17:42:47.135Z'
last_activity: 2026-05-13
progress:
  total_phases: 0
  completed_phases: 0
  total_plans: 0
  completed_plans: 0
  percent: 0
---

# Project State

## Project Reference

See: .planning/PROJECT.md (updated 2026-05-12)

**Core value:** Modern open cross-platform control center for AJAZZ devices with Qt 6 / QML UI + Python plugin system.
**Current focus:** v1.1 — device lifecycle hardening, time-sync scaffolding, scaffolded-device wiring, CR-01 + WR-01 carry-overs.

## Current Position

Phase: Not started (defining requirements)
Plan: —
Status: Defining requirements
Last activity: 2026-05-13 — Milestone v1.1 started

## Performance Metrics

**Velocity:** N/A — v1.1 just started; metrics will accumulate per phase.

| Phase | Plans | Total | Avg/Plan |
| ----- | ----- | ----- | -------- |
| —     | —     | —     | —        |

## Accumulated Context

### Decisions

See PROJECT.md Key Decisions table.

Recent:

- Phase 1: Out-of-process Python plugin host pattern adopted (SEC-003).
- Phase 2: `qmlRegisterSingletonInstance` is the canonical pattern for shared C++ singletons exposed to QML; bare `QML_SINGLETON` macro is the anti-pattern.
- Phase 2 (post-review): `static_assert(!std::is_default_constructible_v<T>)` co-located with the `QML_SINGLETON` declaration converts the load-bearing comment-only invariant into a build break. Applied to all 9 sites: 6 from the `e221b21` sweep + 3 sibling sites (`ThemeService`, `TrayController`, `DeviceModel`). Commits `5b1d679`, `6c785f0`.

### Pending Todos

None tracked in `.planning/todos/` — see repo-root `TODO.md` for legacy checklist.

### Blockers/Concerns

- **CR-01 — Win32 OOP host pollutes parent env** (`src/plugins/src/out_of_process_plugin_host_win32.cpp:463-467`). The Win32 backend calls `_putenv_s` for `PYTHONPATH` / `PYTHONDONTWRITEBYTECODE` / `PYTHONUNBUFFERED` in the **parent** process before `CreateProcessW(lpEnvironment=nullptr)`, polluting both subsequent host instances and any sibling subprocess (notably the manifest verifier). Per the review, this should land before CI actually exercises the OOP host on Windows. Untestable from this Linux dev box. Fix paths in `milestones/v1.0-phases/01-sec-003-plugin-host/01-FIX-DEFERRED.md`.

## Deferred Items

| Category         | Item                                                                                                                                                                 | Status                                                                                                              | Deferred At |
| ---------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------- | ----------- |
| Security (Win32) | CR-01: OOP host env pollution → needs per-spawn UTF-16 env block + `CREATE_UNICODE_ENVIRONMENT`                                                                      | Documented in `milestones/v1.0-phases/01-sec-003-plugin-host/01-FIX-DEFERRED.md`; ~40-60 LoC, untestable from Linux | 2026-05-12  |
| Code quality     | WR-01: `loadTrustRoots` mini-grep parser fragility — full replacement requires picking one of {add `nlohmann::json` dep, break COD-031, write 80-LoC custom scanner} | Partial window-widening fix landed in `1fbb46b` + new Catch2 case; full parser deferred for architectural decision  | 2026-05-12  |

## Session Continuity

Last session: 2026-05-13
Stopped at: v1.1 milestone bootstrap in progress — PROJECT.md updated, STATE.md reset via SDK. Requirements + roadmap still to write.
Resume file: None

## Operator Next Steps

- Define REQUIREMENTS.md (`/gsd-new-milestone` continuation) — group v1.1 scope into REQ-IDs.
- Spawn `gsd-roadmapper` to phase the milestone (starting at Phase 3).
- After roadmap approval: `/gsd-discuss-phase 3` to scope hot-plug hardening.
