---
phase: 34-per-app-profiles-event-parity-audit
plan: 01
subsystem: infra
tags: [wayland, qtwaylandscanner, x11, foreground-window, watcher, cod-031, catch2, debug-rpc, cmake, ci, flatpak]

# Dependency graph
requires:
  - phase: 21-input-synthesis
    provides: input_synthesizer.hpp platform-split interface + recording-stub + free-factory pattern (the canonical analog this watcher mirrors)
  - phase: 19-plugin-device-bridge
    provides: debug_control_facade input.* synthetic-RPC idiom (window.setForeground mirrors it)
provides:
  - IActiveWindowWatcher pure-core interface (Qt-free, nlohmann-free, COD-031) + ActiveWindowInfo value struct + makeDefaultActiveWindowWatcher() factory
  - StubActiveWindowWatcher recording stub with injectForeground() seam, settable capabilityAvailable(), log()/clearLog()
  - Vendored swaywm/wlr-protocols wlr-foreign-toplevel-management-unstable-v1.xml (rev-pinned) + qtwaylandscanner codegen wiring
  - 'Build env: Qt6::WaylandClient + libX11 link (Linux-gated), per-OS placeholder watcher TUs, CI qtwayland+wayland+X11 deps, Flatpak doc'
  - window.setForeground debug RPC injecting synthetic foreground changes through the watcher seam + Application::activeWindowWatcher() accessor
  - 'Three registered Catch2 scaffolds: active_window (GREEN), app_profile_switch + app_lifecycle_events (RED [!shouldfail], pending Plan 04)'
affects: [34-02, 34-03, 34-04, watcher-backends, app-profile-auto-switch, event-parity]

# Tech tracking
tech-stack:
  added: [Qt6::WaylandClient, qtwaylandscanner, libX11 (pkg x11), qtwayland CI module]
  patterns:
    - 'Platform-split interface + per-OS gated TU (mirrors input_synthesizer): pure-core interface + stub-defined factory + self-#if-guarded per-OS TUs added unconditionally'
    - Vendored Wayland protocol XML -> qt_generate_wayland_protocol_client_sources (PRIVATE_CODE) with -w on the generated TU to bypass project -Werror
    - Synthetic-injection debug RPC seam mirroring input.* (window.setForeground -> StubActiveWindowWatcher::injectForeground via dynamic_cast)
    - RED Wave-0 scaffolds via Catch2 [!shouldfail] so they register + run + are visibly pending without breaking the suite

key-files:
  created:
    - src/core/include/ajazz/core/active_window_watcher.hpp
    - src/core/src/active_window_watcher_stub.cpp
    - src/app/wayland/wlr-foreign-toplevel-management-unstable-v1.xml
    - src/app/src/active_window_watcher_wayland.cpp
    - src/app/src/active_window_watcher_x11.cpp
    - src/app/src/active_window_watcher_win.cpp
    - src/app/src/active_window_watcher_mac.mm
    - tests/unit/test_active_window_watcher.cpp
    - tests/unit/test_app_profile_switch.cpp
    - tests/unit/test_app_lifecycle_events.cpp
  modified:
    - src/core/CMakeLists.txt
    - src/app/CMakeLists.txt
    - src/app/src/application.hpp
    - src/app/src/application.cpp
    - src/app/src/debug_control_facade.cpp
    - tests/unit/CMakeLists.txt
    - .github/workflows/ci.yml
    - packaging/flatpak/io.github.Aiacos.AjazzControlCenter.yml
    - .pre-commit-config.yaml

key-decisions:
  - IActiveWindowWatcher kept Qt-free in ajazz_core via a std::function callback seam (RESEARCH A1 chosen), so COD-031 holds and all Qt/Wayland/X11 lives in app-tier per-OS TUs
  - StubActiveWindowWatcher declared in the public header (NOT anonymous like the input-synth stub) because the debug facade + unit tests need its concrete injectForeground/log seam
  - Qt6::WaylandClient find_package + codegen + libX11 link are Linux-gated (qtwayland is not built on macOS/Windows; REQUIRED would break those configures)
  - Generated qwayland-*.cpp compiled with -w (it trips -Werror old-style-cast + sign-conversion; generated code we do not own); rest of app stays strict
  - RED scaffolds use Catch2 [!shouldfail] (registered + run + fail-as-expected) rather than FAIL(), keeping ctest green while making Plan-04 work visible

patterns-established:
  - Per-OS watcher TUs added to the app source list unconditionally + self-#if-guarded behind AJAZZ_FEATURE_ACTIVE_WINDOW (stable build-list shape; bodies land Plan 03)
  - Test-case titles prefixed with the feature token (active_window / app_profile_switch / app_lifecycle_events) so ctest -R matches by name (project idiom from test_action_instance)

requirements-completed: [APROF-01]

# Metrics
duration: ~55min
completed: 2026-06-08
---

# Phase 34 Plan 01: APROF Foundation (Watcher Interface + Build Env + Debug Seam + RED Scaffolds) Summary

**Qt-free IActiveWindowWatcher interface + recording stub + vendored wlr-foreign-toplevel XML with qtwaylandscanner/libX11 build wiring, a window.setForeground synthetic-injection debug RPC, and three registered Catch2 scaffolds (active_window GREEN; app_profile_switch + app_lifecycle_events RED for Plan 04).**

## Performance

- **Duration:** ~55 min
- **Started:** 2026-06-08T13:40Z (approx)
- **Completed:** 2026-06-08
- **Tasks:** 3
- **Files modified/created:** 19 (10 created, 9 modified)

## Accomplishments

- Defined the Phase-34 contract: `IActiveWindowWatcher` + `ActiveWindowInfo` + `makeDefaultActiveWindowWatcher()`, Qt-free and nlohmann-free in `ajazz_core` (COD-031 holds), mirroring `input_synthesizer.hpp` exactly.
- `StubActiveWindowWatcher` records injected foreground changes, exposes the `injectForeground()` Wave-0 seam and a settable `capabilityAvailable()` (APROF-03 degradation), and is the default factory result.
- Vendored the canonical wlr-foreign-toplevel XML (rev-pinned) and wired `qt_generate_wayland_protocol_client_sources` + `Qt6::WaylandClient` + `libX11` (Linux-gated), plus CI (`qtwayland`, `libwayland-dev`, `wayland-protocols`, `libx11-dev`) and a Flatpak build-env note. `cmake --preset linux-release` configures + builds clean.
- Added the `window.setForeground` debug RPC (mirrors `input.*`) and the `Application::activeWindowWatcher()` accessor that backs it.
- Three test files registered in ctest: `active_window` 10/10 green against the stub; `app_profile_switch` + `app_lifecycle_events` registered + failing-as-expected ([!shouldfail]) pending Plan 04. Full suite 760/760 green.

## Task Commits

1. **Task 1: Vendor wlr XML + Qt-free watcher interface + recording stub + factory** - `0f378e4` (feat)
1. **Task 2: CMake codegen + link wiring + CI/Flatpak build-env deps** - `080b83a` (build)
1. **Task 3: window.setForeground RPC + RED Catch2 scaffolds** - `2e16b82` (feat, TDD: active_window GREEN, switch/lifecycle RED)

**Plan metadata:** (this commit) `docs(34-01): complete APROF foundation plan`

## Files Created/Modified

- `src/core/include/ajazz/core/active_window_watcher.hpp` - Qt-free/nlohmann-free interface + value struct + public stub decl + factory.
- `src/core/src/active_window_watcher_stub.cpp` - recording stub + factory definition (platform forward-decl behind AJAZZ_FEATURE_ACTIVE_WINDOW).
- `src/app/wayland/wlr-foreign-toplevel-management-unstable-v1.xml` - vendored protocol, rev b010a03 pinned in a leading comment.
- `src/app/src/active_window_watcher_{wayland,x11,win}.cpp` + `_mac.mm` - self-#if-guarded placeholder per-OS TUs (bodies land Plan 03).
- `src/core/CMakeLists.txt` / `src/app/CMakeLists.txt` - stub TU unconditional; Wayland codegen + WaylandClient/libX11 (Linux), AppKit (mac), user32 (win).
- `src/app/src/application.{hpp,cpp}` - own + construct the watcher; `activeWindowWatcher()` accessor.
- `src/app/src/debug_control_facade.cpp` - `window.setForeground` RPC.
- `tests/unit/test_active_window_watcher.cpp` (GREEN) / `test_app_profile_switch.cpp` + `test_app_lifecycle_events.cpp` (RED) / `tests/unit/CMakeLists.txt` registration.
- `.github/workflows/ci.yml` - apt + Qt module deps.
- `packaging/flatpak/...yml` - build-env documentation.
- `.pre-commit-config.yaml` - exclude `.mm` from clang-format (see Deviations).

## Decisions Made

See `key-decisions` frontmatter. Most load-bearing: the interface stays Qt-free in core (std::function seam, RESEARCH A1) so COD-031 is preserved; the WaylandClient/codegen is Linux-gated so Windows/macOS configures don't break.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] Generated qtwaylandscanner TU trips project -Werror**

- **Found during:** Task 2 (CMake codegen wiring)
- **Issue:** The qtwaylandscanner-generated `qwayland-*.cpp` uses C-style casts and int->uint32 conversions that fail `-Werror=old-style-cast` / `-Werror=sign-conversion` from `ajazz::warnings`, breaking the app build.
- **Fix:** `set_source_files_properties(... COMPILE_OPTIONS "-w")` on the generated TU only (it is generated code we do not own); the rest of the app stays strict.
- **Files modified:** src/app/CMakeLists.txt
- **Verification:** `cmake --build --preset linux-release --target ajazz-control-center` OK.
- **Committed in:** `080b83a`

**2. [Rule 3 - Blocking] Qt6::WaylandClient REQUIRED would break Windows/macOS configure**

- **Found during:** Task 2
- **Issue:** The plan specified `find_package(Qt6 REQUIRED COMPONENTS WaylandClient)`, but qtwayland is not built on macOS/Windows, so REQUIRED fails at configure on those runners.
- **Fix:** Gated the WaylandClient find_package + codegen + libX11 link under `if(CMAKE_SYSTEM_NAME STREQUAL "Linux")`. The wayland/x11 backend TUs are already `#if __linux__`-gated and the factory picks the backend at runtime on Linux only.
- **Files modified:** src/app/CMakeLists.txt
- **Verification:** linux-release configures + builds; the Windows/macOS branches link AppKit/user32 only.
- **Committed in:** `080b83a`

**3. [Rule 3 - Blocking] clang-format pre-commit hook fails on the .mm Objective-C++ TU**

- **Found during:** Task 2 commit
- **Issue:** `active_window_watcher_mac.mm` is the first Objective-C++ file in the repo; pre-commit classifies `.mm` as objective-c++ and feeds it to clang-format, which exits 1 ("Configuration file(s) do(es) not support Objective-C") because `.clang-format` has no ObjC language block. The hook itself is broken for `.mm`.
- **Fix:** Added `exclude: '\.mm$'` to the clang-format hook in `.pre-commit-config.yaml` (config fix, NOT `--no-verify`). The `.mm` placeholder is hand-formatted to style.
- **Files modified:** .pre-commit-config.yaml
- **Verification:** Commits pass the hook; the `.mm` builds (placeholder, self-#if-guarded).
- **Committed in:** `080b83a`

______________________________________________________________________

**Total deviations:** 3 auto-fixed (all Rule 3 - blocking build/tooling). No scope creep — each was required to keep the build/commit green under the project's strict cross-platform `-Werror` + pre-commit constraints.

## Issues Encountered

- `ctest -R <token>` matches the Catch2 **test-case title**, not the tag or filename. Initial titles did not contain the feature token so the verify commands found "No tests". Resolved by prefixing each title with `active_window` / `active_window_debounce` / `app_profile_switch` / `app_lifecycle_events` (the project idiom seen in `test_action_instance.cpp`).
- Test files were named `test_*` (project convention) rather than the plan's `*_test.cpp` filenames — intentional consistency with the existing 100+ `test_*.cpp` suite; CMake registration matches.

## Known Stubs

This plan is the interface-first / Wave-0 foundation; the following are intentional, tracked stubs resolved by downstream APROF plans:

| Stub                                                              | File                                                               | Reason / resolved by                                                                                                                              |
| ----------------------------------------------------------------- | ------------------------------------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------- |
| Per-OS watcher backends are empty placeholders (self-#if-guarded) | src/app/src/active_window_watcher\_{wayland,x11,win}.cpp, \_mac.mm | Real Wayland/X11/Win/macOS bodies land in **Plan 03** (AJAZZ_FEATURE_ACTIVE_WINDOW gate). Build stays green; factory returns the stub until then. |
| app_profile_switch matcher                                        | tests/unit/test_app_profile_switch.cpp ([!shouldfail])             | applicationHints match + default fallback wired in **Plan 04**.                                                                                   |
| app_lifecycle_events fan-out                                      | tests/unit/test_app_lifecycle_events.cpp ([!shouldfail])           | applicationDidLaunch/Terminate registered-plugin delivery wired in **Plan 04**.                                                                   |

No stub blocks this plan's goal (defining contracts + build env + seams + RED tests). All are documented and assigned to a future plan.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- The watcher contract, build env (Wayland codegen + WaylandClient/libX11 + CI/Flatpak deps), the synthetic-injection debug seam, and the RED tests are all in place for Plan 03 (per-OS backends) and Plan 04 (auto-switch matcher + lifecycle fan-out).
- Plan 03 must: fill the four per-OS TU bodies, add the `AJAZZ_FEATURE_ACTIVE_WINDOW` CMake option, and make `makeDefaultActiveWindowWatcher()` pick wayland-vs-x11 at runtime by session type (RESEARCH Pitfall 6). Construct on the GUI thread after QGuiApplication (Pitfall 5).
- Plan 04 must: turn `test_app_profile_switch` + `test_app_lifecycle_events` GREEN (remove [!shouldfail]) by wiring the applicationHints matcher -> ProfileController::activateDeviceProfile -> profileChanged reconcile, and the registered-plugin lifecycle fan-out via SdPluginServer::sendEvent.

## Self-Check: PASSED

All 11 created files exist on disk; all 3 task commits (0f378e4, 080b83a, 2e16b82) are present in git history.

______________________________________________________________________

*Phase: 34-per-app-profiles-event-parity-audit*
*Completed: 2026-06-08*
