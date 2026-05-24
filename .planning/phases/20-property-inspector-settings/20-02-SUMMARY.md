---
phase: 20-property-inspector-settings
plan: '02'
subsystem: property-inspector
tags: [feat, plugin-09, cefquery, sdpi-css, pi-bridge, tdd]
dependency_graph:
  requires: [20-01]
  provides: [PLUGIN-09-cefquery-polyfill, PLUGIN-09-sdpi-css-served, PIBridge-invoke-dispatcher]
  affects:
    - src/app/src/pi_cef_shim.hpp
    - src/app/src/pi_cef_shim.cpp
    - src/app/src/pi_bridge.hpp
    - src/app/src/pi_bridge.cpp
    - src/app/src/pi_url_policy.hpp
    - src/app/src/pi_url_policy.cpp
    - src/app/src/pi_url_request_interceptor.cpp
    - resources/streamdeck/sdpi.css
    - src/app/CMakeLists.txt
    - tests/unit/CMakeLists.txt
    - tests/unit/test_pi_bridge.cpp
tech_stack:
  added: []
  patterns:
    - QWebEngineScript DocumentCreation/MainWorld injection (mirrors plugin_mirabox_shim.hpp pattern)
    - QRC bundled static asset with QT_RESOURCE_ALIAS + interceptor redirect
    - Closed-dispatch invoke() table (no default-to-arbitrary, COD-031 Qt JSON only)
    - TDD RED/GREEN per task (failing test committed before implementation)
key_files:
  created:
    - src/app/src/pi_cef_shim.hpp
    - src/app/src/pi_cef_shim.cpp
    - resources/streamdeck/sdpi.css
  modified:
    - src/app/src/pi_bridge.hpp
    - src/app/src/pi_bridge.cpp
    - src/app/src/pi_url_policy.hpp
    - src/app/src/pi_url_policy.cpp
    - src/app/src/pi_url_request_interceptor.cpp
    - src/app/CMakeLists.txt
    - tests/unit/CMakeLists.txt
    - tests/unit/test_pi_bridge.cpp
decisions:
  - kCefQueryShimSource as inline constexpr std::string_view (not char*) for unit-testable pure string per 18-03 discipline
  - invoke() uses a closed dispatch table (explicit if/else on event string) not a map to keep the dispatch surface auditable
  - isSdpiCssRequest in pi_url_policy.cpp (no WebEngine dep, unit-testable) not in the interceptor TU
  - sdpi.css redirect placed BEFORE the allow/deny check so qrc: is already Allow and no allowlist change needed
  - Fixed qrc target constant (T-20-CSS-REDIR) -- never derived from the request URL
metrics:
  duration_minutes: 10
  completed_date: '2026-05-24T16:18:21Z'
  tasks_completed: 2
  files_modified: 11
requirements: [PLUGIN-09]
---

# Phase 20 Plan 02: cefQuery Polyfill + sdpi.css Bundle Summary

**One-liner:** cefQuery JS polyfill (kCefQueryShimSource + QWebEngineScript builder) + PIBridge::invoke closed dispatcher + bundled Elgato sdpi.css served via interceptor qrc redirect, both unit-tested with TDD RED/GREEN.

## What Was Built

This plan closes two PLUGIN-09 gaps: the cefQuery polyfill (no cefQuery existed in tree) and the bundled sdpi.css stylesheet (no sdpi\* file existed). All production code follows the established patterns from plan 18-03 (Mirabox shim) and the existing pi_url_policy/interceptor architecture.

### Task 1 - cefQuery polyfill + PIBridge::invoke dispatcher

**`src/app/src/pi_cef_shim.hpp`:**

- `kCefQueryShimSource` -- `inline constexpr std::string_view` with the polyfill JS body. Always available without WebEngine; unit-testable as a pure string (18-03 string-test discipline). Contains `window.cefQuery`, `onSuccess`, `onFailure`, `$SD`.
- WebEngine-gated `QWebEngineScript makeCefQueryShim()` declaration: name `"ajazz-cefquery-shim"`, `DocumentCreation`, `MainWorld`, `runsOnSubFrames(true)`. Mirrors `makeMiraboxShim()` exactly.

**`src/app/src/pi_cef_shim.cpp`:**

- `makeCefQueryShim()` implementation. Sets DocumentCreation injection point (per 18-03 Pitfall 4 -- never `runJavaScript`-after-load), MainWorld (same world as PI JS), runsOnSubFrames, sourceCode from kCefQueryShimSource.

**`src/app/src/pi_bridge.hpp/.cpp`:**

- Added `Q_INVOKABLE QString invoke(QString const& json)` -- the §8-verbatim generic dispatcher. Parses JSON with QJsonDocument/QJsonObject (Qt JSON only, COD-031). Dispatches on `"event"` field to exactly: `setSettings`, `getSettings`, `setGlobalSettings`, `getGlobalSettings`, `sendToPlugin`, `openUrl`, `logMessage`. Unknown events: AJAZZ_LOG_WARN + return `"{}"`. Never reaches C++ outside the fixed typed-slot set (T-20-SHIM security).

**`src/app/CMakeLists.txt`:**

- `pi_cef_shim.hpp` added to `ACC_QML_MODULE_SOURCES` (header always reachable, including non-WebEngine builds).
- `pi_cef_shim.cpp` added to the WebEngine-gated `target_sources` block alongside `pi_url_request_interceptor.cpp` and `plugin_mirabox_shim.cpp`.

**Tests (TDD):**

- RED commit `6f7a259`: added 4 failing `[pi-bridge][cefquery]` cases to `test_pi_bridge.cpp` -- string token assertions, getSettings dispatch round-trip, unknown-event guard, WebEngine-gated injection-point case.
- GREEN commit `9a3a52e`: implementation; 3 unconditional cases pass (WebEngine-gated case compiles out since the test binary has no `AJAZZ_HAVE_WEBENGINE`).

### Task 2 - Bundle and serve sdpi.css

**`resources/streamdeck/sdpi.css`:**

- Vendored Elgato sdpi-css (sdpi-wrapper / .sdpi-item class set) with provenance comment citing elgatosf/sdpi-css. Non-empty; contains `.sdpi-item` and `.sdpi-wrapper` as required by tests and acceptance criteria.

**`src/app/src/pi_url_policy.hpp/.cpp`:**

- Added `bool isSdpiCssRequest(QUrl const& url)` in the pure-policy TU (no WebEngine dep; unit-testable). Returns true iff path ends in `/sdpi.css` or equals `sdpi.css` (suffix-exact; rejects `sdpi.css.evil` and directory paths).

**`src/app/src/pi_url_request_interceptor.cpp`:**

- In `interceptRequest()`: calls `isSdpiCssRequest(url)` BEFORE the allow/deny policy check. If true, redirects to fixed constant `"qrc:/qt/qml/AjazzControlCenter/streamdock/sdpi.css"` (T-20-CSS-REDIR: target never derived from request URL). Existing remote-nav policy (kPiHttpsCdnAllowlist) unchanged.

**`src/app/CMakeLists.txt`:**

- `ACC_SDPI_CSS` variable with `QT_RESOURCE_ALIAS streamdock/sdpi.css`; appended to `ACC_QML_MODULE_RESOURCES`. qrc path: `qrc:/qt/qml/AjazzControlCenter/streamdock/sdpi.css`.

**`tests/unit/CMakeLists.txt`:**

- `qt_add_resources` `sdpi_css_test_resource` under PREFIX `/qt/qml/AjazzControlCenter`, alias `streamdock/sdpi.css` -- same qrc path the bundled-resource-present test asserts.

**Tests (TDD):**

- RED commit `f76fcd8`: added 3 failing `[pi-bridge][sdpi]` cases -- `isSdpiCssRequest` true/false, qrc presence/content.
- GREEN commit `25f4597`: implementation; all 3 pass.

## Test Results

```
ctest --preset linux-release -R "cefQuery|PIBridge invoke": 3/3 PASS
ctest --preset linux-release -R "isSdpiCssRequest|bundled sdpi": 3/3 PASS
Full suite: 529/529 PASS (526 pre-plan + 3 new)
```

## Commits

| Task | Commit  | Phase | Description                                                                  |
| ---- | ------- | ----- | ---------------------------------------------------------------------------- |
| 1    | 6f7a259 | RED   | test(20-02): add failing cefquery and invoke dispatcher test cases           |
| 1    | 9a3a52e | GREEN | feat(20-02): cefQuery polyfill + PIBridge::invoke dispatcher (PLUGIN-09)     |
| 2    | f76fcd8 | RED   | test(20-02): add failing sdpi.css helper and bundled resource test cases     |
| 2    | 25f4597 | GREEN | feat(20-02): bundle sdpi.css qrc resource + interceptor redirect (PLUGIN-09) |

## Verification

- `grep -c 'window.cefQuery' src/app/src/pi_cef_shim.hpp` = 6 (>= 1)
- `grep -c 'DocumentCreation' src/app/src/pi_cef_shim.cpp` = 1 (>= 1)
- `grep -rln nlohmann src/app/src/pi_cef_shim.cpp src/app/src/pi_bridge.cpp` -> comment-only references (COD-031)
- `grep -rn QCefView src/app/src/` -> 0 production hits
- `test -s resources/streamdeck/sdpi.css` -> CSS_PRESENT
- `grep -c 'sdpi.css' src/app/src/pi_url_request_interceptor.cpp` = 3 (>= 1)
- `kPiHttpsCdnAllowlist` unchanged; http:// and non-allowlist https:// still deny

## Deviations from Plan

### None -- plan executed exactly as written.

The `typos` pre-commit hook rejected the word "look-alike" variants (spelling false-positive) in a code comment during the Task 2 GREEN commit; the comment was reworded to "suffix variants" before re-staging. No functional change.

## Known Stubs

None. Both cefQuery polyfill and sdpi.css redirect are fully wired. The `makeCefQueryShim()` builder is not yet called from `PropertyInspectorController::loadInspector()` -- that wiring is Plan 20-03's scope (the plan explicitly defers the UI wire to the next plan).

## Threat Flags

| Flag                     | File                            | Description                                                                                       |
| ------------------------ | ------------------------------- | ------------------------------------------------------------------------------------------------- |
| T-20-SHIM mitigated      | pi_cef_shim.hpp + pi_bridge.cpp | cefQuery shim is pure forwarder to sandboxed $SD; invoke() closed dispatch (no arbitrary routing) |
| T-20-SHIM-RACE mitigated | pi_cef_shim.cpp                 | DocumentCreation injection point prevents shim-after-plugin-script race                           |
| T-20-CSS-REDIR mitigated | pi_url_request_interceptor.cpp  | Redirect target is fixed qrc: constant; isSdpiCssRequest uses suffix-exact match                  |

## Self-Check: PASSED

- `src/app/src/pi_cef_shim.hpp` exists: FOUND
- `src/app/src/pi_cef_shim.cpp` exists: FOUND
- `resources/streamdeck/sdpi.css` exists and non-empty: FOUND
- Commit 6f7a259 exists: FOUND
- Commit 9a3a52e exists: FOUND
- Commit f76fcd8 exists: FOUND
- Commit 25f4597 exists: FOUND
- 529/529 ctest green: PASSED
