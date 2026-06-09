# Roadmap: AJAZZ Control Center

## Milestones

- ✅ **v1.0–v1.3** — Phases 1–29 (archived: `.planning/milestones/v1.3-ROADMAP.md`)
- ✅ **v2.0 Modular Plugin & Binding System** — Phases 30–35 (shipped 2026-06-09; archived: `.planning/milestones/v2.0-ROADMAP.md`)
- 📋 **v2.1+ (next)** — run `/gsd:new-milestone` to define

## Phases

<details>
<summary>✅ v2.0 Modular Plugin & Binding System (Phases 30–35) — SHIPPED 2026-06-09</summary>

- [x] Phase 30: Plugin-Host Modular Foundation (3 plans) — completed 2026-06-07
- [x] Phase 31: ActionInstance Core Model + Profile Schema v2 (2 plans) — completed 2026-06-07
- [x] Phase 32: Binding Layer Fix + Multi/Toggle Action + Device Editor (5 plans) — completed 2026-06-08
- [x] Phase 33: Property Inspector End-to-End (3 plans) — completed 2026-06-08
- [x] Phase 34: Per-App Profiles + Event-Parity Audit (5 plans) — completed 2026-06-08
- [x] Phase 35: Windows Plugin Support + Security Hardening + Milestone Verification (3 plans) — completed 2026-06-09

Full phase detail: `.planning/milestones/v2.0-ROADMAP.md`. Audit: `.planning/milestones/v2.0-MILESTONE-AUDIT.md`.
Outstanding human/hardware-gated live walks (accepted at close): per-phase `*-HUMAN-UAT.md` (32/33/34/35).

</details>

## Progress

| Phase                                   | Milestone | Plans | Status   | Completed  |
| --------------------------------------- | --------- | ----- | -------- | ---------- |
| 30. Plugin-Host Modular Foundation      | v2.0      | 3/3   | Complete | 2026-06-07 |
| 31. ActionInstance Core + Schema v2     | v2.0      | 2/2   | Complete | 2026-06-07 |
| 32. Binding + Multi/Toggle + Editor     | v2.0      | 5/5   | Complete | 2026-06-08 |
| 33. Property Inspector E2E              | v2.0      | 3/3   | Complete | 2026-06-08 |
| 34. Per-App Profiles + Event-Parity     | v2.0      | 5/5   | Complete | 2026-06-08 |
| 35. Windows Plugins + Security + Verify | v2.0      | 3/3   | Complete | 2026-06-09 |

## Backlog

Next milestone (v2.1+) is defined via `/gsd:new-milestone`. Carry-forward candidates from the v2.0
HUMAN-UAT backlog: WINPLG-03 Wine launch path (needs Wine/Windows host), the retail-AKP05E live
input/repaint walks, the real-JS Property Inspector round-trip (PI-03), the niri/X11 per-app-profile
live focus walks, and `systemDidWakeUp` real logind D-Bus wiring.
