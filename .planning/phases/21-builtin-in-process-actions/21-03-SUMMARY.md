---
phase: 21-builtin-in-process-actions
plan: '03'
subsystem: builtin-actions
tags: [builtin-actions, registry, dispatch, input-synthesis, obs-studio, lunbo, multi-actions, tdd]
dependency_graph:
  requires: [21-01-IInputSynthesizer, 21-02-ObsClient, 15-02-ActionEngine, 16-03-StreamDockControlService]
  provides: [BuiltinActionRegistry, BuiltinActionsService, Application-plugin-short-circuit]
  affects: [application.cpp, stream_dock_input_service.hpp, tests/unit/test_builtin_actions.cpp]
tech_stack:
  added: []
  patterns: [dependency-injection-via-std-function, dispatch-table, per-key-cursor, opt-in-gate]
key_files:
  created:
    - src/core/include/ajazz/core/builtin_action_registry.hpp
    - src/core/src/builtin_action_registry.cpp
    - src/app/src/builtin_actions_service.hpp
    - src/app/src/builtin_actions_service.cpp
  modified:
    - src/core/CMakeLists.txt
    - src/app/CMakeLists.txt
    - src/app/src/application.hpp
    - src/app/src/application.cpp
    - src/app/src/stream_dock_input_service.hpp
    - tests/unit/CMakeLists.txt
    - tests/unit/test_builtin_actions.cpp
decisions:
  - 'profile.rotate deferred: no ProfileController rotation seam available in 21-03; registered as logged no-op, documented for follow-on phase'
  - BuiltinActionRegistry returns false for builtin-prefix UUIDs that are not registered (handles() = prefix check AND map lookup)
  - LunBo bindingId keyed as page+key string; reset on resetLunBoCursors() called from profile-change event
  - 'T-21-hook default OFF: sendChord (output synthesis) is the default path; captureHotkeys only if QSettings toggle explicitly set'
  - 'Test file rewrite: all #include directives moved to top of file to avoid namespace resolution failures with anonymous namespace + late includes'
metrics:
  duration: ~3.5 hours (across context boundary)
  completed: '2026-05-24'
  tasks_completed: 2
  files_modified: 10
---

# Phase 21 Plan 03: BuiltinActionRegistry + BuiltinActionsService (complete dispatch wiring) Summary

Pure-core UUID dispatch table + app-tier QObject wiring all 24 com.hotspot.streamdock.\* built-in actions into the Phase-15 plugin executor with 35 hardware-free tests.

## What Was Built

### Task 1 — core::BuiltinActionRegistry (COD-031-clean)

A pure C++20 dispatch table with no Qt, no nlohmann, no RTTI. Stored in `ajazz_core` behind
the COD-031 boundary.

- `kBuiltinPrefix = "com.hotspot.streamdock."` constexpr sentinel
- `registerAction(uuid, BuiltinHandler)` via `insert_or_assign`
- `handles(id)`: prefix check (`id.starts_with(kBuiltinPrefix)`) AND map lookup
- `dispatch(id, settingsJson)`: calls handler exactly once; clean no-op if not found

12 unit tests cover: prefix constant, handles true/false, dispatch invocation count, verbatim
settings forwarding, cross-fire isolation, empty table, overwrite, COD-031 compliance assertion.

**Commit:** `8b4f792`

### Task 2 — app::BuiltinActionsService + Application wiring

`BuiltinActionsService` (QObject) owns the registry and all Qt/OS-touching handler bodies.
Injected seams (all `std::function`): `BrightnessSink`, `NavigateSink`, `OpenUrlFn`,
`PluginFallback`. Also holds `core::ActionEngine*`, `IInputSynthesizer`, and conditionally
`ObsClient` (behind `AJAZZ_HAVE_WEBSOCKETS`).

**Registered UUIDs and their dispatch targets:**

| UUID suffix            | Target                                                               |
| ---------------------- | -------------------------------------------------------------------- |
| `page.previous`        | NavigateSink(-1)                                                     |
| `page.next`            | NavigateSink(+1)                                                     |
| `page.goto`            | NavigateSink(target from `{"target":N}`)                             |
| `page.indicator`       | Logged no-op (display-only hint)                                     |
| `page.change`          | NavigateSink(+1 CW / -1 CCW)                                         |
| `profile.openchild`    | `engine->pushPage(childId)`                                          |
| `profile.backtoparent` | `engine->popPage()`                                                  |
| `profile.rotate`       | Logged deferred no-op (no rotation seam in 21-03)                    |
| `device.brightness`    | clamp(0..100) → BrightnessSink                                       |
| `system.hotkey`        | parseModifiers + parseKeyToHid → `synth->sendChord` (OUTPUT default) |
| `plain.text`           | `synth->typeText(text)`                                              |
| `system.multimedia`    | parseMediaKey → `synth->sendMediaKey`                                |
| `system.volume`        | parseMediaKey / direction alias → `synth->sendMediaKey`              |
| `browser`              | http/https validation (WR-01) → OpenUrlFn                            |
| `multiactions`         | decodeActionChain → `engine->run` ordered                            |
| `multiactions.LunBo`   | per-key cursor advance → run single step                             |
| `obsstudio`            | lazy connect → ObsClient (AJAZZ_HAVE_WEBSOCKETS gate)                |

**Application.cpp wiring:** `execs.plugin` lambda checks `m_builtinActions->onPluginAction(id, s)`
first; non-builtins forward to Phase-15 logged fallback. Engine pointer retrieved via new
`StreamDockInputService::engine()` accessor (moved-in engine, always non-null after init).

**Anti-features:**

- T-21-hook: `captureHotkeys` OFF by default; `sendChord` output synthesis is the default path
- T-21-obsauth: ObsClient refuses Identify when password unset (inherited from 21-02)
- LunBo: `resetLunBoCursors()` resets all per-key cursors (called on profile change)

**23 app-tier unit tests** cover all dispatch targets using `FakeSynth`, lambda spies for
BrightnessSink/NavigateSink/OpenUrlFn/Fallback, and a `TestHarness` struct that wires
a real `BuiltinActionsService` instance.

**Commit:** `dcd4a47`

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Malformed std::format string in builtin_actions_service.cpp**

- **Found during:** Task 2 compilation
- **Issue:** `AJAZZ_LOG_WARN("builtin", "{}}: malformed settingsJson..."` — extra `}` after
  `{}` caused compile error: "call to consteval function is not a constant expression"
- **Fix:** Removed spurious `}` → `"{}: malformed settingsJson..."`
- **Files modified:** `src/app/src/builtin_actions_service.cpp`
- **Commit:** Included in `dcd4a47`

**2. [Rule 3 - Blocking] Test file #include order caused namespace resolution failures**

- **Found during:** Task 2 test compilation
- **Issue:** Adding `#include` directives in the middle of the test file (after anonymous
  namespace and `using namespace` declarations) caused "core has not been declared",
  "invalid use of incomplete type FakeSynth" errors
- **Fix:** Rewrote test file with ALL includes at top of file, single unified anonymous
  namespace for helpers, all tests in file scope
- **Files modified:** `tests/unit/test_builtin_actions.cpp`
- **Commit:** Included in `dcd4a47`

**3. [Rule 2 - Missing seam] StreamDockInputService lacked engine() accessor**

- **Found during:** Task 2 Application.cpp wiring
- **Issue:** ActionEngine is moved into StreamDockInputService via `std::move(m_actionEngine)`,
  leaving `m_actionEngine` null. BuiltinActionsService needs the engine pointer.
- **Fix:** Added `engine() const noexcept -> core::ActionEngine*` and
  `activeDeviceCodename() const noexcept -> QString` public accessors to
  `StreamDockInputService`
- **Files modified:** `src/app/src/stream_dock_input_service.hpp`
- **Commit:** Included in `dcd4a47`

## Acceptance Criteria Verification

| Criterion                                                   | Result                                            |
| ----------------------------------------------------------- | ------------------------------------------------- |
| `ctest --preset linux-release -R BuiltinAction` 100% passed | 35/35 PASSED                                      |
| `grep -rn nlohmann src/core/include/` == 0 (COD-031)        | 0 violations (only comments)                      |
| Application.cpp short-circuit wired                         | Confirmed at lines 334-335                        |
| Single ActionEngine instance                                | Confirmed (one `make_unique<core::ActionEngine>`) |
| App compiles warning-clean                                  | ninja: no work to do (clean build)                |
| akp05.cpp/akp05_protocol.hpp not touched                    | Confirmed via git diff                            |

## Key Decisions

1. **profile.rotate as deferred no-op**: No `ProfileController` rotation seam exists in the
   injection design for 21-03. Rather than inventing a new seam (Rule 4 architectural), it
   is registered as a logged no-op. A follow-on phase can wire it when ProfileController
   rotation is implemented.

1. **handles() = prefix AND registered**: A builtin-prefix UUID that is NOT registered still
   returns `handles() == false`, forwarding to fallback. This prevents silent swallowing of
   future/unknown UUIDs.

1. **All includes at top of test file**: Late `#include` directives inside anonymous namespace

   - using-namespace context cause C++ namespace resolution failures that are hard to diagnose.
     Convention: test files must have all includes at the top.

1. **T-21-hook default OFF**: Global hotkey capture (`captureHotkeys`) starts disabled.
   `sendChord` (output synthesis to the OS) is always the default path. The opt-in gate
   requires an explicit `QSettings` flag.

## Known Stubs

None — all wired targets call through to real seams or explicit deferred no-ops.

## Threat Flags

None — no new network endpoints, auth paths, or trust boundaries introduced beyond what
21-02 ObsClient already adds (documented in 21-02-SUMMARY.md).

## Self-Check: PASSED

- `src/core/include/ajazz/core/builtin_action_registry.hpp` — EXISTS
- `src/core/src/builtin_action_registry.cpp` — EXISTS
- `src/app/src/builtin_actions_service.hpp` — EXISTS
- `src/app/src/builtin_actions_service.cpp` — EXISTS
- Commit `8b4f792` — FOUND
- Commit `dcd4a47` — FOUND
- `ctest -R BuiltinAction` — 35/35 PASSED
- `grep -rn nlohmann src/core/include/` — 0 violations
