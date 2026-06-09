---
phase: 32-binding-layer-fix-multi-toggle-action-device-editor
plan: 01
subsystem: core
tags: [action-instance, action-chain, multi-action, delayMs, profile-serialization, cod-031]

# Dependency graph
requires:
  - phase: 31-action-instance-model
    provides: ActionInstance / ActionState core model (states[]/currentState/settings/children) + hand-rolled schema-v2 serializer with the CR-01 DepthGuard
provides:
  - ActionInstance.delayMs additive optional field (wire key "delayMs"), serialized omit-when-zero, reader-tolerant, round-trip tested (present / absent->0 / nested)
  - instanceChildrenToChain(parent) adapter converting ActionInstance.children into a flat core::ActionChain (pre-order depth-first, delay-carrying, depth-capped)
affects: [32-02-multi-toggle-dispatch, builtin_action_registry, StreamDockInputService.dispatch]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - 'Multi Action dispatch seam: typed ActionInstance.children flatten into the existing ActionChain the ActionEngine already walks sequentially (no new async runner)'
    - Adapter-side depth cap mirrors the profile reader DepthGuard (kMaxDepth=64); the adapter caps/truncates rather than rejects (it operates on already-parsed data)

key-files:
  created:
    - src/core/include/ajazz/core/action_chain_adapter.hpp
    - src/core/src/action_chain_adapter.cpp
    - tests/unit/test_multiaction_dispatch.cpp
  modified:
    - src/core/include/ajazz/core/action_instance.hpp
    - src/core/src/profile.cpp
    - src/core/CMakeLists.txt
    - tests/unit/test_action_instance.cpp
    - tests/unit/CMakeLists.txt

key-decisions:
  - delayMs serialized omit-when-zero (matching the existing id/settings emit policy) so legacy v2 profiles stay byte-stable; reader defaults absent -> 0
  - instanceChildrenToChain flattens pre-order depth-first (child then its children inline); the top-level parent node is NOT emitted, only its descendants
  - Adapter depth cap = 64 (kMaxDepth) mirroring the reader DepthGuard; beyond-cap nests are silently truncated (T-32-01 DoS mitigation) since the adapter cannot reject already-parsed data

patterns-established:
  - Pure-core adapter (no JSON library, no Qt) bridges the Phase-31 typed model to the Phase-engine ActionChain, preserving the COD-031 boundary

requirements-completed: [BIND-04, BIND-05]

# Metrics
duration: 18min
completed: 2026-06-08
---

# Phase 32 Plan 01: Multi Action core foundation (delayMs + children->ActionChain adapter) Summary

**Additive optional `ActionInstance.delayMs` (serialized, reader-tolerant) plus the pure-core `instanceChildrenToChain` adapter that flattens a Multi Action children tree into the delay-carrying `core::ActionChain` the existing `ActionEngine` already walks.**

## Performance

- **Duration:** ~18 min
- **Completed:** 2026-06-08
- **Tasks:** 2 (both TDD: RED + GREEN)
- **Files modified:** 8 (3 created, 5 modified)

## Accomplishments

- `ActionInstance` now carries an additive optional `delayMs` (uint32, wire key `delayMs`) mirroring `Action::delayMs`; the serializer emits it omit-when-zero and the reader tolerates absence (defaults 0). Round-tripped present / absent->0 / nested-in-children.
- New pure-core `instanceChildrenToChain(parent)` adapter: pre-order depth-first flatten of `ActionInstance.children` into `core::ActionChain`, copying each child's `id`/`settings`/`delayMs` onto a `kind=Plugin` `Action`. This is the missing dispatch seam (Phase 31 was model-only) that Wave 2's Multi Action handler reuses to feed the existing `ActionEngine` sequential walk.
- Depth cap (kMaxDepth=64) mirroring the profile reader's CR-01 DepthGuard truncates a hostile deep nest without overflowing the stack (T-32-01).
- COD-031 boundary held: both new/modified core public headers and the new adapter TU are JSON-library-free and Qt-free.

## Task Commits

Each task was TDD (RED test commit -> GREEN implementation commit):

1. **Task 1: ActionInstance.delayMs + serializer round-trip**
   - RED: `182dece` (test)
   - GREEN: `cb59362` (feat)
1. **Task 2: instanceChildrenToChain adapter (children -> ActionChain)**
   - RED: `1354cee` (test)
   - GREEN: `7f2d29b` (feat)

## Files Created/Modified

- `src/core/include/ajazz/core/action_instance.hpp` - added `std::uint32_t delayMs{0}` (wire key `delayMs`), doc-commented mirroring `Action::delayMs`.
- `src/core/src/profile.cpp` - `writeActionInstance` emits `"delayMs"` only when non-zero; `readActionInstance` parses it via `readUInt` (absent -> 0).
- `src/core/include/ajazz/core/action_chain_adapter.hpp` - declares `instanceChildrenToChain` (pure core).
- `src/core/src/action_chain_adapter.cpp` - implements the depth-first, depth-capped flatten.
- `src/core/CMakeLists.txt` - registered `action_chain_adapter.cpp` in the `ajazz_core` sources.
- `tests/unit/test_action_instance.cpp` - 3 new `[action_instance][delay]` cases.
- `tests/unit/test_multiaction_dispatch.cpp` - new `[multiaction]` suite (5 cases: order, field mapping, empty, depth-first flatten, depth-cap truncation).
- `tests/unit/CMakeLists.txt` - registered the new test TU.

## Verification

- `ctest -R "action_instance|action_engine|multiaction|action_chain"` -> 21/21 passed.
- `ctest -R profile` -> 34/34 passed (no regression).
- Full suite `ctest -LE qml` -> 712/712 passed.
- COD-031: `! grep -rq '#include.*nlohmann' src/core/include/` exits 0 (no match).
- Adapter TU: `! grep -q 'nlohmann\|#include <Q' src/core/src/action_chain_adapter.cpp` exits 0.
- `grep -n 'delayMs' src/core/include/ajazz/core/action_instance.hpp` and `grep -n 'instanceChildrenToChain' .../action_chain_adapter.hpp` both match.

## Decisions Made

- **omit-when-zero for delayMs** to keep legacy v2 profiles byte-stable, consistent with the existing `id`/`settings` emit policy; reader-tolerant absence.
- **parent node not emitted** by the adapter — only descendants flatten (the parent IS the Multi Action container, its children ARE the steps).
- **adapter caps instead of rejecting** — it consumes already-parsed in-memory data (no untrusted byte stream), so truncating at kMaxDepth is the right DoS posture vs the reader's `fail()`.

## Deviations from Plan

None - plan executed exactly as written. Both tasks followed the TDD RED->GREEN flow; no REFACTOR commit was needed (implementations were minimal and clean on first GREEN).

## Issues Encountered

- Two early commit attempts hit the documented pre-commit stash/restore conflict (clang-format auto-fix conflicting with overlapping staged+unstaged hunks). Resolved by the standard re-add dance (re-stage the hook-formatted files and re-commit) — no `--no-verify` was used; every commit ran the full hook chain. The RED+GREEN commits all landed cleanly via re-add.

## Next Phase Readiness

- Wave 2 (Multi/Toggle dispatch) can now call `instanceChildrenToChain` at the `StreamDockInputService::dispatch` seam and feed the result to `ActionEngine::run`, with per-step delay already carried.
- No blockers. The built-in id namespace decision (`com.hotspot.streamdock.*`, per 32-RESEARCH) is a Wave-2 concern and untouched here.

## Self-Check: PASSED

All created files present on disk; all four task commits (182dece, cb59362, 1354cee, 7f2d29b) exist in git history.

______________________________________________________________________

*Phase: 32-binding-layer-fix-multi-toggle-action-device-editor*
*Completed: 2026-06-08*
