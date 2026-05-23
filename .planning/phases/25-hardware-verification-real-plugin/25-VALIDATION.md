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

| Task ID  | Plan | Wave | Requirement | Threat Ref | Secure Behavior                                                                                               | Test Type | Automated Command      | File Exists | Status     |
| -------- | ---- | ---- | ----------- | ---------- | ------------------------------------------------------------------------------------------------------------- | --------- | ---------------------- | ----------- | ---------- |
| 25-01-01 | 01   | 1    | VERIFY-05   | —          | image-on-key, key press, 4 encoders CW/CCW/press, touch tap+swipe, brightness, clear all verified by operator | manual    | `25-UAT.md` (operator) | ❌          | ⬜ pending |

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
