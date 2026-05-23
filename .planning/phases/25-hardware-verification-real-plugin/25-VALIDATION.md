---
phase: 25
slug: hardware-verification-real-plugin
status: draft
nyquist_compliant: false
wave_0_complete: false
created: 2026-05-23
---

# Phase 25 — Validation Strategy

> Per-phase validation contract. Phase 25 is a HARDWARE-GATED verification phase: the
> automated proof is "the Phase 14-24 suite is green"; the phase-specific witness is a human
> operator + a real `.sdPlugin` against the physical AKP05E.

______________________________________________________________________

## Test Infrastructure

| Property               | Value                                                                                   |
| ---------------------- | --------------------------------------------------------------------------------------- |
| **Framework**          | Catch2 full suite (the 14-24 gate) + a human-executed `25-UAT.md` runbook on the device |
| **Quick run command**  | `ctest --preset linux-release` (full suite green = the slice exists)                    |
| **Full suite command** | `ctest --preset linux-release`                                                          |
| **Estimated runtime**  | suite ~minutes; UAT operator-paced                                                      |

The automated gate is the existing Phase 14-24 suite (no new unit code expected, beyond small
RE-doc/code corrections when the hardware contradicts a provisional §5 value). The phase-specific
verification is **manual** (the device + a real plugin) — it cannot be auto-executed.

______________________________________________________________________

## Sampling Rate

- **Before the UAT:** full suite green (the slice is built + verified by Phases 14-24).
- **UAT:** operator walks `25-UAT.md` against the connected AKP05E.

______________________________________________________________________

## Per-Task Verification Map

> Filled by the planner. Template row:

| Task ID  | Plan | Wave | Requirement          | Threat Ref       | Secure Behavior                                                                                                                           | Test Type | Automated Command                                           | File Exists | Status     |
| -------- | ---- | ---- | -------------------- | ---------------- | ----------------------------------------------------------------------------------------------------------------------------------------- | --------- | ----------------------------------------------------------- | ----------- | ---------- |
| 25-01-01 | 01   | 1    | VERIFY-05            | T-25-03          | akp05e descriptor advertises hasClock=false (no Sync button); DEVICES-11/ARCH-05 honesty correction                                       | automated | `grep ... hasClock = false` in register.cpp                 | ❌          | ⬜ pending |
| 25-01-02 | 01   | 1    | VERIFY-05, VERIFY-06 | T-25-03          | 25-UAT.md authored: all 10 VERIFY-05 rows + 3 provisional-§5 confirm-or-correct rows + VERIFY-06 round-trip exist                         | automated | `test -f 25-UAT.md && grep -c 'result: [pending]'`          | ❌          | ⬜ pending |
| 25-02-01 | 02   | 2    | VERIFY-05            | T-25-06          | operator walks VERIFY-05 live on AKP05E (image, key, 4 encoders CW/CCW/press, touch tap-zone, swipe, brightness, clear, hasClock=false)   | manual    | `25-UAT.md` (operator-witnessed)                            | ❌          | ⬜ pending |
| 25-02-02 | 02   | 2    | VERIFY-06            | T-25-04, T-25-05 | real .sdPlugin registers over loopback WS, setImage paints a key, physical press/turn delivers keyDown/dialRotate with observable effect  | manual    | `25-UAT.md` (operator-witnessed)                            | ❌          | ⬜ pending |
| 25-02-03 | 02   | 2    | VERIFY-05            | —                | provisional §5 (DRA / encoder-overlay framing / touch zone+swipe) reconciled; RE doc corrected where hardware contradicts (hardware wins) | automated | `grep ... provisional` in akp05\*.md; ctest if code changed | ❌          | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

______________________________________________________________________

## Wave 0 Requirements

- [ ] `.planning/phases/25-hardware-verification-real-plugin/25-UAT.md` — the operator runbook (VERIFY-05 per-criterion + the provisional-§5 confirm-or-correct rows + `hasClock=false` row + VERIFY-06 real-`.sdPlugin` round-trip)
- [ ] Full Phase 14-24 ctest suite green is the precondition (the slice exists)

______________________________________________________________________

## Manual-Only Verifications

| Behavior                                                                                                                                     | Requirement | Why Manual                                                 | Test Instructions                                                                           |
| -------------------------------------------------------------------------------------------------------------------------------------------- | ----------- | ---------------------------------------------------------- | ------------------------------------------------------------------------------------------- |
| Image on key; key press fires action; each of 4 encoders rotate(CW/CCW)+press; touch tap-zone + swipe-page; brightness slider; clear-all     | VERIFY-05   | Requires the physical AKP05E + a human observing the panel | `25-UAT.md` per-criterion checklist on the connected device (replug/`setfacl` if root-only) |
| Provisional §5 (DRA rect / ENC-vs-zone / touch zone+swipe) reconciled; RE doc updated where hardware contradicts; `hasClock=false` confirmed | VERIFY-05   | Hardware ground-truth; hardware wins                       | Confirm-or-correct rows in `25-UAT.md`; update `akp05*.md` + code on mismatch               |
| A real third-party `.sdPlugin` registers, paints a key via setImage, and receives keyDown/dialRotate from a physical press/turn              | VERIFY-06   | Requires a spawned real plugin + the device                | Run a sample Elgato/Mirabox `.sdPlugin`; observe the round-trip                             |

______________________________________________________________________

## Validation Sign-Off

- [ ] Full Phase 14-24 suite green (the slice exists)
- [ ] `25-UAT.md` runbook authored + walked by the operator
- [ ] Provisional §5 items reconciled (RE doc updated where hardware contradicts)
- [ ] `hasClock=false` confirmed on the AKP05E row
- [ ] A real `.sdPlugin` round-trip witnessed (VERIFY-06)

**Approval:** pending
