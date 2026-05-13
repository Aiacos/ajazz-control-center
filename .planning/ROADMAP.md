# Roadmap: AJAZZ Control Center

## Milestones

- ✅ **v1.0 milestone** — Phases 1-2, retro-fit catalogue (shipped 2026-05-13). See [milestones/v1.0-ROADMAP.md](milestones/v1.0-ROADMAP.md).
- 📋 **v1.1** — Not yet scoped. Run `/gsd-new-milestone` to begin.

## Phases

<details>
<summary>✅ v1.0 milestone (Phases 1-2) — SHIPPED 2026-05-13</summary>

- [x] Phase 1: SEC-003 Plugin Host — completed 2026-05-03
- [x] Phase 2: QML Singleton Sweep — completed 2026-05-04

Audit: `tech_debt` — 7/7 success criteria PASSED; CR-01 (Win32 env pollution) and WR-01 (trust-roots parser) deferred for the next milestone touching those areas.

</details>

### 📋 v1.1 (Not yet scoped)

To start the next milestone, run `/gsd-new-milestone`.

Carried-over candidates from v1.0 tech debt:

- CR-01 — Win32 OOP host env pollution fix (needs Windows CI to exercise)
- WR-01 — `loadTrustRoots` parser hardening (needs architectural decision: JSON dep vs. custom scanner)

Out-of-band work (not GSD-tracked, lives in `docs/superpowers/`):

- Time-sync — spec + 8-task plan at `docs/superpowers/plans/2026-05-13-time-sync.md`

## Progress

| Phase                  | Milestone | Plans Complete | Status           | Completed  |
| ---------------------- | --------- | -------------- | ---------------- | ---------- |
| 1. SEC-003 Plugin Host | v1.0      | 1/1            | Complete (retro) | 2026-05-03 |
| 2. QML Singleton Sweep | v1.0      | 1/1            | Complete (retro) | 2026-05-04 |
