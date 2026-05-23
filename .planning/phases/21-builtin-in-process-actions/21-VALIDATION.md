---
phase: 21
slug: builtin-in-process-actions
status: planned
nyquist_compliant: true
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
| **Quick run command**  | `ctest --preset linux-release -R "BuiltinAction\|InputSynth\|ObsClient"`                                        |
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

| Task ID  | Plan | Wave | Requirement | Threat Ref                            | Secure Behavior                                                                                                     | Test Type  | Automated Command                               | File Exists | Status     |
| -------- | ---- | ---- | ----------- | ------------------------------------- | ------------------------------------------------------------------------------------------------------------------- | ---------- | ----------------------------------------------- | ----------- | ---------- |
| 21-01-01 | 01   | 1    | PLUGIN-12   | T-21-hook / T-21-xplat                | STOP-gate 15/16/19 SUMMARY files; IInputSynthesizer pure-core (no Qt/nlohmann); opt-in capture gate declared OFF    | build+gate | `ctest --preset linux-release` (compile + gate) | ❌ W0       | ⬜ pending |
| 21-01-02 | 01   | 1    | PLUGIN-12   | T-21-hook / T-21-uinput               | fake backend asserts chord/text/media; capture OFF by default; uinput EACCES degrades no-op                         | unit       | `ctest --preset linux-release -R InputSynth`    | ❌ W0       | ⬜ pending |
| 21-02-01 | 02   | 1    | PLUGIN-12   | T-21-obsauth / T-21-obscrypto         | computeObsAuth byte-correct SHA256 v5; Identify only with password (auth default-on)                                | unit       | `ctest --preset linux-release -R ObsClient`     | ❌ W0       | ⬜ pending |
| 21-02-02 | 02   | 1    | PLUGIN-12   | T-21-obsauth                          | mock OBS server: Identify-on-auth vs refuse-when-no-password (zero Identify); well-formed request                   | unit       | `ctest --preset linux-release -R ObsClient`     | ❌ W0       | ⬜ pending |
| 21-03-01 | 03   | 2    | PLUGIN-12   | T-21-json                             | registry dispatch UUID->handler+verbatim settings; prefix non-match forwards; no JSON in core                       | unit       | `ctest --preset linux-release -R BuiltinAction` | ❌ W0       | ⬜ pending |
| 21-03-02 | 03   | 2    | PLUGIN-12   | T-21-hook / T-21-obsauth / T-21-lunbo | brightness/synthesis/nav/profile/multiactions/LunBo per-key; plugin-executor short-circuit; opt-in OFF; OBS auth-on | unit       | `ctest --preset linux-release -R BuiltinAction` | ❌ W0       | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

______________________________________________________________________

## Wave 0 Requirements

- [ ] `tests/unit/test_input_synth.cpp` (21-01) — injected fake `IInputSynthesizer` asserts the synthesized key-combo / text / media key; opt-in hook capture OFF by default; `makeDefaultInputSynthesizer()` non-null no-op stub (no `/dev/uinput`)
- [ ] `tests/unit/test_obs_client.cpp` (21-02) — mock OBS `QWebSocketServer`: v5 Hello/Identify auth `Base64(SHA256(Base64(SHA256(password+salt))+challenge))`; refuses unauthenticated (zero Identify when no password); well-formed op:6 request; gated `AJAZZ_HAVE_WEBSOCKETS`
- [ ] `tests/unit/test_builtin_actions.cpp` (21-03) — registry dispatch (each core UUID → right handler + verbatim settings); browser→openUrl; nav→navigate/pushPage/popPage; device.brightness→control-service setBrightness; multiactions→ordered chain; LunBo per-key cursor; system.\* → fake synth; plugin-executor short-circuit
- [ ] Reuse `test_action_engine.cpp` spy + `test_sd_plugin_server.cpp` loopback patterns; no new framework

______________________________________________________________________

## Manual-Only Verifications

| Behavior                                                                                | Requirement              | Why Manual                    | Test Instructions                                                                                                                                                                                                                                         |
| --------------------------------------------------------------------------------------- | ------------------------ | ----------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| A bound hotkey/media/text action actually drives the focused OS app; OBS scene switches | PLUGIN-12 (live witness) | Real OS input + a running OBS | Deferred to Phase 25; the fake-synthesizer + mock-OBS tests are the Phase 21 gating proof. Linux uinput needs `/dev/uinput` write perms (operator/replug concern per CLAUDE.md uaccess); Windows SendInput + macOS CGEvent are stubbed-but-compiling now. |

______________________________________________________________________

## Validation Sign-Off

- [x] All tasks have `<automated>` verify or Wave 0 dependencies
- [x] Sampling continuity: no 3 consecutive tasks without automated verify
- [x] Wave 0 covers all MISSING references
- [x] No watch-mode flags
- [x] Feedback latency < ~15s (targeted)
- [x] `nyquist_compliant: true` set in frontmatter

**Approval:** planner-complete (pending execution)
