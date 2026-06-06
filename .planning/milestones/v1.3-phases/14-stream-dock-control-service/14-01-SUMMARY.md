---
phase: 14-stream-dock-control-service
plan: '01'
subsystem: streamdeck-descriptor
tags: [devices, descriptor, honesty, clock, DEVICES-11, ARCH-05]
dependency_graph:
  requires: []
  provides: [akp05e-hasClock-false, DEVICES-11-complete]
  affects: [runtime-sync-button-visibility, docs/_data/devices.yaml]
tech_stack:
  added: []
  patterns: [register.cpp-descriptor-edit, Catch2-registry-assertion]
key_files:
  created:
    - tests/unit/test_register_akp05e_clock.cpp
  modified:
    - src/devices/streamdeck/src/register.cpp
    - tests/unit/CMakeLists.txt
    - docs/_data/devices.yaml
    - README.md
    - docs/wiki/Supported-Devices.md
decisions:
  - Scope DEVICES-11 to akp05e row only (0x0300:0x3004); akp05/mirabox_n4 deferred to a later honesty sweep
  - Prune clock from devices.yaml akp05e capabilities in same commit (doc-only, non-destructive)
  - Test case name includes akp05e_clock substring for ctest -R discoverability
metrics:
  duration: ~8 min
  completed: '2026-05-24T08:49:37Z'
  tasks_completed: 2
  files_changed: 6
---

# Phase 14 Plan 01: akp05e Descriptor Honesty Fix (DEVICES-11) Summary

**One-liner:** Flip akp05e 0x0300:0x3004 hasClock from true to false (no firmware RTC on Stream Dock family) plus a regression test that pins the value.

## Tasks Completed

| Task | Name                                          | Commit  | Files                                                                 |
| ---- | --------------------------------------------- | ------- | --------------------------------------------------------------------- |
| 1    | Flip akp05e hasClock to false (DEVICES-11)    | 07c5902 | register.cpp, devices.yaml, README.md, docs/wiki/Supported-Devices.md |
| 2    | Pin DEVICES-11 with a descriptor honesty test | 2ac7e33 | tests/unit/test_register_akp05e_clock.cpp, tests/unit/CMakeLists.txt  |

## What Was Done

### Task 1 — register.cpp honesty fix

Changed `register.cpp` line 305 (the akp05e / `0x0300:0x3004` descriptor row) from:

```cpp
.hasClock = true, // A-03 / D-03: shares Akp05Device backend → same IClockCapable.
```

to:

```cpp
.hasClock = false, // DEVICES-11 / ARCH-05: Stream Dock family has no firmware RTC.
```

The `akp05` (provisional, line ~267) and `mirabox_n4` (line ~284) rows are unchanged — their scope is deferred to a later Stream Dock family honesty sweep (see "Follow-up Note" below).

The `clock` entry was also removed from the `akp05e` capabilities array in `docs/_data/devices.yaml` (doc-only; drives README/wiki AUTOGEN). The pre-commit AUTOGEN hook regenerated `README.md` and `docs/wiki/Supported-Devices.md` automatically and those files were included in the commit.

### Task 2 — Regression test (TDD GREEN)

Created `tests/unit/test_register_akp05e_clock.cpp` with a single `TEST_CASE` containing two `SECTION`s:

1. Asserts `akp05e` entry exists after `registerAll()` and `hasClock == false`.
1. Asserts `mirabox_n4` entry still has `hasClock == true` (over-broad flip guard).

Registered in `tests/unit/CMakeLists.txt` using the existing `add_executable` source-list pattern. The test binary links against the already-present `ajazz::streamdeck` target — no new dependencies.

Result: `ctest --preset linux-release -R akp05e_clock` runs 1 test, 100% passed. Full 409-test suite green.

## Verification

```
$ grep -A12 'codename = "akp05e"' src/devices/streamdeck/src/register.cpp | grep 'hasClock'
            .hasClock = false, // DEVICES-11 / ARCH-05: Stream Dock family has no firmware RTC.

$ grep -A12 'codename = "mirabox_n4"' src/devices/streamdeck/src/register.cpp | grep 'hasClock'
            .hasClock = true, // A-03 / D-03: shares Akp05Device backend → same IClockCapable.

$ ctest --preset linux-release -R akp05e_clock --output-on-failure
100% tests passed, 0 tests failed out of 1

$ ctest --preset linux-release
100% tests passed, 0 tests failed out of 409
```

`git diff --stat` for Task 1 commit lists only `register.cpp`, `devices.yaml`, `README.md`, `docs/wiki/Supported-Devices.md` — no `ak980pro` row touched.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Lambda parameter namespace not resolved**

- **Found during:** Task 2 build
- **Issue:** `core::DeviceDescriptor const& d` in lambda parameters failed with "core has not been declared" (requires full `ajazz::core::` prefix outside a `using namespace` scope).
- **Fix:** Changed lambda parameter type to `auto const& d` (C++20 abbreviated function template syntax, idiomatic and cleaner), then fixed the top-level `core::DeviceRegistry` to `ajazz::core::DeviceRegistry`.
- **Files modified:** `tests/unit/test_register_akp05e_clock.cpp`
- **Commit:** included in 2ac7e33

**2. [Rule 3 - Blocking] clang-format reformatted lambda body layout**

- **Found during:** Task 2 pre-commit hook
- **Issue:** clang-format moved lambda bodies to the next line, causing the pre-commit hook to modify the staged file.
- **Fix:** Re-staged the clang-format output and committed; no logic change.
- **Files modified:** `tests/unit/test_register_akp05e_clock.cpp`
- **Commit:** 2ac7e33

**3. [Rule 2 - Missing AUTOGEN] README.md / Supported-Devices.md regeneration**

- **Found during:** Task 1 pre-commit hook
- **Issue:** The AUTOGEN pre-commit hook triggered on `devices.yaml` modification and regenerated `README.md` + `docs/wiki/Supported-Devices.md`, which needed to be staged before the commit.
- **Fix:** Added both auto-generated files to the commit — they are part of the same honesty fix.
- **Files modified:** `README.md`, `docs/wiki/Supported-Devices.md`
- **Commit:** 07c5902

## Follow-up Note (Open Question 1)

The `akp05` (provisional VID:PID `0x0300:0x5001`, line ~256-269) and `mirabox_n4` (line ~271-286) rows share the same `makeAkp05` backend and the same ARCH-05 reasoning (no RTC in the Stream Dock family). DEVICES-11 names `akp05e` only; the wider flip is deferred to a later honesty sweep. A future commit touching all three rows should also update the corresponding `devices.yaml` entries and update/extend the test.

## Known Stubs

None — this plan makes no stubs; it only changes a boolean constant and adds a regression test.

## Threat Flags

None — no new network endpoints, auth paths, or trust boundaries introduced. This plan modifies a compile-time static descriptor constant only.

## Self-Check: PASSED

- `src/devices/streamdeck/src/register.cpp` exists and contains `.hasClock = false` for akp05e.
- `tests/unit/test_register_akp05e_clock.cpp` exists (52 lines, > 20).
- `docs/_data/devices.yaml` akp05e capabilities no longer lists `clock`.
- Commits 07c5902 and 2ac7e33 exist in git log.
- Full suite: 409/409 tests passed.
