---
phase: 31-actioninstance-core-model-profile-schema-v2
plan: 01
subsystem: core
tags: [profile, json, serialization, actioninstance, streamdeck, opendeck, schema, cod-031]

# Dependency graph
requires:
  - phase: 26-device-editor (touchZones)
    provides: the presence-discriminator lazy-migration pattern (gate on key presence, never _schemaVersion) that this plan follows
provides:
  - nlohmann-free/Qt-free ActionInstance + ActionState core data model (action_instance.hpp)
  - std::optional<ActionInstance> instance field on Binding and EncoderBinding (additive)
  - hand-rolled writeActionInstance/readActionInstance + writeActionState/readActionState serializers
  - lazy v1->v2 fold of legacy singular "state" into states[] on read
  - PROFILE_SCHEMA.md ActionInstance/ActionState $defs + instance wire key
affects: [32-multi-toggle-dispatch, 33-property-inspector, profile-serialization]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - Hand-rolled JSON serializer pair mirroring writeKeyState/readKeyState in profile.cpp anon namespace
    - Additive optional nested object on a binding, gated on key presence (no schema-version field)
    - 'Lazy v1->v2 read fold: legacy singular key folds into array-of-one; writer always emits the new array form'
    - Recursive std::vector<ActionInstance> children (incomplete-type vector, no unique_ptr indirection)

key-files:
  created:
    - src/core/include/ajazz/core/action_instance.hpp
  modified:
    - src/core/include/ajazz/core/profile.hpp
    - src/core/src/profile.cpp
    - docs/protocols/PROFILE_SCHEMA.md

key-decisions:
  - Relocated KeyState into action_instance.hpp (depends only on capabilities.hpp) so profile.hpp includes it one-directionally; the plan's planned two-way include cycled and would not compile
  - ActionState is a thin struct wrapping KeyState (forward-compatible seam at zero wire cost), per RESEARCH A2
  - ActionInstance carries an optional id (wire key id; reader also accepts uuid), per RESOLVED open question A1
  - settings stored as an escaped JSON string mirroring Action::settingsJson, never a nested object

patterns-established:
  - 'Pattern: serializer helpers compose existing primitives (writeKeyState/escape/readUInt) rather than re-implementing parsing machinery'
  - 'Pattern: defensive read clamp (currentState >= states.size() -> 0) for lossless load, never reject'

requirements-completed: [BIND-01]

# Metrics
duration: 6min
completed: 2026-06-08
---

# Phase 31 Plan 01: ActionInstance Core Model + Profile Schema v2 Summary

**OpenDeck/StreamDeck-shaped ActionInstance/ActionState core model with hand-rolled, nlohmann-free JSON serialization wired additively onto Binding/EncoderBinding, plus lazy v1->v2 state-fold and the PROFILE_SCHEMA.md $defs that document the wire keys.**

## Performance

- **Duration:** 6 min
- **Started:** 2026-06-08T22:45:56Z
- **Completed:** 2026-06-08T22:51:01Z
- **Tasks:** 3
- **Files modified:** 4 (1 created, 3 modified)

## Accomplishments

- New installed public core header `action_instance.hpp` defining `ActionInstance` (id/states/currentState/settings/children) and `ActionState{KeyState visual}` — nlohmann-free and Qt-free, satisfying COD-031.
- `Binding` and `EncoderBinding` each gained an additive `std::optional<ActionInstance> instance`; the legacy action chains and `KeyState state` are untouched.
- Four hand-rolled serializer helpers (`writeActionInstance`/`readActionInstance` + `writeActionState`/`readActionState`) wired into all four binding I/O functions; the writer always emits `states[]`, the reader folds a legacy singular `"state"` into a one-element array (lazy v1->v2, BIND-02 groundwork), gates purely on key presence, and defensively clamps `currentState`.
- `PROFILE_SCHEMA.md` updated with `$defs.ActionInstance` + `$defs.ActionState`, the `instance` key on both binding $defs, wire-key table rows, and a compatibility-policy note on the legacy-state fold.

## Task Commits

Each task was committed atomically:

1. **Task 1: Define the ActionInstance / ActionState core header** - `4b5977e` (feat)
1. **Task 2: Add additive optional<ActionInstance> instance to bindings** - `7d5286b` (feat)
1. **Task 3: Hand-roll the serializer + wire into binding I/O + schema doc** - `3fda790` (feat; includes the Rule 3 include-cycle fix)

## Files Created/Modified

- `src/core/include/ajazz/core/action_instance.hpp` (created) - ActionInstance/ActionState structs + KeyState (relocated here); stdlib + capabilities.hpp only.
- `src/core/include/ajazz/core/profile.hpp` (modified) - includes action_instance.hpp one-directionally; adds optional<ActionInstance> instance to Binding/EncoderBinding; KeyState removed (now in action_instance.hpp).
- `src/core/src/profile.cpp` (modified) - four new serializer helpers + instance wiring in writeBinding/writeEncoderBinding/readBinding/readEncoderBinding; legacy state fold + currentState clamp.
- `docs/protocols/PROFILE_SCHEMA.md` (modified) - ActionInstance/ActionState $defs, instance key, wire-key rows, compatibility note.

## Decisions Made

- **KeyState relocation (Rule 3 deviation):** The plan specified a two-way include (profile.hpp ⇄ action_instance.hpp) broken only by `#pragma once` + ordering. This does not compile: when a TU includes `action_instance.hpp` first, it re-enters `profile.hpp`, whose `Binding` references `ActionInstance` before that struct is defined (re-entrant `#pragma once` skip). The plan explicitly sanctioned the alternative of moving KeyState's dependency. I moved the full `KeyState` definition into `action_instance.hpp` (which then needs only `capabilities.hpp` for `Rgb`); `profile.hpp` includes `action_instance.hpp` one-directionally and reuses `KeyState`. No behavior change; ajazz_core compiles clean and all 34 profile tests pass.
- ActionState is a thin wrapper struct; ActionInstance carries an optional `id` (both as RESEARCH recommended/RESOLVED).

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] Relocated KeyState to break the include cycle**

- **Found during:** Task 3 (build of ajazz_core after adding the instance fields)
- **Issue:** The plan's two-way include design (`action_instance.hpp` includes `profile.hpp` for KeyState; `profile.hpp` includes `action_instance.hpp` for ActionInstance) cycles under `#pragma once`: a TU that includes `action_instance.hpp` first re-enters a half-parsed `profile.hpp` where `Binding`/`EncoderBinding` reference `ActionInstance` before it is declared. Compile error: "'ActionInstance' was not declared in this scope".
- **Fix:** Moved the `KeyState` struct definition from `profile.hpp` into `action_instance.hpp` (which now depends only on `capabilities.hpp`). `profile.hpp` includes `action_instance.hpp` one-directionally and reuses `KeyState`. This is the alternative the plan's Task 1 NOTE explicitly permits ("move ONLY KeyState's required dependency").
- **Files modified:** src/core/include/ajazz/core/action_instance.hpp, src/core/include/ajazz/core/profile.hpp
- **Verification:** `cmake --build --preset linux-release --target ajazz_core` exits 0; `ctest --preset linux-release -R profile` 34/34 pass; COD-031 grep gate (`! grep -rq '#include.*nlohmann' src/core/include/`) exits 0.
- **Committed in:** 4b5977e (header), 7d5286b (profile.hpp field), 3fda790 (final KeyState relocation + wiring)

______________________________________________________________________

**Total deviations:** 1 auto-fixed (1 blocking)
**Impact on plan:** The fix was necessary for the code to compile and was an explicitly-sanctioned alternative in the plan. No scope creep; the model, wire format, and acceptance criteria are unchanged.

## Issues Encountered

- The include-cycle was the only build issue; resolved via the KeyState relocation above. No other problems.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- The ActionInstance model + serializer + schema doc are in place and round-trip-safe by construction (existing profile tests green). Tests specific to ActionInstance round-trip/migration land in **31-02 (Wave 2)**, which depends on this serializer.
- Phase 32 (Multi/Toggle dispatch, device editor) and Phase 33 (Property Inspector) can build on `ActionInstance`.
- No blockers.

## Self-Check: PASSED

- FOUND: src/core/include/ajazz/core/action_instance.hpp
- FOUND: .planning/phases/31-actioninstance-core-model-profile-schema-v2/31-01-SUMMARY.md
- FOUND commit: 4b5977e (Task 1)
- FOUND commit: 7d5286b (Task 2)
- FOUND commit: 3fda790 (Task 3)
- COD-031 gate: `! grep -rq '#include.*nlohmann' src/core/include/` exits 0

______________________________________________________________________

*Phase: 31-actioninstance-core-model-profile-schema-v2*
*Completed: 2026-06-08*
