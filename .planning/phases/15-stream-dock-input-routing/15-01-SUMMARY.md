---
phase: 15-stream-dock-input-routing
plan: 01
subsystem: app-layer input dispatch
tags: [stream-dock, input, action-engine, qt-timer, coalescer, encoder, touch-strip, tdd]

# Dependency graph
requires:
  - phase: 14-stream-dock-control-service/14-02
    provides: StreamDockControlService held shared_ptr<IDevice> + ProfileController::activeProfile()
  - phase: earlier phases
    provides: >
      Akp05Device backend (poll/onEvent/parseInputReport),
      core::ActionEngine + ActionExecutors + QtExecutor,
      core::Profile/Binding/EncoderBinding,
      MockTransport + makeAkp05WithTransport DI seam
provides:
  - StreamDockInputService: app-layer device input dispatch (INPUT-03/04/05)
  - First core::ActionEngine instantiation in the app
  - 16 ms encoder rotation coalescer (Pattern 3)
  - Encoder press -> synthetic release pairing (Pattern 4)
  - Provisional touch-strip zone map zoneForX (X*4/640, PROVISIONAL akp05.md para5)
  - pageNavRequested(int) signal for Phase 16 page model
  - encoderReleaseSynthesised(uint16_t) signal (observable release synthesis)
affects:
  - Phase 15-02: Application wiring (real executors + shared Phase-14 handle)
  - Phase 16 (controls/persistence): reuses pageNavRequested signal + setActiveDevice
  - Phase 19 (plugin bridge): reuses dispatch path; adds second EventBus consumer
  - Phase 25 (hardware smoke): verifies INPUT-03/04/05 end-to-end on real AKP05E

# Tech tracking
tech-stack:
  added: []
  patterns:
    - ProfileAccessor lambda seam (std::function<Profile const&()>): mirrors StreamDockControlService
    - ActionEngine owned via unique_ptr (injected by Application / spy-replaced by tests)
    - QTimer poll pump (8 ms Qt::PreciseTimer): tight cadence on GUI thread, no I/O thread
    - 16 ms single-shot rotation coalescer (QTimer Qt::CoarseTimer): accumulate delta, one dispatch/window
    - Encoder press->synthetic-release via encoderReleaseSynthesised Q_SIGNAL: no wire release needed
    - Held shared_ptr<IDevice> session handle (ARCH-03): set once by setActiveDevice, never re-opens

key-files:
  created:
    - src/app/src/stream_dock_input_service.hpp
    - src/app/src/stream_dock_input_service.cpp
    - tests/unit/test_stream_dock_input_service.cpp
  modified:
    - src/app/CMakeLists.txt
    - tests/unit/CMakeLists.txt

key-decisions:
  - 'Headless QObject for Phase 15: no QML_SINGLETON / qmlRegisterSingletonInstance — service is app-layer C++ only; QML/UI wiring is Phase 16 scope'
  - 'ProfileAccessor seam: std::function<Profile const&()> lambda (matches StreamDockControlService precedent); Application wires m_profileController->activeProfile(); tests inject fake lambda'
  - 'ActionEngine injected via unique_ptr: Application constructs engine with real executors + QtExecutor; tests inject spy engines; service owns the engine'
  - 'Poll cadence 8 ms Qt::PreciseTimer: poll() drains <=8 reports/cycle -> ~1000 reports/s ceiling; suffices for >100 Hz encoders (event_bus.hpp:33); revisit with dedicated thread only on measured Phase-25 hardware stall'
  - 'GUI-thread dispatch (no dedicated I/O thread): all dispatch runs on GUI thread via QTimer pump; avoids cross-thread shared_ptr hazards (device.hpp:141 thread-affine note); audit A2 satisfied by QtExecutor injection (Sleep defers, never blocks GUI)'
  - 'Rotation coalescer: accumulates signed delta per encoder; fires onCw/onCcw once per 16 ms window (Open Question 3 resolved to fire-once-per-frame); accumulated magnitude preserved in m_encAccum for potential per-detent semantics in a later phase'
  - 'Encoder release synthesis: synthesised immediately after onPress chain (EncoderBinding has no onRelease field as of profile.hpp:113-118); observable via encoderReleaseSynthesised signal'
  - 'keyPress executor: STUBBED with a log line (Phase 21 scope -- OS key injection is platform- specific uinput/SendInput/CGEvent, deferred per Open Question 4 / T-15-03 accepted)'
  - 'plugin executor: logged no-op stub (Phase 19 seam, T-15-03 accepted)'
  - runCommand uses QProcess::startDetached(program, args) -- never system() (T-15-02 mitigated)
  - openUrl uses QDesktopServices::openUrl (standard Qt cross-platform -- T-15-02 N/A for URLs)
  - 'EventBus deferred: Phase 15 has one consumer; direct onEvent callback used; migrate to EventBus when Phase 19 adds plugin bridge as a second subscriber (Open Question 2 resolved)'

requirements-completed: [INPUT-03, INPUT-04, INPUT-05]

# Metrics
duration: ~7 min
completed: 2026-05-24
---

# Phase 15 Plan 01: StreamDockInputService Summary

**App-layer input dispatch service that instantiates core::ActionEngine for the first time in the app -- turns physical AKP05E key/encoder/touch events into executed action chains via MockTransport-fed test coverage (hardware-free).**

## Performance

- **Duration:** ~7 min
- **Started:** 2026-05-24T09:33:25Z
- **Completed:** 2026-05-24T09:40:30Z
- **Tasks:** 1/1 completed
- **Files modified:** 5 (3 new, 2 modified)

## Accomplishments

- Closed the Phase-10/15 dispatch gap: `grep poll()/onEvent src/app/` now returns hits
  in `stream_dock_input_service.cpp` (was 0 before this plan)
- Built StreamDockInputService as the first instantiation of core::ActionEngine in the app:
  holds one `shared_ptr<IDevice>` (ARCH-03 flyweight), pumps poll() on a tight 8 ms
  Qt::PreciseTimer, dispatches DeviceEvents to Profile binding chains via one ActionEngine
  with the app's ActionExecutors and injected QtExecutor
- 16 ms rotation coalescer: 5 rapid encoder ticks coalesce to ONE onCw/onCcw dispatch per
  16 ms window; accumulated magnitude preserved for future per-detent semantics
- Encoder press->synthetic-release: observable via `encoderReleaseSynthesised(uint16_t)` signal;
  no wire release frame needed (device is press-only per akp05.md:76)
- Provisional touch-strip zone map: static `zoneForX(x) = min(x*4/640, 3)` labelled
  PROVISIONAL / Phase 25 throughout; swipe emits `pageNavRequested(-1/+1)`
- 9 MockTransport-fed TEST_CASEs cover INPUT-03/04a/04b/04c/05a/05b + Sleep-non-block +
  held-handle; 423/423 full suite green

## Task Commits

1. **Task 1: Implement StreamDockInputService + MockTransport dispatch test** - `c62f3b5` (feat)

## Files Created/Modified

- `src/app/src/stream_dock_input_service.hpp` - QObject with ProfileAccessor/ActionEngine
  injection ctors, setActiveDevice/pump/zoneForX public API, pageNavRequested +
  encoderReleaseSynthesised signals, 8 ms poll + 16 ms coalescer timer members
- `src/app/src/stream_dock_input_service.cpp` - Full implementation: poll pump, dispatch
  switch (KeyPressed/Released -> onPress/onRelease; EncoderTurned -> coalescer;
  EncoderPressed -> onPress + synth release; TouchStrip -> zone tap or swipe signal),
  drainCoalescedRotation, synthesiseEncoderRelease; real runCommand/openUrl executors;
  keyPress/plugin logged stubs (Phase 21/19 seams)
- `tests/unit/test_stream_dock_input_service.cpp` - 9 Catch2 TEST_CASEs tagged
  [stream-dock-input], ASCII-only titles, MockTransport frame builders + spy executors +
  QSignalSpy + pumpUntil event-loop helper
- `src/app/CMakeLists.txt` - Added service to qt_add_executable source list AND
  ACC_QML_MODULE_SOURCES (AUTOMOC)
- `tests/unit/CMakeLists.txt` - Added test + service sources (qt_executor.cpp already present)

## Key Design Decisions

### Injection Seams

**ProfileAccessor:** `std::function<core::Profile const&()>` lambda (mirrors
StreamDockControlService precedent). Application wires `[this]{ return m_profileController->activeProfile(); }`. Tests inject a lambda returning a stack-allocated
fake Profile. This keeps the service testable without a real ProfileController.

**ActionEngine:** `std::unique_ptr<core::ActionEngine>` injected by the caller. Application
constructs the engine with real executors + its owned QtExecutor. Tests inject spy engines.
The service owns the engine's lifetime.

### Poll Cadence (8 ms Qt::PreciseTimer)

`poll()` drains \<=8 reports/cycle (akp05.cpp:474). At 8 ms cadence the effective ceiling
is ~1000 reports/s, which covers the >100 Hz encoder spec cited in event_bus.hpp:33.
`Qt::PreciseTimer` minimises cadence drift vs `Qt::CoarseTimer`'s +/-5% slack.
No dedicated reader thread added -- all dispatch on the GUI thread avoids cross-thread
`shared_ptr<IDevice>` hazards (device.hpp:141 thread-affine note). Revisit with a
dedicated thread only if Phase-25 hardware stall is measured.

### QML Exposure Decision

Phase 15 dispatch is headless C++ -- no `QML_SINGLETON` / `qmlRegisterSingletonInstance`.
`ACC_QML_MODULE_SOURCES` lists the sources for AUTOMOC (Q_OBJECT in the header), but the
class is not QML-named or QML-exposed. UI wiring is Phase 16.

### Observable Encoder Release Synthesis

The `encoderReleaseSynthesised(uint16_t)` Q_SIGNAL makes the host-side synthesis
observable for tests (asserts release fired WITHOUT a wire release frame). `EncoderBinding`
has no `onRelease` field as of `profile.hpp:113-118`; if Phase 16 adds one, the synthesis
handler in `synthesiseEncoderRelease()` is the correct insertion point.

### Coalescer Semantics (Open Question 3 resolved)

One `onCw`/`onCcw` dispatch per 16 ms window, regardless of accumulated tick count.
Matches the INPUT-04c verification contract (5 ticks -> ONE dispatch). The raw accumulated
magnitude is preserved in `m_encAccum[e]` and can be read before zeroing if a future
phase wants per-detent chain semantics (pass magnitude into chain context).

### Provisional Touch Zone Formula

`zoneForX(x) = min(x * EncoderCount / TouchStripRangeX, EncoderCount-1)` where
`EncoderCount=4`, `TouchStripRangeX=640`. Named helper in one place; commented
`PROVISIONAL (akp05.md para5) -- hardware-reconciled in Phase 25` throughout.
Both the helper and the dispatch call site carry the PROVISIONAL marker.

### Executor Shape for Phase 15-02

The service does NOT include Application-specific executor construction -- that is
Plan 15-02's scope (wiring the real QtExecutor-backed engine + sharing the Phase-14
held handle). For Phase 15-01, tests inject spy engines; the header documents the
intended executor shape.

## Follow-up Items (Not Phase 15-01 Scope)

| Item                                                         | Phase | Reason deferred                              |
| ------------------------------------------------------------ | ----- | -------------------------------------------- |
| Application wiring (real executors + shared Phase-14 handle) | 15-02 | Deliberately out of scope per plan           |
| QML/UI exposure (active-device selector, bindings UI)        | 16    | Phase 16 owns binding persistence + pages    |
| OS key injection backend (uinput/SendInput/CGEvent)          | 21    | Cross-platform; PLUGIN-12 system.hotkey      |
| Plugin action execution via plugin host                      | 19    | Phase 19 seam; plugin stub for now           |
| EventBus migration for multi-subscriber fan-out              | 19    | One consumer now; add when Phase 19 needs it |
| Touch zone / swipe framing hardware reconciliation           | 25    | Provisional formula; live AKP05E verify      |
| Long-press encoder (no onRelease on EncoderBinding yet)      | 16    | EncoderBinding.onRelease not added yet       |

## Threat Mitigations Applied

| Threat                                 | Mitigation                                                                           |
| -------------------------------------- | ------------------------------------------------------------------------------------ |
| T-15-01: malformed input frame         | Reused parseInputReport guards (backend range-check/clamp); zoneForX bounded formula |
| T-15-02: runCommand shell injection    | QProcess::startDetached(program, args) -- never system() or shell string             |
| T-15-03: keyPress/plugin OS injection  | keyPress STUBBED with log; plugin logged no-op (Phase 21/19 seams)                   |
| T-15-04: Sleep blocking poll thread    | QtExecutor injected into ActionEngine; Sleep defers via QTimer::singleShot           |
| T-15-05: use-after-free on device yank | Held shared_ptr<IDevice>; stop timer on null device; zombie-contract no-op           |
| T-15-06: encoder signal-storm          | 16 ms single-shot coalescer accumulates delta, one dispatch per window               |

## Deviations from Plan

None - plan executed exactly as written. Two auto-fixed compiler warnings during GREEN phase:

1. [Rule 1 - Bug] Fixed `-Werror=conversion` in zoneForX: `std::min` template parameter
   needed explicit `static_cast<std::uint32_t>(kEncoderCount)` to avoid size_t->uint32
   narrowing. Fixed inline.
1. [Rule 1 - Bug] Fixed `-Werror=sign-conversion` in dispatch: `ev.value & 0xFFFFu`
   applied to `std::int32_t` needs an intermediate `static_cast<std::uint32_t>`.
   Fixed inline.
1. [Formatter] clang-format reformatted staged files on first commit attempt; re-staged
   and committed cleanly on second attempt (expected behaviour per CLAUDE.md).

## Known Stubs

| Stub                             | File                                               | Reason                                                                                                                            |
| -------------------------------- | -------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------- |
| `keyPress` executor logs + no-op | stream_dock_input_service.cpp (Phase 15-02 wiring) | OS key injection is cross-platform Phase-21 scope (uinput/SendInput/CGEvent); INPUT-03 gated by spy executors, not real injection |
| `plugin` executor logs + no-op   | stream_dock_input_service.cpp (Phase 15-02 wiring) | Phase 19 plugin bridge seam; T-15-03 accepted                                                                                     |

Note: stubs are in the executor factory, NOT in the dispatch path. The dispatch path
(ProfileAccessor -> ActionEngine -> executors) is fully wired. Plan 15-02 will replace
the stub construction with Application's real executors.

## Self-Check: PASSED

Files confirmed present:

- `src/app/src/stream_dock_input_service.hpp` - FOUND (200 lines)
- `src/app/src/stream_dock_input_service.cpp` - FOUND (266 lines)
- `tests/unit/test_stream_dock_input_service.cpp` - FOUND (485 lines)

Commits confirmed:

- `c62f3b5` (Task 1) - FOUND

ctest StreamDockInput filter: 9/9 passed (tests 199-207).
Full suite: 423/423 green.
