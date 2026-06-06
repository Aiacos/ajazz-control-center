---
phase: 15-stream-dock-input-routing
plan: 02
subsystem: app-layer input dispatch wiring
tags: [stream-dock, input, action-engine, qt-executor, hotplug, application-wiring]

# Dependency graph
requires:
  - phase: 15-stream-dock-input-routing/15-01
    provides: StreamDockInputService (ProfileAccessor + ActionEngine injection seams, setActiveDevice, pageNavRequested signal)
  - phase: 14-stream-dock-control-service/14-02
    provides: StreamDockControlService held shared_ptr<IDevice> + DeviceRegistry flyweight + ProfileController::activeProfile()
  - phase: earlier phases
    provides: >
      core::ActionEngine + ActionExecutors + QtExecutor,
      DeviceRegistry flyweight open() (ARCH-03 single-handle),
      HotplugMonitor + HotplugDebouncer (300ms debounce pattern)
provides:
  - Application owns first QtExecutor + first ActionEngine (four executor lambdas: keyPress/runCommand/openUrl/plugin)
  - StreamDockInputService live in the running app (INPUT-03/04/05 end-to-end wired)
  - On Stream Deck arrival: input service drives the Phase-14 held handle (ARCH-03 flyweight, no second open())
  - On Stream Deck removal: input service poll pump stopped cleanly (T-15-05 UAF mitigation)
  - pageNavRequested logged sink (Phase 16 replaces with real page-model consumer)
  - qt_executor.cpp/.hpp added to ACC_QML_MODULE_SOURCES (QML smoke test linker fix)
affects:
  - Phase 16 (controls/persistence): replaces pageNavRequested logged sink with real page-model consumer
  - Phase 19 (plugin bridge): adds plugin bridge as second EventBus consumer; replaces plugin executor stub
  - Phase 25 (hardware smoke): verifies INPUT-03/04/05 end-to-end on real AKP05E

# Tech tracking
tech-stack:
  added: []
  patterns:
    - QtExecutor as shared_ptr<Executor> via non-owning shared_ptr alias (lambda deleter)
    - ARCH-03 flyweight share: DeviceRegistry::open() called twice with same (vid,pid) returns same backend shared_ptr
    - Minimal/defensive QJsonDocument parse for runCommand (Phase 20 owns full PI schema)
    - Stream Deck departure -> setActiveDevice(nullptr) pattern (stops pump, releases handle)

key-files:
  created: []
  modified:
    - src/app/src/application.hpp
    - src/app/src/application.cpp
    - src/app/CMakeLists.txt

key-decisions:
  - 'Handle sharing via ARCH-03 flyweight: after m_streamDockControl->setActiveDevice(codename), call m_deviceRegistry.open(same devId) to obtain the shared handle; the flyweight returns the same shared_ptr<IDevice> backend the control service holds -- no second HID open, no second handle'
  - 'QtExecutor shared_ptr wrapping: ActionEngine ctor requires shared_ptr<Executor>; m_qtExecutor is unique_ptr<QtExecutor> (owned by Application for lifetime safety); wrapped as a non-owning shared_ptr with a no-op deleter so Application remains the sole owner (qt_executor.hpp lifetime note)'
  - 'm_actionEngine moved into StreamDockInputService: the service takes unique_ptr<ActionEngine>; Application constructs the engine in m_actionEngine then moves it into the service ctor. After the move, m_actionEngine is null -- Phase 19 accesses the engine via m_streamDockInput. This is the designed ownership model (15-01 SUMMARY: service owns the engine)'
  - 'Departure wiring added: onHotplug currently only handled Arrived; added Removed branch for StreamDeck family -> m_streamDockInput->setActiveDevice(nullptr) to stop the poll pump and prevent UAF on the stale handle (T-15-05 mitigation -- Rule 2 auto-add)'
  - 'profileChanged -> ActionEngine::setProfile: NOT connected in Phase 15. The engine nav stack resets to root only when a profile is explicitly loaded (profileChanged fires); in Phase 15 no profile persistence is wired (in-memory default only), and the ProfileAccessor lambda always reads the fresh profile directly for dispatch. Phase 16 may add this connection when persistence lands; the deferred decision is documented in application.cpp comments'
  - 'runCommand minimal parse: QJsonDocument::fromJson with "program" key extraction and optional "args" array; defensive fallback to log + return on malformed input. Phase 20 PI schema owns the full parse shape; Phase 15 needs runCommand usable for simple bindings now (T-15-02 mitigated)'
  - 'qt_executor.cpp/.hpp added to ACC_QML_MODULE_SOURCES: the QML smoke tests (tests/qml/ CMakeLists.txt) recompile application.cpp directly; since application.cpp now uses QtExecutor, qt_executor.cpp must be in the module sources so the test target links it. Rule 3 auto-fix.'

requirements-completed: [INPUT-03, INPUT-04, INPUT-05]

# Metrics
duration: ~8 min
completed: 2026-05-24
---

# Phase 15 Plan 02: Application Wiring -- StreamDockInputService Summary

**Application-level wiring that makes physical AKP05E key/encoder/touch events fire bound action chains live in the running app: one QtExecutor-backed ActionEngine with real runCommand/openUrl executors (keyPress/plugin stubs for Phases 21/19), sharing the Phase-14 held device handle via the ARCH-03 flyweight on hot-plug arrival.**

## Performance

- **Duration:** ~8 min
- **Started:** 2026-05-24T09:44:46Z
- **Completed:** 2026-05-24T09:52:51Z
- **Tasks:** 1/1 completed
- **Files modified:** 3 (2 modified source + 1 CMakeLists)

## Accomplishments

- First `core::ActionEngine` instantiation in the running app: constructed with
  `QProcess::startDetached` (runCommand), `QDesktopServices::openUrl`, keyPress STUB
  (Phase 21 / T-15-03 accepted), plugin STUB (Phase 19 seam / T-15-03 accepted), and
  `m_qtExecutor` as the non-blocking Executor (T-15-04 mitigated -- Sleep defers via
  QTimer::singleShot, never blocks GUI/poll thread)
- ARCH-03 flyweight handle share: on Stream Deck arrival (300ms debounce), control
  service resolves/lights the panel via `setActiveDevice(codename)`, then input service
  receives the SAME `shared_ptr<IDevice>` via `m_deviceRegistry.open(same devId)` --
  one HID session shared between both services, no second open()
- Departure safety: added `HotplugAction::Removed` handler for StreamDeck family --
  `m_streamDockInput->setActiveDevice(nullptr)` stops the poll pump and releases the
  held handle before the backend closes the device (T-15-05 UAF mitigation)
- `pageNavRequested` connected to a logged sink (Phase 16 page-model consumer)
- Rule 3 auto-fix: `qt_executor.cpp/.hpp` added to `ACC_QML_MODULE_SOURCES` so the
  QML smoke test target (which recompiles `application.cpp` directly) can link `QtExecutor`
- 423/423 full ctest suite green

## Task Commits

1. **Task 1: Wire StreamDockInputService + ActionEngine + QtExecutor into Application** - `fb955b6` (feat)

## Files Created/Modified

- `src/app/src/application.hpp` - Added `m_qtExecutor`, `m_actionEngine`, `m_streamDockInput`
  members in -Wreorder-clean order (after m_streamDockControl, before m_hotplug); added
  `#include "qt_executor.hpp"`, `"stream_dock_input_service.hpp"`, `ajazz/core/action_engine.hpp`
- `src/app/src/application.cpp` - ActionEngine ctor with four executor lambdas in init-list;
  `m_qtExecutor` as non-owning `shared_ptr<Executor>` wrapper; `m_streamDockInput` with
  ProfileAccessor + moved engine; arrival/departure wiring in `onHotplug`; `pageNavRequested`
  logged sink connect; added Qt includes (QProcess, QDesktopServices, QJsonDocument, QJsonObject,
  QJsonArray, QUrl, QStringList)
- `src/app/CMakeLists.txt` - Added `src/qt_executor.cpp` and `src/qt_executor.hpp` to
  `ACC_QML_MODULE_SOURCES` so `tests/qml` target can link QtExecutor (Rule 3 auto-fix)

## Key Design Decisions

### Handle Sharing (ARCH-03 Flyweight)

The plan offered two options: (a) expose a `heldDevice()` getter on `StreamDockControlService`,
or (b) re-resolve via the shared `DeviceLookup`. Phase 14's `StreamDockControlService` did NOT
add a `heldDevice()` getter (confirmed from 14-02 SUMMARY and the header). Chose option (b):
after `m_streamDockControl->setActiveDevice(codename)` runs inside the debounce timer, call
`m_deviceRegistry.open(devId)` with the same `(vid, pid, serial={})` key. The `DeviceRegistry`
flyweight guarantees this returns the exact same `shared_ptr<IDevice>` backend the control
service holds. Both services share one HID session, zero extra opens.

### QtExecutor Ownership and shared_ptr Wrapping

`ActionEngine` ctor takes `shared_ptr<Executor>` for the continuation scheduler. `m_qtExecutor`
is a `unique_ptr<QtExecutor>` owned by Application (per qt_executor.hpp lifetime note: must
outlive the engine). Wrapping it as a non-owning `shared_ptr` with a no-op deleter
(`shared_ptr<core::Executor>(m_qtExecutor.get(), [](core::Executor*) {})`) satisfies the ctor
without transferring ownership. Application remains the sole owner and destruction sequence is
preserved.

### m_actionEngine Moved into Service

`StreamDockInputService` ctor takes `std::unique_ptr<core::ActionEngine>`. Application
constructs the engine in `m_actionEngine`, then `std::move(m_actionEngine)` transfers ownership
to the service. After construction, `m_actionEngine` is null. This is the deliberate design from
15-01: "the service owns the engine". Phase 19 accesses the engine through `m_streamDockInput`
(not through the now-null `m_actionEngine` member). The member is declared in application.hpp
for documentation and -Wreorder safety during construction.

### Departure Handler Added (Rule 2 Auto-add)

The existing `onHotplug` only handled `HotplugAction::Arrived`. On Stream Deck removal, the
Phase-14 control service's handle becomes stale but the input service would continue polling it.
Added a `HotplugAction::Removed` branch: enumerate the registry for the departed (vid, pid), and
if it is a `DeviceFamily::StreamDeck`, call `m_streamDockInput->setActiveDevice(nullptr)`. The
service's `setActiveDevice(nullptr)` stops the QTimer and resets `m_device` (zombie-contract
no-op). This closes T-15-05 (tampering/DoS via stale handle UAF).

### runCommand Minimal/Defensive Parse

The `settingsJson` schema for `RunCommand` is owned by Phase 20 (PI schema). Phase 15 uses a
`QJsonDocument::fromJson` parse extracting `"program"` (required string) and `"args"` (optional
array). Malformed input logs the issue and returns without invoking `QProcess`. This is
intentionally minimal -- Phase 20 will provide the full structured parse. `QProcess::startDetached(program, args)` is used exclusively; no `system()` call exists (T-15-02 mitigated).

### profileChanged -> ActionEngine::setProfile: NOT Connected

The engine's `setProfile()` resets the folder-nav stack. In Phase 15 no profile persistence is
wired (profiles exist only in memory at their default state); `profileChanged` effectively never
fires. The `ProfileAccessor` lambda always reads the fresh profile for dispatch. This connection
is deferred to Phase 16 when profile persistence lands; the decision is documented in
application.cpp comments.

## Follow-up Items

| Item                                                             | Phase | Reason deferred                                         |
| ---------------------------------------------------------------- | ----- | ------------------------------------------------------- |
| pageNavRequested -> real page-model consumer                     | 16    | Phase 16 owns the page model                            |
| profileChanged -> ActionEngine::setProfile                       | 16    | Profile persistence not wired until Phase 16            |
| plugin executor -> real plugin host bridge                       | 19    | Phase 19 seam; T-15-03 accepted                         |
| keyPress executor -> uinput/SendInput/CGEvent                    | 21    | Cross-platform OS key injection; T-15-03 accepted       |
| Touch zone / swipe framing hardware reconciliation               | 25    | Provisional formula (Phase 15-01); live AKP05E verify   |
| Full runCommand settingsJson parse (program/args/cwd/env schema) | 20    | PI schema owned by Phase 20                             |
| EventBus migration for multi-subscriber input fan-out            | 19    | One consumer now; add when Phase 19's bridge subscribes |

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 2 - Missing Critical] Added Stream Deck departure -> setActiveDevice(nullptr)**

- **Found during:** Task 1 (onHotplug departure path analysis)
- **Issue:** Existing `onHotplug` only handled `HotplugAction::Arrived`. Without a departure
  handler, the input service's poll timer would continue calling `m_device->poll()` on a
  closed/yanked device handle, risking UAF (T-15-05 mitigated in the threat register but the
  actual departure path was not wired).
- **Fix:** Added `HotplugAction::Removed` branch in `onHotplug` for `DeviceFamily::StreamDeck`
  devices: enumerate registry to match the departed (vid, pid), call
  `m_streamDockInput->setActiveDevice(nullptr)`. The service's null-device path stops the
  timer and resets the held `shared_ptr`.
- **Files modified:** `src/app/src/application.cpp`
- **Committed in:** `fb955b6` (part of task commit)

**2. [Rule 3 - Blocking] Added qt_executor.cpp/.hpp to ACC_QML_MODULE_SOURCES**

- **Found during:** Task 1 build (linker error on `ajazz_qml_tests`)
- **Issue:** `tests/qml/CMakeLists.txt` recompiles `application.cpp` directly. `application.cpp`
  now includes `qt_executor.hpp` and constructs `QtExecutor`; the QML test target previously did
  not compile `qt_executor.cpp`, causing an "undefined reference to QtExecutor::QtExecutor" linker
  error.
- **Fix:** Added `src/qt_executor.cpp` and `src/qt_executor.hpp` to `ACC_QML_MODULE_SOURCES` in
  `src/app/CMakeLists.txt`. The `_qmlt_sources` variable in `tests/qml/CMakeLists.txt` is built
  from this exported list, so the fix propagates automatically.
- **Files modified:** `src/app/CMakeLists.txt`
- **Committed in:** `fb955b6` (part of task commit)

**3. [Formatter] clang-format reformatted on first commit attempt**

- clang-format hook modified staged files; re-staged and committed cleanly on second attempt
  (expected per CLAUDE.md).

______________________________________________________________________

**Total deviations:** 2 functional (1 missing critical, 1 blocking) + 1 formatter\
**Impact on plan:** Rule 2 fix closes T-15-05 properly; Rule 3 fix is a CMake hygiene issue
surfaced by the new dependency. Neither is scope creep.

## Threat Mitigations Applied

| Threat                                 | Mitigation                                                                                   |
| -------------------------------------- | -------------------------------------------------------------------------------------------- |
| T-15-02: runCommand shell injection    | QProcess::startDetached(program, args) -- never system(); QJsonDocument minimal parse        |
| T-15-03: keyPress/plugin OS injection  | keyPress STUBBED with log; plugin logged no-op (Phase 21/19 seams, accepted)                 |
| T-15-04: Sleep blocking poll thread    | QtExecutor injected into ActionEngine; Sleep defers via QTimer::singleShot                   |
| T-15-05: use-after-free on device yank | Shared shared_ptr<IDevice>; setActiveDevice(nullptr) on departure; stop timer on null device |
| T-15-SC: no package installs           | Pure in-tree C++/Qt wiring; Package Legitimacy Gate N/A                                      |

## Known Stubs

| Stub                             | File                        | Reason                                                                                                                                                 |
| -------------------------------- | --------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------ |
| `keyPress` executor logs + no-op | src/app/src/application.cpp | OS key injection is cross-platform Phase-21 scope (uinput/SendInput/CGEvent; PLUGIN-12 system.hotkey; T-15-03 accepted)                                |
| `plugin` executor logs + no-op   | src/app/src/application.cpp | Phase 19 plugin bridge seam; T-15-03 accepted; does NOT prevent INPUT-03/04/05 (dispatch path is fully wired, stubs only affect these two ActionKinds) |

Note: stubs do NOT prevent the plan's goal. The dispatch path (ProfileAccessor ->
ActionEngine -> executors) is fully wired for all DeviceEvent kinds. Stubs only
affect two ActionKind cases (KeyPress, Plugin) that are explicitly deferred.

## Self-Check: PASSED

Files confirmed present and modified:

- `src/app/src/application.hpp` - FOUND (contains m_qtExecutor, m_actionEngine, m_streamDockInput)
- `src/app/src/application.cpp` - FOUND (contains StreamDockInputService, ActionEngine, startDetached, pageNavRequested)
- `src/app/CMakeLists.txt` - FOUND (contains qt_executor.cpp in ACC_QML_MODULE_SOURCES)

Commits confirmed:

- `fb955b6` (Task 1) - FOUND

Grep checks:

- `grep -n 'm_qtExecutor' application.cpp` - PRESENT
- `grep -n 'core::ActionEngine\|m_actionEngine' application.cpp` - PRESENT (ONE engine)
- `grep -n 'startDetached' application.cpp` - PRESENT; `grep -n 'system(' application.cpp` - no shell launch
- `grep -n 'StreamDockInputService\|m_streamDockInput' application.cpp` - PRESENT
- `grep -n 'pageNavRequested' application.cpp` - PRESENT (connect + logged sink)
- `git diff --stat -- akp05.cpp akp05_protocol.hpp` - EMPTY (untouched)
- `grep -rn nlohmann src/core/include/` - 0 hits (COD-031 intact)
- ctest --preset linux-release: 423/423 PASSED (incl. INPUT-03/04a/04b/04c/05a/05b)
