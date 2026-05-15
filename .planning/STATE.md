---
gsd_state_version: 1.0
milestone: v1.2
milestone_name: Connected-Device Capability Parity
status: ready_to_plan
last_updated: '2026-05-15T06:00:00.000Z'
last_activity: 2026-05-15
progress:
  total_phases: 5
  completed_phases: 0
  total_plans: 0
  completed_plans: 0
  percent: 0
---

# Project State

## Project Reference

See: .planning/PROJECT.md (updated 2026-05-15)

**Core value:** Honest, capability-driven control of AJAZZ hardware with a sandboxed plugin system — never lying about what a device can do, never crashing when a device is yanked, never silently leaking host state into plugin children.
**Current focus:** Phase 9 — Research, Captures, Hygiene (gating phase; captures-driven; CAPTURE-01 MUST-FIX-FIRST).

## Current Position

Phase: 9 of 13 (Research, Captures, Hygiene) — first v1.2 phase
Plan: — (planning has not started yet)
Status: Ready to plan
Last activity: 2026-05-15 — v1.2 roadmap created; 33 v1.2 requirements mapped across Phases 9-13 (100% coverage validated).

Progress: [░░░░░░░░░░] 0%

## Performance Metrics

**Velocity (v1.0 + v1.1 carried forward):**

- Total plans completed: 27 (1 retro + 26 forward-planned in v1.1)
- v1.1 calendar duration: ~2 days end-to-end, ~80 commits

**v1.2 baseline:** Counters reset to 0/5 phases, 0/? plans (plan counts TBD per phase).

**Recent Trend:**

- v1.1 sustained ≥6 plans/day with 178/178 ctest pass.
- Cap concurrent execute agents at 2 in autonomous runs (v1.1 retrospective lesson).

*Updated after each plan completion.*

## Accumulated Context

### Decisions

See PROJECT.md Key Decisions table for the full log (v1.0 + v1.1 entries with outcomes).

Phase 9 will ratify three new written ADRs:

- **ARCH-04**: AKP03 image-encoding pipeline location (recommended: Qt6 `QImage::scaled(SmoothTransformation)` + `QImageWriter` JPEG host-side in `src/devices/streamdeck/src/image_pipeline.{hpp,cpp}`, PRIVATE-linked).
- **ARCH-05**: per-device `setTime` outcome (default verdict: NO RTC opcode in any AJAZZ corpus → `hasClock=false` on `akp03_variant_3004` and `ak980pro`; `setTime` stays `NotImplemented`).
- **ARCH-06**: composite-HID dedup (default verdict: NOT firing — topology proves `0c45:7016` is a separate dongle on a different bus branch).

### Pending Todos

None tracked in `.planning/todos/`. v1.1 deferred items are captured in PROJECT.md and inherited by v1.2 phases — VERIFY-01..04 (Phase 13) closes the real-hardware UI back-fills from v1.1.

### Blockers/Concerns

- **CAPTURE-01 is MUST-FIX-FIRST inside Phase 9**: capture-data-hygiene policy + `.pcap`/`.pcapng` gitignore + pre-commit reject hook MUST land before any researcher does their first capture (Pitfall 17 — keystroke recovery from raw `.pcap` is deterministic via `tshark` / `USB-Keyboard-Parser`).
- **Phase 11 (8K mouse) mid-phase research flag**: zero 3rd-party OSS corpus exists for `3151:5007`; if Phase 9 captures reveal AJ199 V1.0 vs Max envelope diverges materially, invoke `/gsd-research-phase` on the SONiX 3151 chipset family before committing to a factory split.
- **Phase 12 (AK980 PRO) mid-phase research flag**: if Phase 9 captures reveal TFT cmd 0x72 / per-key RGB / macros / layers materially divergent from the TaxMachine baseline, invoke `/gsd-research-phase` on the Microdia 0c45 chipset family.
- **Concurrent agent cap**: 2 max in autonomous runs. Three concurrent agents created a git race during v1.1 (split Phase 7 atomic commit + forced Phase 5 `--no-verify`).
- **COD-031 boundary**: no `nlohmann::json` in `ajazz_core` or any installed public header. Capture-extraction tooling is dev-time Python; runtime code reads `std::array<uint8_t>` literals.

## Deferred Items

| Category         | Item                                                                          | Status                            | Deferred At              |
| ---------------- | ----------------------------------------------------------------------------- | --------------------------------- | ------------------------ |
| v1.3+ (KEYBOARD) | AK980 PRO per-key custom RGB / macros / layers / battery (KEYBOARD-05..08)    | Pending captures                  | v1.2 milestone-bootstrap |
| v1.2.x (DISPLAY) | AK980 PRO 1.14" TFT chunked image upload (DISPLAY-05; cmd 0x72)               | Capture in Phase 9; impl deferred | v1.2 milestone-bootstrap |
| v1.2.x / v1.3    | AKP815 + Mirabox N3 promotion (devices not physically connected)              | Blocked on captures               | v1.1 close               |
| v1.2.x           | Explicit `Toast.qml` cap=1 implementation (A-05)                              | Carried                           | v1.1 close               |
| v1.2.x           | TimeSyncService Pitfall-13 contextual INFO message                            | Carried                           | v1.1 close               |
| v1.2.x           | Codename→maturity map → Qt resource + runtime YAML parse (if catalogue grows) | Carried                           | v1.1 close               |
| v1.2.x           | libFuzzer Fedora packaging once `libclang_rt.fuzzer.a` lands                  | Upstream                          | v1.1 close               |

## Session Continuity

Last session: 2026-05-15 — `/gsd-new-milestone v1.2` (research → requirements → roadmap)
Stopped at: ROADMAP.md written; 33 v1.2 requirements mapped across Phases 9-13; 100% coverage validated.
Resume file: none — Phase 9 ready to plan via `/gsd-plan-phase 9`.

## Operator Next Steps

- `/gsd-plan-phase 9` to decompose Phase 9 (Research, Captures, Hygiene) into atomic plans. CAPTURE-01 is the first plan (gates everything else).
- After Phase 9 ships, `/gsd-plan-phase 10` to start the AKP03 0x3004 promotion (canonical device-promotion template).
