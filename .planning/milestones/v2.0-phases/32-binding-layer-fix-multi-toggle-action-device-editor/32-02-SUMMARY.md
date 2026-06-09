---
phase: 32-binding-layer-fix-multi-toggle-action-device-editor
plan: 02
subsystem: app
tags: [multi-action, dispatch, input-service, builtin-registry, bind-04, bind-05]

# Dependency graph
requires:
  - phase: 32-binding-layer-fix-multi-toggle-action-device-editor
    plan: 01
    provides: instanceChildrenToChain(parent) adapter + ActionInstance.delayMs (delay-carrying flat ActionChain the ActionEngine already walks)
provides:
  - Multi Action dispatch at the StreamDockInputService::dispatch seam (KeyPressed, EncoderPressed, touch-tap zone) -- a firing Binding/EncoderBinding whose instance is a Multi Action runs instance.children sequentially via instanceChildrenToChain + ActionEngine::run, honoring each child's delayMs, with no new async runner and no SKU branching
  - BuiltinActionRegistry::kMultiActionId constant (com.hotspot.streamdock.multiaction)
  - com.hotspot.streamdock.multiaction built-in registration (classification so handles() is true + flat-JSON {"actions":[...]} fallback)
  - BuiltinActionsService::handles(id) classification accessor
affects: [stream_dock_input_service, builtin_actions_service, builtin_action_registry]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - Multi Action dispatch resolved at the seam where Binding.instance + key index are in scope (the opaque BuiltinHandler(string_view) cannot see the binding -- RESEARCH Findings 2+3, Q4); the typed children path lives at StreamDockInputService::dispatch, the registry entry is classification + flat-JSON fallback only
    - 'Id-match classification (id == kMultiActionId) over a bare children-non-empty test for OpenDeck parity: a Multi Action is identified by its action UUID and stays in lock-step with BuiltinActionRegistry::handles()'

key-files:
  created: []
  modified:
    - src/core/include/ajazz/core/builtin_action_registry.hpp
    - src/app/src/stream_dock_input_service.cpp
    - src/app/src/builtin_actions_service.hpp
    - src/app/src/builtin_actions_service.cpp
    - tests/unit/test_stream_dock_input_service.cpp
    - tests/unit/test_builtin_actions.cpp

key-decisions:
  - Multi Action id is com.hotspot.streamdock.multiaction (the REAL dispatch prefix); the OpenDeck opendeck.multiaction id would fail handles() and never fire (RESEARCH A3/Q2). The only opendeck.* occurrences in the code are comments warning AGAINST that id.
  - Dispatch resolved at the input-service seam via a template runPressBinding(engine, binding, legacyChain) helper, reused for KeyPressed, EncoderPressed and the touch-tap zone path; legacy onPress chains (no instance) fire unchanged.
  - Reused ActionEngine::run for sequencing -- the engine already defers per-step delayMs through its executor, so no new async runner was introduced.
  - Added a public BuiltinActionsService::handles() accessor (delegates to the registry) so callers/tests can ask whether a UUID dispatches in-process without reaching into the private registry.

requirements-completed: [BIND-04, BIND-05]

# Metrics
duration: 22min
completed: 2026-06-08
---

# Phase 32 Plan 02: Multi Action dispatch at the input-service seam Summary

**A key/dial/touch-zone bound to a Multi Action now runs its `instance.children` sequentially on a single press -- resolved at the `StreamDockInputService::dispatch` seam via Wave-1's `instanceChildrenToChain` fed to the existing `ActionEngine::run` (per-child `delayMs` honored, no new async runner) -- plus a `com.hotspot.streamdock.multiaction` built-in registration (classification + flat-JSON fallback).**

## Performance

- **Duration:** ~22 min
- **Completed:** 2026-06-08
- **Tasks:** 2 code/test tasks (both TDD RED+GREEN); 1 checkpoint:human-verify deferred to the consolidated live pass.
- **Files modified:** 6

## Accomplishments

- **Multi Action dispatch (BIND-04/05):** `StreamDockInputService::dispatch` now resolves a firing `Binding`/`EncoderBinding` whose `instance` is a Multi Action (id == `com.hotspot.streamdock.multiaction`) by flattening `instance.children` through `core::instanceChildrenToChain(*instance)` and running the result on the shared `ActionEngine`. Children fire in order; each child's `delayMs` is deferred by the engine's executor (the engine already does this -- no new runner). Wired for `KeyPressed`, `EncoderPressed`, and the touch-tap zone path via a single `runPressBinding(engine, binding, legacyChain)` template helper.
- **No regression:** bindings without a Multi Action instance run their legacy `onPress` chain unchanged (asserted by a dedicated test).
- **Built-in registration:** `com.hotspot.streamdock.multiaction` registered in `BuiltinActionsService::populate()` -- (a) classification so `handles()` returns true, (b) a thin flat-JSON `{"actions":[...]}` fallback via `decodeActionChain` for the legacy flat shape. New `kMultiActionId` constant on `BuiltinActionRegistry` keeps the id literal in one place.
- **Classification accessor:** `BuiltinActionsService::handles(id)` exposes the registry's `handles()` so callers/tests can classify a UUID without reaching into the private registry.
- **Invariants held:** no SKU/codename branching in the new dispatch path (BIND-06); COD-031 untouched (app-layer only; no `nlohmann` in core public headers).

## Task Commits

Each code task was TDD (RED test commit -> GREEN implementation commit):

1. **Task 1: Multi Action resolution at the dispatch seam**
   - RED: `0ce820d` (test) -- failing key/encoder Multi Action dispatch + delay tests; adds `kMultiActionId`.
   - GREEN: (folded behavior) -- `feat(32-02): dispatch Multi Action children at the input-service seam` landed and its clang-format wrap was normalized into the next commit's re-add (see Issues).
1. **Task 2: com.hotspot.streamdock.multiaction built-in**
   - RED: `2e17fbc` (test) -- `handles()` accessor + failing classification + flat-JSON fallback tests (also carried the Task-1 GREEN formatting normalization).
   - GREEN: `68cbd7a` (feat) -- registry entry under the correct prefix.

## Files Created/Modified

- `src/core/include/ajazz/core/builtin_action_registry.hpp` - added `static constexpr kMultiActionId = "com.hotspot.streamdock.multiaction"`.
- `src/app/src/stream_dock_input_service.cpp` - `#include` the adapter + registry; anonymous-namespace `isMultiAction()` + `runPressBinding()` helpers; wired into the `KeyPressed`, `EncoderPressed`, and touch-tap zone cases.
- `src/app/src/builtin_actions_service.hpp` - public `handles(id)` classification accessor.
- `src/app/src/builtin_actions_service.cpp` - registered `com.hotspot.streamdock.multiaction` (classification + flat-JSON fallback).
- `tests/unit/test_stream_dock_input_service.cpp` - 4 new `[stream-dock-input][multiaction]` cases (key order, child delay deferral, encoder order, no-instance legacy regression) using the recording-`plugin`/`sleep` executor seam.
- `tests/unit/test_builtin_actions.cpp` - 2 new `[builtin-actions][multiaction]` cases (handles() true, flat-JSON fallback ordered run).

## Verification

- `ctest --preset linux-release -R "multiaction"` -> 8/8 passed.
- `ctest --preset linux-release -R "multiaction|input|builtin|action"` -> 68/68 passed.
- Full suite `ctest --preset linux-release -LE qml` -> **721/721 passed** (no regression; was 712 pre-plan).
- `grep -n 'instanceChildrenToChain' src/app/src/stream_dock_input_service.cpp` matches.
- `grep -n 'com.hotspot.streamdock.multiaction' src/app/src/builtin_actions_service.cpp` matches (correct prefix).
- No `opendeck.` dispatch/registration literal: the only `opendeck.` strings are explanatory comments warning AGAINST that id.
- No SKU branching in the new dispatch block (`isMultiAction`/`runPressBinding` + the three cases contain no `akp03|akp05|akp153`/codename literal; the pre-existing `zoneForX` AKP05 references are untouched).
- COD-031: `! grep -rq '#include.*nlohmann' src/core/include/` exits 0.

## Pending live verification

Task 3 was a `checkpoint:human-verify` (MANDATORY live debug-channel pass per CLAUDE.md -- ctest is necessary but not sufficient). Per sequential-executor rules this was NOT run here (no GUI launch); the exact commands are recorded for the consolidated live pass:

```bash
# 1. Build + launch headless with debug channel + untrusted plugins allowed
AJAZZ_DEBUG_CONTROL=1 AJAZZ_ALLOW_UNTRUSTED_PLUGINS=1 \
  nohup build/linux-release/src/app/ajazz-control-center &

# 2. Select the device, install/bind a 2-child Multi Action to a key.
#    Each child should be a real plugin action; put a delayMs on the SECOND child.
#    (Bind via the editor or profile.* debug RPCs; the firing binding's
#     instance.id must be com.hotspot.streamdock.multiaction with two children.)

# 3. Fire the key (synthetic input -- the demo unit delivers zero real input, Pitfall 2):
scripts/ajazz-debug input.key '{"index":0,"pressed":true}'
scripts/ajazz-debug input.key '{"index":0,"pressed":false}'

# 4. Confirm BOTH children fired in order with the inter-step gap:
scripts/ajazz-debug plugin.protocolLog
```

**Expected:** `plugin.protocolLog` shows the first child's action dispatched, then ~`delayMs` later the second child's action -- both in chain order, the gap visible between them. If only one child fires, or they fire out of order, or with no gap, the live pass FAILS.

## Decisions Made

- **`com.hotspot.streamdock.multiaction` (singular)** is the canonical id, exposed as `kMultiActionId`. The OpenDeck `opendeck.multiaction` id is deliberately NOT used (would fail `handles()`).
- **Seam-resolved typed children, registry-resolved flat fallback.** The opaque `BuiltinHandler(string_view)` cannot see the firing `Binding`, so typed `instance.children` dispatch lives at `StreamDockInputService::dispatch`; the registry handler is classification + a flat-JSON `{"actions":[...]}` fallback only.
- **Reused `ActionEngine::run`** for sequencing + delay deferral; no new async runner (the engine already defers per-step `delayMs`).
- **Template `runPressBinding`** factors the Multi-Action-or-legacy decision once and reuses it across key/encoder/touch press paths.

## Deviations from Plan

- **[Rule 3 - blocking] clang-format wrap of the Task-1 helper signature.** The pre-commit clang-format hook re-wrapped `runPressBinding`'s signature after the Task-1 GREEN commit (the documented stash/restore conflict). Resolved with the standard re-add dance -- the cosmetic re-wrap was folded into the Task-2 RED commit (`2e17fbc`). No `--no-verify` was used; every commit ran the full hook chain. Behavior unchanged.

## Issues Encountered

- The pre-commit stash/restore conflict surfaced once (Task-2 RED commit) -- clang-format auto-fix conflicting with overlapping staged/unstaged hunks across the already-committed input-service file. Recovered via re-add (re-stage the hook-formatted file, re-commit). Standard for this repo under formatting hooks.

## Threat Surface

No new threat surface beyond the plan's `<threat_model>`:

- T-32-05 (runCommand/openUrl via a Multi Action child): children route through the SAME `ActionEngine` executors as today; the adapter only copies kind/id/settingsJson/delayMs. Accepted, unchanged.
- T-32-06 (nested Multi Action recursion DoS): the seam does NOT re-introduce recursion -- it consumes Wave-1's depth-capped `instanceChildrenToChain` output.

## Next Phase Readiness

- BIND-04/05 (Multi Action dispatch) is delivered at the seam + classified in the registry. Toggle Action (BIND-toggle) and the device editor remain other-plan concerns.
- **One open gate:** the live debug-channel verification (above) must be walked in the consolidated live pass before this is considered hardware-confirmed. ctest coverage proves ordering + delay deferral at the unit seam.

## Self-Check: PASSED

All modified files present on disk; all three task commits (0ce820d, 2e17fbc, 68cbd7a) exist in git history. Scoped suite (68/68) and full suite (721/721, -LE qml) green.

______________________________________________________________________

*Phase: 32-binding-layer-fix-multi-toggle-action-device-editor*
*Completed: 2026-06-08*
