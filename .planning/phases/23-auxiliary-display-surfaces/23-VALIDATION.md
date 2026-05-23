---
phase: 23
slug: auxiliary-display-surfaces
status: draft
nyquist_compliant: false
wave_0_complete: false
created: 2026-05-23
---

# Phase 23 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

______________________________________________________________________

## Test Infrastructure

| Property               | Value                                                                                                    |
| ---------------------- | -------------------------------------------------------------------------------------------------------- |
| **Framework**          | Catch2 (C++) under CMake/CTest; `MockTransport` + `makeAkp05WithTransport` service-level wire assertions |
| **Config file**        | `CMakePresets.json` (preset `linux-release`)                                                             |
| **Quick run command**  | \`ctest --preset linux-release -R "StreamDockAuxSurfaces                                                 |
| **Full suite command** | `ctest --preset linux-release`                                                                           |
| **Estimated runtime**  | quick \<15s; full ~minutes                                                                               |

Hardware-free for the wiring: the backend aux paths are byte-tested already; Phase 23 asserts the
app **control-service** layer emits the right wire via `MockTransport` (ENC index byte, MAI
whole-strip envelope, DRA rect header `x = zone*200`). The LIVE confirmation (image appears on the
encoder zone / strip; DRA rect lands correctly) is the **Phase-25** hardware witness. ASCII names.

______________________________________________________________________

## Sampling Rate

- **After every task commit:** the quick `-R` run above
- **After every plan wave:** `ctest --preset linux-release`
- **Before `/gsd:verify-work`:** full suite green
- **Max feedback latency:** ~15s (targeted)

______________________________________________________________________

## Per-Task Verification Map

> Filled by the planner — one row per task. Template row:

| Task ID  | Plan | Wave | Requirement | Threat Ref | Secure Behavior                                                                 | Test Type | Automated Command                                       | File Exists | Status     |
| -------- | ---- | ---- | ----------- | ---------- | ------------------------------------------------------------------------------- | --------- | ------------------------------------------------------- | ----------- | ---------- |
| 23-01-01 | 01   | 1    | DISPLAY-10  | —          | encoder/main/touch assign emits the right wire envelope via the control service | unit      | `ctest --preset linux-release -R StreamDockAuxSurfaces` | ❌ W0       | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

______________________________________________________________________

## Wave 0 Requirements

- [ ] `tests/unit/test_stream_dock_aux_surfaces.cpp` — control-service assign to encoder overlay (ENC index byte), main strip (MAI), touch-strip zone (DRA rect `x=zone*200`); dims from descriptor not hardcoded (DISPLAY-10)
- [ ] Reuse `MockTransport` + `makeAkp05WithTransport`; the backend DRA/ENC/MAI byte tests (`test_akp05_touch_strip.cpp`) already exist — do not duplicate them

______________________________________________________________________

## Manual-Only Verifications

| Behavior                                                                                                                            | Requirement               | Why Manual                                                              | Test Instructions                                                                                                                                                        |
| ----------------------------------------------------------------------------------------------------------------------------------- | ------------------------- | ----------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| An assigned image appears on the encoder zone / main strip / touch strip; the DRA rect lands where expected; ENC-vs-zone reconciled | DISPLAY-10 (live framing) | Requires the physical AKP05E + `uaccess` ACL; §5 framing is provisional | Deferred to Phase 25 (VERIFY-05) — the hardware witness reconciles ENC-vs-zone + DRA rect and updates the RE doc; MockTransport wire tests are the Phase 23 gating proof |

______________________________________________________________________

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references
- [ ] No watch-mode flags
- [ ] Feedback latency < ~15s (targeted)
- [ ] `nyquist_compliant: true` set in frontmatter

**Approval:** pending
