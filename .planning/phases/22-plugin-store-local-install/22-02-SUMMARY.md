---
phase: 22-plugin-store-local-install
plan: 02
subsystem: ui
tags: [qt6, qml, plugins, security, ed25519, file-dialog, online-catalog, privacy]

# Dependency graph
requires:
  - phase: 22-plugin-store-local-install-01
    provides: verifyStagedPlugin, VerifyVerdict, VerifyOutcome, verdictToTrustLevel (Ed25519 verify gate)
  - phase: 18-plugin-manifest-discovery-lifecycle-spawn
    provides: installedPlugins/ layout + manifest.json discovery contract (promote target)
  - phase: 13-sdplugin-extractor
    provides: extractSdPluginArchive zip-slip-guarded extractor (reused unchanged)
provides:
  - installFromFile Q_INVOKABLE staging->verify->promote with explicit-confirm developer-sideload policy
  - No-phone-home by default: PluginCatalogModel constructor makes zero outbound requests (opt-in QSettings flag)
  - PluginStore.qml FileDialog affordance + opt-in online-catalog toggle
  - Local-install unit suite (test_plugin_install_from_file.cpp, 6 TEST_CASEs)
  - No-network unit suite (test_catalog_offline.cpp, 4 TEST_CASEs)
affects:
  - 23-plugin-store-ui (QML affordances extend this plan's FileDialog + opt-in toggle)
  - 25-hardware-verification (PLUGIN-14 requirement closed here)

# Tech tracking
tech-stack:
  added: []
  patterns:
    - staging-before-promote (extract to .plugin_staging/, verify, atomic rename only on pass)
    - opt-in QSettings flag (plugins/onlineCatalogEnabled, default false) for privacy-first catalog
    - disabled-sentinel seam (ACC_STREAMDOCK_CATALOG_URL=disabled) for offline-only test isolation
    - g_pluginsDirOverride TU-level anonymous namespace test seam (avoids private-access pattern)
    - EnvGuard RAII for environment variable override + restore in tests

key-files:
  created:
    - tests/unit/test_plugin_install_from_file.cpp
    - tests/unit/test_catalog_offline.cpp
  modified:
    - src/app/src/plugin_catalog_model.hpp
    - src/app/src/plugin_catalog_model.cpp
    - src/app/qml/PluginStore.qml
    - tests/unit/CMakeLists.txt

key-decisions:
  - "Unsigned-package policy: SelfSigned -> explicit-confirm developer-sideload (userConfirmedUnsigned=true param); hard-Refused -> always quarantined/removed, never promoted. Self-signed path emits installFinished(path, false, 'self-signed plugin -- confirm to install') so QML can route to warning dialog before confirm call."
  - 'Test seam: g_pluginsDirOverride in TU-level anonymous namespace (not a private class member) avoids private-member access from free functions; setPluginsDirOverride() sets TU-level var.'
  - "Phone-home kill: setCatalogUrlOverride('disabled') on both fetchers in reload() when m_onlineCatalogEnabled==false; constructor no longer calls live refresh(), only emitSnapshot for cached/bundled offline path."
  - QSettings key 'plugins/onlineCatalogEnabled' (default false) persists user opt-in across sessions; setOnlineCatalogEnabled(true) persists + calls refreshOnline() immediately.
  - 'FileDialog wiring: onAccepted calls PluginCatalog.installFromFile(selectedFile.toString(), false); self-signed outcome (installFinished false with confirm-required message) routed to selfSignedDialog whose onAccepted re-calls with userConfirmedUnsigned=true.'

patterns-established:
  - 'staging-before-promote: extract to sibling .plugin_staging/ dir, verify there, QDir::rename() only on Trusted or SelfSigned+confirmed. Refused packages are deleted from staging; they never appear under installedPlugins/.'
  - 'disabled-sentinel for offline-only tests: ACC_STREAMDOCK_CATALOG_URL=disabled + ACC_OPENDECK_CATALOG_URL=disabled forces fetchers to emit cached/bundled snapshot only; no live HTTP POST occurs. Combined with QStandardPaths::setTestModeEnabled(true) for settings isolation.'
  - 'EnvGuard RAII: constructor captures previous value + hadPrev, destructor restores/unsets. Prevents env-var pollution across TEST_CASEs.'

requirements-completed: [PLUGIN-14]

# Metrics
duration: 60min
completed: 2026-05-24
---

# Phase 22 Plan 02: Plugin Store Local Install Summary

**installFromFile staging->verify->promote with Ed25519 verify gate, no-phone-home by default (opt-in QSettings flag), and FileDialog + opt-in toggle wired in PluginStore.qml (PLUGIN-14)**

## Performance

- **Duration:** ~60 min
- **Started:** 2026-05-24T17:56:00Z
- **Completed:** 2026-05-24T18:56:22Z
- **Tasks:** 2 (3 atomic commits)
- **Files modified:** 6

## Accomplishments

- `installFromFile` Q_INVOKABLE: normalize URL -> read+validate (size cap + ZIP magic) -> extract to `.plugin_staging/` -> `verifyStagedPlugin` gate -> promote (atomic `QDir::rename`) only on Trusted/SelfSigned+confirmed; tampered always quarantined
- No-phone-home: constructor drops the live `refresh()` call; `reload()`/`refreshOnline()` gates live fetch behind `m_onlineCatalogEnabled` QSettings flag (default false); cached/bundled snapshot continues to populate the store offline
- `PluginStore.qml`: `FileDialog` (import QtQuick.Dialogs) with `*.sdPlugin *.zip` filter, `selfSignedDialog` warning with confirm path, opt-in online-catalog `Switch`, and Refresh buttons converted to `refreshOnline()`
- 10 new unit TEST_CASEs across two suites (6 install, 4 offline); full suite (614/614) green

## Task Commits

Each task was committed atomically:

1. **Task 1: installFromFile staging->verify->promote + local-install unit suite** - `2209f05` (feat)
1. **Task 2a: no-phone-home default-off + offline-only unit suite** - `2a46f92` (feat)
1. **Task 2b: PluginStore.qml FileDialog + opt-in online toggle affordances** - `82e897d` (feat)

## Files Created/Modified

- `src/app/src/plugin_catalog_model.hpp` - Added `installFromFile`, `onlineCatalogEnabled`/`setOnlineCatalogEnabled`/`refreshOnline` Q_INVOKABLEs, `onlineCatalogEnabledChanged` signal, `m_onlineCatalogEnabled` member, `setPluginsDirOverride` test seam
- `src/app/src/plugin_catalog_model.cpp` - `g_pluginsDirOverride` TU-level seam, `installFromFile` implementation (validate+extract+verify+promote), `reload()` opt-in gate, `onlineCatalogEnabled` QSettings persistence, `setPluginsDirOverride`
- `src/app/qml/PluginStore.qml` - `FileDialog`, `selfSignedDialog`, `localInstallStatus` label, online opt-in `Switch`, Refresh buttons call `refreshOnline()`
- `tests/unit/test_plugin_install_from_file.cpp` - 6 TEST_CASEs: non-zip rejection, oversized rejection, tampered->Refused, self-signed without confirm->not promoted, signed+confirm->promoted, `file://` URL acceptance
- `tests/unit/test_catalog_offline.cpp` - 4 TEST_CASEs: construction no-network (opt-in OFF), opt-in enabled live branch, `installFromFile` no online fetch, `onlineCatalogEnabled` defaults to false
- `tests/unit/CMakeLists.txt` - Registered both new test files

## Decisions Made

1. **Unsigned-package policy**: SelfSigned -> explicit-confirm developer-sideload via `userConfirmedUnsigned=true` param. Hard-Refused -> always quarantined, never promoted. Without confirm, `installFinished(path, false, "self-signed plugin -- confirm to install")` so QML routes to `selfSignedDialog`.

1. **Test seam placement**: `g_pluginsDirOverride` in TU-level anonymous namespace rather than a private class member. The anonymous namespace `userPluginsDir()` free function cannot access private class members — the TU-level var is the correct pattern.

1. **Phone-home kill mechanism**: Reused the existing `setCatalogUrlOverride("disabled")` sentinel on both fetchers inside `reload()` when `m_onlineCatalogEnabled==false`. The constructor now calls `reload()` which sets the sentinel and emits offline snapshot only. No structural changes to the fetcher required.

1. **`refreshOnline()`** added as a distinct Q_INVOKABLE that clears the URL override and calls live refresh — lets QML Refresh buttons work without exposing internal flag management.

1. **Self-signed confirm flow in QML**: `Connections` block catches `installFinished` with `self-signed plugin -- confirm to install` in the error string, stores `pendingLocalInstallPath`, opens `selfSignedDialog`. On `selfSignedDialog.onAccepted`: `PluginCatalog.installFromFile(pendingLocalInstallPath, true)`.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Private member access blocked anonymous namespace test seam**

- **Found during:** Task 1 (installFromFile implementation)
- **Issue:** `static QString PluginCatalogModel::s_pluginsDirOverride` as a private class member is inaccessible from the anonymous-namespace `userPluginsDir()` free function (C++ access control applies even within the same TU)
- **Fix:** Replaced private class member with `QString g_pluginsDirOverride{}` at TU-level in the anonymous namespace; `setPluginsDirOverride()` sets the TU-level variable directly
- **Files modified:** `src/app/src/plugin_catalog_model.hpp`, `src/app/src/plugin_catalog_model.cpp`
- **Verification:** Compiled cleanly; `test_catalog_offline.cpp` `setPluginsDirOverride(tmp.filePath(...))` works correctly
- **Committed in:** `2209f05` (part of Task 1 commit)

**2. [Rule 1 - Bug] `-Werror=parentheses` on compound boolean in REQUIRE macro**

- **Found during:** Task 2a (test_catalog_offline.cpp)
- **Issue:** `REQUIRE((sdState == QStringLiteral("offline") || sdState == QStringLiteral("cached") || sdState == QStringLiteral("loading") == false))` — operator precedence ambiguity triggers `-Werror=parentheses` on GCC/Clang
- **Fix:** Simplified to `REQUIRE(sdState != QStringLiteral("loading"))` — cleaner and unambiguous
- **Files modified:** `tests/unit/test_catalog_offline.cpp`
- **Verification:** Build clean; assertion semantics unchanged (state != loading is the critical invariant)
- **Committed in:** `2a46f92` (part of Task 2a commit)

**3. [Rule 3 - Blocking] Pre-commit hook clang-format reformatted staged files**

- **Found during:** Task 1 commit, Task 2a commit
- **Issue:** clang-format hook reformatted staged C++ files; `git commit` failed (hooks exit non-zero on diff). Affected Task 1 (2 attempts) and Task 2a (1 re-stage)
- **Fix:** Re-staged reformatted files after each hook run; re-committed
- **Files modified:** `src/app/src/plugin_catalog_model.hpp`, `src/app/src/plugin_catalog_model.cpp`
- **Verification:** Pre-commit passed on subsequent attempt
- **Committed in:** Part of `2209f05` and `2a46f92` respectively

______________________________________________________________________

**Total deviations:** 3 auto-fixed (2 bugs, 1 blocking)
**Impact on plan:** All three necessary for compilation correctness and hook compliance. No scope creep.

## Issues Encountered

- None beyond the three auto-fixed deviations above.

## Security Posture (STRIDE mitigations landed)

| Threat ID         | Status    | Mechanism                                                                                                                                                    |
| ----------------- | --------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| T-22-toctou       | Mitigated | Extract to `.plugin_staging/`, verify, `QDir::rename()` only on pass — Refused package never in `installedPlugins/`                                          |
| T-22-tamper-local | Mitigated | `verifyStagedPlugin` -> `Refused` -> staging dir deleted, `installFinished(false)`                                                                           |
| T-22-unsigned     | Mitigated | SelfSigned requires `userConfirmedUnsigned=true`; without it emits confirm-required error; QML dialog enforces the two-step                                  |
| T-22-phonehome    | Mitigated | Constructor no longer calls live `refresh()`; `reload()` sets `"disabled"` sentinel when `m_onlineCatalogEnabled==false`; asserted by `CatalogOffline` suite |
| T-22-zipslip      | Mitigated | `extractSdPluginArchive` reused unchanged (zip-slip guard from Phase 13)                                                                                     |
| T-22-bomb         | Mitigated | `validateDownloadedArchive` size cap (`kMaxPluginDownloadBytes`) before extraction                                                                           |

## Known Stubs

None — all data paths are wired. The `localInstallStatus` QML label is cleared to `""` on success (not a placeholder — it's intentional UX: status disappears after install).

## Next Phase Readiness

- PLUGIN-14 is complete; Phase 22 is fully done (plan 22-01 + 22-02)
- Phase 23 (plugin-store-ui polish) can extend the `FileDialog` + opt-in toggle affordances built here
- Phase 25 hardware-verification does not depend on PLUGIN-14

## Self-Check: PASSED

All files found, all commits verified, COD-031 clean (0 nlohmann matches in changed files), FileDialog present (2 occurrences), installFromFile present (8 occurrences in cpp).

______________________________________________________________________

*Phase: 22-plugin-store-local-install*
*Completed: 2026-05-24*
