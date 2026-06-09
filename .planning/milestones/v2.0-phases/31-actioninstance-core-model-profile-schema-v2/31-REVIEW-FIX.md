---
phase: 31-actioninstance-core-model-profile-schema-v2
fixed_at: 2026-06-08T00:00:00Z
review_path: .planning/phases/31-actioninstance-core-model-profile-schema-v2/31-REVIEW.md
iteration: 1
findings_in_scope: 4
fixed: 4
skipped: 0
status: all_fixed
---

# Phase 31: Code Review Fix Report

**Fixed at:** 2026-06-08T00:00:00Z
**Source review:** .planning/phases/31-actioninstance-core-model-profile-schema-v2/31-REVIEW.md
**Iteration:** 1

**Summary:**

- Findings in scope: 4 (CR-01, WR-01, WR-02, WR-04)
- Fixed: 4
- Skipped: 0

Out-of-scope (per the fix brief, deliberately untouched): WR-03 (clamp-to-0 is
a LOCKED design decision), WR-05 (pre-existing std::stoul map-key behaviour
shared by keys/encoders, not introduced by Phase 31), IN-01/IN-02/IN-03 (info).

## Fixed Issues

### CR-01: Unbounded recursion in the JSON reader (stack-overflow DoS)

**Files modified:** `src/core/src/profile.cpp`
**Commit:** 61bb6c6
**Applied fix:** Added a `depth_` counter to `JsonReader` plus an RAII
`DepthGuard` (non-copyable/non-movable) that increments on construction and
`fail()`s past `kMaxDepth = 64`, throwing the parser's usual
`std::runtime_error` instead of overflowing the stack. Guarded BOTH recursion
sites: `readActionInstance` (the recursive `children` loop) and `skipValue`
(nested unknown object/array descent). 64 is deep enough for any legitimate
Multi Action nesting, far short of a stack overflow.

### WR-01: escape() emits raw control chars 0x00-0x1F -> invalid JSON

**Files modified:** `src/core/src/profile.cpp`
**Commit:** 9a6268d
**Applied fix:** Added named `\b` and `\f` escapes and a `default`-branch
`\u00XX` (lowercase, zero-padded via `snprintf`) for every byte `< 0x20`. The
byte is tested as `unsigned char` so 0x80..0xFF UTF-8 continuation bytes are not
misclassified as control characters. The in-house reader already decodes
`\u00XX` back to the original byte (incl. NUL), so the round-trip stays lossless.

### WR-02: Legacy state/states fold is key-order-dependent, can drop data

**Files modified:** `src/core/src/profile.cpp`
**Commit:** 2d9c6af
**Applied fix:** Added a `sawStatesKey` flag tracking PRESENCE (not emptiness).
The v2 `states` array always wins: reading `states` clears any previously folded
legacy state and sets the flag; the legacy singular `state` is folded into
`states[]` only when no `states` key was seen, otherwise it is read-and-discarded
so it can never overwrite an already-populated array. Outcome is now identical in
both key orders. Precedence documented inline.

### WR-04: Round-trip tests don't assert byte-stable double-serialise / deep nesting

**Files modified:** `tests/unit/test_action_instance.cpp`
**Commit:** 33b4630
**Applied fix:** Added serialize-parse-serialize byte-stability `REQUIRE(j1 == j2)`
assertions to the 3-state and 2-children round-trip cases; a positive
shallow-nesting parse test (8 levels) and two deep-nesting REJECTION tests
(`REQUIRE_THROWS_AS(..., std::runtime_error)`) that exercise CR-01's guard via
both the `children` path and an unknown key routed through `skipValue`; WR-02
precedence tests (states[] wins over a legacy state in BOTH key orders); and a
WR-01 control-char escape + lossless round-trip test (NUL/bell/unit-separator).
All TEST_CASE titles are ASCII-only.

## Verification

- `cmake --build --preset linux-release --target ajazz_unit_tests` -> exit 0
  (built clean under -Werror; the only environment build blocker is a known
  GCC-16 + Qt-6.11 `-Wnull-dereference` false positive in the unrelated,
  untouched `src/app/src/profile_controller.cpp` -- the same one CI already
  silences -- which was downgraded for the LOCAL test-binary build only; no
  committed file was changed for it, and both edited files compile clean under
  full `-Werror`).
- `ctest --preset linux-release -R action_instance` -> 12/12 passed (was 7
  before; +5 new cases).
- `ctest --preset linux-release -R profile` -> 34/34 passed (no regression).
- COD-031: `grep -rq '#include.*nlohmann' src/core/include/` returns nothing
  (boundary still holds).

______________________________________________________________________

_Fixed: 2026-06-08T00:00:00Z_
_Fixer: Claude (gsd-code-fixer)_
_Iteration: 1_
