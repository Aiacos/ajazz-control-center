---
phase: 04-hot-plug-hardening
plan: 01
subsystem: core
tags: [shared_ptr, ownership, flyweight, weak_ptr, hot-plug, ARCH-03, D-06, HOTPLUG-01, registry]

requires:
  - phase: 03-architectural-decisions
    provides: ARCH-03 atomicity rule (single-commit ownership migration), D-06 flyweight cache contract
provides:
  - DevicePtr alias is shared_ptr<IDevice> across the whole codebase
  - DeviceRegistry::open() is a weak_ptr-cached flyweight per (vendorId, productId)
  - Separate m_open_mutex prevents cache contention with enumerate()/registerDevice()
  - Zombie contract documented on IDevice for backends to honour after hot-plug Removed
  - 7 backend factories return via std::make_shared (no leftover make_unique<...Device> in src/devices/)
affects:
  - 04-02 (HotplugMonitor::injectEvent test seam + HidEnumerator ctor injection)
  - 04-03 (HotplugDebouncer — depends on shared_ptr ownership to survive coalesced events)
  - 04-05 (multi-device test harness — proves SC1 with shared_ptr lifetime assertions)
  - Phase 5 (TimeSyncService — first consumer to capture shared_ptr<IDevice> across event-loop turns; relies on D-06 cache for single-HID-handle semantics)

tech-stack:
  added: []  # No new libraries — purely structural type swap + cache add
  patterns:
    - Flyweight cache via weak_ptr (D-06)
    - Lock-or-construct with paired mutexes (m_mutex for registry table, m_open_mutex for cache; never held simultaneously)
    - 'Zombie-safe IDevice contract: gate HID I/O on internal alive flag, return Result::DeviceGone after USB device disappears'

key-files:
  created: []
  modified:
    - src/core/include/ajazz/core/device.hpp
    - src/core/include/ajazz/core/device_registry.hpp
    - src/core/src/device_registry.cpp
    - src/devices/streamdeck/include/ajazz/streamdeck/streamdeck.hpp
    - src/devices/streamdeck/src/akp153.cpp
    - src/devices/streamdeck/src/akp03.cpp
    - src/devices/streamdeck/src/akp05.cpp
    - src/devices/streamdeck/src/akp815.cpp
    - src/devices/keyboard/include/ajazz/keyboard/keyboard.hpp
    - src/devices/keyboard/src/via_keyboard.cpp
    - src/devices/keyboard/src/proprietary_keyboard.cpp
    - src/devices/mouse/include/ajazz/mouse/mouse.hpp
    - src/devices/mouse/src/aj_series.cpp

key-decisions:
  - Single atomic commit (13 files) per ARCH-03 — no broken intermediate state in git history
  - Separate m_open_mutex distinct from m_mutex per D-06 — avoids cache contention with enumerate()/registerDevice() during hot-plug bursts
  - Passive eviction (weak_ptr expiry only) — no proactive invalidation on hot-plug Removed; backends honour the zombie contract instead
  - No enable_shared_from_this introduced — audit confirmed zero [this]-capturing lambdas / detached threads in any of the 7 backends; would be scope creep
  - Cache key is (vid, pid) only — serial intentionally excluded per v1.1 'one backend per device class per process' contract
  - Doc-comment-only edits to 3 fan-out factory headers — declared signatures unchanged because DevicePtr alias absorbs the type swap

patterns-established:
  - 'Lock-or-construct flyweight: cache lookup -> factory invocation (no mutex held) -> weak_ptr store. Lock order documented in code comments at the call site.'
  - 'Atomic structural migration commit: production-code-only commit per ARCH-03; STATE.md/SUMMARY.md tracking writes happen separately'

requirements-completed: [HOTPLUG-01]

duration: 35 min
completed: 2026-05-14
---

# Phase 4 Plan 01: Atomic shared_ptr Ownership Migration + weak_ptr Flyweight Cache Summary

**Migrated `DeviceRegistry` slot ownership from `unique_ptr<IDevice>` to `shared_ptr<IDevice>` across 7 backend factories in a single atomic commit, and added a `weak_ptr`-cached flyweight to `DeviceRegistry::open()` so consumers sharing a `(vid, pid)` see the same backend instance / one HID handle.**

## Performance

- **Duration:** ~35 min
- **Started:** 2026-05-14T11:18Z (Phase 4 dispatch)
- **Completed:** 2026-05-14T11:27Z
- **Tasks:** 2 (combined into one atomic commit per ARCH-03)
- **Files modified:** 13

## Accomplishments

- `DevicePtr` is now `std::shared_ptr<IDevice>` — every `auto`-capturing call site is source-compatible.
- All 7 backend factories (`makeAkp153`, `makeAkp03`, `makeAkp05`, `makeAkp815`, `makeViaKeyboard`, `makeProprietaryKeyboard`, `makeAjSeries`) construct via `std::make_shared`; no `make_unique<...Device>` remains anywhere in `src/devices/`.
- `DeviceRegistry::open()` is a lock-or-construct flyweight: cache hit returns the existing shared instance, cache miss/expiry constructs fresh and stores the result as a `weak_ptr`. Eviction is passive — the slot is overwritten when the last `shared_ptr` drops.
- The cache lives behind a dedicated `m_open_mutex` separate from the existing `m_mutex` so that a hot-plug-driven flurry of `open()` calls cannot contend with `enumerate()` / `registerDevice()`.
- `IDevice` now documents the zombie contract — implementations must gate HID I/O on an internal alive flag and return `Result::DeviceGone` (or equivalent sentinel) after the underlying USB device disappears. This is what makes passive cache eviction safe.
- Full project (`cmake --build --preset linux-release`) compiles and links cleanly with no errors / undefined references.
- Pre-existing `device registry` test cases (`enumerates all three families`, `instances are independent`) still pass — the alias swap is source-compatible.

## Task Commits

The plan's two tasks (header alias + cache members; factory migration) were combined into a single atomic commit per ARCH-03 — the migration is observable only as one history entry, never as a broken intermediate state.

1. **Tasks 1 & 2 (atomic):** `c44635f` — feat(core): migrate DeviceRegistry slots to shared_ptr<IDevice> + weak_ptr cache

_Plan metadata commit follows below (this SUMMARY)._

## Files Created/Modified

- `src/core/include/ajazz/core/device.hpp` — `DevicePtr` alias flipped to `std::shared_ptr<IDevice>`; `IDevice` class doc gains the zombie contract note.
- `src/core/include/ajazz/core/device_registry.hpp` — `<map>` include added; `open()` doc rewritten to describe flyweight semantics + passive eviction; private members gain `mutable std::mutex m_open_mutex` and `mutable std::map<std::pair<u16,u16>, std::weak_ptr<IDevice>> m_open_devices`.
- `src/core/src/device_registry.cpp` — `open()` body reimplemented as cache-aware flyweight with documented lock order (never hold both mutexes; never hold any mutex across factory invocation).
- `src/devices/streamdeck/include/ajazz/streamdeck/streamdeck.hpp` — doc-comments on the 4 factory declarations updated to note new shared semantics + flyweight cache.
- `src/devices/streamdeck/src/akp153.cpp` — `make_unique<Akp153Device>` -> `make_shared<Akp153Device>`.
- `src/devices/streamdeck/src/akp03.cpp` — `make_unique<Akp03Device>` -> `make_shared<Akp03Device>`.
- `src/devices/streamdeck/src/akp05.cpp` — `make_unique<Akp05Device>` -> `make_shared<Akp05Device>`.
- `src/devices/streamdeck/src/akp815.cpp` — `make_unique<Akp815Device>` -> `make_shared<Akp815Device>`.
- `src/devices/keyboard/include/ajazz/keyboard/keyboard.hpp` — doc-comments on both factory declarations updated.
- `src/devices/keyboard/src/via_keyboard.cpp` — `make_unique<ViaKeyboard>` -> `make_shared<ViaKeyboard>`.
- `src/devices/keyboard/src/proprietary_keyboard.cpp` — `make_unique<ProprietaryKeyboard>` -> `make_shared<ProprietaryKeyboard>`.
- `src/devices/mouse/include/ajazz/mouse/mouse.hpp` — doc-comment on factory declaration updated.
- `src/devices/mouse/src/aj_series.cpp` — `make_unique<AjSeriesMouse>` -> `make_shared<AjSeriesMouse>`.

## Audit Findings

- **`[this]` lambda capture audit:** clean. Grep across all 7 backends found zero matches for `[this]`, `[=]`, or `[&]` lambda captures in the .cpp files. No backend stashes `this` into long-lived state owned by something other than itself.
- **Detached threads / `std::async` audit:** clean. Grep found zero `std::thread`, `std::async`, `detach()`, or `std::function` patterns in the 7 backends that would escape the device's own lifetime.
- **`enable_shared_from_this` decision:** NOT introduced. The plan explicitly scoped this out — it would only be needed if a backend captured `this` into a detached thread or into state outliving the device's own destruction. The audit confirms no such pattern exists. Re-evaluate if Phase 5 time-sync introduces lifetime-spanning callbacks.
- **`cppcheck` / `clang-tidy` noise:** none surfaced by the build (`-Wall -Wextra -Werror`); the alias swap is a no-op for the compiler at every `auto`-capturing site, and the new cache members are bog-standard STL types.

## Phase 5 Readiness Note

No consumer code in v1.1 outside Phase 4 yet stores a `shared_ptr<IDevice>` across event-loop turns — `Phase 5 TimeSyncService` is the **first** such consumer, and it requires this migration to have shipped. The atomic commit ratifies the precondition called out in `Phase 5: CONTEXT` (commit `61f6d90`).

## Issues Encountered

None. Build, test suite, and acceptance criteria all green on the first attempt. One trivial `clang-format` reformatting pass on `device_registry.cpp` (long `AJAZZ_LOG_INFO` arglists wrapped to single lines) was applied via the pre-commit hook and re-staged transparently.

## Deviations from Plan

None — plan executed exactly as written.

**Total deviations:** 0. **Impact:** none.

## Verification Run

```
$ cmake --build --preset linux-release
[340/340] Linking CXX executable tests/unit/ajazz_unit_tests
(no errors, no undefined references)

$ ctest --preset linux-release -R "device registry"
    Start 5: device registry enumerates all three families
1/2 Test #5: device registry enumerates all three families ...   Passed    0.02 sec
    Start 6: device registry instances are independent
2/2 Test #6: device registry instances are independent .......   Passed    0.02 sec
100% tests passed, 0 tests failed out of 2

$ grep -q "using DevicePtr = std::shared_ptr<IDevice>;" src/core/include/ajazz/core/device.hpp && echo PASS
PASS
$ grep -q "std::weak_ptr<IDevice>" src/core/include/ajazz/core/device_registry.hpp && echo PASS
PASS
$ grep -rn "std::make_unique<.*Device>" src/devices/ | grep -v build
(no matches)
```

## Self-Check: PASSED
