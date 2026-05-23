---
phase: 15
slug: stream-dock-input-routing
status: draft
nyquist_compliant: false
wave_0_complete: false
created: 2026-05-23
---

# Phase 15 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

______________________________________________________________________

## Test Infrastructure

| Property               | Value                                             |
| ---------------------- | ------------------------------------------------- |
| **Framework**          | Catch2 (C++ unit) under CMake/CTest               |
| **Config file**        | `CMakePresets.json` (preset `linux-release`)      |
| **Quick run command**  | `ctest --preset linux-release -R StreamDockInput` |
| **Full suite command** | `ctest --preset linux-release`                    |
| **Estimated runtime**  | quick \<10s; full ~minutes                        |

Hardware-free: feed canned input frames via `MockTransport::enqueueRead(frame)`, pump
`Akp05Device::poll()`, and assert spy `ActionExecutors` ran the expected `ActionChain`. The
`makeAkp05WithTransport` DI factory plus a fake `DeviceLookup`/active-profile provide the seams.
ASCII-only `TEST_CASE`/`SECTION` titles. Live encoder/touch witness deferred to Phase 25.

______________________________________________________________________

## Sampling Rate

- **After every task commit:** `ctest --preset linux-release -R StreamDockInput`
- **After every plan wave:** `ctest --preset linux-release`
- **Before `/gsd:verify-work`:** full suite green
- **Max feedback latency:** ~10s (targeted)

______________________________________________________________________

## Per-Task Verification Map

> Filled by the planner — one row per task. Template row:

| Task ID  | Plan | Wave | Requirement | Threat Ref | Secure Behavior                                          | Test Type | Automated Command                                 | File Exists | Status     |
| -------- | ---- | ---- | ----------- | ---------- | -------------------------------------------------------- | --------- | ------------------------------------------------- | ----------- | ---------- |
| 15-01-01 | 01   | 1    | INPUT-03    | —          | bound key press fires its onPress chain via ActionEngine | unit      | `ctest --preset linux-release -R StreamDockInput` | ❌ W0       | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

______________________________________________________________________

## Wave 0 Requirements

- [ ] `tests/unit/test_stream_dock_input.cpp` — stubs covering INPUT-03/04/05 (key press/release dispatch; encoder CW≠CCW + press→synthetic-release + 16 ms coalesce; touch tap-zone→encoder onPress + swipe→page-nav intent)
- [ ] Reuse `tests/unit/fixtures/mock_transport.hpp` + `makeAkp05WithTransport` + spy `ActionExecutors` — no new framework

______________________________________________________________________

## Manual-Only Verifications

| Behavior                                                                                                | Requirement                   | Why Manual                                                                                     | Test Instructions                                                                    |
| ------------------------------------------------------------------------------------------------------- | ----------------------------- | ---------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------ |
| Live key/encoder/touch on the physical AKP05E fires the bound action; touch zones + swipe map correctly | INPUT-03/04/05 (live witness) | Requires the physical AKP05E + working `uaccess` ACL; touch zone/coordinate map is provisional | Deferred to Phase 25; MockTransport-fed dispatch tests are the Phase 15 gating proof |

______________________________________________________________________

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references
- [ ] No watch-mode flags
- [ ] Feedback latency < ~10s (targeted)
- [ ] `nyquist_compliant: true` set in frontmatter

**Approval:** pending
