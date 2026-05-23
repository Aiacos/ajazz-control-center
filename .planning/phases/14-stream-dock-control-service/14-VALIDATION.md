---
phase: 14
slug: stream-dock-control-service
status: draft
nyquist_compliant: false
wave_0_complete: false
created: 2026-05-23
---

# Phase 14 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

______________________________________________________________________

## Test Infrastructure

| Property               | Value                                                                 |
| ---------------------- | --------------------------------------------------------------------- |
| **Framework**          | Catch2 (C++ unit/integration) under CMake/CTest                       |
| **Config file**        | `CMakePresets.json` (preset `linux-release`)                          |
| **Quick run command**  | `ctest --preset linux-release -R StreamDockControlService`            |
| **Full suite command** | `ctest --preset linux-release`                                        |
| **Estimated runtime**  | ~quick \<10s for the targeted regex; full suite ~minutes (≈408 cases) |

Reuse the header-only `MockTransport` (`tests/unit/fixtures/mock_transport.hpp`) + the
`makeAkp05WithTransport` DI factory for byte-level wire assertions — **no hardware required**
for the Phase 14 gating proof. ASCII-only `TEST_CASE`/`SECTION` titles (Win32 CMD codepage).

______________________________________________________________________

## Sampling Rate

- **After every task commit:** `ctest --preset linux-release -R StreamDockControlService`
- **After every plan wave:** `ctest --preset linux-release`
- **Before `/gsd:verify-work`:** Full suite green
- **Max feedback latency:** ~10s (targeted) / full suite per wave

______________________________________________________________________

## Per-Task Verification Map

> Filled by the planner — one row per task with its automated command. Template row:

| Task ID  | Plan | Wave | Requirement | Threat Ref | Secure Behavior                      | Test Type | Automated Command                                          | File Exists | Status     |
| -------- | ---- | ---- | ----------- | ---------- | ------------------------------------ | --------- | ---------------------------------------------------------- | ----------- | ---------- |
| 14-01-01 | 01   | 1    | DISPLAY-06  | —          | brightness-ON `LIG` issued at open() | unit      | `ctest --preset linux-release -R StreamDockControlService` | ❌ W0       | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

______________________________________________________________________

## Wave 0 Requirements

- [ ] `tests/unit/test_stream_dock_control_service.cpp` — stubs covering DISPLAY-06/07/08, DOCK-01/02
- [ ] `tests/unit/test_register_akp05e_clock.cpp` (or extend an existing register/descriptor test) — DEVICES-11 `hasClock=false` assertion
- [ ] Reuse `tests/unit/fixtures/mock_transport.hpp` + `makeAkp05WithTransport` — no new fixture install needed

______________________________________________________________________

## Manual-Only Verifications

| Behavior                                                                                      | Requirement                  | Why Manual                                           | Test Instructions                                                                                  |
| --------------------------------------------------------------------------------------------- | ---------------------------- | ---------------------------------------------------- | -------------------------------------------------------------------------------------------------- |
| Panel physically lights; assigned image survives a 100-image power-cycle burst with no freeze | DISPLAY-06/07 (live witness) | Requires the physical AKP05E + working `uaccess` ACL | Deferred to Phase 25 (hardware-gated); MockTransport byte assertions are the Phase 14 gating proof |

______________________________________________________________________

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references
- [ ] No watch-mode flags
- [ ] Feedback latency < ~10s (targeted)
- [ ] `nyquist_compliant: true` set in frontmatter

**Approval:** pending
