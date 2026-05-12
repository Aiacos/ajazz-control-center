---
phase: 02-qml-singleton-sweep
plan: 01
subsystem: qml, app
tags: [qt6, qml, qml-singleton, theming, registration, dual-instance-bug]
provides:
  - Branding light-theme bug fixed (silent dual-instance was the root cause)
  - Preventive sweep across 5 other QML_SINGLETON services
  - Tests updated to reflect corrected singleton lifecycle
affects: [BrandingService consumers, ThemeService consumers, all QML pages importing the affected services]
tech-stack:
  added: []
  patterns: [qmlRegisterSingletonInstance over bare QML_SINGLETON macro for shared C++ instances]
key-files:
  created: []
  modified:
    - src/app/src/branding_service.hpp
    - src/app/src/autostart_service.hpp
    - src/app/src/loaded_plugins_model.hpp
    - src/app/src/plugin_catalog_model.hpp
    - src/app/src/profile_controller.hpp
    - src/app/src/property_inspector_controller.hpp
    - tests/unit/test_branding_service.cpp
    - tests/unit/test_theme_service.cpp
key-decisions:
  - ? The bare `QML_SINGLETON` macro silently creates a second instance per QML import context when a C++-side instance already exists. Canonical pattern
    : register the C++ instance via `qmlRegisterSingletonInstance` so QML and C++ share one object.
  - Apply the fix to all 6 affected services even though only branding had a user-visible symptom — this is a class of bug, not a point bug.
duration: ~minutes (2 commits, same day, 2026-05-04)
completed: 2026-05-04
---

# Phase 2: QML_SINGLETON Dual-Instance Sweep Summary

**Fixes the silent dual-instance bug in `BrandingService` (light theme rendered dark) and applies the same fix prophylactically across five other `QML_SINGLETON` services.**

## Performance

- **Duration:** Same-day work (2026-05-04, 12-second gap between the two commits)
- **Tasks:** 2 commits — point fix + preventive sweep
- **Files modified:** 8

## Accomplishments

- **Root cause:** Bare `QML_SINGLETON` macro on a C++ class registered via `qmlRegisterSingletonType`-equivalent path silently double-instantiates per QML import. The C++ side held one instance; QML imports created a second. Setters on the QML-side instance were therefore invisible to C++ consumers (and vice-versa).
- Branding light-theme bug fixed (`d7f932f`) by registering a single shared instance via `qmlRegisterSingletonInstance`.
- Preventive sweep (`e221b21`) applied the same fix to 5 other affected services: `AutostartService`, `LoadedPluginsModel`, `PluginCatalogModel`, `ProfileController`, `PropertyInspectorController`.
- Tests for `BrandingService` and `ThemeService` updated to assert the corrected lifecycle.

## Task Commits

1. **fix(branding): light theme silently rendered dark — QML_SINGLETON dual-instance** — `d7f932f`
1. **chore(qml): prevent same dual-instance bug on five other QML_SINGLETON services** — `e221b21`

## Files Created/Modified

- `src/app/src/branding_service.hpp` — Fixed singleton registration (the originally-buggy service).
- `src/app/src/autostart_service.hpp` — Preventive fix.
- `src/app/src/loaded_plugins_model.hpp` — Preventive fix.
- `src/app/src/plugin_catalog_model.hpp` — Preventive fix.
- `src/app/src/profile_controller.hpp` — Preventive fix.
- `src/app/src/property_inspector_controller.hpp` — Preventive fix.
- `tests/unit/test_branding_service.cpp` — Test updates for corrected lifecycle.
- `tests/unit/test_theme_service.cpp` — Test updates for corrected lifecycle.

## Next Phase Readiness

Already shipped to `main`. Retro-spec'd for code review.
