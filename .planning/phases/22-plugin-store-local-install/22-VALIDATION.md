---
phase: 22
slug: plugin-store-local-install
status: planned
nyquist_compliant: true
wave_0_complete: false
created: 2026-05-23
---

# Phase 22 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

______________________________________________________________________

## Test Infrastructure

| Property               | Value                                                                                       |
| ---------------------- | ------------------------------------------------------------------------------------------- |
| **Framework**          | Catch2 (C++) under CMake/CTest; signed/unsigned/tampered fixture `.sdPlugin` + fetcher spy  |
| **Config file**        | `CMakePresets.json` (preset `linux-release`)                                                |
| **Quick run command**  | `ctest --preset linux-release -R "PluginVerifyGate\|PluginInstallFromFile\|CatalogOffline"` |
| **Full suite command** | `ctest --preset linux-release`                                                              |
| **Estimated runtime**  | quick \<15s; full ~minutes                                                                  |

Hardware-free + network-free: build signed/unsigned/tampered fixture `.sdPlugin` archives, run the
install through the staging→verify→promote gate, and assert NO outbound request on launch/install
(fetcher spy / `ACC_STREAMDOCK_CATALOG_URL` default offline). Reuse the `manifest_signer` +
`sdplugin_extractor` fixture recipes. ASCII test names.

______________________________________________________________________

## Sampling Rate

- **After every task commit:** the quick `-R` run above
- **After every plan wave:** `ctest --preset linux-release`
- **Before `/gsd:verify-work`:** full suite green
- **Max feedback latency:** ~15s (targeted)

______________________________________________________________________

## Per-Task Verification Map

| Task ID  | Plan | Wave | Requirement | Threat Ref                                                             | Secure Behavior                                                                                                                                                                | Test Type  | Automated Command                                                      | File Exists | Status     |
| -------- | ---- | ---- | ----------- | ---------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | ---------- | ---------------------------------------------------------------------- | ----------- | ---------- |
| 22-01-01 | 01   | 1    | PLUGIN-14   | T-22-bypass                                                            | STOP-gate Phase 18; signer link + verifier defs available regardless of `AJAZZ_BUILD_PYTHON_HOST`; `verifyStagedPlugin` fail-closed (Refused) when verifier unavailable        | unit/build | `cmake --build --preset linux-release --target ajazz-control-center`   | ❌ W0       | ⬜ pending |
| 22-01-02 | 01   | 1    | PLUGIN-14   | T-22-tamper, T-22-backdoor                                             | signed→self-signed/trusted; tampered→Refused (never promoted); unsigned→Refused; signer-unavailable→Refused; network `install()` + launch sweep routed through the gate        | unit       | `ctest --preset linux-release -R PluginVerifyGate`                     | ❌ W0       | ⬜ pending |
| 22-02-01 | 02   | 2    | PLUGIN-14   | T-22-toctou, T-22-tamper-local, T-22-unsigned, T-22-zipslip, T-22-bomb | `installFromFile` staging→verify→promote into Phase-18 `installedPlugins/`; tampered always refused (staging-before-promote invariant); unsigned gated behind explicit confirm | unit       | `ctest --preset linux-release -R PluginInstallFromFile`                | ❌ W0       | ⬜ pending |
| 22-02-02 | 02   | 2    | PLUGIN-14   | T-22-phonehome                                                         | NO outbound request on construction/launch (opt-in flag OFF default); live fetch opt-in only; offline snapshot still populates; FileDialog + opt-in toggle in QML              | unit       | `ctest --preset linux-release -R "CatalogOffline\|streamdock-catalog"` | ❌ W0       | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

______________________________________________________________________

## Wave 0 Requirements

- [ ] `tests/unit/test_plugin_verify_gate.cpp` (plan 01) — signed→self-signed/trusted; tampered→Refused; unsigned→Refused; signer-unavailable→Refused (fail-closed); `verdictToTrustLevel` vocabulary (PLUGIN-14)
- [ ] `tests/unit/test_plugin_install_from_file.cpp` (plan 02) — local `.sdPlugin` install: signed→promoted; unsigned→confirm/refuse policy; tampered→always refused; staging-before-promote invariant; ALL install paths (local + network + launch sweep) apply the gate (PLUGIN-14)
- [ ] `tests/unit/test_catalog_offline.cpp` (plan 02) — `PluginCatalogModel` makes NO outbound request on construction/launch (opt-in OFF); live fetch only on explicit opt-in
- [ ] fail-closed: if the Ed25519 signer is unavailable (`AJAZZ_BUILD_PYTHON_HOST=OFF`), the gate refuses to trust (no silent bypass) — signer link un-gated (plan 01)
- [ ] Reuse `manifest_signer` + `sdplugin_extractor` fixtures; no new framework

______________________________________________________________________

## Manual-Only Verifications

| Behavior                                                                                                | Requirement         | Why Manual                             | Test Instructions                                                                           |
| ------------------------------------------------------------------------------------------------------- | ------------------- | -------------------------------------- | ------------------------------------------------------------------------------------------- |
| A user installs a real signed `.sdPlugin` from disk and it appears + loads; an online refresh is opt-in | PLUGIN-14 (live UX) | Needs the running app + a real package | Deferred to Phase 25; the fixture install + fetcher-spy tests are the Phase 22 gating proof |

______________________________________________________________________

## Validation Sign-Off

- [x] All tasks have `<automated>` verify or Wave 0 dependencies
- [x] Sampling continuity: no 3 consecutive tasks without automated verify
- [x] Wave 0 covers all MISSING references
- [x] No watch-mode flags
- [x] Feedback latency < ~15s (targeted)
- [x] `nyquist_compliant: true` set in frontmatter

**Approval:** planned 2026-05-23
