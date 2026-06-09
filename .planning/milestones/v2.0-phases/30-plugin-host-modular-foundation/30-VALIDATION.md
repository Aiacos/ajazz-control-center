---
phase: 30
slug: plugin-host-modular-foundation
status: draft
nyquist_compliant: false
wave_0_complete: false
created: 2026-06-06
---

# Phase 30 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

______________________________________________________________________

## Test Infrastructure

| Property               | Value                                           |
| ---------------------- | ----------------------------------------------- |
| **Framework**          | Catch2 (existing)                               |
| **Config file**        | CMakePresets.json (`linux-release` preset)      |
| **Quick run command**  | `ctest --preset linux-release -R plugin`        |
| **Full suite command** | `ctest --preset linux-release`                  |
| **Estimated runtime**  | ~quick \<60s subset; full suite several minutes |

______________________________________________________________________

## Sampling Rate

- **After every task commit:** Run `ctest --preset linux-release -R plugin`
- **After every plan wave:** Run `ctest --preset linux-release`
- **Before `/gsd:verify-work`:** Full suite must be green AND `scripts/ajazz-debug plugin.list` returns `connectedCount: 0` (not a crash) in the disconnect-before-register scenario
- **Max feedback latency:** ~60 seconds (quick subset)

______________________________________________________________________

## Per-Task Verification Map

| Task ID  | Plan | Wave | Requirement | Threat Ref             | Secure Behavior                                            | Test Type | Automated Command                                                                    | File Exists | Status     |
| -------- | ---- | ---- | ----------- | ---------------------- | ---------------------------------------------------------- | --------- | ------------------------------------------------------------------------------------ | ----------- | ---------- |
| 30-W0-01 | W0   | 0    | HOST-02     | T-30-pre-reg           | Pre-registration teardown traceable via sentinel UUID      | unit      | `ctest --preset linux-release -R "disconnect.before.register"`                       | ❌ W0       | ⬜ pending |
| 30-W0-02 | W0   | 0    | HOST-02     | —                      | Pre-reg exit not counted in crash window                   | unit      | `ctest --preset linux-release -R "pre.registration.exit"`                            | ❌ W0       | ⬜ pending |
| 30-W0-03 | W0   | 0    | HOST-03     | T-30-bind/T-30-sigpipe | Loopback-only bind + SIGPIPE ignored                       | CI grep   | `grep -rn 'QHostAddress::Any' src/` (==0) + `grep -n 'SIGPIPE' src/app/src/main.cpp` | ❌ W0       | ⬜ pending |
| 30-xx    | impl | 1    | HOST-01     | —                      | Node runtime spawns/registers, no SKU branch               | unit      | `ctest --preset linux-release -R PluginManagerTest`                                  | ✅          | ⬜ pending |
| 30-xx    | impl | 1    | HOST-01     | —                      | HTML runtime loads via QWebEnginePage, no SKU branch       | unit      | `ctest --preset linux-release -R PluginManagerTest`                                  | ✅          | ⬜ pending |
| 30-xx    | impl | 1    | HOST-01     | —                      | Native runtime spawns, no SKU branch                       | unit      | `ctest --preset linux-release -R PluginManagerTest`                                  | ✅          | ⬜ pending |
| 30-xx    | impl | 1    | HOST-01     | —                      | Python runtime dispatches via OOP host on unified contract | unit      | `ctest --preset linux-release -R OutOfProcess`                                       | ✅          | ⬜ pending |
| 30-xx    | impl | 1    | HOST-02     | —                      | 3-in-30s → disable+notify (not restart)                    | unit      | `ctest --preset linux-release -R "crash 3 in 30s"`                                   | ✅          | ⬜ pending |
| 30-xx    | impl | 1    | HOST-02     | —                      | exitApp sent before QProcess::terminate                    | unit      | `ctest --preset linux-release -R "shutdown sends exitApp"`                           | ✅          | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

______________________________________________________________________

## Wave 0 Requirements

- [ ] `tests/unit/test_plugin_host2.cpp` — disconnect-before-register (HOST-02), pre-registration exit not in crash window (HOST-02), IPluginHost2 interface conformance for all four runtimes (HOST-01)
- [ ] CI step in `.github/workflows/ci.yml` — `QHostAddress::Any` grep gate (==0) + SIGPIPE presence check in main.cpp (HOST-03), following the existing `hid_open` invariant step pattern (ci.yml lines ~52-62)
- [ ] `.planning/phases/30-plugin-host-modular-foundation/30-ADR-plugin-host-unification.md` — IPluginHost2 UNIFY decision with rationale (criterion #5)

______________________________________________________________________

## Manual-Only Verifications

| Behavior                                              | Requirement | Why Manual                                   | Test Instructions                                                                                                                                                                         |
| ----------------------------------------------------- | ----------- | -------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Live crash scenario returns 0 connected (not a crash) | HOST-02     | Requires running app + debug channel         | Launch with `AJAZZ_DEBUG_CONTROL=1`, connect a plugin socket that dies before registerPlugin, run `scripts/ajazz-debug plugin.list`, confirm `connectedCount: 0` and app still responsive |
| SKU-decoupling audit (HOST-03)                        | HOST-03     | CODE-REVIEW ONLY per CONTEXT.md (no CI gate) | `grep -rn "akp05e\|akp03\|akp153" src/app/src/plugin_manager.cpp` returns 0 at audit time                                                                                                 |

______________________________________________________________________

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references
- [ ] No watch-mode flags
- [ ] Feedback latency < 60s
- [ ] `nyquist_compliant: true` set in frontmatter

**Approval:** pending
