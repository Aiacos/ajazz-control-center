---
phase: 18-plugin-manifest-discovery-lifecycle-spawn
fixed_at: 2026-05-24T12:48:12Z
review_path: (direct objective — no REVIEW.md source)
iteration: 1
findings_in_scope: 2
fixed: 2
skipped: 0
status: all_fixed
---

# Phase 18: Build-Break Fix Report

**Fixed at:** 2026-05-24T12:48:12Z
**Commit:** 853fcf2
**Iteration:** 1

**Summary:**

- Findings in scope: 2 (both were hard -Werror build errors in plugin_manager.cpp)
- Fixed: 2
- Skipped: 0

## Fixed Issues

### FIX-01: `scripts()` does not exist on `QQuickWebEngineProfile`

**File modified:** `src/app/src/plugin_manager.cpp`, `src/app/CMakeLists.txt`
**Commit:** 853fcf2
**Applied fix:**

`profile->scripts()->insert(makeMiraboxShim())` (line 253, under `AJAZZ_HAVE_WEBENGINE`)
was changed to `profile->userScripts()->insert(makeMiraboxShim())`.

`scripts()` does not exist on `QQuickWebEngineProfile` in Qt 6. The correct API is
`userScripts()` which returns `QQuickWebEngineScriptCollection*` (confirmed against
`/usr/include/qt6/QtWebEngineQuick/qquickwebengineprofile.h`). `QQuickWebEngineScriptCollection`
has a public `insert(QWebEngineScript const&)` method, but its definition is only in the
private header `qquickwebenginescriptcollection_p.h`.

To make the complete type visible without a full class redefinition, the fix:

1. Adds `#include <QtWebEngineQuick/private/qquickwebenginescriptcollection_p.h>` inside
   the `#if defined(AJAZZ_HAVE_WEBENGINE)` block in `plugin_manager.cpp`.

1. Wires `Qt6::WebEngineQuickPrivate` into `src/app/CMakeLists.txt` (inside
   `if(AJAZZ_HAVE_WEBENGINE)`) via `find_package(Qt6 QUIET COMPONENTS WebEngineQuickPrivate)`.
   This exposes the private include path. A `string(CONCAT ...)` fallback is provided for
   Qt installations that lack the cmake package. No additional `.so` link is needed — the
   symbols live in `Qt6::WebEngineQuick` (already linked); only the include path is added.

The Mirabox shim injection is real, already-implemented work from Phase 18 plan 18-03
(PLUGIN-11). The fix preserves this functionality correctly.

### FIX-02: `loadInspector` semantic misuse and wrong argument count

**File modified:** `src/app/src/plugin_manager.cpp`
**Commit:** 853fcf2
**Applied fix:**

`m_piController->loadInspector(code, manifest.codePath, {})` (line 255) was removed and
replaced with an honest `qInfo()` deferral log message.

The call was wrong for two independent reasons:

1. **Wrong argument count:** `PropertyInspectorController::loadInspector` takes 4 `QString`
   arguments (`pluginUuid`, `htmlAbsPath`, `actionUuid`, `contextUuid`), not 3.

1. **Semantic misuse:** `loadInspector()` is the Phase-20 per-action Property Inspector
   settings-panel loader. It is NOT a plugin main-view loader. Calling it with placeholder
   args would corrupt the active PI state (replace `activeProfile`/`activeChannel`/`activeUrl`)
   and mis-represent the PI lifecycle to QML.

The honest fix is a deferral: the plugin's in-process Chromium page-load (rendering
`index.html` as the plugin's main UI) requires the Phase-19 device-bridge surface and
Phase-20 WebEngine view-routing work, which are not yet built. The plugin IS still
registered in `m_live` immediately after the `#if defined(AJAZZ_HAVE_WEBENGINE)` block,
so lifecycle tracking (crash restart, `exitApp` shutdown) is fully active. Only the
Chromium page-load is deferred.

A `qInfo()` message is emitted to make the deferral discoverable at runtime:

```
PluginManager: HTML plugin '<name>' registered in m_live;
in-process WebEngine page-load deferred to Phase 19/20 bridge work
```

This matches the project's core value: "never lying about what a device/feature can do."

## Build and Test Verification

**App binary:** `Linking CXX executable src/app/ajazz-control-center` — no errors, no
`FAILED`, no `ninja: build stopped`.

**Test suite:** `ctest --preset linux-release` — **478/478 passed** (100%), 0 failed.

______________________________________________________________________

_Fixed: 2026-05-24T12:48:12Z_
_Fixer: Claude (gsd-code-fixer)_
_Iteration: 1_

______________________________________________________________________

______________________________________________________________________

## phase: 18-plugin-manifest-discovery-lifecycle-spawn fixed_at: 2026-05-24T15:15:00Z review_path: .planning/phases/18-plugin-manifest-discovery-lifecycle-spawn/18-REVIEW.md iteration: 2 findings_in_scope: 7 fixed: 7 skipped: 0 status: all_fixed

# Phase 18: Code Review Fix Report (Iteration 2)

**Fixed at:** 2026-05-24T15:15:00Z
**Source review:** `.planning/phases/18-plugin-manifest-discovery-lifecycle-spawn/18-REVIEW.md`
**Iteration:** 2

**Summary:**

- Findings in scope: 7 (3 Critical, 3 Warning, 1 Info)
- Fixed: 7
- Skipped: 0

**Build gate:** PASSED — `Linking CXX executable src/app/ajazz-control-center` (65 MB binary)
**Test gate:** PASSED — 484/484 tests, 0 failures (+6 new security regression tests)

## Fixed Issues

### CR-01: Spawned child processes inherit the full host environment

**Files modified:** `src/app/src/plugin_manager.cpp`, `src/app/src/plugin_manager.hpp`
**Commit:** `4d38346`
**Applied fix:**
Added `buildChildEnv()` static helper that constructs a `QProcessEnvironment` from
an explicit allowlist: PATH, HOME, TMPDIR/TEMP/TMP, LANG, LC_ALL, LC_CTYPE,
DISPLAY, WAYLAND_DISPLAY. All other variables (DBUS_SESSION_BUS_ADDRESS,
XDG_RUNTIME_DIR, *\_TOKEN/*\_SECRET/*\_KEY/*\_PASSWORD, etc.) are excluded.
Applied `setProcessEnvironment(buildChildEnv())` to both the node and native
`QProcess` before `start()`. Exposed via `buildChildEnvironmentForTesting()` public
static for test assertions. Used default constructor `QProcessEnvironment()` for
Qt 6.7 compatibility (`QProcessEnvironment::empty()` is Qt 6.8+).

### CR-02: Double-fire of `onProcessFailed` on `QProcess::FailedToStart`

**Files modified:** `src/app/src/plugin_manager.cpp`
**Commit:** `4d38346` (included with CR-01 — same QProcess setup blocks)
**Applied fix:**
In both the node and native `QProcess` signal connections, changed
`errorOccurred` lambda to skip `FailedToStart` errors (they will also fire
`finished`), routing FailedToStart exclusively through the `finished` handler.
This eliminates the double crash-credit accumulation and the rapid double-restart
loop on a persistently-missing binary.

### CR-03: `resolveCodePath()` result is not path-validated

**Files modified:** `src/app/src/plugin_manager.cpp`
**Commit:** `5fc9ef3`
**Applied fix:**
After `resolveCodePath(manifest)` (which may return `codePathWin`/`codePathMac`
bypassing the existing `isSafeUuidComponent` check on `pluginId`), added a
separator/traversal guard: reject if code is empty, contains `/`, contains `\`,
or contains `..`. This closes the T-18-PATHTRAV bypass for platform-override paths.

Same commit also fixes WR-03: added `pluginUuid` variable that prefers
`manifest.puuid` over `pluginId` (codePath-based key) for the `-pluginUUID`
argument passed to `buildNodeArgv()`. PUUID is validated with `isSafeUuidComponent`
when non-empty.

### WR-01: Private Qt header `qquickwebenginescriptcollection_p.h` has no public-API path

**Files modified:** `src/app/src/plugin_manager.cpp`
**Commit:** `07f0957`
**Applied fix:**
Added `static_assert(QT_VERSION >= QT_VERSION_CHECK(6, 7, 0), ...)` adjacent to
the `#include <QtWebEngineQuick/private/qquickwebenginescriptcollection_p.h>`.
If a Qt minor-version bump moves the private header path, the build fails loudly
at compile time rather than silently. Extended the comment to document the Qt API
gap and the correct maintainer action.

### WR-02: `onProcessFailed` restart branch re-spawns HTML plugins

**Files modified:** `src/app/src/plugin_manager.cpp`
**Commit:** `7f58f93`
**Applied fix:**
Changed `if (it != m_live.end())` to `if (it != m_live.end() && it->second.process != nullptr)`.
HTML/WebEngine plugins are stored in `m_live` with `process == nullptr` (they run
in-process via Chromium). The missing null-process guard meant a spurious
`onProcessFailed` call for an HTML UUID would re-invoke `spawn(manifest)`, causing
a duplicate `profile->userScripts()->insert(makeMiraboxShim())`.

### WR-03: `-pluginUUID` argument uses `codePath` filename instead of identity UUID

**Files modified:** `src/app/src/plugin_manager.cpp`
**Commit:** `5fc9ef3` (included with CR-03)
**Applied fix:**
Added `QString const pluginUuid = manifest.puuid.isEmpty() ? pluginId : manifest.puuid;`
and validated `pluginUuid` with `isSafeUuidComponent` when `manifest.puuid` is
non-empty. Passed `pluginUuid` (instead of `pluginId`) to `buildNodeArgv()` as the
`-pluginUUID` argument. Fallback to codePath-based `pluginId` when PUUID is absent
preserves backwards compatibility.

### IN-01: `discover()` reads manifest files without a size cap

**Files modified:** `src/app/src/plugin_manager.cpp`
**Commit:** `cb5315a`
**Applied fix:**
Added a 1 MiB size guard (`constexpr qint64 kMaxManifestBytes = 1LL * 1024 * 1024`)
before `f.readAll()` in `discover()`. Files exceeding the cap are logged and skipped.
Guards against multi-megabyte or corrupt manifest.json files being fully read into
memory before the `QJsonDocument` parser could reject them, in the user-configurable
plugins directory.

## Security Tests Added

**Commit:** `05fff2a`
**File:** `tests/unit/test_plugin_lifecycle.cpp`

6 new `PluginManagerTest` cases added:

| Test name                                                | Covers                                                        |
| -------------------------------------------------------- | ------------------------------------------------------------- |
| `child env excludes secrets and includes PATH`           | CR-01: allowlist env contains safe keys, excludes banned keys |
| `FailedToStart fires onProcessFailed only once`          | CR-02: single physical failure = one crash credit             |
| `spawn rejects code path with directory separator`       | CR-03: '/' and '..' in codePath rejected                      |
| `HTML plugin is not re-spawned on failure`               | WR-02: null-process guard                                     |
| `spawn uses puuid as -pluginUUID when set`               | WR-03: PUUID passed as argv[4]                                |
| `spawn uses codePath as -pluginUUID when puuid is empty` | WR-03: fallback to codePath                                   |

______________________________________________________________________

_Fixed: 2026-05-24T15:15:00Z_
_Fixer: Claude (gsd-code-fixer)_
_Iteration: 2_
