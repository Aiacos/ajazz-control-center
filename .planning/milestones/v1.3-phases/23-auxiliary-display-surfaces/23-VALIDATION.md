---
phase: 23
slug: auxiliary-display-surfaces
status: draft
nyquist_compliant: true
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
| **Quick run command**  | `ctest --preset linux-release -R "stream-dock-control\|StreamDockControlService"`                        |
| **Full suite command** | `ctest --preset linux-release`                                                                           |
| **Estimated runtime**  | quick \<15s; full ~minutes                                                                               |

Hardware-free for the wiring: the backend aux paths are byte-tested already; Phase 23 asserts the
app **control-service** layer emits the right wire via `MockTransport` (ENC index byte, MAI
whole-strip envelope, DRA rect header `x = zone*200`). Phase 23 EXTENDS the Phase-14
`test_stream_dock_control_service.cpp` (tag `[stream-dock-control]`) rather than adding a parallel
test target — the aux assigns are siblings of `assignKeyImage`. The LIVE confirmation (image appears
on the encoder zone / strip; DRA rect lands correctly; ENC-vs-zone reconciled) is the **Phase-25**
hardware witness. ASCII test names.

______________________________________________________________________

## Sampling Rate

- **After every task commit:** the quick `-R` run above
- **After every plan wave:** `ctest --preset linux-release`
- **Before `/gsd:verify-work`:** full suite green + `git diff --stat` excludes `akp05.cpp` / `akp05_protocol.hpp`
- **Max feedback latency:** ~15s (targeted)

______________________________________________________________________

## Per-Task Verification Map

> One row per task across both plans. The STOP gate (23-01-01) is a verification-only checkpoint.

| Task ID  | Plan | Wave | Requirement | Threat Ref            | Secure Behavior                                                                                                           | Test Type   | Automated Command                                                                                            | File Exists   | Status     |
| -------- | ---- | ---- | ----------- | --------------------- | ------------------------------------------------------------------------------------------------------------------------- | ----------- | ------------------------------------------------------------------------------------------------------------ | ------------- | ---------- |
| 23-01-01 | 01   | 1    | DISPLAY-10  | —                     | STOP gate — confirm Phase 14 service exists + `14-02-SUMMARY.md` present + Phase-14 test green before any wiring          | checkpoint  | `ls src/app/src/stream_dock_control_service.hpp && ctest --preset linux-release -R StreamDockControlService` | ✅ (Phase 14) | ⬜ pending |
| 23-01-02 | 01   | 1    | DISPLAY-10  | T-23-01..05 / T-23-SC | MAI/ENC/DRA assign via held handle + dynamic_cast null-check; index/zone range-check not bypassed; ENC kept (PROVISIONAL) | unit/wire   | `ctest --preset linux-release -R "stream-dock-control\|StreamDockControlService"`                            | ❌ W0         | ⬜ pending |
| 23-02-01 | 02   | 2    | DISPLAY-10  | T-23b-01..04          | `repaintEncodersFromProfile` paints each bound encoder via DRA zone (x=i\*200); ENC fallback reachable                    | unit/wire   | `ctest --preset linux-release -R "stream-dock-control\|StreamDockControlService"`                            | ❌ W0         | ⬜ pending |
| 23-02-02 | 02   | 2    | DISPLAY-10  | T-23b-01/04           | encoder repaint wired into the existing `profileChanged` path; no second monitor/accessor/timer                           | build/integ | `cmake --build --preset linux-release && grep -n repaintEncodersFromProfile src/app/src/application.cpp`     | ❌ W0         | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

______________________________________________________________________

## Wave 0 Requirements

- [ ] STOP gate (23-01-01): `src/app/src/stream_dock_control_service.{hpp,cpp}` exist, `14-02-SUMMARY.md` exists, and `ctest -R StreamDockControlService` is green — else halt (RESEARCH Pitfall 1).
- [ ] Extend `tests/unit/test_stream_dock_control_service.cpp` (NOT a new file) — control-service assign to encoder overlay (ENC index byte at 12), main strip (MAI, no index byte), touch-strip zone (DRA rect `x=zone*200`); dims from the descriptor (100x100 / 800x100), not hardcoded at the call site; encoder repaint from `Profile::encoders` (DISPLAY-10).
- [ ] Reuse `MockTransport` + `makeAkp05WithTransport` + `tests::qtGuiApp()`; the backend DRA/ENC/MAI byte tests (`test_akp05_touch_strip.cpp`, `test_akp05_protocol.cpp`) already exist — do not duplicate them.

______________________________________________________________________

## Manual-Only Verifications

| Behavior                                                                                                                            | Requirement               | Why Manual                                                              | Test Instructions                                                                                                                                                        |
| ----------------------------------------------------------------------------------------------------------------------------------- | ------------------------- | ----------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| An assigned image appears on the encoder zone / main strip / touch strip; the DRA rect lands where expected; ENC-vs-zone reconciled | DISPLAY-10 (live framing) | Requires the physical AKP05E + `uaccess` ACL; §5 framing is provisional | Deferred to Phase 25 (VERIFY-05) — the hardware witness reconciles ENC-vs-zone + DRA rect and updates the RE doc; MockTransport wire tests are the Phase 23 gating proof |

______________________________________________________________________

## Validation Sign-Off

- [x] All tasks have `<automated>` verify or Wave 0 dependencies (STOP gate uses a checkpoint `<how-to-verify>`)
- [x] Sampling continuity: no 3 consecutive tasks without automated verify
- [x] Wave 0 covers all MISSING references
- [x] No watch-mode flags
- [x] Feedback latency < ~15s (targeted)
- [x] `nyquist_compliant: true` set in frontmatter

**Approval:** planner-complete (pending execution)
