---
phase: 31-actioninstance-core-model-profile-schema-v2
plan: 02
subsystem: core
tags: [profile, json, serialization, actioninstance, test, roundtrip, migration, bind-02]

# Dependency graph
requires:
  - phase: 31-01
    provides: the ActionInstance/ActionState model + hand-rolled serializer (writeActionInstance/readActionInstance) + KeyState relocation to action_instance.hpp that this plan verifies
provides:
  - '[action_instance]-tagged Catch2 verification suite proving the 31-01 serializer round-trips losslessly across 0/1/3-state + 2-children variants'
  - regression guard locking the v1->v2 singular-state fold, the no-instance -> nullopt contract, and the out-of-range currentState clamp
  - ctest --preset linux-release -R action_instance runnable target (7 cases)
affects: [32-multi-toggle-dispatch, 33-property-inspector, profile-serialization]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - 'Round-trip idiom: profileFromJson(profileToJson(p)) for constructed-Profile cases; literal JSON inputs for the reader-only migration/nullopt/clamp cases'
    - Test source joins the monolithic ajazz_unit_tests target (one source-list line; ajazz::core + Catch2 already linked; catch_discover_tests auto-registers by name)

key-files:
  created:
    - tests/unit/test_action_instance.cpp
  modified:
    - tests/unit/CMakeLists.txt

key-decisions:
  - Used the actual 31-01 field name ActionInstance::id (the RESEARCH skeleton's inst.actionUuid was pre-implementation shorthand; 31-01 shipped id with the reader also accepting uuid)
  - currentState clamp and 0-state cases assert currentState == 0 because the 31-01 reader clamps when states is empty OR currentState >= states.size()

patterns-established:
  - 'Pattern: a verification-only plan adds zero production code and exactly one CMake source-list line; all behavior is asserted through the public profileToJson/profileFromJson API'

requirements-completed: [BIND-02]

# Metrics
duration: 7min
completed: 2026-06-08
---

# Phase 31 Plan 02: ActionInstance Round-Trip + Migration Verification Summary

**A 7-case [action_instance]-tagged Catch2 suite that proves the 31-01 ActionInstance serializer round-trips losslessly (0/1/3-state + 2-children), folds a legacy singular `state:` into a `states[]` array of one, returns `std::nullopt` for an absent `instance` key on both Binding and EncoderBinding, and clamps an out-of-range `currentState` to 0 — registered as a runnable `ctest -R action_instance` target with no production-code change.**

## Performance

- **Duration:** 7 min
- **Started:** 2026-06-08
- **Completed:** 2026-06-08
- **Tasks:** 2
- **Files modified:** 2 (1 created, 1 modified)

## Accomplishments

- New `tests/unit/test_action_instance.cpp` with 7 `[action_instance]`-tagged `TEST_CASE`s covering every CONTEXT-mandated variant: 0-state (asserts `"states":[]` on the wire + clamp to 0), 1-state (text preserved), 3-state (`currentState == 2`, escaped `settings` byte-equal, inner state text), 2-children (recursive Multi Action), v1->v2 singular-state fold (re-emits `"states":[`), no-instance -> `std::nullopt` on both `Binding` and `EncoderBinding`, and `currentState:99` -> clamp to 0.
- Registered the source in the monolithic `ajazz_unit_tests` target (one source-list line + a Phase 31 comment) so `ctest --preset linux-release -R action_instance` selects the 7 cases by name via `catch_discover_tests`; no new target or `target_link_libraries` needed.
- All TEST_CASE titles are ASCII-only (uses `-` and `->`), satisfying the cross-platform ctest-filter rule.
- Round-trip cases drive the public `profileToJson`/`profileFromJson` API; migration/nullopt/clamp cases feed literal JSON across the reader boundary so the legacy singular `state`, the missing `instance` key, and the out-of-range index are exercised directly on the parser.

## Task Commits

Each task was committed atomically:

1. **Task 1: Write the [action_instance] round-trip + migration + nullopt + clamp tests** - `98ccd84` (test)
1. **Task 2: Register the test source and run the suite green** - `d70ba55` (test)

## Files Created/Modified

- `tests/unit/test_action_instance.cpp` (created) - 7 `[action_instance]`-tagged Catch2 cases; mirrors the `test_profile_serialization.cpp` round-trip and v1-migration idioms.
- `tests/unit/CMakeLists.txt` (modified) - one source-list line (`test_action_instance.cpp`) appended next to the sibling profile tests, with a Phase 31 (BIND-01/BIND-02) comment.

## Verification Results

- `ctest --preset linux-release -R action_instance`: **7/7 passed** (Tests 296-302), exit 0, >0 matched.
- Regression guard `ctest --preset linux-release -R "profile|action_instance"`: **41/41 passed**, exit 0 — no v1 profile-serialization regression (BIND-02 backward-compat holds).
- Phase gate full `ctest --preset linux-release`: **708/708 passed** (701 prior + 7 new), exit 0 — no regression across the whole suite.
- COD-031 boundary: `grep -rn '#include.*nlohmann' src/core/include/` returns **0 lines** — boundary held.
- CMake entry count: `grep -c 'test_action_instance.cpp' tests/unit/CMakeLists.txt` == **1** (single source-list entry, no duplicate target).

## Decisions Made

- **Field name `id` not `actionUuid`:** the RESEARCH test skeleton used `inst.actionUuid`, but 31-01 shipped the field as `ActionInstance::id` (wire key `"id"`; the reader also accepts `"uuid"`). The tests use the actual shipped field.
- **0-state asserts currentState == 0:** the 31-01 reader clamps `currentState` to 0 when `states` is empty (not only when the index is out of range), so the 0-state and clamp cases both assert 0.

## Deviations from Plan

None - plan executed exactly as written. Both tasks landed with no auto-fixes; the 31-01 serializer behaved exactly as the model contract specified (no serializer defect found, the expected green outcome).

## Issues Encountered

- The first commit attempt for Task 1 tripped the clang-format pre-commit hook (it rewrapped one long TEST_CASE title line and reported "files were modified by this hook"). Resolved with the normal re-add-and-recommit dance; never used `--no-verify`.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- BIND-02 is verified: the ActionInstance model + serializer is proven round-trip-safe and migration-safe. Phase 32 (Multi/Toggle dispatch + wire + editor) can build dispatch on top of the model knowing the wire format is locked by these regression tests.
- Phase 33 (Property Inspector round-trip) depends on Phase 31 only and is now unblocked on the model side.
- No blockers.

## Self-Check: PASSED

- FOUND: tests/unit/test_action_instance.cpp
- FOUND: tests/unit/CMakeLists.txt (test_action_instance.cpp registered)
- FOUND: .planning/phases/31-actioninstance-core-model-profile-schema-v2/31-02-SUMMARY.md
- FOUND commit: 98ccd84 (Task 1)
- FOUND commit: d70ba55 (Task 2)
- ctest -R action_instance: 7/7 pass; full suite 708/708 pass; COD-031 grep 0 lines

______________________________________________________________________

*Phase: 31-actioninstance-core-model-profile-schema-v2*
*Completed: 2026-06-08*
