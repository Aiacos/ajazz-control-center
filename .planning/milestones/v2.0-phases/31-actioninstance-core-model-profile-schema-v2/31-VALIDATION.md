---
phase: 31
slug: actioninstance-core-model-profile-schema-v2
status: draft
nyquist_compliant: true
wave_0_complete: false
created: 2026-06-08
---

# Phase 31 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.
> Derived from 31-RESEARCH.md "Validation Architecture".

______________________________________________________________________

## Test Infrastructure

| Property               | Value                                                                                      |
| ---------------------- | ------------------------------------------------------------------------------------------ |
| **Framework**          | Catch2 (`Catch2::Catch2WithMain`), monolithic `ajazz_unit_tests` target                    |
| **Config file**        | `tests/unit/CMakeLists.txt` (single `add_executable` source list + `catch_discover_tests`) |
| **Quick run command**  | `ctest --preset linux-release -R action_instance`                                          |
| **Full suite command** | `ctest --preset linux-release`                                                             |
| **Estimated runtime**  | ~quick: \<5s; full: ~couple min (~408 cases)                                               |

______________________________________________________________________

## Sampling Rate

- **After every task commit:** Run `ctest --preset linux-release -R action_instance` AND `grep -rn nlohmann src/core/include/` (expect 0).
- **After every plan wave:** Run `ctest --preset linux-release -R "profile|action_instance"` (catch profile-serialization regressions).
- **Before `/gsd:verify-work`:** Full `ctest --preset linux-release` must be green.
- **Max feedback latency:** ~5 seconds (quick run).

______________________________________________________________________

## Per-Task Verification Map

| Task ID  | Plan | Wave | Requirement | Threat Ref | Secure Behavior                       | Test Type         | Automated Command                                            | File Exists                        | Status     |
| -------- | ---- | ---- | ----------- | ---------- | ------------------------------------- | ----------------- | ------------------------------------------------------------ | ---------------------------------- | ---------- |
| 31-01-\* | 01   | 1    | BIND-01     | —          | N/A (internal model)                  | unit              | `ctest --preset linux-release -R action_instance`            | ❌ W0 (`test_action_instance.cpp`) | ⬜ pending |
| 31-01-\* | 01   | 1    | BIND-01     | —          | COD-031 boundary held                 | grep gate         | `grep -rn nlohmann src/core/include/` (expect 0)             | ✅ existing invariant              | ⬜ pending |
| 31-02-\* | 02   | 2    | BIND-02     | —          | lossless v1→v2 load                   | unit (migration)  | `ctest --preset linux-release -R action_instance`            | ❌ W0                              | ⬜ pending |
| 31-02-\* | 02   | 2    | BIND-02     | —          | additive `instance` (nullopt absent)  | unit              | `ctest --preset linux-release -R action_instance`            | ❌ W0                              | ⬜ pending |
| 31-02-\* | 02   | 2    | BIND-02     | —          | existing v1.3 profile loads unchanged | unit (regression) | `ctest --preset linux-release -R "profile\|action_instance"` | ✅ regression-guard                | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*
*Plan/wave split is indicative — the planner finalizes task IDs.*

______________________________________________________________________

## Wave 0 Requirements

- [ ] `tests/unit/test_action_instance.cpp` — covers BIND-01, BIND-02 (0/1/3-state + 2-children round-trip, v1→v2 migration fold, no-`instance`→nullopt on Binding + EncoderBinding, currentState out-of-range clamp).
- [ ] `tests/unit/CMakeLists.txt` — add `test_action_instance.cpp` to the `ajazz_unit_tests` source list (after line 184).
- [ ] `src/core/include/ajazz/core/action_instance.hpp` — new nlohmann-free header.
- [ ] Framework install: none — Catch2 already vendored and linked.

______________________________________________________________________

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions                                                                           |
| -------- | ----------- | ---------- | ------------------------------------------------------------------------------------------- |
| —        | —           | —          | All phase behaviors have automated verification (round-trip + migration units + grep gate). |

*No device/UI behavior in this phase — pure core serialization.*

______________________________________________________________________

## Validation Sign-Off

- [x] All tasks have `<automated>` verify or Wave 0 dependencies
- [x] Sampling continuity: no 3 consecutive tasks without automated verify
- [x] Wave 0 covers all MISSING references
- [x] No watch-mode flags
- [x] Feedback latency < 5s (quick run)
- [x] `nyquist_compliant: true` set in frontmatter

**Approval:** pending
