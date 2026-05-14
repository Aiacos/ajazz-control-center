---
phase: 05-time-sync-scaffolding
plan: 01
subsystem: core
tags: [capability, clock, time-sync, scaffolding, mixin, pitfall-13]

requires:
  - phase: 04-hot-plug-hardening
    provides: shared_ptr<IDevice> ownership migration (ARCH-03) — IClockCapable will be dynamic_cast'd off shared_ptr.get() in Plan 05-04.
provides:
  - Capability::Clock = 1u << 15 (append-only enum entry, Pitfall-13-locked)
  - TimeSyncResult tri-state enum (Ok / NotImplemented / IoError)
  - IClockCapable mix-in interface (single virtual setTime method)
  - DeviceDescriptor.hasClock static-hint flag
  - static_assert lock pinning Capability::Clock to 1u << 15 (A-01 amendment)
affects: [05-02, 05-03, 05-04, 05-05, 05-06, 05-07, 05-08]

tech-stack:
  added: []  # header-only types; no new libraries
  patterns:
    - Pitfall 13 static_assert lock for append-only enums
    - Tri-state Result enum so scaffolded capabilities can advertise honestly

key-files:
  created: []
  modified:
    - src/core/include/ajazz/core/capabilities.hpp
    - src/core/include/ajazz/core/device.hpp

key-decisions:
  - Bit 15 assigned (verified bits 15/16 free before insertion)
  - 'A-01: static_assert locks 1u << 15 — renumbering becomes a build error, not a silent ABI break'
  - Header-only commit — no .cpp churn, every TU including these headers recompiles cleanly

patterns-established:
  - 'Pitfall 13 lock pattern: static_assert(static_cast<unsigned>(Capability::X) == (1u << N), "never renumber") immediately after enum closing brace'
  - 'Tri-state Result for scaffolded capabilities: Ok / NotImplemented / IoError lets backends advertise the capability without lying about implementing it'

requirements-completed: [TIMESYNC-01]

duration: ~10min
completed: 2026-05-14
---

# Phase 5 Plan 01: Time-Sync Foundation Atoms

**Capability::Clock = 1u \<< 15 + TimeSyncResult tri-state + IClockCapable interface + DeviceDescriptor.hasClock — all four foundation atoms in one header-only atomic commit, locked against future renumbering by a static_assert.**

## Performance

- **Duration:** ~10 min
- **Tasks:** 1 (single atomic foundation task)
- **Files modified:** 2

## Accomplishments

- `Capability::Clock` bit appended at 1u \<< 15 (next free bit; verified empty before insertion).
- `TimeSyncResult` tri-state enum lets backends advertise Capability::Clock honestly while returning `NotImplemented` until a real wire format lands.
- `IClockCapable` abstract mix-in with `[[nodiscard]] setTime(std::chrono::system_clock::time_point)` — representation-agnostic; backends own BCD / Unix-epoch / vendor-frame encoding.
- `DeviceDescriptor.hasClock` static hint mirrors the existing `hasRgb` / `hasTouchStrip` pattern — cheap UI gating without speculative `dynamic_cast` on every render.
- A-01 amendment beyond upstream: `static_assert` pins the bit to 1u \<< 15. Any future renumbering of capability bits that shifts Clock now produces a compile error.

## Task Commits

1. **Task 1: Foundation atoms (Steps 1.1–1.4 + A-01)** — `450846b` (feat(core))

## Files Created/Modified

- `src/core/include/ajazz/core/capabilities.hpp` — added `#include <chrono>`, `Capability::Clock = 1u << 15`, the Pitfall-13 static_assert, the `TimeSyncResult` enum, and the `IClockCapable` class.
- `src/core/include/ajazz/core/device.hpp` — added `bool hasClock{false}` to `DeviceDescriptor` immediately after `hasTouchStrip`.

## Decisions Made

- **Bit position:** 1u \<< 15. Design doc's recommendation; verified bit 15 and bit 16 are both free via `grep -nE "1u << (15|16)" capabilities.hpp` returning zero hits.
- **Amendment A-01 lands inline with Task 1.** The upstream plan (Task 1 verbatim) plus this amendment is a single atomic commit, not a follow-up commit.

## Deviations from Plan

None — plan executed exactly as written. Clang-format (pre-commit hook) reflowed two comment continuations into two-line form; no semantic change.

## Issues Encountered

- First commit attempt failed pre-commit due to clang-format auto-reformatting two long doc comments. Re-staged the formatted output and committed cleanly. Standard pre-commit hook interaction.

## Next Phase Readiness

- **Plan 05-02 (Stream Dock backend stubs)** can now `#include "ajazz/core/capabilities.hpp"` and inherit `IClockCapable`. Capability bit + tri-state enum + interface are all visible.
- **Plan 05-03 (AKB980 PRO keyboard stub)** — same.
- **Plan 05-04 (TimeSyncService skeleton + unit tests)** — `dynamic_cast<IClockCapable*>` site has a real target type; `TimeSyncResult` map to QML-side states is now well-defined.
- **Plan 05-05 (DeviceModel::HasClockRole)** — `descriptor.hasClock` exists; role can read it.
- **Plan 05-08 (devices.yaml + docs)** — `clock` capability key in devices.yaml now corresponds to a real bit; per-protocol-doc "## Time sync" sections can reference the real interface name.

Build verification: full `cmake --build build/linux-release` succeeded (70/70 targets). `ctest --preset linux-release` reports 168/168 tests passing — no regressions on the existing test surface.

______________________________________________________________________

*Phase: 05-time-sync-scaffolding*
*Completed: 2026-05-14*
