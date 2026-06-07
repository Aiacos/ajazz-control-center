---
phase: 31-actioninstance-core-model-profile-schema-v2
verified: 2026-06-08T01:40:00Z
status: passed
score: 6/6 must-haves verified
overrides_applied: 0
re_verification:
  previous_status: none
  previous_score: n/a
---

# Phase 31: ActionInstance Core Model + Profile Schema v2 Verification Report

**Phase Goal:** The `ActionInstance` / `ActionState` data model exists in `src/core/` with hand-rolled serialization; existing profiles load unchanged; the schema supports Multi Action children and per-state images.
**Verified:** 2026-06-08T01:40:00Z
**Status:** passed
**Re-verification:** No — initial verification

## Goal Achievement

### Observable Truths

| #   | Truth                                                                                                                    | Status     | Evidence                                                                                                                                                                                                                                                                                                                                     |
| --- | ------------------------------------------------------------------------------------------------------------------------ | ---------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1   | COD-031 boundary preserved — new nlohmann-free public header `action_instance.hpp` (ROADMAP SC1)                         | ✓ VERIFIED | `grep -rn '#include.*nlohmann' src/core/include/` → 0 matches (exit 1). The only `nlohmann` token in action_instance.hpp is the doc comment "nlohmann-free" at line 15, not an include. Header is also Qt-free (Qt include grep exit 1).                                                                                                     |
| 2   | Catch2 round-trip tests pass for 0/1/3-state and 2-children ActionInstance variants (ROADMAP SC2)                        | ✓ VERIFIED | `ctest --preset linux-release -R action_instance` → **12/12 passed, exit 0**. Cases 297-299 cover 0/1/3-state; case 299 covers 2-children with recursion + byte-stable double-serialise (WR-04).                                                                                                                                             |
| 3   | Existing v1 profile with singular `state:` key loads and round-trips as `states[]` of one (ROADMAP SC3)                  | ✓ VERIFIED | Dedicated migration test "action_instance folds a legacy singular state into a states array of one" passes (exit 0); reads `inst.states.size()==1`, then asserts re-serialised JSON contains `"states":[`. Reader fold logic at profile.cpp:868-878; writer emits only `"states":[` (line 293), never singular `"state"` inside an instance. |
| 4   | `Binding` and `EncoderBinding` carry `std::optional<ActionInstance> instance`; absent key → `std::nullopt` (ROADMAP SC4) | ✓ VERIFIED | `grep -c 'std::optional<ActionInstance> instance' profile.hpp` == 2 (both structs, lines 103 + 120). Nullopt test passes for both Binding (`p.keys.at(0).instance == std::nullopt`) and EncoderBinding (`p.encoders.at(0).instance == std::nullopt`). Legacy `KeyState state;` still present (grep ==3) → additive, not replacing.           |
| 5   | profileToJson always emits instance as `states[]`; reader gates on key presence not `_schemaVersion` (PLAN truth)        | ✓ VERIFIED | writeActionInstance body (profile.cpp:278-326) emits `"states":[` only, no singular `"state":`. `awk '/ActionInstance readActionInstance/,/^}/' \| grep -c schemaVersion` == 0. Reader dispatches purely on key presence; `state`/`states` precedence (WR-02) handled by `sawStatesKey` flag.                                                |
| 6   | PROFILE_SCHEMA.md documents ActionInstance/ActionState $defs + instance key (PLAN truth)                                 | ✓ VERIFIED | `$defs.ActionInstance` (line 81), `$defs.ActionState` (line 110), `instance` `$ref` added to both $defs.Binding (line 67) and $defs.EncoderBinding (line 78); wire-key table rows for `settings`/`id` (lines 16-17); v1→v2 fold compatibility note (line 233).                                                                               |

**Score:** 6/6 truths verified

### Required Artifacts

| Artifact                                          | Expected                                                                | Status     | Details                                                                                                                                                                                              |
| ------------------------------------------------- | ----------------------------------------------------------------------- | ---------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `src/core/include/ajazz/core/action_instance.hpp` | ActionInstance + ActionState (stdlib + KeyState only)                   | ✓ VERIFIED | Defines `struct ActionState{KeyState visual;}` and `struct ActionInstance{id, states, currentState, settings, children}`. nlohmann-free, Qt-free. Includes only capabilities.hpp + 4 stdlib headers. |
| `src/core/include/ajazz/core/profile.hpp`         | optional<ActionInstance> instance on Binding + EncoderBinding           | ✓ VERIFIED | Both structs carry the field (grep ==2); includes action_instance.hpp one-directionally (line 28); legacy `state` + chains untouched.                                                                |
| `src/core/src/profile.cpp`                        | write/read ActionInstance + ActionState helpers wired into binding I/O  | ✓ VERIFIED | All 4 helpers present; writeBinding/writeEncoderBinding emit optional instance (lines 228-231, 247-250); readBinding/readEncoderBinding parse `instance` key (lines 929-930, 959-960).               |
| `tests/unit/test_action_instance.cpp`             | [action_instance]-tagged round-trip + migration + nullopt + clamp tests | ✓ VERIFIED | 12 TEST_CASEs, all [action_instance]-tagged, ASCII-only titles (grep exit 1). Registered in CMakeLists.txt:188.                                                                                      |
| `docs/protocols/PROFILE_SCHEMA.md`                | ActionInstance/ActionState $defs + instance wire key                    | ✓ VERIFIED | See truth #6.                                                                                                                                                                                        |

### Key Link Verification

| From                             | To                      | Via                          | Status  | Details                              |
| -------------------------------- | ----------------------- | ---------------------------- | ------- | ------------------------------------ |
| profile.hpp                      | action_instance.hpp     | `#include`                   | ✓ WIRED | grep ==1 at line 28                  |
| writeBinding/writeEncoderBinding | writeActionInstance     | optional emit                | ✓ WIRED | lines 228-231, 247-250               |
| readBinding/readEncoderBinding   | readActionInstance      | `key == "instance"` dispatch | ✓ WIRED | lines 929-930, 959-960               |
| test_action_instance.cpp         | ajazz_unit_tests target | CMakeLists source-list entry | ✓ WIRED | CMakeLists.txt:188; tests run & pass |

### Behavioral Spot-Checks

| Behavior                    | Command                                                      | Result                 | Status |
| --------------------------- | ------------------------------------------------------------ | ---------------------- | ------ |
| action_instance suite green | `ctest --preset linux-release -R action_instance`            | 12/12 passed, exit 0   | ✓ PASS |
| no v1 regression            | `ctest --preset linux-release -R "profile\|action_instance"` | 46/46 passed, exit 0   | ✓ PASS |
| full suite                  | `ctest --preset linux-release`                               | 713/713 passed, exit 0 | ✓ PASS |
| COD-031 boundary            | `grep -rn '#include.*nlohmann' src/core/include/`            | 0 matches (exit 1)     | ✓ PASS |

### Code Review Fix Confirmation

CR-01 (Critical) + WR-01/WR-02/WR-04 (Warnings) fixed; independently confirmed:

| Finding                          | Commit  | Verified                                                                                                                                                                                 |
| -------------------------------- | ------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| CR-01 unbounded reader recursion | 61bb6c6 | ✓ `JsonReader::DepthGuard` (RAII, kMaxDepth=64) applied to readActionInstance (line 838) AND skipValue (line 620); reject tests for 4096-deep children + unknown-key-skipValue both pass |
| WR-01 raw control chars          | 9a6268d | ✓ control-char escape test passes (NUL/bell/unit-sep → `\u00XX`, lossless round-trip)                                                                                                    |
| WR-02 order-dependent fold       | 2d9c6af | ✓ `sawStatesKey` precedence; both-key-order precedence test passes                                                                                                                       |
| WR-04 byte-stable round-trip     | 33b4630 | ✓ `REQUIRE(j1==j2)` on 3-state + 2-children cases pass                                                                                                                                   |

WR-03 (clamp-to-0) is a LOCKED design decision; WR-05 deferred (pre-existing, not introduced by Phase 31). Confirmed consistent with 31-REVIEW-FIX.md.

### Requirements Coverage

| Requirement | Source Plan | Description                                                                                                       | Status      | Evidence                                                                  |
| ----------- | ----------- | ----------------------------------------------------------------------------------------------------------------- | ----------- | ------------------------------------------------------------------------- |
| BIND-01     | 31-01-PLAN  | ActionInstance/ActionState core model + hand-rolled serialization, no nlohmann; Binding carries optional instance | ✓ SATISFIED | Truths 1, 4, 5; artifacts action_instance.hpp + profile.hpp + profile.cpp |
| BIND-02     | 31-02-PLAN  | v1→v2 lossless backward-compatible reader; round-trip tests for 0/1/3-state + 2-children                          | ✓ SATISFIED | Truths 2, 3; test_action_instance.cpp 12/12                               |

Both requirement IDs from PLAN frontmatter are present in REQUIREMENTS.md mapped to Phase 31 (lines 24-25, traceability 107-108). No orphaned requirements.

### Anti-Patterns Found

| File                     | Line          | Pattern                                | Severity                 | Impact                                                                                                                                                                           |
| ------------------------ | ------------- | -------------------------------------- | ------------------------ | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| src/core/src/profile.cpp | 543, 547, 561 | `\uXXXX` text in comment/error strings | ℹ️ Info (false positive) | NOT debt markers — these are references to the JSON `\uXXXX` unicode-escape syntax in pre-Phase-31 reader code (commit 4a47f1a). Not introduced by this phase; no action needed. |

No `TODO`/`FIXME`/`TBD`/`HACK`/`PLACEHOLDER` debt markers in any Phase 31 file. No stubs, empty returns, or hollow implementations.

### Gaps Summary

None. All 6 must-haves and all 4 ROADMAP success criteria are verified against the live tree by reading the actual source and running the actual `ctest` commands (not trusting SUMMARY claims). The data model exists, serializes without nlohmann (COD-031 held), existing v1 profiles fold losslessly to `states[]` of one, both Binding and EncoderBinding carry the additive optional instance with clean nullopt behavior, and the schema doc is updated. The CR-01 critical recursion-DoS fix and 3 warnings are independently confirmed via their commits and passing tests. Full suite: 713/713.

Phase goal achieved. Ready to proceed to Phase 32.

______________________________________________________________________

_Verified: 2026-06-08T01:40:00Z_
_Verifier: Claude (gsd-verifier)_
