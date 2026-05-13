---
phase: 01-sec-003-plugin-host
plan: 01
subsystem: plugins, app
tags: [qt6, cpp20, python, ipc, sandboxing, manifest-signing, security]
provides:
  - OutOfProcessPluginHost wired into Application lifecycle
  - LoadedPluginsModel routed through plugin host
  - Manifest signature verification before plugin load
  - CMake exports so app target sees plugins headers
  - Self-review pass + two robustness gaps closed
affects: [LoadedPluginsPage QML, plugin SDK consumers, CI workflow ci.yml]
tech-stack:
  added: []
  patterns: [out-of-process IPC, signed-manifest gating, child-process host]
key-files:
  created: [python/ajazz_plugins/_host_child.py]
  modified:
    - .github/workflows/ci.yml
    - TODO.md
    - src/app/CMakeLists.txt
    - src/app/qml/LoadedPluginsPage.qml
    - src/app/src/application.cpp
    - src/app/src/application.hpp
    - src/app/src/loaded_plugins_model.cpp
    - src/app/src/loaded_plugins_model.hpp
    - src/plugins/src/manifest_signer.cpp
    - src/plugins/src/manifest_signer_win32.cpp
    - src/plugins/src/out_of_process_plugin_host_win32.cpp
    - tests/unit/test_manifest_signer.cpp
key-decisions:
  - Plugins run in a separate Python child process (not in-app threads) for blast-radius isolation.
  - Manifest signatures are verified before any plugin code is loaded, on both POSIX and Win32 paths.
  - App target is granted PUBLIC visibility on plugins headers via CMake to avoid intermittent build breakage.
duration: ~2 days (2026-05-02 → 2026-05-03)
completed: 2026-05-03
---

# Phase 1: SEC-003 Plugin Host Integration Summary

**Wires the out-of-process Python plugin host into the Application lifecycle, gates plugin loading on manifest signature verification, and closes two pre-existing robustness gaps surfaced during self-review.**

## Performance

- **Duration:** ~2 days (commits 2026-05-02 → 2026-05-03)
- **Tasks:** 4 commits as a single thematic plan
- **Files modified:** 13

## Accomplishments

- `Application` now constructs and owns an `OutOfProcessPluginHost`; `LoadedPluginsModel` reflects host state to QML.
- Manifest signature verification runs **before** plugin load on both POSIX and Win32 paths.
- CMake `target_include_directories` for the app now consistently exposes plugins headers (PUBLIC visibility) — fixes intermittent build break that motivated `4b4cbcc`.
- Two robustness gaps in the SEC-003 path closed (`f044660`); details in commit body.
- Self-review pass (`65015a1`) addressed feedback before merge.

## Task Commits

1. **chore(review): address self-review pass on SEC-003 #51** — `65015a1`
1. **fix(plugins): close two pre-existing robustness gaps in SEC-003 path** — `f044660`
1. **feat(app): wire OutOfProcessPluginHost into Application (SEC-003 #51)** — `43add98`
1. **fix(cmake): always expose plugins headers to the app target** — `4b4cbcc`

## Files Created/Modified

- `python/ajazz_plugins/_host_child.py` — Python child-process entrypoint for the plugin host.
- `src/app/src/application.{hpp,cpp}` — Owns the plugin host; initializes it during Application setup.
- `src/app/src/loaded_plugins_model.{hpp,cpp}` — Routed through the plugin host so the QML page reflects live plugin state.
- `src/app/qml/LoadedPluginsPage.qml` — Consumes the model.
- `src/plugins/src/manifest_signer.cpp` + `manifest_signer_win32.cpp` — Signature verification.
- `src/plugins/src/out_of_process_plugin_host_win32.cpp` — Win32-specific IPC for the host.
- `tests/unit/test_manifest_signer.cpp` — Unit tests for signer.
- `src/app/CMakeLists.txt` — PUBLIC exposure of plugins headers.
- `.github/workflows/ci.yml` — CI updates for the new plugin host paths.
- `TODO.md` — checklist updates.

## Next Phase Readiness

Already shipped to `main`. Retro-spec'd for code review.
