---
phase: 27
slug: plugin-install-trust-persistence-hardening
status: draft
nyquist_compliant: false
wave_0_complete: false
created: 2026-05-31
---

# Phase 27 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

______________________________________________________________________

## Test Infrastructure

| Property               | Value                                                                        |
| ---------------------- | ---------------------------------------------------------------------------- |
| **Framework**          | Catch2 v3 via ctest                                                          |
| **Config file**        | `CMakePresets.json` (preset `linux-release`); unit target `ajazz_unit_tests` |
| **Quick run command**  | `ctest --preset linux-release -R "Plugin\|Verify\|Crash" -E qml`             |
| **Full suite command** | `ctest --preset linux-release -E qml`                                        |
| **Estimated runtime**  | ~60-120 seconds (build incremental + suite)                                  |

______________________________________________________________________

## Sampling Rate

- **After every task commit:** Run the quick command (the Plugin/Verify/Crash subset).
- **After every plan wave:** Run the full suite (`-E qml`).
- **Before `/gsd:verify-work`:** Full suite must be green (no regressions vs the ≈645 baseline — trust the live count).
- **Max feedback latency:** ~120 seconds.

______________________________________________________________________

## Per-Task Verification Map

| Task ID  | Plan  | Wave | Requirement | Threat Ref  | Secure Behavior                                                                                                                                             | Test Type | Automated Command                                        | File Exists | Status     |
| -------- | ----- | ---- | ----------- | ----------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------- | --------- | -------------------------------------------------------- | ----------- | ---------- |
| 27-01-\* | 01    | 1    | PLUGIN-16   | T-27-TAMPER | Tampered `.sdPlugin` (signature present, invalid) is quarantined EVEN with `userConfirmedUnsigned=true`; unsigned (no signature) installs only WITH consent | unit      | `ctest --preset linux-release -R Verify -E qml`          | ❌ W0       | ⬜ pending |
| 27-02-\* | 02    | 2    | PLUGIN-15   | —           | `rediscover()` spawns only newly-added plugins; never double-spawns or tears down a live plugin                                                             | unit      | `ctest --preset linux-release -R Plugin -E qml`          | ❌ W0       | ⬜ pending |
| 27-03-\* | 02/03 | 2    | PLUGIN-15   | —           | A user-disabled plugin is NOT spawned by `discover()` across a simulated restart; re-enable spawns it                                                       | unit      | `ctest --preset linux-release -R Plugin -E qml`          | ❌ W0       | ⬜ pending |
| 27-04-\* | 03    | 3    | PLUGIN-16   | T-27-TRUST  | "Allow unsigned plugins" setting + per-plugin allow persist (QSettings); tampered rows are never allowable from the UI                                      | unit      | `ctest --preset linux-release -R Plugin -E qml`          | ❌ W0       | ⬜ pending |
| 27-05-\* | 03    | 3    | PLUGIN-17   | —           | One plugin crashing to the 3-in-30s threshold disables ONLY itself; sibling `m_connections` stay; `connectedPluginCount()` drops by exactly one             | unit      | `ctest --preset linux-release -R "Crash\|Plugin" -E qml` | ❌ W0       | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky. Task IDs are placeholders; the planner assigns exact IDs.*

______________________________________________________________________

## Wave 0 Requirements

*Existing infrastructure (Catch2 + ctest `linux-release` + `tests/unit/test_plugin_*` + `QStandardPaths::setTestModeEnabled` + QZipWriter fixtures) covers all phase requirements. No new framework install.*

______________________________________________________________________

## Manual-Only Verifications

| Behavior                                                                                       | Requirement | Why Manual                                                                 | Test Instructions                                                                                                                                                                                                                                        |
| ---------------------------------------------------------------------------------------------- | ----------- | -------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Trust UX affordance renders (toggle in settings; per-plugin "Allow" action on an unsigned row) | PLUGIN-16   | QML visual affordance; offscreen-QPA can assert presence but not look/feel | Launch app with `AJAZZ_DEBUG_CONTROL=1`; open Loaded Plugins page; confirm an unsigned plugin shows a red chip + an "Allow this plugin" action, and Settings shows an "Allow unsigned plugins" toggle. Optional debug-channel: `qml.tree` / `qml.click`. |
| Live install→spawn with no restart (optional smoke)                                            | PLUGIN-15   | End-to-end through the running app + debug channel                         | Isolated `XDG_RUNTIME_DIR` + `QT_QPA_PLATFORM=offscreen`; `plugin.installFromFile` an unsigned `.sdPlugin` with consent, then assert `connectedPluginCount` increments without restart. Kill own PID after; never `pkill -f`.                            |

______________________________________________________________________

## Validation Sign-Off

- [ ] All tasks have an automated verify or Wave 0 dependency
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references (none — existing infra)
- [ ] No watch-mode flags
- [ ] Feedback latency < 120s
- [ ] `nyquist_compliant: true` set in frontmatter (planner sets after task map is finalized)

**Approval:** pending
