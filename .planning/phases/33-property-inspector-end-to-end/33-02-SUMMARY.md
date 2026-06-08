---
phase: 33-property-inspector-end-to-end
plan: 02
subsystem: plugins
tags: [property-inspector, sdk-2, thin-ui, objectname, debug-channel, qml, pi-02]

# Dependency graph
requires:
  - phase: 33
    plan: 01
    provides: PI-04 lifecycle events (propertyInspectorDidAppear/DidDisappear, titleParametersDidChange) emitted on the wire — this plan adds the debug-addressable controls that drive them
provides:
  - openPiButton objectName + affordance (SecondaryButton, onClicked -> maybeLoadInspector()) on Inspector.qml — drives loadInspector for the current selection
  - closePiButton objectName + affordance (SecondaryButton, onClicked -> PropertyInspectorController.closeInspector()) on Inspector.qml
  - piPanelLoader objectName on the htmlPiLoader Loader (qml.get active/visible target)
  - piWebView objectName on the PIWebView root Item (screenshot / qml.get target)
affects: [Phase 33 plan 03 (real-PI human-verify checkpoint — uses these same affordances), Phase 34 EVENT-01]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - 'Debug-addressable thin-UI contract: every new PI control carries an objectName so scripts/ajazz-debug findByName can qml.invoke / qml.get / screenshot it (CLAUDE.md DOD)'
    - Explicit open/close affordances are plain Buttons (SecondaryButton), never a Switch/CheckBox — qml.invoke fires onClicked but cannot reproduce Switch.toggled (CLAUDE.md harness gap)
    - openPiButton reuses the existing maybeLoadInspector() resolve+load path rather than re-implementing PI path/uuid/context resolution

key-files:
  created: []
  modified:
    - src/app/qml/Inspector.qml
    - src/app/qml/PIWebView.qml

key-decisions:
  - No C++ change needed — loadInspector and closeInspector are already Q_INVOKABLE on PropertyInspectorController; openPiButton calls maybeLoadInspector() (which encapsulates resolve+load) and closePiButton calls closeInspector() directly. No openForCurrentSelection() invokable added (research A4 fallback unnecessary; SecondaryButton.onClicked is harness-drivable).
  - Affordances placed in a RowLayout directly under the PageHeader, visible when hasSelection — the HTML PI still auto-opens on selection; these are the discrete debug-drivable controls the headless criteria require.
  - closePiButton enabled only when hasHtmlInspector is true (no-op guard) but always addressable.

patterns-established:
  - PI thin-UI debug contract: objectName-addressable open/close affordances + Loader + WebView container

requirements-completed: [PI-01, PI-02]

# Metrics
duration: ~12min
completed: 2026-06-08
---

# Phase 33 Plan 02: PI-02 Thin-UI objectNames + Open/Close Affordance Summary

**Closes the PI-02 thin-UI debug contract — adds the four objectNames (openPiButton / closePiButton / piPanelLoader / piWebView) and two debug-drivable open/close affordances that make the PI panel headlessly verifiable via scripts/ajazz-debug — and confirms PI-01 + PI-03 GREEN by their existing unit tests. QML-only; zero C++ touched.**

## Performance

- **Duration:** ~12 min
- **Tasks:** 2 (Task 1 QML build, Task 2 verify — live debug-channel pass deferred to orchestrator)
- **Files modified:** 2 (both QML)

## Accomplishments

- **Inspector.qml** — added `objectName: "piPanelLoader"` to the `htmlPiLoader` Loader, and a `RowLayout` (visible when `hasSelection`) of two `SecondaryButton`s directly under the PageHeader:
  - `openPiButton` — `onClicked: root.maybeLoadInspector()` (reuses the existing resolve+load path that derives PI path + pluginUuid + wire context for the current selection and calls `loadInspector`).
  - `closePiButton` — `onClicked: PropertyInspectorController.closeInspector()`, `enabled: PropertyInspectorController.hasHtmlInspector`.
  - Both are plain `Button`s (SecondaryButton), NOT a Switch/CheckBox — per the CLAUDE.md harness gap, `qml.invoke` fires `onClicked` but cannot reproduce `Switch.toggled`.
- **PIWebView.qml** — added `objectName: "piWebView"` to the root `Item` so `screenshot` + `qml.get` can target the rendered PI container.
- No C++ change required: `loadInspector` and `closeInspector` are already `Q_INVOKABLE` on `PropertyInspectorController`; the research A4 fallback (`openForCurrentSelection()` invokable) proved unnecessary.

## Task Commits

1. **Task 1: Add objectNames + open/close affordances to the PI panel** — `75e849f` (feat, QML-only).
1. **Task 2: Build + ctest verify** — no source change; recorded here. Live debug-channel pass is the orchestrator's consolidated step (see "Pending live verification").

## Files Created/Modified

- `src/app/qml/Inspector.qml` — `piPanelLoader` objectName on the Loader + `openPiButton`/`closePiButton` RowLayout affordances.
- `src/app/qml/PIWebView.qml` — `piWebView` objectName on the root Item.

## Decisions Made

- **QML-only, no C++.** The controller already exposes both `Q_INVOKABLE`s; the affordances call them (open via `maybeLoadInspector()`, close via `closeInspector()`). `git diff --stat` confirms only the two QML files changed — every GREEN C++ file (pi_cef_shim, pi_bridge, plugin_settings_store, pi_url_policy, pi_url_request_interceptor, plugin_mirabox_shim, property_inspector_controller, sd_plugin_server) is untouched.
- **SecondaryButton over a bespoke control** — it is a plain `Button` subclass, so `onClicked` runs and the harness can drive it; it matches the existing inspector form styling.

## Deviations from Plan

None — plan executed exactly as written. (No `Q_INVOKABLE` was added because `closeInspector` already exists and `maybeLoadInspector()` already encapsulates the open path; the plan explicitly allowed "a controller `Q_INVOKABLE` is an acceptable fallback" — it was not needed, so `property_inspector_controller` stays untouched, tightening the GREEN-files guard.)

## Validation

- `cmake --build build/linux-release --target ajazz-control-center` → clean (qmlcachegen regenerated Inspector_qml.cpp + PIWebView_qml.cpp after `touch`; linked OK).
- objectName grep: `openPiButton`=1, `closePiButton`=1, `piPanelLoader`=1 (Inspector.qml); `piWebView`=1 (PIWebView.qml).
- `ctest --preset linux-release -LE qml` → **728/728 passed** (matches the plan-01 baseline; no regression). This confirms PI-01 (`test_pi_bridge.cpp:419` cefQuery shim source, `:682` sdpi.css present+non-empty, per-plugin profile) and PI-03 (`test_pi_bridge.cpp:224` per-context survives a fresh PIBridge, `:252` global survives, `:279` per-context isolation, `:304` path-traversal refusal) GREEN.
- `ctest --preset linux-release -L qml` → **17/17 passed** (QML smoke gate still builds + runs with the new objectNames).
- `git diff --stat` → only `src/app/qml/Inspector.qml` (+33) and `src/app/qml/PIWebView.qml` (+3). GREEN files untouched.

## Pending live verification

The live debug-channel pass is the ORCHESTRATOR's consolidated step (this executor does NOT launch the GUI app, per the plan). Run against a `build/linux-release` app launched with `AJAZZ_DEBUG_CONTROL=1` in an isolated `XDG_RUNTIME_DIR` (kill stale instances + rm the socket first; launch via `nohup … &`; kill by exact PID, never `pkill -f`). Install a real `.sdPlugin` with a settings PI (System Monitor recipe, memory `project_plugin_install_demo_working`, or `plugin.installFromFile`) and bind its action to a key, then:

**Criterion 1 — PI HTML renders (not blank), open affordance addressable:**

```
scripts/ajazz-debug qml.invoke openPiButton
scripts/ajazz-debug qml.get piPanelLoader      # expect: active/visible true
scripts/ajazz-debug qml.get piWebView          # expect: container exists
scripts/ajazz-debug screenshot                 # READ it: real PI HTML, NOT a blank frame
```

**Criterion 3 — didAppear on open / didDisappear on close:**

```
scripts/ajazz-debug plugin.protocolLog         # expect: propertyInspectorDidAppear (after openPiButton above)
scripts/ajazz-debug qml.invoke closePiButton
scripts/ajazz-debug plugin.protocolLog         # expect: propertyInspectorDidDisappear
```

**Criterion 2 — PI-03 headless settings round-trip (didReceiveSettings):**

```
scripts/ajazz-debug plugin.simulatePiSettings '{"context":"<ctx>","settings":{"k":"v"}}'
scripts/ajazz-debug plugin.protocolLog         # expect: OUT didReceiveSettings
```

**PI-01 / PI-03 verify-only (no rebuild):** confirmed GREEN above by `test_pi_bridge.cpp` unit tests in the full ctest run; the live checks above exercise the same machinery end-to-end. The four PI QML files were verify-only for C++; only Inspector.qml + PIWebView.qml gained objectNames/affordances.

If the screenshot is blank or any protocolLog event is missing, capture the failure for a gap-closure plan rather than marking the criterion DONE.

## Next Phase Readiness

- PI-02 thin-UI contract complete; criteria 1/2/3 are now headlessly drivable via the objectName-addressed controls.
- **ROADMAP criterion 5 (real PI JS `$SD.setSettings()` round-trip + app restart) is still pending — deferred to plan 33-03's human-verify checkpoint.** PI-03 therefore stays **partial** in REQUIREMENTS until that checkpoint passes; PI-01 + PI-02 are marked complete.

## Self-Check: PASSED

- FOUND: `src/app/qml/Inspector.qml` (piPanelLoader + openPiButton + closePiButton)
- FOUND: `src/app/qml/PIWebView.qml` (piWebView)
- FOUND: `.planning/phases/33-property-inspector-end-to-end/33-02-SUMMARY.md`
- FOUND: commit `75e849f` (Task 1, QML-only).
