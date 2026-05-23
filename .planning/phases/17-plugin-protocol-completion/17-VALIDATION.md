---
phase: 17
slug: plugin-protocol-completion
status: planned
nyquist_compliant: true
wave_0_complete: false
created: 2026-05-23
---

# Phase 17 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

______________________________________________________________________

## Test Infrastructure

| Property               | Value                                                                           |
| ---------------------- | ------------------------------------------------------------------------------- |
| **Framework**          | Catch2 (C++) under CMake/CTest; loopback `QWebSocket` client + `QSignalSpy`     |
| **Config file**        | `CMakePresets.json` (preset `linux-release`)                                    |
| **Quick run command**  | `ctest --preset linux-release -R "plugin-server\|PluginAuth\|SdPluginProtocol"` |
| **Full suite command** | `ctest --preset linux-release`                                                  |
| **Estimated runtime**  | quick \<15s; full ~minutes                                                      |

Hardware-free AND device-free: drive a real `QWebSocket` client against the loopback
`SdPluginServer` (extend the existing `test_sd_plugin_server.cpp` harness — leaked
`QCoreApplication` + `QSignalSpy` pattern). No device, no plugin process (Phase 18), no Property
Inspector (Phase 20). ASCII-only test names (gated `AJAZZ_HAVE_WEBSOCKETS`).

______________________________________________________________________

## Sampling Rate

- **After every task commit:** the quick `-R` run above
- **After every plan wave:** `ctest --preset linux-release`
- **Before `/gsd:verify-work`:** full suite green
- **Max feedback latency:** ~15s (targeted)

______________________________________________________________________

## Per-Task Verification Map

| Task ID  | Plan | Wave | Requirement     | Threat Ref               | Secure Behavior                                                               | Test Type | Automated Command                                           | File Exists | Status     |
| -------- | ---- | ---- | --------------- | ------------------------ | ----------------------------------------------------------------------------- | --------- | ----------------------------------------------------------- | ----------- | ---------- |
| 17-01-01 | 01   | 1    | PLUGIN-03       | T-17-FWD, T-17-DEVFWD    | all 39 actions route via actionReceived; unknown still surfaces as unhandled  | unit      | `ctest --preset linux-release -R "plugin-server"`           | ✅ extend   | ⬜ pending |
| 17-01-02 | 01   | 1    | PLUGIN-01/02/03 | T-17-LAN, T-17-JSON      | LocalHost-only re-pinned; envelope round-trips; setBG inverted; 39 route      | unit      | `ctest --preset linux-release -R "plugin-server"`           | ✅ extend   | ⬜ pending |
| 17-02-01 | 02   | 2    | PLUGIN-04       | T-17-UAF, T-17-DEVTRUST  | sendEvent re-resolves live slot each call; false for unknown uuid; no UAF     | unit      | (build) + `ctest --preset linux-release -R "plugin-server"` | ✅ extend   | ⬜ pending |
| 17-02-02 | 02   | 2    | PLUGIN-04       | T-17-UAF                 | dialRotate carries ticks/pressed/controller; §4.4 events arrive at client     | unit      | `ctest --preset linux-release -R "plugin-server"`           | ❌ W0       | ⬜ pending |
| 17-03-01 | 03   | 3    | PLUGIN-05       | T-17-REPLAY, T-17-CRYPTO | passHello with random nested authentication.salt after registerPlugin         | unit      | (build) + `ctest --preset linux-release -R "PluginAuth"`    | ❌ W0       | ⬜ pending |
| 17-03-02 | 03   | 3    | PLUGIN-05       | T-17-BRUTE, T-17-UAF     | sha256(password+salt) verify; reject-after-5 closes socket; auth before route | unit      | (build) + `ctest --preset linux-release -R "plugin-server"` | ❌ W0       | ⬜ pending |
| 17-03-03 | 03   | 3    | PLUGIN-05       | T-17-BRUTE, T-17-PREAUTH | passHello-salt, acceptsCorrectChallenge, rejectsAfter5BadAttempts loopback    | unit      | `ctest --preset linux-release -R "PluginAuth"`              | ❌ W0       | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

> **Loopback-bind invariant (T-17-LAN)** is asserted in 17-01-02 ("SdPluginServer bind loopback only on random port"): `bindAddress()==LocalHost`, `!= Any/AnyIPv4/AnyIPv6`, and no widening setter exists. The RE's #1 footgun (vendor binds `Any`) stays closed.
> **No TLS (T-17-NOTLS)** is an accepted loopback-only design constraint documented in 17-03's threat model — NOT flagged as a gap.

______________________________________________________________________

## Wave 0 Requirements

- [ ] Extend `tests/unit/test_sd_plugin_server.cpp` (no new file — reuse the harness + `ensureQCoreApp`/`waitForSpy`) — loopback client asserts: LocalHost+random-port (PLUGIN-01); envelope round-trip (PLUGIN-02); all 13+26 actions route without `unhandledEventReceived` (PLUGIN-03); host→plugin events arrive with correct envelope incl. `dialRotate` ticks/pressed/controller (PLUGIN-04); passHello salt + `sha256(password+salt)` accept/reject + reject-after-5 (PLUGIN-05)
- [ ] **INVERT** the existing `setBG → unhandledEventReceived` assertion (test_sd_plugin_server.cpp:171-193) — `setBG` now routes via `actionReceived` (17-01 Task 2)
- [ ] Reuse `QCryptographicHash::Sha256` + `QRandomGenerator::system()` (no new dep)

______________________________________________________________________

## Manual-Only Verifications

| Behavior                                                                                           | Requirement                 | Why Manual                                                       | Test Instructions                                                                  |
| -------------------------------------------------------------------------------------------------- | --------------------------- | ---------------------------------------------------------------- | ---------------------------------------------------------------------------------- |
| A real third-party `.sdPlugin`'s JS shim completes registerPlugin→passHello and exchanges messages | PLUGIN-02/04 (live interop) | Needs a spawned plugin process (Phase 18) + device (Phase 19/25) | Deferred to Phase 25 (VERIFY-06); the loopback client is the Phase 17 gating proof |

______________________________________________________________________

## Validation Sign-Off

- [x] All tasks have `<automated>` verify or Wave 0 dependencies
- [x] Sampling continuity: no 3 consecutive tasks without automated verify
- [x] Wave 0 covers all MISSING references
- [x] No watch-mode flags
- [x] Feedback latency < ~15s (targeted)
- [x] `nyquist_compliant: true` set in frontmatter

**Approval:** planner-complete (awaiting plan-checker)
