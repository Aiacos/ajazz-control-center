---
phase: 24
slug: family-coverage-akp03-153-815
status: draft
nyquist_compliant: false
wave_0_complete: false
created: 2026-05-23
---

# Phase 24 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

______________________________________________________________________

## Test Infrastructure

| Property               | Value                                                                     |
| ---------------------- | ------------------------------------------------------------------------- |
| **Framework**          | Catch2 (C++) under CMake/CTest; `MockTransport` + per-family DI factories |
| **Quick run command**  | \`ctest --preset linux-release -R "StreamDockFamily                       |
| **Full suite command** | `ctest --preset linux-release`                                            |
| **Estimated runtime**  | quick \<15s; full ~minutes                                                |

Hardware-free: per-family `MockTransport` byte tests via the registry / `makeAkp0*WithTransport`
factories assert the capability-generic control service drives each family from its descriptor
(key count, grid, encoder count, rotation/format). Live confirmation covers whatever family member
is connected → Phase 25. ASCII test names.

______________________________________________________________________

## Sampling Rate

- **After every task commit:** the quick `-R` run above
- **After every plan wave:** `ctest --preset linux-release`
- **Before `/gsd:verify-work`:** full suite green
- **Max feedback latency:** ~15s (targeted)

______________________________________________________________________

## Per-Task Verification Map

> Filled by the planner. Template row:

| Task ID  | Plan | Wave | Requirement | Threat Ref | Secure Behavior                                                                        | Test Type | Automated Command                                  | File Exists | Status     |
| -------- | ---- | ---- | ----------- | ---------- | -------------------------------------------------------------------------------------- | --------- | -------------------------------------------------- | ----------- | ---------- |
| 24-01-01 | 01   | 1    | DEVICES-10  | —          | control service drives AKP03/153/815 from the descriptor (no hardcoded AKP05 geometry) | unit      | `ctest --preset linux-release -R StreamDockFamily` | ❌ W0       | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

______________________________________________________________________

## Wave 0 Requirements

- [ ] `tests/unit/test_stream_dock_family.cpp` — control-service assign-image + input-fires-action on AKP03 (6 keys + 3 encoders), AKP153 (15 keys), AKP815 (15 keys) via MockTransport; per-family rotation/format from the descriptor (DEVICES-10)
- [ ] Reuse the per-family backends + `image_pipeline` transforms + the AKP05E control-service tests as the template; no new framework

______________________________________________________________________

## Manual-Only Verifications

| Behavior                                                                           | Requirement       | Why Manual                                                                    | Test Instructions                                                                                      |
| ---------------------------------------------------------------------------------- | ----------------- | ----------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------ |
| A connected AKP03/153/815 shows an assigned image and a key press fires the action | DEVICES-10 (live) | Requires the physical device + `uaccess` ACL; AKP153/815 may not be connected | Deferred to Phase 25 / whatever is connected; MockTransport family tests are the Phase 24 gating proof |

______________________________________________________________________

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references
- [ ] No watch-mode flags
- [ ] Feedback latency < ~15s (targeted)
- [ ] `nyquist_compliant: true` set in frontmatter

**Approval:** pending
