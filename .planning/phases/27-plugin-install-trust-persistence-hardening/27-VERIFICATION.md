---
phase: 27-plugin-install-trust-persistence-hardening
verified: 2026-05-31T15:59:39Z
status: human_needed
score: 6/6 must-haves verified
overrides_applied: 0
human_verification:
  - test: Trust UX visual affordance — toggle + per-plugin Allow + tampered chip render
    expected: |
      Launch app with AJAZZ_DEBUG_CONTROL=1; open Loaded Plugins page:
      (a) unsigned plugin shows red "unsigned" chip + "Allow this plugin" button;
      (b) tampered plugin shows red "tampered" chip with NO action button;
      (c) Settings panel shows "Allow unsigned plugins" Switch bound to PluginCatalog.allowUnsignedPlugins.
    why_human: QML visual affordance; presence of visible:false button proven by grep (line 242 of LoadedPluginsPage.qml), but correct render and look/feel require offscreen or live UI inspection.
  - test: Live install->spawn smoke (optional) — plugin install triggers rediscover with no restart
    expected: |
      Isolated XDG_RUNTIME_DIR + QT_QPA_PLATFORM=offscreen; invoke installFromFile on an unsigned .sdPlugin
      with allowUnsignedPlugins=true; assert connectedPluginCount increments within 5s without app restart.
    why_human: End-to-end through the running app + debug channel. Automated Catch2 tests cover the wiring (installFinished -> rediscover()) but not the full running-app smoke path.
---

# Phase 27: Plugin Install/Trust/Persistence Hardening — Verification Report

**Phase Goal:** Make the Stream Dock `.sdPlugin` plugin system fully usable, concurrent, and persistent from the GUI — closing five gaps: rediscover-after-install, GUI unsigned-install-with-consent (verifier unsigned-vs-tampered split, CR-01), in-app trust UX, persisted per-plugin enable/disable, and a concurrency regression guard.
**Verified:** 2026-05-31T15:59:39Z
**Status:** human_needed
**Re-verification:** No — initial verification

______________________________________________________________________

## Goal Achievement

### Observable Truths

| #   | Truth                                                                                                                                  | Status     | Evidence                                                                                                                                                                                                                                  |
| --- | -------------------------------------------------------------------------------------------------------------------------------------- | ---------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1   | `PluginManager::rediscover()` exists, is idempotent (spawns only new), and is wired to `installFinished`                               | VERIFIED   | Declared at `plugin_manager.hpp:173`; implemented at `plugin_manager.cpp:567-603`; wired at `application.cpp:814-818`                                                                                                                     |
| 2   | Verifier splits `Unsigned` vs `Tampered`; Tampered ALWAYS refused even with consent; Catch2 test proves it                             | VERIFIED   | `plugin_verify_gate.hpp:44-45`; `plugin_catalog_model.cpp:804-819` (no `consentToUnsigned` reference in Refused block); test at `tests/unit/test_plugin_install_from_file.cpp:581-631`                                                    |
| 3   | In-app trust UX: "Allow unsigned plugins" toggle + per-plugin "Allow this plugin" action (tampered rows have NO allow affordance)      | VERIFIED\* | `LoadedPluginsPage.qml:72-254`; allow button `visible: row.trustLevel === "unsigned"` at line 242; tampered chip at lines 205-208; toggle bound two-way at lines 72-80                                                                    |
| 4   | Persisted per-plugin enable/disable: user-disabled plugin not spawned by `discover()` across restart; crash-disable stays session-only | VERIFIED   | `plugin_manager.cpp:168-313` (`shouldSkipSpawn` at spawn boundary); `plugin_manager.cpp:652-695` (`setPluginEnabled`); QSettings at `plugins/disabled/<id>`; crash path (`disableWithNotice`/`onProcessFailed`) has zero QSettings writes |
| 5   | Concurrency regression test: ≥3 plugins, one crash disables only itself, siblings intact; WR-02 HTML-no-respawn guard present          | VERIFIED   | `tests/unit/test_plugin_concurrency.cpp` (4 tests 624-627); `CHECK_FALSE(manager.isDisabled("pluginB.js"))` at line 153; `CHECK_FALSE(manager.isDisabled("pluginC.js"))` at line 156                                                      |
| 6   | `ctest --preset linux-release -E qml` ≥ baseline (645), 0 failed; Python OOP host (`src/plugins/` SEC-003) untouched                   | VERIFIED   | 27-04-SUMMARY: 711/711 passed, 0 failed; no Phase 27 commits touch OOP host files (`_host_child.py`, `bwrap`, `OutOfProcessPluginHost`)                                                                                                   |

\*Truth 3 is verified at code level (QML wiring/visibility logic confirmed by grep); visual render correctness routes to human verification section.

**Score:** 6/6 truths verified (automation)

______________________________________________________________________

### Required Artifacts

| Artifact                                                | Expected                                                               | Status   | Details                                                                                         |
| ------------------------------------------------------- | ---------------------------------------------------------------------- | -------- | ----------------------------------------------------------------------------------------------- |
| `src/plugins/include/ajazz/plugins/manifest_signer.hpp` | `SignatureState { None, Valid, Invalid }` on `ManifestVerifyResult`    | VERIFIED | `enum class SignatureState` at line 65; `signatureState` field at line 87                       |
| `src/plugins/src/manifest_signer.cpp`                   | POSIX backend classifies None/Invalid/Valid                            | VERIFIED | Lines 166, 171, 179, 190, 204, 211 — all three states set correctly                             |
| `src/plugins/src/manifest_signer_win32.cpp`             | Win32 backend mirrors same classification                              | VERIFIED | Lines 150, 154, 161, 178, 185 — mirrors POSIX logic                                             |
| `src/app/src/plugin_verify_gate.hpp`                    | `VerifyVerdict::Unsigned` distinct from `Refused`                      | VERIFIED | Lines 44-45; `Refused -> "tampered"`, `Unsigned -> "unsigned"` in `verdictToTrustLevel`         |
| `src/app/src/plugin_verify_gate.cpp`                    | Maps `None -> Unsigned`, `Invalid -> Refused`                          | VERIFIED | Confirmed via `manifest_signer.cpp` classification; verdict switch on `signatureState`          |
| `src/app/src/plugin_catalog_model.cpp`                  | CR-01: `Refused` branch unconditional, no consent escape               | VERIFIED | Lines 804-819; `consentToUnsigned()` absent from `Refused` block                                |
| `src/app/src/plugin_catalog_model.hpp`                  | `allowUnsignedPlugins` Q_PROPERTY, `allowPlugin` Q_INVOKABLE           | VERIFIED | Confirmed via SUMMARY grep-verified self-check (file line citations confirmed)                  |
| `src/app/src/plugin_manager.hpp`                        | `void rediscover()` declared                                           | VERIFIED | Line 173                                                                                        |
| `src/app/src/plugin_manager.cpp`                        | `rediscover()` implementation + `shouldSkipSpawn` + `setPluginEnabled` | VERIFIED | Lines 567-603, 168, 652-695; QSettings isolated to user-disable path only                       |
| `src/app/src/application.cpp`                           | `installFinished -> rediscover()` connection                           | VERIFIED | Lines 814-818 — lambda guard on `ok==true`                                                      |
| `src/app/qml/LoadedPluginsPage.qml`                     | Toggle + per-plugin Allow button + tampered chip (no action)           | VERIFIED | Lines 72-254; `visible: row.trustLevel === "unsigned"` (line 242)                               |
| `tests/unit/test_plugin_install_from_file.cpp`          | "tampered refused even with consent" Catch2 test                       | VERIFIED | Lines 581-631; builds tampered fixture, calls `installFromFile(..., true)`, asserts `false`     |
| `tests/unit/test_plugin_lifecycle.cpp`                  | rediscover idempotency + persisted disable tests                       | VERIFIED | 6 new tests (2 rediscover + 4 disable); confirmed by commits 11b3516, 4f4b45c, da9bfd7, 8dd9e82 |
| `tests/unit/test_plugin_concurrency.cpp`                | ≥3 plugin concurrency + WR-02 tests (new file)                         | VERIFIED | File exists; 4 tests (624-627); registered in `tests/unit/CMakeLists.txt`                       |

### Key Link Verification

| From                              | To                                    | Via                                       | Status | Details                                                                                            |
| --------------------------------- | ------------------------------------- | ----------------------------------------- | ------ | -------------------------------------------------------------------------------------------------- |
| `manifest_signer.cpp`             | `SignatureState`                      | classification logic (None/Invalid/Valid) | WIRED  | All three states set in both POSIX and Win32 backends                                              |
| `plugin_verify_gate.cpp`          | `verifyManifest` / `SignatureState`   | switch on `result.signatureState`         | WIRED  | Maps `None -> Unsigned`, `Invalid -> Refused`                                                      |
| `plugin_catalog_model.cpp`        | `verifyStagedPlugin`                  | `installFromFile` verdict switch          | WIRED  | `Refused` block has no `consentToUnsigned` escape; `Unsigned` block consults `consentToUnsigned()` |
| `plugin_catalog_model.cpp`        | `allowPlugin()`                       | re-verifies via `verifyStagedPlugin`      | WIRED  | Lines 548-561; returns false on Refused; writes `plugins/allowed/<uuid>` on success                |
| `application.cpp`                 | `PluginCatalogModel::installFinished` | `QObject::connect` lambda                 | WIRED  | Lines 814-818; guarded on `ok==true`                                                               |
| `application.cpp` lambda          | `PluginManager::rediscover()`         | direct call                               | WIRED  | Line 818 calls `m_pluginManager->rediscover()`                                                     |
| `plugin_manager.cpp::spawn()`     | `shouldSkipSpawn(pluginId)`           | early-return predicate                    | WIRED  | Line 312-313; called at spawn boundary so launch loop AND rediscover() both honor it               |
| `LoadedPluginsPage.qml` Allow btn | `PluginCatalog.allowPlugin()`         | `onClicked` handler                       | WIRED  | Lines 249-252; `visible: row.trustLevel === "unsigned"` (absent on tampered)                       |
| `LoadedPluginsPage.qml` toggle    | `PluginCatalog.allowUnsignedPlugins`  | two-way binding via `onToggled`           | WIRED  | Lines 77-80                                                                                        |

### Data-Flow Trace (Level 4)

| Artifact                | Data Variable              | Source                                                     | Produces Real Data                         | Status  |
| ----------------------- | -------------------------- | ---------------------------------------------------------- | ------------------------------------------ | ------- |
| `LoadedPluginsPage.qml` | `row.trustLevel`           | `PluginCatalogModel` role — `verdictToTrustLevel(verdict)` | Yes — sourced from Ed25519 verifier result | FLOWING |
| `plugin_manager.cpp`    | `m_live` (rediscover diff) | `discover()` re-scan of plugins dir                        | Yes — reads filesystem                     | FLOWING |
| `plugin_manager.cpp`    | `shouldSkipSpawn`          | `QSettings::value("plugins/disabled/<id>", false)`         | Yes — real QSettings read                  | FLOWING |

### Behavioral Spot-Checks

Step 7b skipped for the QML layer (requires running app/offscreen QPA). The Catch2 suite serves as the behavioral verification for all non-visual behaviors.

| Behavior                                        | Command                                                                              | Result           | Status |
| ----------------------------------------------- | ------------------------------------------------------------------------------------ | ---------------- | ------ |
| `rediscover()` exists + wired (build check)     | Confirmed by `application.cpp:814-818` + `plugin_manager.hpp:173`                    | No stub          | PASS   |
| Tampered refused with consent (test exists)     | `tests/unit/test_plugin_install_from_file.cpp:581-631` — real Ed25519 fixture + flip | Substantive test | PASS   |
| Crash isolation (3 plugins, 1 disable)          | `test_plugin_concurrency.cpp:75-156` — `CHECK_FALSE(isDisabled("pluginB.js"))`       | Substantive test | PASS   |
| QSettings in crash path? (scope fence check)    | `grep disableWithNotice plugin_manager.cpp \| grep QSettings` = empty                | Zero hits        | PASS   |
| COD-031 boundary (`nlohmann` in public headers) | `grep -rn "#include.*nlohmann" src/core/include/` = 0 actual includes                | Clean            | PASS   |
| Wire/protocol/opcode changes in Phase 27 files  | `grep -rn "BAT\|LIG\|CLE\|HID\|hidraw\|ULEND" <phase27_files>` = 0                   | Zero hits        | PASS   |

### Probe Execution

Step 7c: No probe scripts declared for this phase. VALIDATION.md specifies `ctest --preset linux-release -E qml` as the full-suite gate. Orchestrator-confirmed: 711/711 passed, 0 failed (27-04-SUMMARY).

### Requirements Coverage

| Requirement | Source Plan  | Description                                                   | Status    | Evidence                                                                                                                                          |
| ----------- | ------------ | ------------------------------------------------------------- | --------- | ------------------------------------------------------------------------------------------------------------------------------------------------- |
| PLUGIN-15   | 27-02, 27-03 | rediscover-after-install + persisted enable/disable           | SATISFIED | `rediscover()` at `plugin_manager.cpp:567`; `shouldSkipSpawn` at line 168; 6 hermetic Catch2 tests                                                |
| PLUGIN-16   | 27-01, 27-04 | Unsigned/Tampered split + GUI consent + trust UX              | SATISFIED | `VerifyVerdict::Unsigned` at `plugin_verify_gate.hpp:44`; CR-01 gate at `plugin_catalog_model.cpp:804-819`; QML at `LoadedPluginsPage.qml:72-254` |
| PLUGIN-17   | 27-05        | Concurrency regression guard (one crash disables only itself) | SATISFIED | `test_plugin_concurrency.cpp` 4 tests; sibling isolation assertions at lines 153, 156                                                             |

### Anti-Patterns Found

| File | Line | Pattern | Severity | Impact |
| ---- | ---- | ------- | -------- | ------ |
| None | —    | —       | —        | —      |

No `TBD`, `FIXME`, `XXX`, `HACK`, or `placeholder` markers found in any Phase 27 modified file. No stub `return null` / `return []` / `return {}` patterns in behavioral code paths. No `console.log`-only handlers.

### Human Verification Required

#### 1. Trust UX Visual Affordance Render

**Test:** Launch app with `AJAZZ_DEBUG_CONTROL=1` (isolated `XDG_RUNTIME_DIR` + `QT_QPA_PLATFORM=offscreen`). Install one unsigned and one tampered `.sdPlugin`. Open Loaded Plugins page.
**Expected:**

- Unsigned plugin: red "unsigned" chip visible; "Allow" button present and clickable.
- Tampered plugin: red "tampered" chip visible; NO "Allow" button (completely absent, not greyed-out).
- Settings area (above plugin list): "Allow unsigned plugins" Switch visible, toggling it changes `PluginCatalog.allowUnsignedPlugins`.
  **Why human:** QML `visible: false` verified at line 242 of `LoadedPluginsPage.qml`, but correct chip labeling and layout render require visual or offscreen-QPA assertion. The `qml.tree` debug RPC can assist but is not a substitute for visual inspection.

#### 2. Live Install → Spawn Smoke (Optional)

**Test:** Isolated `XDG_RUNTIME_DIR` + `QT_QPA_PLATFORM=offscreen`; invoke `plugin.installFromFile` (debug channel) on an unsigned `.sdPlugin` with `allowUnsignedPlugins=true`; check `connectedPluginCount` increments without app restart.
**Expected:** Count increments by 1 within a few seconds. Kill by exact PID after test.
**Why human:** End-to-end through the running application process. The `installFinished -> rediscover()` wiring is verified by code (application.cpp:814-818 + unit tests), but the full running-app data flow (WebSocket handshake completing, count signal firing) can only be confirmed live.

______________________________________________________________________

### Gaps Summary

No automated gaps. All 6 ROADMAP success criteria are verified in code:

1. **SC-1 (rediscover):** `PluginManager::rediscover()` exists, is idempotent, and is connected to `installFinished` in `application.cpp`.
1. **SC-2 (verifier split + CR-01):** `SignatureState {None/Valid/Invalid}` on both backends; `VerifyVerdict::Unsigned` distinct from `Refused`; `Refused` block in `installFromFile` has zero consent escape; "tampered refused even with consent" Catch2 test is real (builds a byte-flipped Ed25519 fixture).
1. **SC-3 (trust UX):** `LoadedPluginsPage.qml` has the toggle (line 77), "Allow" button (`visible: row.trustLevel === "unsigned"`, line 242), and tampered chip with NO action (comment at line 237).
1. **SC-4 (persisted enable/disable):** `shouldSkipSpawn` reads `plugins/disabled/<id>` from QSettings at every `spawn()` call; `setPluginEnabled` writes QSettings; crash path (`disableWithNotice`) has zero QSettings writes (confirmed by grep).
1. **SC-5 (concurrency regression):** 4-test suite guards `5725cb0` key-distinctness and WR-02 HTML-no-respawn; `CHECK_FALSE(isDisabled("pluginB/C.js"))` asserts sibling isolation.
1. **SC-6 (ctest green + OOP untouched):** 711/711 passed per 27-04-SUMMARY; no Phase 27 commit touches `_host_child.py`, `bwrap`, or `OutOfProcessPluginHost`.

The two human items are both quality/smoke checks, not blockers. The trust UX visual affordance is confirmed correct by code (`visible: row.trustLevel === "unsigned"`) but needs a human eye on the actual render. The live-install smoke is optional per VALIDATION.md.

______________________________________________________________________

_Verified: 2026-05-31T15:59:39Z_
_Verifier: Claude (gsd-verifier)_
