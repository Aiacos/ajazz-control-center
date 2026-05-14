# Roadmap: AJAZZ Control Center

## Milestones

- ✅ **v1.0 milestone** — Phases 1-2, retro-fit catalogue (shipped 2026-05-13). See [milestones/v1.0-ROADMAP.md](milestones/v1.0-ROADMAP.md).
- ✅ **v1.1 milestone** — Phases 3-8, device lifecycle hardening + scaffolding-to-functional (shipped 2026-05-14). See [milestones/v1.1-ROADMAP.md](milestones/v1.1-ROADMAP.md).

## Phases

<details>
<summary>✅ v1.0 milestone (Phases 1-2) — SHIPPED 2026-05-13</summary>

- [x] Phase 1: SEC-003 Plugin Host — completed 2026-05-03
- [x] Phase 2: QML Singleton Sweep — completed 2026-05-04

Audit: `tech_debt` — 7/7 success criteria PASSED; CR-01 (Win32 env pollution) and WR-01 (trust-roots parser) deferred to v1.1.

</details>

<details>
<summary>✅ v1.1 milestone (Phases 3-8) — SHIPPED 2026-05-14</summary>

- [x] Phase 3: Architectural Decisions (1/1 plan) — completed 2026-05-14
- [x] Phase 4: Hot-plug Hardening (7/7 plans) — completed 2026-05-14
- [x] Phase 5: Time-Sync Scaffolding (8/8 plans) — completed 2026-05-14
- [x] Phase 6: CR-01 Win32 OOP Env Pollution Fix (3/3 plans) — completed 2026-05-14
- [x] Phase 7: WR-01 Trust-Roots Parser Hardening (3/3 plans) — completed 2026-05-14
- [x] Phase 8: Scaffolded-Device Wiring (4/4 plans) — completed 2026-05-14

Audit: `tech_debt` — 28/28 requirements satisfied, 178/178 tests pass; deferred items (real-hardware UI verifies, Windows CI back-fill, AKP815 / Mirabox N3 maturity promotion blocked on real-device captures, libFuzzer Fedora packaging) carried to v1.1.x / v1.2 backlog.

</details>

### 📋 Next milestone — TBD

Next milestone will be bootstrapped via `/gsd-new-milestone`. Carry-over candidates:

- Real-hardware UI verification for Phase 5 (Sync button visibility, Settings auto-sync persistence, glyph behavior) and Phase 8 (MaturityRole tooltip).
- AKP815 + Mirabox N3 promotion `probed`/`partial` → `functional` when real-device captures land.
- Explicit `Toast.qml` cap=1 implementation (A-05 mitigation).
- TimeSyncService Pitfall-13 contextual INFO message.
- Codename→maturity map → Qt resource + runtime YAML parse (if catalogue grows).
- libFuzzer Fedora packaging once `libclang_rt.fuzzer.a` lands.
- Real `IClockCapable::setTime` wire formats when a backend reverse-engineers firmware support.

## Progress

| Phase                       | Milestone | Plans Complete | Status           | Completed  |
| --------------------------- | --------- | -------------- | ---------------- | ---------- |
| 1. SEC-003 Plugin Host      | v1.0      | 1/1            | Complete (retro) | 2026-05-03 |
| 2. QML Singleton Sweep      | v1.0      | 1/1            | Complete (retro) | 2026-05-04 |
| 3. Architectural Decisions  | v1.1      | 1/1            | Complete         | 2026-05-14 |
| 4. Hot-plug Hardening       | v1.1      | 7/7            | Complete         | 2026-05-14 |
| 5. Time-Sync Scaffolding    | v1.1      | 8/8            | Complete         | 2026-05-14 |
| 6. CR-01 Win32 Env Fix      | v1.1      | 3/3            | Complete         | 2026-05-14 |
| 7. WR-01 Trust-Roots Parser | v1.1      | 3/3            | Complete         | 2026-05-14 |
| 8. Scaffolded-Device Wiring | v1.1      | 4/4            | Complete         | 2026-05-14 |
