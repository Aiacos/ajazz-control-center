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
