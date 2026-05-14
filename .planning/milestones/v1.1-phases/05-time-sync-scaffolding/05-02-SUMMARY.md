---
phase: 05-time-sync-scaffolding
plan: 02
subsystem: devices
tags: [streamdeck, time-sync, scaffolding, mixin, once-flag, pitfall-14]

requires:
  - phase: 05-time-sync-scaffolding
    provides: IClockCapable interface + TimeSyncResult enum + Capability::Clock bit (Plan 05-01)
provides:
  - Akp153Device inherits IClockCapable, setTime returns NotImplemented with WARN-once
  - Akp03Device inherits IClockCapable, setTime returns NotImplemented with WARN-once
  - Akp05Device inherits IClockCapable, setTime returns NotImplemented with WARN-once
  - Akp815Device inherits IClockCapable, setTime returns NotImplemented with WARN-once (A-02)
  - All 17 register.cpp rows set .hasClock = true (A-03 / D-03)
affects: [05-04, 05-05, 05-06, 05-07, 05-08]

tech-stack:
  added: []
  patterns:
    - Per-backend std::once_flag at file scope (anonymous namespace) for WARN-once semantics

key-files:
  created: []
  modified:
    - src/devices/streamdeck/src/akp153.cpp
    - src/devices/streamdeck/src/akp03.cpp
    - src/devices/streamdeck/src/akp05.cpp
    - src/devices/streamdeck/src/akp815.cpp
    - src/devices/streamdeck/src/register.cpp

key-decisions:
  - Skipped upstream "or-in Capability::Clock in capabilities()" step — IDevice has no capabilities() method in this codebase; runtime detection is via dynamic_cast<I*Capable*>. This is a benign deviation captured in commit body.
  - Each backend has its OWN s_warned_<codename> once_flag — distinct per TU, not shared. First AKP153 call cannot silence first AKP03 call.
  - .hasClock = true repeated inline 17 times (not factored into helper default) — keeps capability advertisement visible per row; new rows added in the future cannot accidentally regress to hasClock=false through helper inheritance.

patterns-established:
  - 'Multiple inheritance from capability mix-ins is the established pattern: IDevice + IDisplayCapable + IFirmwareCapable + IClockCapable.'
  - 'WARN-once stub body: std::call_once(s_warned_<codename>, [] { AJAZZ_LOG_WARN(category, message); }); return NotImplemented;'

requirements-completed: [TIMESYNC-02, TIMESYNC-06]

duration: ~20min
completed: 2026-05-14
---

# Phase 5 Plan 02: Stream Dock Backend setTime Stubs

**All 4 Stream Dock backends (Akp153, Akp03, Akp05, Akp815) inherit IClockCapable and return TimeSyncResult::NotImplemented from setTime() with per-backend std::once_flag-gated WARN; all 17 register.cpp rows advertise .hasClock = true.**

## Performance

- **Duration:** ~20 min
- **Tasks:** 3 (Task 1: akp153/03/05 stubs; Task 2: AKP815 stub; Task 3: 17 register.cpp rows)
- **Files modified:** 5

## Accomplishments

- Akp153Device, Akp03Device, Akp05Device, Akp815Device each gain `public IClockCapable` in their inheritance list.
- Each backend has its OWN `s_warned_<codename>` std::once_flag at file-scope in the anonymous namespace — distinct per TU.
- Each `setTime()` body is `std::call_once(s_warned_<codename>, [] { AJAZZ_LOG_WARN("streamdeck.<codename>", "setTime() not yet implemented for <codename>"); }); return TimeSyncResult::NotImplemented;` (≤ 6 lines per Pitfall 14 contract).
- All 17 rows in `register.cpp` advertise `.hasClock = true` inline — `grep -c 'hasClock = true' register.cpp` returns exactly 17.

## Task Commits

1. **Tasks 1-3 (combined atomic commit per plan contract):** `11f34e7` (feat(streamdeck))

Per the upstream plan's commit step 2.7, all five files (4 backends + register.cpp) land in a single atomic commit. The grouping mirrors the plan's "Each backend gets identical treatment; one atomic capability bringup".

## Files Created/Modified

- `src/devices/streamdeck/src/akp153.cpp` — added `IClockCapable` inheritance, `s_warned_akp153` once_flag in anonymous namespace, and `setTime()` method body.
- `src/devices/streamdeck/src/akp03.cpp` — same pattern with `s_warned_akp03`, codename `akp03`.
- `src/devices/streamdeck/src/akp05.cpp` — same with `s_warned_akp05`, codename `akp05`.
- `src/devices/streamdeck/src/akp815.cpp` — same with `s_warned_akp815`, codename `akp815` (A-02 amendment).
- `src/devices/streamdeck/src/register.cpp` — every row now has `.hasClock = true`. AKP153 / AKP03 family rows go through `{ auto d = helper(...); d.hasClock = true; reg.registerDevice(d, &factory); }` blocks (13 rows); AKP815 and the 2 AKP05 rows use inline `.hasClock = true,` in their `DeviceDescriptor{...}` literals (3 rows). Total = 17 source-level occurrences.

## Decisions Made

- **Skipped upstream `capabilities()` or-in step.** The upstream plan (Task 2 Step 2.1) prescribes "or-in Capability::Clock in the `capabilities()` return value" but IDevice in this codebase has no `capabilities()` method — runtime capability detection is via `dynamic_cast<I*Capable*>` (and the comment in `capabilities.hpp` line 8 says exactly that). The skip is benign: the inheritance is the advertisement, the descriptor flag is the UI hint, and `devices.yaml` (Plan 05-08) will surface the capability for documentation. Captured in commit body.
- **Per-row hasClock visibility via `auto d = ...; d.hasClock = true; reg.registerDevice(d, ...)` blocks.** The plan's literal acceptance criterion (`grep -c 'hasClock = true' == 17`) drove this style. The alternative — pushing `.hasClock = true` into the helper default — would have produced the same runtime behaviour but only 2 source-level occurrences and would have hidden the capability decision from per-row review.
- **Inline `.hasClock = true,` for the 3 rows that use direct `DeviceDescriptor{...}` literals (AKP815 + 2 AKP05/Mirabox N4).** Same intent: every row visibly carries the field.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Plan/codebase divergence] Upstream "or-in Capability::Clock in capabilities()" step**

- **Found during:** Task 1 (when grepping for `capabilities()` returns to or-in the bit)
- **Issue:** No `capabilities()` method exists on `IDevice` or any backend in this codebase. The upstream plan's Step 2.1 line "or-in Capability::Clock in capabilities()" is inapplicable — runtime detection is via `dynamic_cast<I*Capable*>`, not a bitset method.
- **Fix:** Skip the or-in step; rely on inheritance + descriptor flag + (future) devices.yaml for advertisement.
- **Files modified:** None (no `capabilities()` to edit).
- **Verification:** `grep -rn "capabilities()" src/devices/ src/core/` returns only the doc comment in `capabilities.hpp` line 8. Confirms the method is conceptual, not extant.
- **Committed in:** `11f34e7` (deviation noted in commit body).

**2. [Rule 2 - Pre-commit auto-format] clang-format reflowed `.hasClock = true, // long comment` across 2 lines**

- **Found during:** First commit attempt for Plan 05-02
- **Issue:** A long inline comment on the AKP815 row's `.hasClock = true` line pushed the line over the column limit; clang-format split it as `.hasClock =\n            true,` which broke the literal `hasClock = true` grep match (count dropped from 17 to 16).
- **Fix:** Shortened the inline comment so it fits within the column limit. Comment is functionally identical.
- **Files modified:** src/devices/streamdeck/src/register.cpp (one comment shortened).
- **Verification:** `grep -c 'hasClock = true' register.cpp` returns 17 again.
- **Committed in:** Same commit `11f34e7`.

______________________________________________________________________

**Total deviations:** 2 auto-fixed (1 plan/codebase divergence — skipped step doesn't apply to this codebase; 1 pre-commit hook formatting)
**Impact on plan:** No functional impact. The skipped `capabilities()` or-in step has no effect because nothing in this codebase ever called it. The clang-format issue was style-only.

## Issues Encountered

None beyond the deviations above.

## Next Phase Readiness

- **Plan 05-03 (AKB980 keyboard stub)** — exact same pattern, different namespace.
- **Plan 05-04 (TimeSyncService skeleton + unit tests)** — has 4 concrete IClockCapable implementations to test against (via fake or real `Akp153Device`); `dynamic_cast<IClockCapable*>` site now has multiple real targets.
- **Plan 05-05 (DeviceModel::HasClockRole)** — every Stream Dock row's `descriptor.hasClock` is `true`; the model role reads cleanly.
- **Plan 05-08 (devices.yaml + docs)** — devices.yaml `clock` capability flag corresponds to a real bit + a real interface inheritance.

WARN spam check (mental model): even if every Stream Dock backend's `setTime()` is invoked 10,000 times during an app session, the log produces exactly 4 distinct WARN lines (one per backend) thanks to the 4 distinct `s_warned_<codename>` flags.

Build verification: `cmake --build build/linux-release` produces 16/16 streamdeck targets, full project (app + tests) links. `ctest --preset linux-release` reports 168/168 tests passing.

______________________________________________________________________

*Phase: 05-time-sync-scaffolding*
*Completed: 2026-05-14*
