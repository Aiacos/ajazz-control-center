# Phase 5: Time-Sync Scaffolding - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-05-14
**Phase:** 05-time-sync-scaffolding
**Areas discussed:** Plan adoption, Manual UX (toast vs glyph), Backend coverage
**Mode:** `/gsd-autonomous --interactive` (default discuss-phase mode, batched questions because design doc + plan are pre-locked)

______________________________________________________________________

## Plan adoption

### Q1 — How to handle the 3 deltas since the 2026-05-13 plan was written?

| Option                                      | Description                                                                                                                                                                | Selected |
| ------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------- |
| Adopt plan + amend for the three deltas     | Use the 8-task plan as spine; amend Task 2 (add AKP815), Task 7 (read shared_ptr from D-06 cache), register.cpp coverage (all 17 streamdeck rows). ~15-20 lines amendment. | ✓        |
| Adopt plan verbatim, file follow-up tickets | Implement 8 tasks as written; AKP815 + rebadges + cache-aware lookup become separate quick tasks afterward.                                                                |          |
| Re-decompose from scratch with planner      | Ignore existing plan; let gsd-planner produce fresh PLAN.md. Wasteful — existing plan high-quality.                                                                        |          |

**User's choice:** Adopt plan + amend for the three deltas.
**Notes:** The three deltas are surgical: AKP815 backend (commit 62da68c), 7 AKP03 rebadges + canonical AKP153 PIDs (62da68c), and Phase 4 D-06 weak_ptr cache. Each delta touches one task; total amendment ~15-20 lines.

______________________________________________________________________

## Manual UX

### Q1 — Manual "Sync now" success/failure: toast or glyph only?

| Option                                             | Description                                                                                                                                                                                    | Selected |
| -------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------- |
| Toast for user-initiated sync, glyph for auto-sync | Manual click → toast (success/failure) + glyph; auto-sync → glyph only. Phase 4 D-01 silent-toast policy was specifically about hot-plug events; user-initiated actions get the toast surface. | ✓        |
| Glyph only — no toast for either                   | Total silence on toast surface. Loses "I just clicked something" feedback.                                                                                                                     |          |
| Toast for everything                               | Toasts on every sync. Reverts Phase 4 D-01 in this surface.                                                                                                                                    |          |

**User's choice:** Toast for user-initiated sync, glyph for auto-sync.
**Notes:** The toast surface is reserved for *things the user did*. Phase 4 D-01 governs *system-initiated* events (hot-plug arrive/depart). Phase 5 occupies a different lane on the same surface — no conflict.

______________________________________________________________________

## Backend coverage

### Q1 — Symmetric IClockCapable + hasClock=true coverage across all Stream Dock variants?

| Option                                               | Description                                                                                                                                                                                                                                                      | Selected |
| ---------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------- |
| Yes, symmetric coverage                              | Every Stream Dock backend (Akp153/03/05/815) inherits IClockCapable; AKP03 rebadges share Akp03Device's stub via shared factory. All 17 streamdeck register.cpp rows get .hasClock=true. AKB980 PRO keyboard too (per design). One row = one backend = one stub. | ✓        |
| Only AKP153/AKP03/AKP05 + AKB980 (per original plan) | Skip AKP815 + rebadges. Inconsistent with Phase 4 D-04 (rebadges share a backend).                                                                                                                                                                               |          |
| Functional tier only, scaffolded skip                | Only `functional` maturity-tier devices get IClockCapable. Adds branching in register.cpp.                                                                                                                                                                       |          |

**User's choice:** Yes, symmetric coverage.
**Notes:** Per Phase 4 D-04, capability advertisement is per-backend, not per-VID/PID. Total stub count: 5 backend classes (Akp153, Akp03, Akp05, Akp815, ProprietaryKeyboard) — same effort as the original 4-stub plan +1 for AKP815. The 17 register.cpp rows automatically inherit the right behavior.

______________________________________________________________________

## Claude's Discretion

The following were left for Claude / planner to decide:

- **`Capability::Clock` bit number** — design doc says `1u << 15`; planner verifies bit free + adds static_assert lock.
- **`std::once_flag` placement** — per-backend file-static flag; first call WARN, subsequent silent.
- **Toast component used** — existing `src/app/qml/components/Toast.qml`.
- **`QSettings` key** — `QSettings("Time/AutoSync", false)` with load-time capability validation per Pitfall 13.
- **Integration test harness** — uses Phase 4's `HotplugMonitor::injectEvent` + `MockHidEnumerator`.
- **Per-decision artefact files** — `05-PLAN.md` (or multi-plan), `05-SUMMARY.md` under `.planning/phases/05-time-sync-scaffolding/`.

## Deferred Ideas

- **Real wire-format implementations** — out of scope by design.
- **Device → host time read-back** — explicit non-goal.
- **Interval-based time re-sync timer** — drift-detection ≠ time-keeping.
- **Per-device detail panel for Sync now** — design picked DeviceRow only because no detail panel exists yet.
- **Render-time-on-keyface clock widget** — separate "Clock Widget" feature.
- **AKB980 PRO real wire format** — Delphi installer extraction blocked on missing wine/innoextract.
- **Pre-2026-05-14 placeholder VID:PID retirement** — `0x0300:0x5001` and `0x0300:0x3001` stay until captures confirm canonical alternatives.
