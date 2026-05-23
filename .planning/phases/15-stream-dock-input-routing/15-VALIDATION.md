---
phase: 15
slug: stream-dock-input-routing
status: planned
nyquist_compliant: true
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
`makeAkp05WithTransport` DI factory plus a fake `ProfileAccessor` and a spy-executor
`ActionEngine` provide the seams. ASCII-only `TEST_CASE`/`SECTION` titles. Live encoder/touch
witness deferred to Phase 25.

______________________________________________________________________

## Sampling Rate

- **After every task commit:** `ctest --preset linux-release -R StreamDockInput`
- **After every plan wave:** `ctest --preset linux-release`
- **Before `/gsd:verify-work`:** full suite green
- **Max feedback latency:** ~10s (targeted)

______________________________________________________________________

## Per-Task Verification Map

> One row per task. The single 15-01 task carries the full INPUT-03/04/05 dispatch
> proof (multiple SECTIONs); 15-02 wires it into Application.

| Task ID  | Plan | Wave | Requirement    | Threat Ref       | Secure Behavior                                                                                                                                                                                             | Test Type    | Automated Command                                                                         | File Exists | Status     |
| -------- | ---- | ---- | -------------- | ---------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------ | ----------------------------------------------------------------------------------------- | ----------- | ---------- |
| 15-01-01 | 01   | 1    | INPUT-03/04/05 | T-15-01..06      | key press/release -> bound chain; encoder CW!=CCW + press->synth-release + 16ms coalesce; touch tap-zone->encoder onPress + swipe->page-nav; Sleep defers via QtExecutor; single held handle never re-opens | unit         | `ctest --preset linux-release -R StreamDockInput`                                         | ❌ W0       | ⬜ pending |
| 15-02-01 | 02   | 2    | INPUT-03/04/05 | T-15-02,03,04,05 | Application owns one QtExecutor-backed ActionEngine (runCommand=QProcess argv, no shell; keyPress/plugin stubbed); shares Phase-14 held handle on arrival; page-nav intent logged                           | unit + build | `cmake --build --preset linux-release && ctest --preset linux-release -R StreamDockInput` | ❌ (15-01)  | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

______________________________________________________________________

## Wave 0 Requirements

- [ ] `tests/unit/test_stream_dock_input_service.cpp` (created in Task 15-01-01 alongside the implementation, TDD) — covers INPUT-03/04/05 + Sleep-non-block + held-handle, via `MockTransport.enqueueRead` + spy `ActionExecutors` + a fake `ProfileAccessor` + the makeAkp05WithTransport device.
- [ ] Reuse `tests/unit/fixtures/mock_transport.hpp` + `makeAkp05WithTransport` + spy `ActionExecutors` — no new framework.
- [ ] Register the new test in `tests/unit/CMakeLists.txt`, linking `stream_dock_input_service.cpp` + `action_engine.cpp` + `qt_executor.cpp` + the streamdeck device source (mirror `test_firmware_update_service.cpp` / `test_action_engine.cpp`).
- [ ] No QML test needed (Phase 15 dispatch is headless C++; UI is Phase 16).

______________________________________________________________________

## Manual-Only Verifications

| Behavior                                                                                                | Requirement                   | Why Manual                                                                                     | Test Instructions                                                                    |
| ------------------------------------------------------------------------------------------------------- | ----------------------------- | ---------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------ |
| Live key/encoder/touch on the physical AKP05E fires the bound action; touch zones + swipe map correctly | INPUT-03/04/05 (live witness) | Requires the physical AKP05E + working `uaccess` ACL; touch zone/coordinate map is provisional | Deferred to Phase 25; MockTransport-fed dispatch tests are the Phase 15 gating proof |

______________________________________________________________________

## Validation Sign-Off

- [x] All tasks have `<automated>` verify or Wave 0 dependencies
- [x] Sampling continuity: no 3 consecutive tasks without automated verify
- [x] Wave 0 covers all MISSING references (the new test is created in 15-01-01 itself, TDD)
- [x] No watch-mode flags
- [x] Feedback latency < ~10s (targeted)
- [x] `nyquist_compliant: true` set in frontmatter

**Approval:** planned 2026-05-23
