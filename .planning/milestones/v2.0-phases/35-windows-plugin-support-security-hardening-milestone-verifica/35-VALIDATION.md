---
phase: 35
slug: windows-plugin-support-security-hardening-milestone-verifica
status: approved
nyquist_compliant: true
wave_0_complete: true
created: 2026-06-08
---

# Phase 35 — Validation Strategy

> Per-phase validation contract. Derived from `35-RESEARCH.md` § Validation Architecture (HIGH).
> Phase is ~70% verify-and-audit (PLGSEC/VERIF already shipped); the new test budget is WINPLG.

______________________________________________________________________

## Test Infrastructure

| Property               | Value                                                                                               |
| ---------------------- | --------------------------------------------------------------------------------------------------- |
| **Framework**          | Catch2 (via ctest)                                                                                  |
| **Config file**        | CMake presets; `tests/unit/CMakeLists.txt`                                                          |
| **Quick run command**  | `ctest --preset linux-release -R 'plugin-install\|plugin-verify-gate\|plugin-manifest\|win-plugin'` |
| **Full suite command** | `ctest --preset linux-release`                                                                      |
| **Estimated runtime**  | quick ~5s · full ~140s (~785+ cases)                                                                |

______________________________________________________________________

## Sampling Rate

- **After every task commit:** quick run (install + verify-gate + manifest + new classification suite)
- **After every plan wave:** `ctest --preset linux-release`
- **Phase gate (mandatory live, VERIF-01):** full suite green + live debug-channel drive of the new chip(s) + the consent restart round-trip + `ajazz-debug state` loopback confirm, BEFORE verify
- **Max feedback latency:** ~140s

______________________________________________________________________

## Per-Task Verification Map

| Req ID    | Behavior                                                                                          | Test Type        | Automated Command                                                       | Status                         |
| --------- | ------------------------------------------------------------------------------------------------- | ---------------- | ----------------------------------------------------------------------- | ------------------------------ |
| PLGSEC-01 | Tampered refused EVEN with consent (CR-01)                                                        | unit             | `ctest ... -R plugin-install` (test_plugin_install_from_file.cpp:581)   | EXISTS — don't regress         |
| PLGSEC-01 | Tampered manifest → Refused; unsigned → Unsigned                                                  | unit             | `ctest ... -R plugin-verify-gate` (test_plugin_verify_gate.cpp:265/305) | EXISTS                         |
| PLGSEC-02 | Consent persists `plugins/allowed/<uuid>`; survives launch-sweep + restart                        | unit + live      | new round-trip test (if absent) + grant→restart→`plugin.list`           | likely NEW + live              |
| PLGSEC-03 | WS server loopback-only; no phone-home                                                            | unit + CI + live | code assert LocalHost + `ci.yml:67` grep gate + `ajazz-debug state`     | CI EXISTS; confirm code assert |
| WINPLG-01 | classifyWindowsPlugin: CodePath suffix + PE-magic → WsOnlyIpc vs VendorDll                        | unit             | `ctest ... -R win-plugin`                                               | NEW                            |
| WINPLG-02 | supportsCurrentPlatform accepts WS-only win manifest on Linux (run native)                        | unit + live      | new unit + live install→`plugin.list`                                   | NEW + live                     |
| WINPLG-03 | Chip states "Runs natively"/"Requires Wine"/"Unsupported on this OS" (chip-only; launch deferred) | live (QML)       | `qml.get` chip objectName + label + screenshot                          | NEW + live                     |
| VERIF-01  | Every new v2.0 control objectName-addressable; honest screenshot/HUMAN-UAT reconciliation         | grep + doc       | objectName-coverage grep over new QML + audit doc                       | NEW                            |
| VERIF-02  | No mirajazz in core/bridge; no nlohmann #include in core; no revived akp05; no QHostAddress::Any  | grep gates       | the 4 success-criterion-5 greps (comment-aware)                         | PASS now — lock + document     |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

______________________________________________________________________

## Wave 0 Requirements

- [ ] `tests/unit/test_win_plugin_classification.cpp` (or extend `test_plugin_manifest.cpp`) — WINPLG-01/02 classification + supportsCurrentPlatform
- [ ] Consent-persistence round-trip test — PLGSEC-02 (only if grep shows none exists; check `tests/unit` for `plugins/allowed`/`perPluginAllowed`)
- [ ] (No framework install — Catch2 + ctest present)

______________________________________________________________________

## Manual-Only Verifications

| Behavior                   | Requirement        | Why Manual                      | Test Instructions                                                                 |
| -------------------------- | ------------------ | ------------------------------- | --------------------------------------------------------------------------------- |
| Consent restart round-trip | PLGSEC-02          | Real app restart                | Grant unsigned consent, restart app, `plugin.list` shows loaded without re-prompt |
| Chip states live           | WINPLG-03/VERIF-01 | Real QML render                 | `qml.get` each chip objectName → correct label; screenshot read                   |
| Wine launch path           | WINPLG-03          | DEFERRED — no Wine/Windows host | Out of scope this phase (chip-only); Windows/Wine-host walk later                 |
| Loopback confirm           | PLGSEC-03          | Running server                  | `ajazz-debug state`/`ping` shows loopback bind                                    |

______________________________________________________________________

## Validation Sign-Off

- [x] All tasks have `<automated>` verify or Wave 0 dependencies
- [x] Sampling continuity maintained
- [x] Wave 0 covers all MISSING references (WINPLG classification + PLGSEC-02 round-trip)
- [x] No watch-mode flags
- [x] Feedback latency < 140s
- [x] `nyquist_compliant: true`

**Approval:** approved 2026-06-08 (derived from RESEARCH HIGH-confidence validation architecture)
