---
phase: 17
slug: plugin-protocol-completion
status: draft
nyquist_compliant: false
wave_0_complete: false
created: 2026-05-23
---

# Phase 17 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

______________________________________________________________________

## Test Infrastructure

| Property               | Value                                                                       |
| ---------------------- | --------------------------------------------------------------------------- |
| **Framework**          | Catch2 (C++) under CMake/CTest; loopback `QWebSocket` client + `QSignalSpy` |
| **Config file**        | `CMakePresets.json` (preset `linux-release`)                                |
| **Quick run command**  | \`ctest --preset linux-release -R "SdPluginServer                           |
| **Full suite command** | `ctest --preset linux-release`                                              |
| **Estimated runtime**  | quick \<15s; full ~minutes                                                  |

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

> Filled by the planner — one row per task. Template row:

| Task ID  | Plan | Wave | Requirement | Threat Ref | Secure Behavior                                       | Test Type | Automated Command                            | File Exists | Status     |
| -------- | ---- | ---- | ----------- | ---------- | ----------------------------------------------------- | --------- | -------------------------------------------- | ----------- | ---------- |
| 17-01-01 | 01   | 1    | PLUGIN-05   | T-17-auth  | rejects after 5 bad challenge attempts, socket closed | unit      | `ctest --preset linux-release -R PluginAuth` | ❌ W0       | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

______________________________________________________________________

## Wave 0 Requirements

- [ ] Extend `tests/unit/test_sd_plugin_server.cpp` (or a new `test_sd_plugin_protocol.cpp`) — loopback client asserts: LocalHost+random-port invariant (PLUGIN-01); envelope round-trip (PLUGIN-02); all 13+26 actions route without `unhandledEventReceived` (PLUGIN-03); host→plugin events arrive with correct envelope incl. `dialRotate` ticks/pressed/controller (PLUGIN-04); passHello salt + `sha256(password+salt)` accept/reject + reject-after-5 (PLUGIN-05)
- [ ] **INVERT** the existing `setBG → unhandledEventReceived` assertion (test_sd_plugin_server.cpp:171-193) — `setBG` now routes via `actionReceived`
- [ ] Reuse `QCryptographicHash::Sha256` (no new dep)

______________________________________________________________________

## Manual-Only Verifications

| Behavior                                                                                           | Requirement                 | Why Manual                                                       | Test Instructions                                                                  |
| -------------------------------------------------------------------------------------------------- | --------------------------- | ---------------------------------------------------------------- | ---------------------------------------------------------------------------------- |
| A real third-party `.sdPlugin`'s JS shim completes registerPlugin→passHello and exchanges messages | PLUGIN-02/04 (live interop) | Needs a spawned plugin process (Phase 18) + device (Phase 19/25) | Deferred to Phase 25 (VERIFY-06); the loopback client is the Phase 17 gating proof |

______________________________________________________________________

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references
- [ ] No watch-mode flags
- [ ] Feedback latency < ~15s (targeted)
- [ ] `nyquist_compliant: true` set in frontmatter

**Approval:** pending
