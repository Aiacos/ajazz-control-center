---
phase: 18
slug: plugin-manifest-discovery-lifecycle-spawn
status: draft
nyquist_compliant: false
wave_0_complete: false
created: 2026-05-23
---

# Phase 18 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

______________________________________________________________________

## Test Infrastructure

| Property               | Value                                                                                    |
| ---------------------- | ---------------------------------------------------------------------------------------- |
| **Framework**          | Catch2 (C++) under CMake/CTest; fixture `.sdPlugin` dirs + injected fake QProcess events |
| **Config file**        | `CMakePresets.json` (preset `linux-release`)                                             |
| **Quick run command**  | \`ctest --preset linux-release -R "PluginManifest                                        |
| **Full suite command** | `ctest --preset linux-release`                                                           |
| **Estimated runtime**  | quick \<15s; full ~minutes                                                               |

Hardware-free + mostly process-free: manifest parse/validate over fixture manifests; node argv
**built and asserted without launching node** (`NodeRunnerTest::buildsCorrectArgv`); node-version
detection probe is mockable; crash-counter logic driven by injected fake process events; the
Mirabox `QWebEngineScript` alias asserted by unit. A real spawn (live node + WS round-trip) is an
OPTIONAL integration test gated on `node` presence; the live `.sdPlugin` witness is Phase 25
(VERIFY-06). ASCII-only test names.

______________________________________________________________________

## Sampling Rate

- **After every task commit:** the quick `-R` run above
- **After every plan wave:** `ctest --preset linux-release`
- **Before `/gsd:verify-work`:** full suite green
- **Max feedback latency:** ~15s (targeted)

______________________________________________________________________

## Per-Task Verification Map

> Filled by the planner — one row per task. Template row:

| Task ID  | Plan | Wave | Requirement | Threat Ref    | Secure Behavior                                                                            | Test Type | Automated Command                                | File Exists | Status     |
| -------- | ---- | ---- | ----------- | ------------- | ------------------------------------------------------------------------------------------ | --------- | ------------------------------------------------ | ----------- | ---------- |
| 18-01-01 | 01   | 1    | PLUGIN-06   | T-18-manifest | rejects manifest whose OS/MinimumVersion mismatch; accepts AJAZZ ext incl Controllers:Knob | unit      | `ctest --preset linux-release -R PluginManifest` | ❌ W0       | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

______________________________________________________________________

## Wave 0 Requirements

- [ ] `tests/unit/test_plugin_manifest.cpp` — Elgato v6 + AJAZZ ext parse; reject mac-only-on-Linux policy per the locked rule; reject too-high Software.MinimumVersion; Controllers accepts Knob/SecondaryScreen (PLUGIN-06)
- [ ] `tests/unit/test_node_runner.cpp` — exact node argv `-port/-pluginUUID/-registerEvent/-info` without launching node; version-detection probe mocked (PLUGIN-08)
- [ ] `tests/unit/test_plugin_lifecycle.cpp` — discovery over a temp dir of fixture `.sdPlugin`; crash-3-in-30s → disabled via injected fake process events (PLUGIN-07)
- [ ] `tests/unit/test_mirabox_compat.cpp` — `connectMiraBoxSDSocket` aliased before plugin script runs (PLUGIN-11)
- [ ] Reuse `sdplugin_extractor`, the PI QWebEngine stack, `loaded_plugins_model` — no new framework

______________________________________________________________________

## Manual-Only Verifications

| Behavior                                                                                                                | Requirement                    | Why Manual                                                                           | Test Instructions                                                            |
| ----------------------------------------------------------------------------------------------------------------------- | ------------------------------ | ------------------------------------------------------------------------------------ | ---------------------------------------------------------------------------- |
| A real third-party `.sdPlugin` (node + HTML) spawns, connects to the loopback server, and exchanges messages end-to-end | PLUGIN-07/08/11 (live witness) | Needs the device bridge (Phase 19) + a real package; full E2E is the milestone close | Optional node-gated integration test here; full witness Phase 25 (VERIFY-06) |

______________________________________________________________________

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references
- [ ] No watch-mode flags
- [ ] Feedback latency < ~15s (targeted)
- [ ] `nyquist_compliant: true` set in frontmatter

**Approval:** pending
