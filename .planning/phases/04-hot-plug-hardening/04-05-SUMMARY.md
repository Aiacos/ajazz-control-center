---
phase: 04-hot-plug-hardening
plan: 05
subsystem: tests
tags: [hotplug, multi-device, integration, catch2, HOTPLUG-06, SC1, SC2, SC4, mechanical-proof]

requires:
  - phase: 04-hot-plug-hardening
    provides: 04-01 (shared_ptr migration — SC1 test would crash before this lands)
  - phase: 04-hot-plug-hardening
    provides: 04-02 (HotplugMonitor::injectEvent + MockHidEnumerator — the two test seams the harness drives)
  - phase: 04-hot-plug-hardening
    provides: 04-03 (HotplugDebouncer — SC4 test exercises it directly)
  - phase: 04-hot-plug-hardening
    provides: 04-04 (DeviceModel diff-driven refresh — SC2 test asserts the one-dataChanged contract)
provides:
  - tests/unit/test_hotplug_harness.cpp — six Catch2 TEST_CASEs proving SC1/SC2/SC4/HOTPLUG-07
  - StubDevice helper (no-op IDevice with m_alive zombie flag)
  - registerTestBackend helper that wraps DeviceRegistry::registerDevice for compact test fixtures
affects:
  - Phase 5 (the same test pattern — MockHidEnumerator + injectEvent — will drive TimeSyncService auto-sync timing tests)
  - HOTPLUG-06 is half-satisfied; the other half (Windows WM_DEVICECHANGE smoke) lands in Plan 04-06

tech-stack:
  added: []
  patterns:
    - 'Catch2 + Qt-event-loop hybrid: qt_app_fixture::qtApp() bootstraps a process-wide QCoreApplication; QSignalSpy::wait(timeout_ms) drives the trailing-edge debouncer timer to fire'
    - 'In-tree mock seam composition: DeviceRegistry{mock.asEnumerator()} + HotplugMonitor::injectEvent + StubDevice gives end-to-end control over arrival/departure/live-presence without any OS event source'
    - 'CMake link-time AJAZZ_TESTING injection: hotplug_monitor.cpp is recompiled into ajazz_unit_tests with the target-wide AJAZZ_TESTING define so injectEvent resolves; production libajazz_core.a does NOT include the shim symbol'

key-files:
  created:
    - tests/unit/test_hotplug_harness.cpp
  modified:
    - tests/unit/CMakeLists.txt

key-decisions:
  - hotplug_monitor.cpp is linked into the test target directly rather than through libajazz_core.a — the production library is built without AJAZZ_TESTING so the injectEvent symbol is absent (Plan 04-02 invariant); the test target recompiles the TU with AJAZZ_TESTING and gets the shim
  - StubDevice has a m_alive `std::atomic<bool>` flag honouring D-06's zombie contract; poll() short-circuits after markGone() so the SC1+D-06 composite test can prove "shared_ptr holder survives Removed, subsequent poll() returns 0 instead of crashing"
  - SC2 test uses `dataChangedSpy.takeFirst()` to assert the role-mask contains `ConnectedRole` specifically (not just that *some* dataChanged fired) — pins the role-granularity contract from Plan 04-04
  - SC4 test uses `QSignalSpy::wait(kDebounceMs + 100)` rather than `QTest::qWait` — wait() returns early as soon as the signal fires, so the test isn't wall-clock-bound at 400ms
  - SC4.2 fires 9 raw events (3 keys × 3 repeats) round-robin to maximise timer overlap; per-key isolation means each key's window restarts only on its own events, so the result is exactly 3 emissions
  - No real-device or real-USB integration test in this plan — the harness is *mechanical proof of the API contracts*; the manual cable-yank verification in Plan 04-04 covers the user-visible end-to-end UX

patterns-established:
  - 'Catch2 multi-device integration harness pattern: per-TEST_CASE stack-allocated DeviceRegistry + DeviceModel + HotplugDebouncer; MockHidEnumerator settable mid-test; injectEvent on HotplugMonitor for hot-plug simulation; QSignalSpy on Q_OBJECT signals for assertion'

requirements-completed: []  # HOTPLUG-06 marked complete after 04-06 lands the Windows smoke
requirements-progressed: [HOTPLUG-06]  # Linux side fully covered

duration: 32 min
completed: 2026-05-14
---

# Phase 4 Plan 05: Multi-Device Hot-Plug Integration Harness Summary

**Landed `tests/unit/test_hotplug_harness.cpp` with six Catch2 TEST_CASEs that mechanically prove SC1 (no UAF after Removed), SC2 (per-row `dataChanged({ConnectedRole})` correctness), SC4 (300 ms debounce coalescing) + the HOTPLUG-07 disconnect-during-use composite — driven entirely by the ARCH-02 mock seams from Plan 04-02 (`HotplugMonitor::injectEvent` + `MockHidEnumerator`). Zero real USB, zero real hidapi. Full pass: 163/163 unit tests, 4.75 s total; the six new tests run in 0.7 s.**

## Performance

- **Duration:** ~32 min
- **Started:** 2026-05-14T14:20Z
- **Committed:** 2026-05-14T14:52Z (`e482152`)
- **Tasks:** 1
- **Files modified:** 2 (1 created + 1 modified)
- **Test runtime:** 0.71 s wall clock for the 6 new tests (dominated by 2 × 0.30 s `QSignalSpy::wait` for the 300 ms debouncer cases)

## Accomplishments

- **Six Catch2 TEST_CASEs** cover the full ROADMAP success-criterion matrix Phase 4 promised mechanical proof of:

| Test                                                                    | SC/REQ                  | What it proves                                                                                                                                                                                                                  |
| ----------------------------------------------------------------------- | ----------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| SC1 — `shared_ptr` survives Removed                                     | SC1, Pitfall 1, HOTPLUG-01 | A consumer holding a `DevicePtr` (i.e. `shared_ptr<IDevice>`) keeps the backend alive past a `HotplugAction::Removed` event injection; subsequent `poll()`/`id()` return without crashing. Before Plan 04-01 this was the UAF. |
| SC1.5 — weak_ptr cache (D-06)                                           | D-06                    | Two `registry.open(id)` calls for the same `(vid, pid)` return the same instance (`a.get() == b.get()`, `a.use_count() >= 2`). After both shared_ptrs drop, the next `open()` constructs fresh (`use_count() == 1`).            |
| SC4 — 300 ms debounce same-key burst                                    | SC4, HOTPLUG-05, D-05   | 4 rapid `observe()` calls with the same `HotplugEvent` produce exactly ONE `coalesced` signal emission after 400 ms wait — pins the trailing-edge contract.                                                                     |
| SC4.2 — per-key isolation                                               | HOTPLUG-05               | 9 raw events (3 keys × 3 repeats, round-robin) produce exactly 3 emissions — distinct keys never reset each other's windows.                                                                                                    |
| SC2 — `dataChanged({ConnectedRole})` one-row-only                       | SC2, D-03, HOTPLUG-02   | A single mock-driven disconnect emits exactly ONE `dataChanged` whose role-mask contains `ConnectedRole`, zero `modelReset`, and the row count is unchanged (diff-driven contract from Plan 04-04).                              |
| SC1+D-06 composite (HOTPLUG-07 narrative)                                | HOTPLUG-07               | Holds a `DevicePtr`, marks the underlying device gone (zombie contract), injects Removed, asserts the shared_ptr's `poll()` returns safely; then drops the shared_ptr and asserts the next `open()` constructs fresh.            |

- **`StubDevice`** (test-local class, no namespace pollution) — a minimal `IDevice` implementation: stores `DeviceDescriptor` + `DeviceId`, no-op `open()`/`close()`/`onEvent()`, `firmwareVersion()` returns `"stub-1.0"`, `poll()` short-circuits on `m_alive` (D-06 zombie contract). `markGone()` flips `m_alive` to false for the SC1+D-06 composite test.
- **`registerTestBackend(registry, spec)`** helper — compresses 5 lines of `DeviceDescriptor` initialisation + `registry.registerDevice(...)` into a single call so each TEST_CASE reads as a flat narrative.
- **CMake wiring** links `hotplug_debouncer.cpp` + `device_model.cpp` + `hotplug_monitor.cpp` directly into `ajazz_unit_tests` (rather than via `libajazz_core.a` / the app target). The last one matters: `libajazz_core.a` was built WITHOUT `AJAZZ_TESTING` so its compiled `hotplug_monitor.cpp` lacks the `injectEvent` symbol; the test-target recompile picks up the shim correctly.
- **Build + test pass clean** with the full project warning set (`-Wall -Wextra -Wpedantic -Wshadow -Wold-style-cast -Wcast-align -Wconversion -Wsign-conversion -Wnull-dereference -Wdouble-promotion -Wformat=2 -Wimplicit-fallthrough -Werror`).

## Task Commits

Plan has a single task, committed once:

1. **Task 1:** `e482152` — test(04-05): add multi-device hot-plug integration harness (HOTPLUG-06)

_Plan metadata commit follows below (this SUMMARY)._

## Files Created/Modified

- `tests/unit/test_hotplug_harness.cpp` (NEW, 280 LoC) — the six TEST_CASEs + StubDevice + registerTestBackend helper.
- `tests/unit/CMakeLists.txt` — added `test_hotplug_harness.cpp` + `hotplug_debouncer.cpp` + `device_model.cpp` + `hotplug_monitor.cpp` to the `add_executable(ajazz_unit_tests ...)` source list with an inline block-comment quoting the AJAZZ_TESTING invariant.

## Design Choices

### Why recompile `hotplug_monitor.cpp` into the test binary

`libajazz_core.a` is the production library — Plan 04-02's contract says it carries zero `injectEvent` symbols (objdump-verified). The test binary needs `injectEvent` though, so the path is: `target_compile_definitions(ajazz_unit_tests PRIVATE AJAZZ_TESTING)` ensures every TU compiled INTO the test binary sees the macro; listing `hotplug_monitor.cpp` among the test target's source files means the TU is recompiled for this target with that flag set; the resulting object has the shim symbol; link succeeds. The production library is untouched. The test binary contains two compiled versions of the TU (one inside `libajazz_core.a` without the shim, one freshly compiled with it); the linker picks the test-target's object first because it appears earlier in the link line — and the production object's `hotplug_monitor.cpp` symbols are now "weak duplicates" of the test-target's, resolved without conflict.

### Why `QSignalSpy::wait` rather than `QTest::qWait`

`QSignalSpy::wait(timeout_ms)` returns as soon as the next signal fires (or after the timeout elapses, whichever first). `QTest::qWait` always blocks for the full duration. For the SC4 debouncer test the difference matters: `spy.wait(400)` returns ~301 ms after the last `observe()` call (when the timer fires), so the test wallclock is ~300 ms; `qWait(400)` would always be 400 ms. Cumulative across multiple SC4-like tests the savings add up.

### Why six TEST_CASEs rather than one big one

Catch2's per-TEST_CASE isolation gives clean failure attribution: if SC4 regresses but SC1 passes, the failing line is unambiguous. The plan example sketched 5-6 SECTIONs; the actual implementation split SC1.5 and SC4.2 into their own TEST_CASEs because (a) SC1.5 covers the D-06 flyweight which is a related-but-distinct contract from SC1 (the no-UAF guarantee), and (b) SC4.2 covers per-key isolation which is the structural mechanism that makes SC4 correct under multi-device load. Six tests, six tags, six failure surfaces.

### Why `MockHidEnumerator` mutation lives in the test body

The test pattern is: construct mock with initial set, construct registry passing `mock.asEnumerator()` (which captures `&mock`), construct DeviceModel, refresh, then mutate `mock.setKeys(...)` between refreshes. The captured callable reads the live state every time the registry calls `enumerateConnectedHidKeys()`, so mid-test mutations are seen automatically. This is the cleanest way to simulate hot-plug transitions in a single TEST_CASE without spawning timers or worker threads.

## Verification Run

### Build

```
$ cmake --build --preset linux-release --target ajazz_unit_tests
[8/10] Building CXX object .../__/__/src/core/src/hotplug_monitor.cpp.o
[9/10] Linking CXX executable tests/unit/ajazz_unit_tests
[10/10] (link complete, no warnings)
```

### Full test pass

```
$ cd build/linux-release && ctest --output-on-failure
...
128/163 Test #128: SC1: shared_ptr to IDevice survives a Removed hot-plug event ............................ Passed   0.02 sec
129/163 Test #129: SC1.5: weak_ptr cache returns the same instance for shared keys (D-06) .................. Passed   0.02 sec
130/163 Test #130: SC4: 300ms debounce coalesces 4 rapid same-key events into 1 emission ................... Passed   0.33 sec
131/163 Test #131: SC4.2: per-key isolation — 3 distinct keys produce 3 coalesced emissions ............... Passed   0.30 sec
132/163 Test #132: SC2: DeviceModel emits exactly one dataChanged({ConnectedRole}) per row flip ............ Passed   0.02 sec
133/163 Test #133: SC1+D-06: disconnect-during-use composite (HOTPLUG-07 narrative) ........................ Passed   0.02 sec
...
100% tests passed, 0 tests failed out of 163
Total Test time (real) =   4.75 sec
```

## Issues Encountered

- **Link error initial state**: my first `cmake --build` attempt failed with `undefined reference to ajazz::core::HotplugMonitor::injectEvent` because `libajazz_core.a` was built without `AJAZZ_TESTING` (per Plan 04-02 design). Resolved by adding `${CMAKE_SOURCE_DIR}/src/core/src/hotplug_monitor.cpp` to the test target's source list with an inline comment quoting the rationale.
- **Sibling-agent interference on CMakeLists.txt**: a sibling Phase 7 agent appended `manifest_signer_common.cpp` to the same file while I was editing; resolved by selective staging via `git checkout` on the working-tree-only typo (`Q_OBJECTTs` autofix injected by typos hook in a race condition) and then `git add` only the files I owned.

## Deviations from Plan

- **Plan example listed 5-6 SECTIONs inside ONE TEST_CASE**; actual implementation split into 6 distinct TEST_CASEs for better failure attribution. Documented above under Design Choices.
- **Section 6 (HOTPLUG-07 composite)** was implemented; **a hypothetical Section 7 ("device-shuffle round-robin")** was folded into SC4.2 because the per-key-isolation contract is exactly the device-shuffle invariant.

**Total deviations:** 2 (test partitioning + section merge, both intentional). **Impact:** none — every SC the plan promised is mechanically asserted.

## Phase 4 Readiness Note

`HOTPLUG-06` is half-satisfied: the Linux integration harness lands here; the Windows `WM_DEVICECHANGE` smoke lands in Plan 04-06. Once 04-06 ships, REQUIREMENTS.md's HOTPLUG-06 checkbox flips to `[x]` and Phase 4 is fully closed.

## Self-Check: PASSED
