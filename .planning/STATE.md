---
gsd_state_version: 1.0
milestone: v1.2
milestone_name: Connected-Device Capability Parity
status: executing
stopped_at: ARCH-06 ratified at default verdict; Phase 9 partial-scope complete (7/7 plans landed)
last_updated: '2026-05-15T08:14:36.812Z'
last_activity: 2026-05-15
progress:
  total_phases: 5
  completed_phases: 0
  total_plans: 7
  completed_plans: 6
  percent: 0
---

# Project State

## Project Reference

See: .planning/PROJECT.md (updated 2026-05-15)

**Core value:** Honest, capability-driven control of AJAZZ hardware with a sandboxed plugin system — never lying about what a device can do, never crashing when a device is yanked, never silently leaking host state into plugin children.
**Current focus:** Phase 9 — Research, Captures, Hygiene (gating phase; captures-driven; CAPTURE-01 MUST-FIX-FIRST).

## Current Position

Phase: 9 of 13 (Research, Captures, Hygiene) — first v1.2 phase
Plan: 7 of 7 complete (09-01 + 09-02 + 09-03 landed; 09-04 MockTransport next)
Status: Ready to execute
Last activity: 2026-05-15

Progress: [█████████░] 86%

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
- \[Phase 9\]: ARCH-04 default verdict ratified at `.planning/phases/09-research-captures-hygiene/ARCH-04.md` — AKP03 image-pipeline at `src/devices/streamdeck/src/image_pipeline.{hpp,cpp}` (Option C), PRIVATE-linked to `ajazz_devices_streamdeck`; Option B (new `ajazz_imaging` static lib) deferred to v1.3+; D-05 honesty contract preserved (status: DEFAULT VERDICT — PENDING CAPTURE CONFIRMATION) — Phase 10 gates on Phase 9.x captures-confirmation run (Pitfall 22). [commit: 60f3140]
- \[Phase ?\]: Phase 10/11/12 reuse pattern: makeAjSeriesWithTransport public factory overload exposes anonymous-namespace COD-026 DI ctor across TU boundaries (CAPTURE-04)
- \[Phase ?\]: MockTransport is header-only under tests/unit/fixtures/ in the ajazz::tests:: namespace; static_asserts lock rule-of-five contract inherited from ITransport (CAPTURE-04)
- \[Phase Phase 9\]: ARCH-05 default verdict ratified at .planning/phases/09-research-captures-hygiene/ARCH-05.md - per-device IClockCapable::setTime outcome: hasClock=false on akp03_variant_3004 and ak980pro; setTime stays NotImplemented; PROJECT.md Out-of-Scope row preserved; Pitfall 19 three-witness rule STRUCTURALLY unsatisfiable for clock on AKP03 + ak980pro; anti-feature forbidden: synthesizing fake setSystemTimeOn from bytes that look like time; acceptable alternative: host-rendered TftClockWidget via display capability (DISPLAY-05, v1.2.x); D-05 honesty contract preserved (status: DEFAULT VERDICT - PENDING CAPTURE CONFIRMATION); Phase 10 DEVICES-05 + Phase 12 DEVICES-06 + Phase 13 VERIFY-01/03 bind to this ADR; gate on Phase 9.x finalization run. [commit: 5410c2a] — Four-corpus convergence (mirajazz + opendeck-akp03 + ajazz-sdk + TaxMachine AK820 Pro) shows NO RTC opcode in any AJAZZ reference corpus. Pitfall 19 three-witness rule applied: round-trip witness STRUCTURALLY unavailable on AKP03 (no firmware-rendered LCD clock widget per docs/protocols/streamdeck/akp03.md:113-114) and on ak980pro (TFT clock is host-pushed image via cmd 0x72, not firmware time). Two of three witnesses unavailable; even positive capture witness alone cannot satisfy promotion. v1.1 D-02 honesty contract reinforced - no lying success UX on setTime returning Ok when device cannot.
- \[Phase ?\]: \[Phase 9\]: ARCH-06 default verdict ratified at .planning/phases/09-research-captures-hygiene/ARCH-06.md — composite-HID dedup NOT firing in DeviceRegistry::enumerate (topology evidence from live lsusb 2026-05-15 refutes the composite hypothesis at the USB devicefs layer); 0c45:7016 enters Phase 13 DEVICES-08 as separate microdia_dongle_7016 at probed tier; v1.1 ARCH-02 (vid, pid, serial) keying preserved unchanged. D-05 honesty contract preserved (status: DEFAULT VERDICT — PENDING CAPTURE CONFIRMATION). Captures-confirmation trigger is a 2-minute physical unplug test (no capture tooling required). CONDITIONAL: if test contradicts, new Phase 12.5 lands dedup BEFORE Phase 12 and Phase 13 re-sequences (LOW probability). [commit: 4619bb8]

### Pending Todos

None tracked in `.planning/todos/`. v1.1 deferred items are captured in PROJECT.md and inherited by v1.2 phases — VERIFY-01..04 (Phase 13) closes the real-hardware UI back-fills from v1.1.

### Blockers/Concerns

- **CAPTURE-01 is MUST-FIX-FIRST inside Phase 9**: capture-data-hygiene policy + `.pcap`/`.pcapng` gitignore + pre-commit reject hook MUST land before any researcher does their first capture (Pitfall 17 — keystroke recovery from raw `.pcap` is deterministic via `tshark` / `USB-Keyboard-Parser`).
- **Phase 11 (8K mouse) mid-phase research flag**: zero 3rd-party OSS corpus exists for `3151:5007`; if Phase 9 captures reveal AJ199 V1.0 vs Max envelope diverges materially, invoke `/gsd-research-phase` on the SONiX 3151 chipset family before committing to a factory split.
- **Phase 12 (AK980 PRO) mid-phase research flag**: if Phase 9 captures reveal TFT cmd 0x72 / per-key RGB / macros / layers materially divergent from the TaxMachine baseline, invoke `/gsd-research-phase` on the Microdia 0c45 chipset family.
- **Concurrent agent cap**: 2 max in autonomous runs. Three concurrent agents created a git race during v1.1 (split Phase 7 atomic commit + forced Phase 5 `--no-verify`).
- **COD-031 boundary**: no `nlohmann::json` in `ajazz_core` or any installed public header. Capture-extraction tooling is dev-time Python; runtime code reads `std::array<uint8_t>` literals.

## Deferred Items

| Category            | Item                                                                          | Status                            | Deferred At              |
| ------------------- | ----------------------------------------------------------------------------- | --------------------------------- | ------------------------ |
| v1.3+ (KEYBOARD)    | AK980 PRO per-key custom RGB / macros / layers / battery (KEYBOARD-05..08)    | Pending captures                  | v1.2 milestone-bootstrap |
| v1.2.x (DISPLAY)    | AK980 PRO 1.14" TFT chunked image upload (DISPLAY-05; cmd 0x72)               | Capture in Phase 9; impl deferred | v1.2 milestone-bootstrap |
| v1.2.x / v1.3       | AKP815 + Mirabox N3 promotion (devices not physically connected)              | Blocked on captures               | v1.1 close               |
| v1.2.x              | Explicit `Toast.qml` cap=1 implementation (A-05)                              | Carried                           | v1.1 close               |
| v1.2.x              | TimeSyncService Pitfall-13 contextual INFO message                            | Carried                           | v1.1 close               |
| v1.2.x              | Codename→maturity map → Qt resource + runtime YAML parse (if catalogue grows) | Carried                           | v1.1 close               |
| v1.2.x              | libFuzzer Fedora packaging once `libclang_rt.fuzzer.a` lands                  | Upstream                          | v1.1 close               |
| Phase 9 P3          | 6min                                                                          | 2 tasks                           | 2 files                  |
| Phase 09 P05        | 3min                                                                          | 1 tasks                           | 2 files                  |
| Phase 09 P04        | 4min                                                                          | 3 tasks                           | 5 files                  |
| Phase Phase 09 PP06 | 3min                                                                          | 1 tasks tasks                     | 2 files files            |
| Phase 9 P7          | 7min                                                                          | 1 tasks                           | 6 files                  |

## Session Continuity

Last session: 2026-05-15T08:14:36.805Z
Stopped at: ARCH-06 ratified at default verdict; Phase 9 partial-scope complete (7/7 plans landed)
Resume file: None

## Operator Next Steps

- `/gsd-plan-phase 9` to decompose Phase 9 (Research, Captures, Hygiene) into atomic plans. CAPTURE-01 is the first plan (gates everything else).
- After Phase 9 ships, `/gsd-plan-phase 10` to start the AKP03 0x3004 promotion (canonical device-promotion template).
