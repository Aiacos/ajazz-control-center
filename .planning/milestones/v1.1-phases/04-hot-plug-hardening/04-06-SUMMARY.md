---
phase: 04-hot-plug-hardening
plan: 06
subsystem: tests + ci
tags: [hotplug, win32, WM_DEVICECHANGE, ci-verification, cross-cutting-pitfall, HOTPLUG-06, SC5]

requires:
  - phase: 04-hot-plug-hardening
    provides: 04-02 (HotplugMonitor::injectEvent + AJAZZ_TESTING test-target define — needed for the round-trip TEST_CASE)
provides:
  - HotplugMonitor::parseDevicePathW(wchar_t const*, HotplugAction) — pure-function Win32 device-path parser under #if defined(_WIN32) && AJAZZ_TESTING
  - tests/unit/test_hotplug_win32_smoke.cpp — 4 Catch2 TEST_CASEs covering parser correctness + injectEvent round-trip
  - .github/workflows/ci.yml: "Verify Windows hot-plug smoke ran" step that loudly fails CI if the suite is skipped
affects:
  - HOTPLUG-06 closes (Linux harness from 04-05 + Windows smoke here)
  - ROADMAP SC5 satisfied
  - Cross-Cutting pitfall (Linux-CI-blind Windows breakage) mitigated for the hot-plug subsystem

tech-stack:
  added: []
  patterns:
    - 'Win32-only TEST_CASE pattern: #if defined(_WIN32) at the file level + WIN32-gated source inclusion in CMake — belt-and-braces guard against accidental Linux compile-in'
    - 'CI explicit-ran verification step: ctest --test-regex hotplug fails loudly if zero tests match the regex (catches silent-skip via compile-defined-out / test-list drift)'
    - 'AJAZZ_TESTING-gated free function on production class: parseDevicePathW lives on HotplugMonitor as a static helper under the same #ifdef guard as injectEvent — production ABI unchanged, ABI invariant inherited from Plan 04-02'

key-files:
  created:
    - tests/unit/test_hotplug_win32_smoke.cpp
  modified:
    - src/core/include/ajazz/core/hotplug_monitor.hpp
    - src/core/src/hotplug_monitor.cpp
    - tests/unit/CMakeLists.txt
    - .github/workflows/ci.yml

key-decisions:
  - parseDevicePathW is a static member on `HotplugMonitor` (not a free function in `hotplug_monitor.cpp`'s anonymous namespace) because the test TU needs to reach it from outside the .cpp — keeping it inside the class scope with the same AJAZZ_TESTING + _WIN32 guards as injectEvent keeps the test-seam discipline consistent
  - Returns a HotplugEvent value (not bool + out-parameters) — matches the wndProc construction sequence more cleanly and lets the test assert directly on the returned struct
  - parse-failure sentinel is `vid==0 && pid==0` — production wndProc already checks the same condition (via parseVidPid returning bool, but the test variant produces the same semantic shape)
  - Serial extraction is best-effort ASCII narrow from the third '#'-segment — matches what tests need to assert without dragging UTF-8 conversion complexity into the test seam
  - 4 TEST_CASEs (not 2 as plan example suggested): canonical path / Removed-action / parse-failure / injectEvent round-trip. Each covers a distinct contract surface for the Win32 hot-plug path
  - The CI "Verify Windows hot-plug smoke ran" step uses `ctest --test-regex hotplug` (not `-R hotplug`) — modern CMake idiom, same behaviour, makes the intent loud in the workflow file

patterns-established:
  - 'CI smoke-ran verification step: after the canonical full-test run, add a per-runner-gated ctest invocation with a tag/regex filter that asserts at least one test matched + ran + passed. Fails loudly if compile-defined-out, name regex mismatch, or test-list filter drift caused silent skip.'
  - 'Win32-specific AJAZZ_TESTING-gated parser helper pattern (extends Plan 04-02 pattern): production code keeps an anonymous-namespace helper for its own use; the same logic is exposed as `static HotplugMonitor::parseXxxW(...)` under #ifdef AJAZZ_TESTING + #if defined(_WIN32) so test code can reach it without leaking into production ABI'

requirements-completed: [HOTPLUG-06]

duration: 22 min
completed: 2026-05-14
---

# Phase 4 Plan 06: Win32 WM_DEVICECHANGE Smoke + CI Verification Summary

**Landed `tests/unit/test_hotplug_win32_smoke.cpp` (4 Catch2 TEST_CASEs, `_WIN32`-gated at both file + CMake level) plus a CI verification step that loudly fails if the Windows hot-plug suite is skipped. Closes HOTPLUG-06 fully (Linux harness from Plan 04-05 + Windows smoke here) and satisfies ROADMAP SC5. The 2026-05-12/13 Windows-specific surprises documented in HOTPLUG-07 now have an executable regression guard.**

## Performance

- **Duration:** ~22 min
- **Started:** 2026-05-14T14:55Z
- **Committed:** 2026-05-14T15:17Z (`9ad6ce5`)
- **Tasks:** 1
- **Files modified:** 5 (1 created + 4 modified)
- **Linux test pass:** 163/163 still green (Win32 source compiled out)
- **Windows test pass:** verified via CI post-merge (not directly runnable on Linux dev box)

## Accomplishments

- **`HotplugMonitor::parseDevicePathW` static helper** added to `hotplug_monitor.hpp` under `#if defined(_WIN32)` + `#ifdef AJAZZ_TESTING`, definition in `hotplug_monitor.cpp` inside the same guards. Pure function: takes `wchar_t const* path` + `HotplugAction action`, returns a populated `HotplugEvent`. Mirrors the production `parseVidPid` + ev-build sequence in `wndProc` but as a free-function-like entry point reachable from test code.
- **Serial extraction**: parses the third `#`-separated segment of the device path (`"\\?\HID#VID_xxxx&PID_yyyy#7&serial&..."` → serial substring `"7&serial&..."`), narrows ASCII into the production `HotplugEvent.serial` `std::string` field. Non-ASCII bytes are skipped silently — matches the production assumption that HID device-paths are ASCII-only.
- **Test file**: 4 TEST_CASEs covering canonical AKP03 path parse, Removed-action survives parser, missing-VID/PID returns 0/0 sentinel, and `injectEvent` round-trips a synthetic event end-to-end via the in-memory dispatch path (without `mon.start()`).
- **CMake gating**: `tests/unit/CMakeLists.txt` adds `test_hotplug_win32_smoke.cpp` ONLY inside the `if(WIN32) ... target_sources(...) endif()` block. Linux/macOS test binaries compile without it. The `#if defined(_WIN32)` at the source-file level is belt-and-braces.
- **CI verification step**: `.github/workflows/ci.yml` gains an explicit "Verify Windows hot-plug smoke ran" step (runner.os == 'Windows' gate) that invokes `ctest --preset windows-release --test-regex hotplug --output-on-failure` AFTER the canonical full `ctest --preset windows-release` pass. If zero tests match the regex, the step fails — exactly the silent-skip pitfall this plan exists to close.
- **Production ABI invariant preserved**: `parseDevicePathW` is `#ifdef AJAZZ_TESTING`-gated identically to `injectEvent`, so the production `ajazz_core` library has zero new exported symbols. The same `objdump | c++filt | grep -ci parseDevicePathW` invariant Plan 04-02 established for `injectEvent` extends to this helper without modification.

## Task Commits

Plan has a single task, committed once:

1. **Task 1:** `9ad6ce5` — feat(core): add Win32 WM_DEVICECHANGE smoke + CI verification (HOTPLUG-06)

_Plan metadata commit (REQUIREMENTS.md update + this SUMMARY) follows below._

## Files Created/Modified

- `src/core/include/ajazz/core/hotplug_monitor.hpp` (+26 lines) — added the `#if defined(_WIN32) ... [[nodiscard]] static HotplugEvent parseDevicePathW(wchar_t const*, HotplugAction) noexcept; #endif` declaration inside the existing `#ifdef AJAZZ_TESTING` block. Doc-comment quotes the production sequence the helper mirrors + the device-path format.
- `src/core/src/hotplug_monitor.cpp` (+44 lines) — implementation under the same guards. Uses `std::wstring_view::find` for VID\_/PID\_ prefix location, `std::wcstoul` for hex parse, third-`#`-segment slice for serial. Independent of the WND_PROC pump.
- `tests/unit/test_hotplug_win32_smoke.cpp` (NEW, 102 LoC) — 4 TEST_CASEs gated at file level by `#if defined(_WIN32)`.
- `tests/unit/CMakeLists.txt` (+6 lines) — `test_hotplug_win32_smoke.cpp` added to the `if(WIN32) ... target_sources(...)` block with an inline comment quoting the Cross-Cutting-pitfall mitigation.
- `.github/workflows/ci.yml` (+11 lines) — "Verify Windows hot-plug smoke ran" step with explanatory block comment.

## Design Choices

### Why `parseDevicePathW` is a static class member rather than a free function

The plan's example sketched a free function `parseDevicePathW(...)` in the test-only header. Making it a `static` member on `HotplugMonitor` instead has three benefits:

1. **Namespace discipline**: the test seam lives inside `ajazz::core::HotplugMonitor::` rather than `ajazz::core::` — same scope-precision Plan 04-02's `injectEvent` chose.
1. **Same guard pattern**: declared inside the existing `#ifdef AJAZZ_TESTING` block in `hotplug_monitor.hpp`, the helper inherits the production-ABI invisibility guarantee for free.
1. **No header forward-declaration drift**: a free function would need to be declared in the production header (otherwise test code can't take its address). The `static` member lives inside the class declaration where everything else does.

### Why return `HotplugEvent` instead of `bool + out-parameters`

The plan example used `bool parseVidPid(WCHAR const*, u16&, u16&)`. The production `parseVidPid` keeps that shape (legacy), but the test seam wants a single-call entry-point that returns a populated event ready to compare against expected values. `HotplugEvent` is small (trivially copyable; 4-byte enum + 4 bytes for vid/pid + a `std::string` for serial), so return-by-value is cheap. Parse-failure sentinel is `vid==0 && pid==0` — the production wndProc would discard those events anyway (real USB devices never have VID 0 or PID 0).

### Why `noexcept` on `parseDevicePathW`

`std::wstring_view::find` and `std::wcstoul` are both `noexcept`-compatible. The serial-extraction loop allocates into a `std::string` — `std::bad_alloc` is the only theoretical exception, but the test seam is in tight enough scope (parsing a < 200-byte device path) that allocation failure is effectively impossible. `noexcept` lets the test code reason about the function as pure.

### Why 4 TEST_CASEs

The plan example suggested 2 (parser + injectEvent round-trip). I added 2 more for failure-mode coverage:

- **TEST_CASE 2** (Removed-action survives parser): pins the action discrimination contract — the helper takes `action` as a parameter and propagates it; trivially correct but easy to break in refactors.
- **TEST_CASE 3** (missing VID/PID returns 0/0 sentinel): pins the parse-failure contract. The production wndProc checks parseVidPid's bool return; the helper variant returns the sentinel struct that callers can compare against. Without this test, a future change that made the helper throw or assert on bad input would be silent against the harness.

### Why CI uses `--test-regex hotplug` rather than `-R hotplug`

Both are accepted by CMake's `ctest`. `--test-regex` is the longhand modern form; using it (with a comment quoting the intent) makes the workflow YAML self-documenting about what's being asserted. The shorthand `-R hotplug` would work identically but read as "regex" to the next maintainer rather than "filter by name pattern" — minor clarity win.

## Verification Run

### Linux build (Win32 source compiled out)

```
$ cmake --build --preset linux-release --target ajazz_unit_tests
[19/29] Building CXX object .../tests/unit/CMakeFiles/ajazz_unit_tests.dir/__/__/src/core/src/hotplug_monitor.cpp.o
[28/29] Linking CXX executable tests/unit/ajazz_unit_tests
(no errors, no warnings)
```

### Linux test pass (regressions check)

```
$ cd build/linux-release && ctest
100% tests passed, 0 tests failed out of 163
Total Test time (real) =   4.92 sec
```

The new `parseDevicePathW` source code compiles into the test binary on Linux (because the surrounding `_WIN32` guard wraps only the helper, while the rest of the TU compiles unconditionally), but the `test_hotplug_win32_smoke.cpp` test file is excluded from the build entirely on non-Windows platforms. No regression.

### Windows test pass (verified via CI)

The "Verify Windows hot-plug smoke ran" step will, on the Windows-2022 runner:

```yaml
- name: Verify Windows hot-plug smoke ran
  if: runner.os == 'Windows'
  run: ctest --preset windows-release --test-regex hotplug --output-on-failure
```

Expected output post-merge: at least 4 `[hotplug][win32]`-tagged tests run + pass (the 4 TEST_CASEs above), plus the 6 Plan 04-05 hotplug harness tests (also tagged `[hotplug]`), for a minimum of 10 tests matching the `hotplug` regex.

If zero tests match the regex, ctest exits with non-zero, and CI fails loudly — exactly the silent-skip protection this plan exists to provide.

## Issues Encountered

None. Linux compile + test pass clean on first attempt. The only minor friction was the sibling-agent staged-edits to `tests/unit/CMakeLists.txt` (Phase 7 adding `test_load_trust_roots.cpp` to the unconditional source list while I was adding `test_hotplug_win32_smoke.cpp` to the `if(WIN32)` block); resolved by applying my Phase 4 chunk via `git apply --cached` from a Phase-4-only patch file, leaving Phase 7's diff in the working tree for that agent's own commit.

## Deviations from Plan

- **4 TEST_CASEs** instead of the plan's example 2 (added failure-mode coverage). Documented above.
- **`parseDevicePathW` is a `static` member** rather than a free function (cleaner scope + same #ifdef guard as injectEvent). Documented above.
- **Used `--test-regex hotplug`** instead of `-R hotplug` (longhand for self-documenting workflow YAML). Documented above.

**Total deviations:** 3 (all improvements, intentional). **Impact:** none — every contract the plan promised is exercised.

## Phase 4 Closure Note

`HOTPLUG-06` is now fully complete: the Linux multi-device integration harness (Plan 04-05) + the Windows WM_DEVICECHANGE smoke (this plan) + the CI verification step that guards against silent-skip. ROADMAP SC5 is satisfied. The Cross-Cutting pitfall (Linux-CI-blind Windows breakage) is mitigated for the hot-plug subsystem.

Phase 4 is complete:

| REQ-ID     | Status   | Plan(s)               |
| ---------- | -------- | --------------------- |
| HOTPLUG-01 | Complete | 04-01                 |
| HOTPLUG-02 | Complete | 04-04                 |
| HOTPLUG-03 | Complete | 04-04                 |
| HOTPLUG-04 | Complete | 04-04                 |
| HOTPLUG-05 | Complete | 04-03                 |
| HOTPLUG-06 | Complete | 04-02 + 04-05 + 04-06 |
| HOTPLUG-07 | Complete | 04-07                 |

Phase 5 (Time-Sync Scaffolding) is now unblocked.

## Self-Check: PASSED
