---
phase: 21
slug: builtin-in-process-actions
status: draft
nyquist_compliant: false
wave_0_complete: false
created: 2026-05-23
---

# Phase 21 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

______________________________________________________________________

## Test Infrastructure

| Property               | Value                                                                                                           |
| ---------------------- | --------------------------------------------------------------------------------------------------------------- |
| **Framework**          | Catch2 (C++) under CMake/CTest; spy executors + injected fake `IInputSynthesizer` + mock OBS `QWebSocketServer` |
| **Config file**        | `CMakePresets.json` (preset `linux-release`)                                                                    |
| **Quick run command**  | \`ctest --preset linux-release -R "BuiltinAction                                                                |
| **Full suite command** | `ctest --preset linux-release`                                                                                  |
| **Estimated runtime**  | quick \<15s; full ~minutes                                                                                      |

Hardware-free + headless: the `BuiltinActionRegistry` is unit-tested with spy handlers; OS
synthesis runs behind `IInputSynthesizer` with an **injected fake backend** (no `/dev/uinput`,
no real OS input); the OBS client runs against a **mock `QWebSocketServer`** asserting the v5
auth handshake. ASCII test names; reuse the `test_action_engine.cpp` spy pattern + the
`test_sd_plugin_server.cpp` loopback-WS pattern.

______________________________________________________________________

## Sampling Rate

- **After every task commit:** the quick `-R` run above
- **After every plan wave:** `ctest --preset linux-release`
- **Before `/gsd:verify-work`:** full suite green
- **Max feedback latency:** ~15s (targeted)

______________________________________________________________________

## Per-Task Verification Map

> Filled by the planner — one row per task. Template row:

| Task ID  | Plan | Wave | Requirement | Threat Ref | Secure Behavior                                         | Test Type | Automated Command                               | File Exists | Status     |
| -------- | ---- | ---- | ----------- | ---------- | ------------------------------------------------------- | --------- | ----------------------------------------------- | ----------- | ---------- |
| 21-01-01 | 01   | 1    | PLUGIN-12   | T-21-hook  | global-hook capture OFF unless the opt-in toggle is set | unit      | `ctest --preset linux-release -R BuiltinAction` | ❌ W0       | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

______________________________________________________________________

## Wave 0 Requirements

- [ ] `tests/unit/test_builtin_actions.cpp` — registry dispatch (each core UUID → right handler with settings); browser→openUrl; nav→pushPage/popPage; device.brightness→control-service LIG; multiactions→ordered chain (PLUGIN-12)
- [ ] `tests/unit/test_input_synth.cpp` — injected fake `IInputSynthesizer` asserts the synthesized key-combo / text / media key; opt-in hook gate OFF by default
- [ ] `tests/unit/test_obs_client.cpp` — mock OBS `QWebSocketServer`: v5 Hello/Identify auth `Base64(SHA256(Base64(SHA256(password+salt))+challenge))`; refuses unauthenticated
- [ ] Reuse `test_action_engine.cpp` spy + `test_sd_plugin_server.cpp` loopback patterns; no new framework

______________________________________________________________________

## Manual-Only Verifications

| Behavior                                                                                | Requirement              | Why Manual                    | Test Instructions                                                                         |
| --------------------------------------------------------------------------------------- | ------------------------ | ----------------------------- | ----------------------------------------------------------------------------------------- |
| A bound hotkey/media/text action actually drives the focused OS app; OBS scene switches | PLUGIN-12 (live witness) | Real OS input + a running OBS | Deferred to Phase 25; the fake-synthesizer + mock-OBS tests are the Phase 21 gating proof |

______________________________________________________________________

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references
- [ ] No watch-mode flags
- [ ] Feedback latency < ~15s (targeted)
- [ ] `nyquist_compliant: true` set in frontmatter

**Approval:** pending
