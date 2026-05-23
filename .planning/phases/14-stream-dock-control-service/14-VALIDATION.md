---
phase: 14
slug: stream-dock-control-service
status: planned
nyquist_compliant: true
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

- **After every task commit:** `ctest --preset linux-release -R StreamDockControlService` (control service) / `-R akp05e_clock` (descriptor)
- **After every plan wave:** `ctest --preset linux-release`
- **Before `/gsd:verify-work`:** Full suite green
- **Max feedback latency:** ~10s (targeted) / full suite per wave

______________________________________________________________________

## Per-Task Verification Map

| Task ID  | Plan | Wave | Requirement                  | Threat Ref   | Secure Behavior                                                               | Test Type  | Automated Command                                                         | File Exists | Status     |
| -------- | ---- | ---- | ---------------------------- | ------------ | ----------------------------------------------------------------------------- | ---------- | ------------------------------------------------------------------------- | ----------- | ---------- |
| 14-01-01 | 01   | 1    | DEVICES-11                   | T-14a-02     | flip only akp05e row; ak980pro/mirabox_n4/akp05 untouched                     | grep       | `grep -A12 'codename = "akp05e"' src/devices/streamdeck/src/register.cpp` | ❌ W0       | ⬜ pending |
| 14-01-02 | 01   | 1    | DEVICES-11                   | T-14a-01     | regression test pins akp05e hasClock==false (cannot silently regress)         | unit       | `ctest --preset linux-release -R akp05e_clock`                            | ❌ W0       | ⬜ pending |
| 14-02-01 | 02   | 1    | DISPLAY-08                   | —            | ProfileController exposes activeProfile() const-ref (no QML coupling)         | grep       | `grep -n 'activeProfile' src/app/src/profile_controller.hpp`              | ❌ W0       | ⬜ pending |
| 14-02-02 | 02   | 1    | DISPLAY-06/07/08, DOCK-01/02 | T-14b-01..04 | LIG@open, coalesced BAT->chunk->ULEND, repaint, firmware surface, held handle | unit       | `ctest --preset linux-release -R StreamDockControlService`                | ❌ W0       | ⬜ pending |
| 14-02-03 | 02   | 1    | DISPLAY-06/08                | T-14b-01     | Application holds handle on arrival + connects profileChanged->repaint        | unit/build | `ctest --preset linux-release -R StreamDockControlService`                | ❌ W0       | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

> Note: 14-02-02 is the consolidated wire-assertion test covering DISPLAY-06/07/08 + DOCK-01/02
> via MockTransport + makeAkp05WithTransport + a fake DeviceLookup + a fake Profile; each
> requirement maps to a distinct SECTION/CHECK inside `test_stream_dock_control_service.cpp`.

______________________________________________________________________

## Wave 0 Requirements

- [ ] `tests/unit/test_stream_dock_control_service.cpp` — covers DISPLAY-06/07/08, DOCK-01/02 (created in 14-02 Task 2; offscreen Qt + MockTransport)
- [ ] `tests/unit/test_register_akp05e_clock.cpp` — DEVICES-11 `hasClock=false` assertion + mirabox_n4 untouched guard (created in 14-01 Task 2)
- [ ] Reuse `tests/unit/fixtures/mock_transport.hpp` + `makeAkp05WithTransport` — no new fixture install needed
- [ ] Small in-scope addition: `ProfileController::activeProfile()` const-ref getter (14-02 Task 1) — enables the repaint test without coupling the controller to the device layer

______________________________________________________________________

## Manual-Only Verifications

| Behavior                                                                                      | Requirement                  | Why Manual                                           | Test Instructions                                                                                  |
| --------------------------------------------------------------------------------------------- | ---------------------------- | ---------------------------------------------------- | -------------------------------------------------------------------------------------------------- |
| Panel physically lights; assigned image survives a 100-image power-cycle burst with no freeze | DISPLAY-06/07 (live witness) | Requires the physical AKP05E + working `uaccess` ACL | Deferred to Phase 25 (hardware-gated); MockTransport byte assertions are the Phase 14 gating proof |

______________________________________________________________________

## Validation Sign-Off

- [x] All tasks have `<automated>` verify or Wave 0 dependencies
- [x] Sampling continuity: no 3 consecutive tasks without automated verify
- [x] Wave 0 covers all MISSING references
- [x] No watch-mode flags
- [x] Feedback latency < ~10s (targeted)
- [x] `nyquist_compliant: true` set in frontmatter

**Approval:** planned (pending execution)
