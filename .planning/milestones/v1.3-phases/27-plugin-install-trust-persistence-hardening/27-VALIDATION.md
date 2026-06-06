---
phase: 27
slug: plugin-install-trust-persistence-hardening
status: ready
nyquist_compliant: true
wave_0_complete: true
created: 2026-05-31
---

# Phase 27 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

______________________________________________________________________

## Test Infrastructure

| Property               | Value                                                                            |
| ---------------------- | -------------------------------------------------------------------------------- |
| **Framework**          | Catch2 v3 via ctest                                                              |
| **Config file**        | `CMakePresets.json` (preset `linux-release`); unit target `ajazz_unit_tests`     |
| **Quick run command**  | `ctest --preset linux-release -R "Plugin\|Verify\|Crash\|ManifestSigner" -E qml` |
| **Full suite command** | `ctest --preset linux-release -E qml`                                            |
| **Estimated runtime**  | ~60-120 seconds (build incremental + suite)                                      |

______________________________________________________________________

## Sampling Rate

- **After every task commit:** Run the quick command (the Plugin/Verify/Crash/ManifestSigner subset).
- **After every plan wave:** Run the full suite (`-E qml`).
- **Before `/gsd:verify-work`:** Full suite must be green (no regressions vs the ≈645 baseline — trust the live count).
- **Max feedback latency:** ~120 seconds.

______________________________________________________________________

## Per-Task Verification Map

| Task ID | Plan | Wave | Requirement | Threat Ref        | Secure Behavior                                                                                                                                | Test Type    | Automated Command                                                           | File Exists | Status     |
| ------- | ---- | ---- | ----------- | ----------------- | ---------------------------------------------------------------------------------------------------------------------------------------------- | ------------ | --------------------------------------------------------------------------- | ----------- | ---------- |
| 27-01-1 | 01   | 1    | PLUGIN-16   | T-27-FAILOPEN     | `verifyManifest` returns SignatureState None/Valid/Invalid; unsigned (no block) and tampered (invalid block) are distinguishable               | unit         | `ctest --preset linux-release -R "ManifestSigner\|manifest" -E qml`         | ✅          | ⬜ pending |
| 27-01-2 | 01   | 1    | PLUGIN-16   | T-27-TAMPER       | Tampered `.sdPlugin` quarantined EVEN with `userConfirmedUnsigned=true`; unsigned installs only WITH consent, refused without                  | unit         | `ctest --preset linux-release -R "Verify\|InstallFromFile" -E qml`          | ✅          | ⬜ pending |
| 27-02-1 | 02   | 2    | PLUGIN-15   | T-27-DOUBLESPAWN  | `rediscover()` spawns only newly-added plugins; never double-spawns or tears down a live plugin                                                | unit         | `ctest --preset linux-release -R "Plugin\|Lifecycle" -E qml`                | ✅          | ⬜ pending |
| 27-02-2 | 02   | 2    | PLUGIN-15   | T-27-INSTALLGATE  | `installFinished(ok=true)` triggers `rediscover()`; app target builds clean                                                                    | build        | `cmake --build --preset linux-release --target ajazz-control-center`        | ✅          | ⬜ pending |
| 27-05-1 | 05   | 2    | PLUGIN-17   | T-27-SIBLING      | One plugin crashing to 3-in-30s disables ONLY itself; siblings' `isDisabled`==false; WR-02 HTML-no-respawn holds                               | unit         | `ctest --preset linux-release -R "Plugin\|Crash\|Lifecycle" -E qml`         | ✅          | ⬜ pending |
| 27-03-1 | 03   | 3    | PLUGIN-15   | T-27-DISABLE-LEAK | A user-disabled plugin is NOT spawned across a simulated restart (QSettings `plugins/disabled/<id>` + setTestModeEnabled); re-enable spawns it | unit         | `ctest --preset linux-release -R "Plugin\|Lifecycle" -E qml`                | ✅          | ⬜ pending |
| 27-04-1 | 04   | 3    | PLUGIN-16   | T-27-TRUST        | "Allow unsigned plugins" setting + per-plugin `allowPlugin` persist (QSettings); tampered rows never consentable from the model                | unit         | `ctest --preset linux-release -R "Plugin\|InstallFromFile\|Catalog" -E qml` | ✅          | ⬜ pending |
| 27-04-2 | 04   | 3    | PLUGIN-16   | T-27-UIBYPASS     | Trust UX renders: unsigned rows get an "Allow this plugin" action; tampered rows have NO allow affordance; app-level toggle bound              | manual+build | `cmake --build --preset linux-release --target ajazz-control-center`        | ✅          | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky.*

______________________________________________________________________

## Wave 0 Requirements

*Existing infrastructure (Catch2 + ctest `linux-release` + `tests/unit/test_plugin_*` + `QStandardPaths::setTestModeEnabled` + QZipWriter fixtures) covers all phase requirements. No new framework install. Plan 05 adds one new test file `tests/unit/test_plugin_concurrency.cpp` registered in `tests/unit/CMakeLists.txt` — not a framework dependency, so no Wave 0 gate.*

______________________________________________________________________

## Manual-Only Verifications

| Behavior                                                                                                                         | Requirement | Why Manual                                                                 | Test Instructions                                                                                                                                                                                                                                                                                                           |
| -------------------------------------------------------------------------------------------------------------------------------- | ----------- | -------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Trust UX affordance renders (toggle in settings; per-plugin "Allow" action on an unsigned row; tampered row has NO allow action) | PLUGIN-16   | QML visual affordance; offscreen-QPA can assert presence but not look/feel | Launch app with `AJAZZ_DEBUG_CONTROL=1`; open Loaded Plugins page; confirm an unsigned plugin shows a red chip + an "Allow this plugin" action, a tampered plugin shows a distinct "tampered" chip with NO action, and Settings shows an "Allow unsigned plugins" toggle. Optional debug-channel: `qml.tree` / `qml.click`. |
| Live install→spawn with no restart (optional smoke)                                                                              | PLUGIN-15   | End-to-end through the running app + debug channel                         | Isolated `XDG_RUNTIME_DIR` + `QT_QPA_PLATFORM=offscreen`; `plugin.installFromFile` an unsigned `.sdPlugin` with consent, then assert `connectedPluginCount` increments without restart. Kill own PID after; never `pkill -f`.                                                                                               |

______________________________________________________________________

## Validation Sign-Off

- [x] All tasks have an automated verify or Wave 0 dependency
- [x] Sampling continuity: no 3 consecutive tasks without automated verify
- [x] Wave 0 covers all MISSING references (none — existing infra)
- [x] No watch-mode flags
- [x] Feedback latency < 120s
- [x] `nyquist_compliant: true` set in frontmatter (task map finalized)

**Approval:** ready
