---
phase: 22
slug: plugin-store-local-install
status: draft
nyquist_compliant: false
wave_0_complete: false
created: 2026-05-23
---

# Phase 22 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

______________________________________________________________________

## Test Infrastructure

| Property               | Value                                                                                      |
| ---------------------- | ------------------------------------------------------------------------------------------ |
| **Framework**          | Catch2 (C++) under CMake/CTest; signed/unsigned/tampered fixture `.sdPlugin` + fetcher spy |
| **Config file**        | `CMakePresets.json` (preset `linux-release`)                                               |
| **Quick run command**  | \`ctest --preset linux-release -R "PluginInstall                                           |
| **Full suite command** | `ctest --preset linux-release`                                                             |
| **Estimated runtime**  | quick \<15s; full ~minutes                                                                 |

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

> Filled by the planner — one row per task. Template row:

| Task ID  | Plan | Wave | Requirement | Threat Ref | Secure Behavior                                                  | Test Type | Automated Command                               | File Exists | Status     |
| -------- | ---- | ---- | ----------- | ---------- | ---------------------------------------------------------------- | --------- | ----------------------------------------------- | ----------- | ---------- |
| 22-01-01 | 01   | 1    | PLUGIN-14   | T-22-sig   | tampered-signature package always refused; tamper never promoted | unit      | `ctest --preset linux-release -R PluginInstall` | ❌ W0       | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

______________________________________________________________________

## Wave 0 Requirements

- [ ] `tests/unit/test_plugin_install.cpp` — local `.sdPlugin` install: signed→promoted; unsigned→quarantine/confirm policy; tampered→always refused; ALL install paths (local + network + launch sweep) apply the gate (PLUGIN-14)
- [ ] `tests/unit/test_catalog_offline.cpp` — `PluginCatalogModel` makes NO outbound request on construction/launch (fetcher spy); live fetch only on explicit opt-in
- [ ] fail-closed: if the Ed25519 signer is unavailable (`AJAZZ_BUILD_PYTHON_HOST=OFF`), install refuses to trust (no silent bypass) — or the signer link is un-gated
- [ ] Reuse `manifest_signer` + `sdplugin_extractor` fixtures; no new framework

______________________________________________________________________

## Manual-Only Verifications

| Behavior                                                                                                | Requirement         | Why Manual                             | Test Instructions                                                                           |
| ------------------------------------------------------------------------------------------------------- | ------------------- | -------------------------------------- | ------------------------------------------------------------------------------------------- |
| A user installs a real signed `.sdPlugin` from disk and it appears + loads; an online refresh is opt-in | PLUGIN-14 (live UX) | Needs the running app + a real package | Deferred to Phase 25; the fixture install + fetcher-spy tests are the Phase 22 gating proof |

______________________________________________________________________

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references
- [ ] No watch-mode flags
- [ ] Feedback latency < ~15s (targeted)
- [ ] `nyquist_compliant: true` set in frontmatter

**Approval:** pending
