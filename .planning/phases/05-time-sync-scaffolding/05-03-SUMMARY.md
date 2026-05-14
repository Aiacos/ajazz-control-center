---
phase: 05-time-sync-scaffolding
plan: 03
subsystem: devices
tags: [keyboard, akb980, time-sync, scaffolding, mixin, once-flag, pitfall-14]

requires:
  - phase: 05-time-sync-scaffolding
    provides: IClockCapable + TimeSyncResult enum (Plan 05-01); 4 Stream Dock backend stubs (Plan 05-02)
provides:
  - ProprietaryKeyboard inherits IClockCapable; setTime returns NotImplemented with WARN-once via s_warned_akb980
  - AK980 PRO descriptor sets .hasClock = true (1 row)
  - ViaKeyboard explicitly untouched (D-03 negative-space)
affects: [05-04, 05-05, 05-06, 05-07, 05-08]

tech-stack:
  added: []
  patterns:
    - 'Codename split: descriptor codename ak980pro (existing naming), but log category + once_flag name use akb980 per upstream contract'

key-files:
  created: []
  modified:
    - src/devices/keyboard/src/proprietary_keyboard.cpp
    - src/devices/keyboard/src/register.cpp

key-decisions:
  - 'ViaKeyboard is NOT touched (D-03): VIA-protocol keyboards are QMK-style with no vendor clock surface. The exclusion is intentional and verified via grep -E "IClockCapable|hasClock" via_keyboard.cpp returning zero.'
  - Skipped upstream capabilities() or-in step (same as Plan 05-02 — no such method exists in this codebase).
  - 'Codename mismatch retained: descriptor uses ak980pro, log/flag use akb980. The integration test in Plan 05-08 greps for keyboard.akb980, so the log category must match.'

patterns-established:
  - 'Cross-backend stub count: 5 distinct s_warned_<codename> once_flag instances after Plans 05-02 + 05-03 (akp153, akp03, akp05, akp815, akb980).'
  - 'Cumulative hasClock = true count: 17 streamdeck + 1 keyboard = 18 descriptor rows.'

requirements-completed: [TIMESYNC-02]

duration: ~5min
completed: 2026-05-14
---

# Phase 5 Plan 03: AKB980 PRO Keyboard setTime Stub

**ProprietaryKeyboard backend inherits IClockCapable + returns NotImplemented with WARN-once via s_warned_akb980; AK980 PRO descriptor advertises .hasClock = true. ViaKeyboard untouched per D-03.**

## Performance

- Duration: ~5 min
- Tasks: 1 (Steps 3.1-3.4 of upstream Task 3 collapsed)
- Files modified: 2

## Accomplishments

- ProprietaryKeyboard now `public IClockCapable` (5th capability mix-in on this class).
- `s_warned_akb980` once_flag at file-scope anonymous namespace — distinct from the 4 Stream Dock flags.
- setTime body: `std::call_once(s_warned_akb980, [] { AJAZZ_LOG_WARN("keyboard.akb980", "setTime() not yet implemented for akb980"); }); return TimeSyncResult::NotImplemented;`
- AK980 PRO descriptor (vid 0x0c45 pid 0x8009) sets `.hasClock = true`.
- ViaKeyboard (`src/devices/keyboard/src/via_keyboard.cpp`) and the AK820 Pro VIA descriptor are NOT touched — D-03 negative-space verified.

## Task Commits

1. **Task 1 (Steps 3.1-3.4 atomic):** `4249db3` (feat(keyboard))

## Files Created/Modified

- `src/devices/keyboard/src/proprietary_keyboard.cpp` — added `IClockCapable` inheritance, `s_warned_akb980` once_flag, `setTime()` method body.
- `src/devices/keyboard/src/register.cpp` — AK980 PRO descriptor now has `.hasClock = true`.

## Decisions Made

- **ViaKeyboard untouched.** D-03 explicitly excludes VIA-protocol keyboards. Verification: `grep -E "IClockCapable|Capability::Clock|hasClock" via_keyboard.cpp` returns 0 hits.
- **Codename vs log-category mismatch.** Descriptor codename is `ak980pro` (matches existing naming in `register.cpp`). The log category and once_flag identifier use `akb980` (matches upstream plan + integration-test grep at line 1410 of the time-sync plan). Same device, two textual surfaces.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Plan/codebase divergence] Upstream "or-in Capability::Clock in capabilities()" step**

- **Issue:** Identical to Plan 05-02 — no `capabilities()` method exists on `IDevice` in this codebase.
- **Fix:** Skip the or-in step; rely on inheritance + descriptor flag.
- **Committed in:** `4249db3`.

______________________________________________________________________

**Total deviations:** 1 auto-fixed (same plan/codebase divergence as Plan 05-02).
**Impact on plan:** None — the skip has no functional effect.

## Issues Encountered

None.

## Next Phase Readiness

- **Plan 05-04 (TimeSyncService unit tests)** — now has 5 IClockCapable implementations available (the existing 4 Stream Dock backends + AKB980 keyboard).
- **Plan 05-08 (integration test + docs)** — integration test's `keyboard.akb980` log-category grep matches the just-landed WARN string.

Build verification: 23/23 targets, ctest 168/168.

______________________________________________________________________

*Phase: 05-time-sync-scaffolding*
*Completed: 2026-05-14*
