---
phase: 21-builtin-in-process-actions
fixed_at: 2026-05-24T20:20:00Z
review_path: .planning/phases/21-builtin-in-process-actions/21-REVIEW.md
iteration: 1
findings_in_scope: 5
fixed: 5
skipped: 0
status: all_fixed
---

# Phase 21: Code Review Fix Report

**Fixed at:** 2026-05-24T20:20:00Z
**Source review:** .planning/phases/21-builtin-in-process-actions/21-REVIEW.md
**Iteration:** 1

**Summary:**

- Findings in scope: 5 (1 Critical + 2 Warnings + 2 Info)
- Fixed: 5
- Skipped: 0

## Fixed Issues

### CR-01: OBS auth bypass via non-object `d.authentication` value

**Files modified:** `src/app/src/obs_client.cpp`, `tests/unit/test_obs_client.cpp`
**Commit:** 3e3009d
**Applied fix:** Changed `!authVal.isUndefined() && authVal.isObject()` to a
two-stage check: `if (!authVal.isUndefined())` first, then a nested `if (!authVal.isObject())`
that emits `authFailed("OBS sent malformed authentication field; treating as auth-required")`
and closes the socket without sending Identify. The path for a well-formed object with
challenge+salt is unchanged. The no-auth path (genuine `isUndefined()`) is unchanged.

Three regression tests added to `test_obs_client.cpp`:

- `authentication:null` must emit `authFailed` and send zero Identify messages
- `authentication:true` (boolean) same
- `authentication:"not-an-object"` (string) same

All 9 `[obs-client]` test cases pass.

### WR-01: OBS auth-failure causes unbounded per-button-press retry loop

**Files modified:** `src/app/src/builtin_actions_service.cpp`, `src/app/src/builtin_actions_service.hpp`
**Commit:** d87cf08
**Applied fix:** Added `bool m_obsAuthFailed{false}` member (inside `#ifdef AJAZZ_HAVE_WEBSOCKETS`).
Updated the `authFailed` handler lambda to set `m_obsAuthFailed = true` and log the WARN with
"retries suppressed until settings update" note. Updated the `connected` handler lambda to also
clear `m_obsAuthFailed = false`. Gated the lazy-connect block on `!m_obsConnected && !m_obsAuthFailed`;
added an `else if (m_obsAuthFailed)` branch that logs a single WARN on each button press instead of
attempting a new connection.

### WR-02: `resetLunBoCursors()` is never called

**Files modified:** `src/app/src/application.cpp`
**Commit:** 1cf047d
**Applied fix:** Added `QObject::connect(m_profileController.get(), &ProfileController::profileChanged, m_builtinActions.get(), &BuiltinActionsService::resetLunBoCursors)` in the Application constructor after the existing `repaintFromProfile` connection. Both members are fully constructed by the time the constructor body runs.

### IN-01: `kOpRequestResponse` handler emits `requestSucceeded` without checking result

**Files modified:** `src/app/src/obs_client.cpp`
**Commit:** 245ded8
**Applied fix:** Added `QJsonObject const status = d.value("requestStatus").toObject()` and
`bool const ok = status.value("result").toBool(true)` (default true = permissive for forward-compat).
`requestSucceeded` is only emitted when `ok` is true; on failure a WARN is logged with the requestId
and integer error code. All 9 `[obs-client]` tests still pass.

### IN-02: `page.change` silently defaults to CW on any non-`"CCW"` direction string

**Files modified:** `src/app/src/builtin_actions_service.cpp`
**Commit:** 9d45a6e
**Applied fix:** Replaced the single ternary with an explicit three-way check:
`"CCW"` sets `navDir = -1`; `"CW"` (and the absent/empty case) keeps `navDir = +1`;
any other non-empty string logs `AJAZZ_LOG_WARN` with the unknown value before defaulting
to CW. All 35 `[builtin-actions]` tests pass.

______________________________________________________________________

## Build and Test Verification

```
cmake --build build/linux-release: all targets built cleanly (app + ajazz_qml_tests)
ctest --preset linux-release: 100% tests passed, 0 tests failed out of 594
```

Test count increased from 591 (pre-fix baseline) to 594 (+3 new CR-01 regression tests).

______________________________________________________________________

_Fixed: 2026-05-24T20:20:00Z_
_Fixer: Claude (gsd-code-fixer)_
_Iteration: 1_
