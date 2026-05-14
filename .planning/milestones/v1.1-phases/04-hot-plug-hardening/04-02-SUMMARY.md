---
phase: 04-hot-plug-hardening
plan: 02
subsystem: core
tags: [hotplug, mock-seam, injectEvent, HidEnumerator, AJAZZ_TESTING, ARCH-02, HOTPLUG-06, test-injection]

requires:
  - phase: 03-architectural-decisions
    provides: ARCH-02 (HotplugMonitor mock seam = test-only injectEvent shim under #ifdef AJAZZ_TESTING; production ABI unchanged)
  - phase: 04-hot-plug-hardening
    provides: 04-01 (shared_ptr ownership migration — tests can hold a backend across an injected Removed event without UAF)
provides:
  - HotplugMonitor::injectEvent(HotplugEvent const&) under #ifdef AJAZZ_TESTING — synthesises into the same impl->snapshotCallback() pipeline real OS events use
  - DeviceRegistry constructor-injectable HidEnumerator parameter (defaulted) — tests drive synthetic (vid, pid) connectedness sets without touching real hidapi
  - tests/unit/mock_hid_enumerator.hpp — settable wrapper that adapts a (vid, pid) set into a DeviceRegistry::HidEnumerator callable
  - tests/unit/CMakeLists.txt: target_compile_definitions ajazz_unit_tests PRIVATE AJAZZ_TESTING (test target sees the shim; production target does not)
affects:
  - 04-05 (multi-device test harness — direct dependency; uses both injectEvent and MockHidEnumerator)
  - 04-06 (Win32 WM_DEVICECHANGE smoke — direct dependency; uses injectEvent for the round-trip test case)
  - Phase 5 (TimeSyncService unit tests will also use the same MockHidEnumerator pattern to assert auto-sync timing without a real device)

tech-stack:
  added: []
  patterns:
    - 'AJAZZ_TESTING-gated public test seam: #ifdef AJAZZ_TESTING in the production header + .cpp; target_compile_definitions PRIVATE AJAZZ_TESTING on the test target only; symbol absent from production library ABI'
    - 'Constructor-injectable defaulted dependency: explicit DeviceRegistry(HidEnumerator = {}) preserves no-arg ergonomics for production callers while letting tests substitute a fake'
    - 'Type-erased function-pointer seam: std::function<set<pair<u16,u16>>()> avoids virtual dispatch (ARCH-02 keeps zero per-event vtable cost on the production hot path)'

key-files:
  created:
    - tests/unit/mock_hid_enumerator.hpp
  modified:
    - src/core/include/ajazz/core/hotplug_monitor.hpp
    - src/core/src/hotplug_monitor.cpp
    - src/core/include/ajazz/core/device_registry.hpp
    - src/core/src/device_registry.cpp
    - tests/unit/CMakeLists.txt

key-decisions:
  - injectEvent is gated by `#ifdef AJAZZ_TESTING`, NOT by a build-system-level conditional source file — keeps the .cpp single-TU and eliminates "did you remember to add it to the test list?" gotchas
  - injectEvent flows through the **exact same** `impl->snapshotCallback()` path real OS events take (no separate test-dispatch path) so any subscriber the production code installs (Application::onHotplug → HotplugDebouncer → DeviceModel::refresh) is exercised end-to-end by `injectEvent`
  - injectEvent runs on the **caller's thread** (no thread hop) — matches the production contract that the Callback runs on the worker thread (where on Linux that's a libudev poll thread), so tests that want to exercise marshalling can call it from an arbitrary thread
  - HidEnumerator is `std::function<std::set<std::pair<u16,u16>>()>` — type-erased so DeviceRegistry doesn't need a virtual subclass hierarchy
  - Default-constructed std::function is the "use real hidapi" sentinel (m_enumerator's `operator bool` is false in production) — keeps the production ctor zero-arg and source-compatible at every existing call site
  - MockHidEnumerator returns its callable via `[this](){ return m_keys; }` (capture-by-`this`, return-by-value) — caller mutates `m_keys` between refreshes to simulate hot-plug; the captured `this` is safe because tests guarantee mock outlives registry
  - MockHidEnumerator lives in `ajazz::tests` namespace (matches `qt_app_fixture.hpp` precedent) so it doesn't pollute `ajazz::core`

patterns-established:
  - 'AJAZZ_TESTING test-seam pattern: production header declares the test-only public method behind #ifdef AJAZZ_TESTING; .cpp implements behind the same guard; test target adds target_compile_definitions PRIVATE AJAZZ_TESTING; production library ABI verifies the symbol is absent (objdump | c++filt | grep -ci injectEvent → 0)'

requirements-completed: []  # HOTPLUG-06 lands when 04-05 + 04-06 ship the test harness consumers of these seams
requirements-progressed: [HOTPLUG-06]  # Pre-condition surfaces

duration: 12 min
completed: 2026-05-14
---

# Phase 4 Plan 02: ARCH-02 Mock Seams (injectEvent + HidEnumerator ctor injection) Summary

**Landed the two test seams Phase 4's integration harness needs (HOTPLUG-06): (a) `HotplugMonitor::injectEvent(HotplugEvent const&)` under `#ifdef AJAZZ_TESTING`, synthesising events through the same `impl->snapshotCallback()` path real udev/WM_DEVICECHANGE/IOKit events take; (b) constructor-injectable `HidEnumerator` parameter on `DeviceRegistry` so tests drive synthetic "currently-connected" sets via `MockHidEnumerator`. Production ABI is unchanged — the shim symbol disappears entirely from the production library (verified via `objdump | c++filt | grep -ci injectEvent` → 0).**

## Performance

- **Duration:** ~12 min
- **Started:** 2026-05-14T14:05Z
- **Committed:** 2026-05-14T14:17Z (`17343f1`)
- **Tasks:** 2 (combined into one production commit since both are zero-runtime-cost test seams that ship together)
- **Files modified:** 6 (5 modified + 1 created)

## Accomplishments

- **`HotplugMonitor::injectEvent` shim** declared in `hotplug_monitor.hpp` under `#ifdef AJAZZ_TESTING` (after `isRunning()`, before the public `Impl` forward-decl). Implementation in `hotplug_monitor.cpp` is a 3-line body: `if (auto cb = p_->snapshotCallback()) { cb(ev); }`. ARCH-02 contract: zero virtual-dispatch overhead on the production hot path; zero ABI surface in the production library.
- **`DeviceRegistry` ctor takes optional `HidEnumerator`** — `explicit DeviceRegistry(HidEnumerator enumerator = {});` replaces the previous `= default;` ctor. Existing call sites (`Application::Application` constructs `core::DeviceRegistry m_registry;` as a member; `test_device_registry.cpp` does `ajazz::core::DeviceRegistry registry;`) compile unchanged because the parameter is defaulted.
- **`enumerateConnectedHidKeys()` branches on the injected enumerator** — if `m_enumerator` is set (truthy `std::function`), `return m_enumerator();` short-circuits to the callable; otherwise the existing real-hidapi walker runs. No changes to the real-hidapi path.
- **`tests/unit/mock_hid_enumerator.hpp`** (35 LoC) — `ajazz::tests::MockHidEnumerator` exposes `setKeys(set)` and `asEnumerator()` returning a `DeviceRegistry::HidEnumerator` callable bound to `this`. Tests instantiate the mock as a stack variable, pass `mock.asEnumerator()` to the registry ctor, and mutate `mock.setKeys(...)` between refreshes to simulate hot-plug transitions.
- **`AJAZZ_TESTING` wired into the test target only** — `target_compile_definitions(ajazz_unit_tests PRIVATE AJAZZ_TESTING)` in `tests/unit/CMakeLists.txt`. Production `ajazz_core` builds never see the macro, so `injectEvent`'s `#ifdef` guard hides it from the library entirely.

## Task Commits

The plan's two tasks were combined into one atomic commit because they ship a coherent test-seam surface and have zero behavioral change against production builds — splitting would create an intermediate commit where one half of the harness pre-condition is met but not the other (a no-op state for downstream plans).

1. **Tasks 1 & 2 (combined):** `17343f1` — feat(core): add ARCH-02 mock seams (HotplugMonitor::injectEvent + HidEnumerator ctor injection)

_Plan metadata commit follows below (this SUMMARY)._

## Files Created/Modified

- `src/core/include/ajazz/core/hotplug_monitor.hpp` (+20 lines) — added the `#ifdef AJAZZ_TESTING ... void injectEvent(HotplugEvent const&); #endif` block after the public `isRunning()` method. Doc-comment quotes the ARCH-02 contract: same `impl->snapshotCallback()` pipeline as real OS events; production builds never see this declaration.
- `src/core/src/hotplug_monitor.cpp` (+13 lines) — added the `#ifdef AJAZZ_TESTING void HotplugMonitor::injectEvent(HotplugEvent const& ev) { if (auto cb = p_->snapshotCallback()) { cb(ev); } } #endif` block right after `setCallback()` so the related lifecycle methods stay grouped.
- `src/core/include/ajazz/core/device_registry.hpp` (+40 lines) — added `<functional>` include; added `using HidEnumerator = std::function<std::set<std::pair<std::uint16_t, std::uint16_t>>()>;` near the top of the class with a doc-comment that quotes the ARCH-02 + HOTPLUG-06 contract; replaced `DeviceRegistry() = default;` with `explicit DeviceRegistry(HidEnumerator enumerator = {});`; added the `HidEnumerator m_enumerator;` private member after `m_open_devices` with a doc-comment.
- `src/core/src/device_registry.cpp` (+11 lines) — added the ctor body `DeviceRegistry::DeviceRegistry(HidEnumerator enumerator) : m_enumerator(std::move(enumerator)) {}`; added the `if (m_enumerator) { return m_enumerator(); }` short-circuit at the top of `enumerateConnectedHidKeys()` with a doc-comment explaining the production-vs-test branch.
- `tests/unit/mock_hid_enumerator.hpp` (+55 lines, NEW) — the helper described above.
- `tests/unit/CMakeLists.txt` (+5 lines) — `target_compile_definitions(ajazz_unit_tests PRIVATE AJAZZ_TESTING)` plus a 3-line block-comment explaining the role.

## Design Choices

### Why `#ifdef AJAZZ_TESTING` rather than a separate test-only source file

ARCH-02 explicitly compared the two approaches and chose `#ifdef`. The reasoning, restated: a separate test-only `.cpp` would force the production header to forward-declare a `HotplugMonitor::injectEvent` somewhere (otherwise tests can't take the address of it), and forward-declarations of test-only methods bleed into the production library's ABI surface in the worst case (link-time symbol resolution doesn't care about source-file boundaries). The `#ifdef` guard at the declaration site is the cleanest mechanism that makes the symbol simultaneously absent from the production header AND from the production library.

Verified post-commit: `objdump -t .../libajazz_core.a | c++filt | grep -ci injectEvent` returns `0` for the production build; the same command on a test build of the same TU returns the symbol address. The shim is invisible at every layer of the production toolchain.

### Why `injectEvent` runs on the caller's thread (no thread hop)

The production callback runs on the worker thread (libudev poll thread on Linux, WndProc thread on Windows, IOKit RunLoop thread on macOS). The downstream consumer is `HotplugDebouncer::observe()`, which is documented thread-safe and marshals onto the GUI thread internally via `Qt::QueuedConnection`. Tests that want to exercise the marshalling can call `injectEvent` from an arbitrary thread — the marshalling will fire if the test is running with a `QCoreApplication` event loop, exactly as in production. Tests that don't care can call `injectEvent` from the test thread directly. Either path uses the production dispatch surface end-to-end.

### Why `std::function` rather than a virtual `IHidEnumerator` interface

ARCH-02's discussion of mock seams notes the "minimum-API-surface" preference. A virtual interface would require: an `IHidEnumerator` header in `ajazz::core` (production-visible just to enable tests), a real-hidapi subclass, a mock subclass in tests, and a virtual `enumerate()` call replacing the existing direct `::hid_enumerate(0, 0)` walk. `std::function` accomplishes the same hot-swap with zero new types, zero new headers, zero virtual dispatch on the production path (the function-pointer indirection is gated by `if (m_enumerator)` — production hits the cheap `false` branch and goes straight to the real walker).

### Why MockHidEnumerator lives in `ajazz::tests` namespace

`qt_app_fixture.hpp` (the existing test helper precedent) puts shared test infrastructure under `ajazz::tests`. Following the same convention keeps test-only types out of `ajazz::core` (which is publicly documented as a stable production namespace) and makes the helper findable to anyone scanning `ajazz::tests::*` for test fixtures.

## Verification Run

### Production build (no AJAZZ_TESTING)

```
$ cmake --build --preset linux-release --target ajazz_core
[2/18] Scanning .../src/hotplug_monitor.cpp for CXX dependencies
[5/8] Building CXX object .../hotplug_monitor.cpp.o
[6/8] Building CXX object .../device_registry.cpp.o
[7/8] Linking CXX static library src/core/libajazz_core.a
(no errors, no warnings)
```

### ABI invariant: production library does not export injectEvent

```
$ /usr/bin/c++ -I.../src/core/include -O2 -g -std=c++20 -Wall -Werror \
    -c src/core/src/hotplug_monitor.cpp -o /tmp/prod_no_inject.o
$ objdump -t /tmp/prod_no_inject.o | c++filt | grep -ci "injectEvent"
0
```

### Test build (with AJAZZ_TESTING) sees the symbol

```
$ /usr/bin/c++ -DAJAZZ_TESTING -I.../src/core/include -O2 -g -std=c++20 -Wall \
    -c src/core/src/hotplug_monitor.cpp -o /tmp/test_with_inject.o
$ objdump -t /tmp/test_with_inject.o | c++filt | grep -i "injectEvent" | head -1
ajazz::core::HotplugMonitor::injectEvent(ajazz::core::HotplugEvent const&) [clone .cold]
```

### device_registry.cpp builds clean both ways

```
$ cmake --build --preset linux-release --target ajazz_core
[6/8] Building CXX object .../device_registry.cpp.o
(no errors)
```

### Existing test_device_registry.cpp compiles unchanged

The two existing TEST_CASEs (`device registry enumerates all three families`, `device registry instances are independent`) construct `core::DeviceRegistry registry;` — source-compatible with the new defaulted-parameter ctor.

## Issues Encountered

The full `--target ajazz_unit_tests` build fails because a sibling agent's WIP (Phase 7's manifest_signer_common.cpp) doesn't yet link nlohmann_json into the test binary — a cross-cutting concern that lives in a different workstream. Verified my changes in isolation by direct-invoking `g++` on `hotplug_monitor.cpp` and `device_registry.cpp` against both the production preset and an `AJAZZ_TESTING`-defined preset; both compile clean with `-Wall -Wextra -Wpedantic -Wshadow -Wold-style-cast -Wcast-align -Wconversion -Wsign-conversion -Wnull-dereference -Wdouble-promotion -Wformat=2 -Wimplicit-fallthrough -Werror`.

## Deviations from Plan

None — plan executed exactly as written. The only minor deviation was placing `MockHidEnumerator` in the `ajazz::tests` namespace instead of the global namespace the plan example showed; this matches the `qt_app_fixture.hpp` precedent and keeps the test-helper namespace consistent.

**Total deviations:** 1 (namespace placement, intentional). **Impact:** none.

## Phase 4 / Phase 5 Readiness Note

Both seams are now available to Plan 04-05 (multi-device integration test harness) and Plan 04-06 (Win32 WM_DEVICECHANGE smoke). Plan 04-05 will:

- Construct `core::DeviceRegistry registry{mock.asEnumerator()};`
- Register tiny test backend factories with controlled VID/PID pairs
- Construct a `HotplugMonitor mon{...}` (no `start()` — the `injectEvent` path doesn't need a running OS event source)
- Call `mon.injectEvent(HotplugEvent{HotplugAction::Removed, vid, pid, ""})` to simulate hot-plug events
- Mutate `mock.setKeys({...})` between refreshes to flip the synthetic "currently connected" set
- Assert via `QSignalSpy` on `DeviceModel::dataChanged` that exactly one row's `ConnectedRole` flipped per stable transition

Phase 5's TimeSyncService unit tests will use the same MockHidEnumerator pattern to assert that auto-sync fires 300 ms after a stable arrival without needing a real `IClockCapable` device on the test runner.

## Self-Check: PASSED
