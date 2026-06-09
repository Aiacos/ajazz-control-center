---
phase: 32-binding-layer-fix-multi-toggle-action-device-editor
plan: 03
subsystem: app
tags: [toggle-action, dispatch, input-service, profile-controller, plugin-device-bridge, bind-05, bind-07]

# Dependency graph
requires:
  - phase: 32-binding-layer-fix-multi-toggle-action-device-editor
    plan: 02
    provides: the StreamDockInputService::dispatch seam pattern (firing Binding/EncoderBinding + index in scope) + kMultiActionId + the kBuiltinPrefix classification idiom
provides:
  - Toggle Action dispatch at the StreamDockInputService::dispatch seam (KeyPressed, EncoderPressed, touch-tap zone) -- a firing Binding/EncoderBinding whose instance is a Toggle Action (id == kToggleActionId) cycles currentState = (currentState + 1) mod N over ALL states (N>2), renders states[currentState] via the existing repaint path, emits a state-change willAppear, and PERSISTS the new index
  - ProfileController::cycleInstanceState(controller, index) mutator -- advances a binding's instance.currentState mod N, persists via saveActiveProfile(), emits profileChanged()
  - PluginDeviceBridge::renderToggleState(controller, index, instance) -- repaints states[currentState].visual via the existing assignKeyImage/assignEncoderImage setState path + re-sends willAppear with the new state index to the owning plugin (cross-plugin guard preserved)
  - BuiltinActionRegistry::kToggleActionId constant (com.hotspot.streamdock.toggleaction)
  - com.hotspot.streamdock.toggleaction built-in registration (classification only)
  - StreamDockInputService::setToggleCycleHook / setToggleRenderHook injection seams
affects: [stream_dock_input_service, profile_controller, plugin_device_bridge, builtin_actions_service, builtin_action_registry, application]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - 'Toggle Action resolved at the input-service seam (where Binding.instance + index are in scope) via two injected hooks: a cycle hook (ProfileController mutates+persists the mutable Profile) and a render hook (PluginDeviceBridge paints states[currentState] + emits willAppear). The BuiltinHandler(string_view) cannot see the binding, so the registry entry is classification-only (RESEARCH Finding 3, Q1/Q2/Q4).'
    - Direct injected hooks (setToggleCycleHook/setToggleRenderHook) rather than new profileChanged slots, so the Pitfall-3 / IN-02 repaint-before-context connection ordering (application.cpp:456-487) is untouched (T-32-09).
    - Id-match classification (id == kToggleActionId) over a bare states.size() > 1 heuristic, for OpenDeck parity and lock-step with BuiltinActionRegistry::handles() -- same idiom Wave 2 used for Multi Action.

key-files:
  created:
    - tests/unit/test_toggle_dispatch.cpp
  modified:
    - src/core/include/ajazz/core/builtin_action_registry.hpp
    - src/app/src/profile_controller.hpp
    - src/app/src/profile_controller.cpp
    - src/app/src/stream_dock_input_service.hpp
    - src/app/src/stream_dock_input_service.cpp
    - src/app/src/plugin_device_bridge.hpp
    - src/app/src/plugin_device_bridge.cpp
    - src/app/src/builtin_actions_service.cpp
    - src/app/src/application.cpp
    - tests/unit/CMakeLists.txt

key-decisions:
  - Toggle id is com.hotspot.streamdock.toggleaction (the REAL dispatch prefix). The OpenDeck opendeck.toggleaction id would fail handles() and never fire. The only opendeck.* occurrences in the code are comments warning AGAINST that id.
  - currentState PERSISTS to the profile JSON (Q1 user decision) -- cycleInstanceState routes through saveActiveProfile() so a cycled state survives a restart. currentState already round-trips through profileToJson/profileFromJson (Phase 31).
  - State mutation resolves at the dispatch seam (not the registry handler) because mutating currentState needs the firing binding's identity + a mutable Profile, neither of which a BuiltinHandler(string_view) can see (Finding 3). The registry entry exists for CLASSIFICATION only.
  - Render reuses the existing setState repaint path (assignKeyImage for Keypad, assignEncoderImage for Encoder) -- no new render mechanism introduced (Q4, BIND-07).
  - Toggle handled for Keypad, Encoder, AND the touch-tap zone path (which registers under the "Encoder" controller) for full input-surface parity.

requirements-completed: [BIND-05, BIND-07]

# Metrics
duration: 38min
completed: 2026-06-08
---

# Phase 32 Plan 03: Toggle Action dispatch -- cycle currentState mod N, render states[currentState], persist Summary

**A key/dial/touch-zone bound to a Toggle Action now cycles `currentState = (currentState + 1) mod N` through ALL `states[]` (N>2) on each press -- resolved at the `StreamDockInputService::dispatch` seam via a `ProfileController::cycleInstanceState` mutator (mutate + persist) and a `PluginDeviceBridge::renderToggleState` hook (repaint `states[currentState]` through the existing `assignKeyImage`/`assignEncoderImage` setState path + re-send a state-change `willAppear`) -- plus a classification-only `com.hotspot.streamdock.toggleaction` built-in registration. The new index PERSISTS to the profile JSON (survives restart).**

## Performance

- **Duration:** ~38 min
- **Completed:** 2026-06-08
- **Tasks:** 2 code/test tasks; 1 checkpoint:human-verify deferred to the consolidated live pass (see "Pending live verification").
- **Files modified:** 9 + 1 created (test).

## Accomplishments

- **Toggle cycle mutator (BIND-05/07, Task 1):** `ProfileController::cycleInstanceState(controller, index)` resolves the addressed binding's `instance` ("Keypad" -> keys, "Encoder" -> encoders, case-insensitive), advances `currentState = (currentState + 1) % states.size()` over ALL states (N>2 supported), persists via `saveActiveProfile()` (Q1: survives restart), and emits `profileChanged()`. Safe no-op for single-state / no-instance / unknown-controller / out-of-range bindings -- it never creates an instance and the modulo keeps `currentState` in range (T-32-08).
- **Toggle dispatch seam (BIND-07, Task 2):** `StreamDockInputService::dispatch` now classifies a firing `Binding`/`EncoderBinding` whose `instance.id == kToggleActionId` and routes it to `dispatchToggle()` (instead of an action-chain run) for `KeyPressed`, `EncoderPressed`, and the touch-tap zone path. `dispatchToggle` invokes the injected cycle hook (mutate + persist), re-reads the instance through the const `ProfileAccessor` so the render reflects the NEW state, then drives the injected render hook with `states[currentState]`.
- **Per-state render + state-change willAppear (BIND-07):** `PluginDeviceBridge::renderToggleState` repaints `states[currentState].visual` (imagePath/title) by REUSING the existing setState repaint mechanism -- `assignKeyImage` (Keypad) / `assignEncoderImage` (Encoder), with `compositeTitle` + `reapplyTitle` -- and, when a plugin owns the context at that coordinate, advances `ContextRegistry::setState` + re-sends a `willAppear` carrying the new state index. A pure built-in toggle (no owning plugin) renders only; the willAppear is a clean no-op.
- **Classification registration:** `com.hotspot.streamdock.toggleaction` registered in `BuiltinActionsService::populate()` with an inert handler -- it makes `handles()` true while the actual state work happens at the seam (the handler cannot see the binding). New `kToggleActionId` constant on `BuiltinActionRegistry`.
- **Wiring (Pitfall-3 safe):** `application.cpp` injects `setToggleCycleHook -> ProfileController::cycleInstanceState` and `setToggleRenderHook -> PluginDeviceBridge::renderToggleState` as DIRECT hooks (not new `profileChanged` slots), so the repaint-before-context connection ordering at lines 456-487 is untouched (T-32-09).
- **Invariants held:** no SKU/codename branching in the new dispatch / render paths (BIND-06); COD-031 untouched (kToggleActionId is a pure-core `string_view` constant; no `nlohmann` in core public headers); cross-plugin ownership guard preserved on any toggle-driven willAppear (T-32-07).

## Task Commits

1. **Task 1: ProfileController cycle mutator + kToggleActionId + tests** -- `13eb207` (feat). Adds the core id constant, the `cycleInstanceState` mutator (mutate + persist + profileChanged), and `tests/unit/test_toggle_dispatch.cpp` (the [toggle] suite, covering both tasks). Both tasks were TDD: the test file was authored against the not-yet-wired API; this commit makes the Task-1 layer green.
1. **Task 2: dispatch seam + per-state render + state-change willAppear + wiring** -- `2f8a0e6` (feat). Adds the toggle classification + `dispatchToggle` at the input-service seam, `PluginDeviceBridge::renderToggleState`, the classification-only registry entry, and the Application hook injection.

(The plan front-loaded a single test file covering both layers; the RED/GREEN split is per-task within that file. The pre-commit clang-format hook re-wrapped a few new signatures on each commit -- resolved with the standard re-add dance, no `--no-verify`, full hook chain ran on both commits.)

## Files Created/Modified

- `src/core/include/ajazz/core/builtin_action_registry.hpp` - added `static constexpr kToggleActionId = "com.hotspot.streamdock.toggleaction"`.
- `src/app/src/profile_controller.{hpp,cpp}` - `cycleInstanceState(controller, index)` mutator (mutate `currentState` mod N + `saveActiveProfile()` persist + `profileChanged()`).
- `src/app/src/stream_dock_input_service.{hpp,cpp}` - `isToggleAction()` classifier; `dispatchToggle()` (cycle hook -> re-read instance -> render hook); `setToggleCycleHook`/`setToggleRenderHook` injection seams + the two hook members; wired into the `KeyPressed`, `EncoderPressed`, and touch-tap zone cases.
- `src/app/src/plugin_device_bridge.{hpp,cpp}` - `renderToggleState(controller, index, instance)` reusing the assignKeyImage/assignEncoderImage setState path + re-sending willAppear to the owning plugin (ownership guard preserved). Added `<QUrl>` for the imagePath->local-file boundary.
- `src/app/src/builtin_actions_service.cpp` - registered `com.hotspot.streamdock.toggleaction` (classification-only, inert handler).
- `src/app/src/application.cpp` - wired the cycle hook (-> ProfileController) and render hook (-> PluginDeviceBridge) as direct hooks, Pitfall-3 ordering untouched.
- `tests/unit/test_toggle_dispatch.cpp` (new) - 6 `[toggle]` cases.
- `tests/unit/CMakeLists.txt` - registered the new test source.

## Verification

- `ctest --preset linux-release -R "toggle"` -> all 6 BIND-07 [toggle] cases pass (cycle-mod-N wrap N=3, single-state/no-instance no-op, persistence reload, key seam cycle+render+willAppear, encoder seam cycle+render, Multi-Action-does-not-trigger-toggle regression).
- `ctest --preset linux-release -R "toggle|action_instance|input|profile"` -> **62/62 passed** (>0 matched).
- Full suite `ctest --preset linux-release -LE qml` -> **727/727 passed** (no regression; was 721 pre-plan, +6 toggle cases).
- `grep -n 'com.hotspot.streamdock.toggleaction' src/app/src/builtin_actions_service.cpp` matches (correct prefix; NOT opendeck.\*). The only `opendeck.` strings are comments warning AGAINST that id.
- `grep -n 'currentState' src/app/src/profile_controller.cpp` shows the cycle mutation `(currentState + 1u) % states.size()`.
- No SKU branching in the new dispatch / render blocks (`! grep -qiE 'akp03|akp05|akp153'` over `dispatchToggle` / the three input cases / `renderToggleState`; the pre-existing `zoneForX` AKP05 reference is untouched).
- Pitfall 3 preserved: `application.cpp:456-487` repaint connections still registered before `populateContextsForActivePage`; the two new hooks are direct setter injections, not `profileChanged` slots.
- COD-031: `! grep -rq '#include.*nlohmann' src/core/include/` exits 0.

## Pending live verification

Task 3 was a `checkpoint:human-verify` (MANDATORY live debug-channel pass per CLAUDE.md -- ctest is necessary but not sufficient; the render path needs visual confirmation, Phase-27 lesson). Per sequential-executor rules this was NOT run here (no GUI launch). Exact commands for the consolidated live pass:

```bash
# 1. Build + launch headless with debug channel + untrusted plugins allowed
AJAZZ_DEBUG_CONTROL=1 AJAZZ_ALLOW_UNTRUSTED_PLUGINS=1 \
  nohup build/linux-release/src/app/ajazz-control-center &

# 2. Select the device, bind a 3-state Toggle Action to a key (3 distinct state images).
#    The firing binding's instance.id must be com.hotspot.streamdock.toggleaction
#    with three states[], each carrying a distinct imagePath.

# 3. Press FOUR times (synthetic input -- the demo unit delivers zero real input, Pitfall 2):
for i in 1 2 3 4; do
  scripts/ajazz-debug input.key '{"index":0,"pressed":true}'
  scripts/ajazz-debug input.key '{"index":0,"pressed":false}'
  scripts/ajazz-debug screenshot   # READ each screenshot
done

# 4. (If a plugin owns the action) confirm a willAppear with incrementing payload.state:
scripts/ajazz-debug plugin.protocolLog

# 5. Persistence: relaunch the app and confirm the key renders the last currentState.
AJAZZ_DEBUG_CONTROL=1 nohup build/linux-release/src/app/ajazz-control-center &
scripts/ajazz-debug screenshot
```

**Expected:** the key face cycles state 0 -> 1 -> 2 on the first three presses and WRAPS back to 0 on the 4th; if a plugin owns the action, `plugin.protocolLog` shows a `willAppear` per press with `payload.state` 1, 2, 0; after relaunch the key renders the persisted `currentState` (Q1). If the face does not change, does not wrap, or the state resets to 0 on restart, the live pass FAILS.

## Decisions Made

- **`com.hotspot.streamdock.toggleaction`** is the canonical id, exposed as `kToggleActionId`. The OpenDeck `opendeck.toggleaction` id is deliberately NOT used (would fail `handles()`).
- **Persist `currentState`** (Q1 user decision): `cycleInstanceState` saves through `saveActiveProfile()` so the state survives a restart. `currentState` already serializes (Phase 31).
- **Two injected hooks at the seam** (cycle + render), not the registry handler: the `BuiltinHandler(string_view)` cannot see the firing `Binding`, so the typed mutation + render resolve where the identity is in scope (Finding 3, Q4). The registry handler is classification-only.
- **Reuse the setState repaint path** for the render (`assignKeyImage`/`assignEncoderImage`) -- no new render mechanism (BIND-07).
- **Direct hooks, not profileChanged slots**, to preserve the Pitfall-3 / IN-02 connection ordering (T-32-09).

## Deviations from Plan

- **[Rule 3 - blocking] clang-format wrap of new signatures (both commits).** The pre-commit clang-format hook re-wrapped `cycleInstanceState` / `dispatchToggle` / `renderToggleState` signatures after the first commit attempt of each commit (the documented stash/restore behavior). Resolved with the standard re-add dance -- the cosmetic re-wrap was re-staged and the commit re-run. No `--no-verify` was used; the full hook chain ran on both commits. Behavior unchanged.

## Known Stubs

None. The toggle path is fully wired: cycle (ProfileController, real save), render (PluginDeviceBridge, real assignKeyImage/assignEncoderImage), and willAppear (real SdPluginServer::sendEvent when a plugin owns the context). The only gap is hardware-visual confirmation (see Pending live verification), which is a checkpoint, not a stub.

## Threat Surface

No new threat surface beyond the plan's `<threat_model>`:

- **T-32-07 (cross-plugin context drive via toggle):** `renderToggleState` only sends `willAppear` to the registered `ctx.pluginUuid` for the resolved context; it never drives a context owned by a different plugin. A built-in toggle with no owning plugin emits no willAppear.
- **T-32-08 (currentState out of range):** the mutator uses `(currentState + 1) % states.size()` with a `states.size() <= 1` no-op guard; `renderToggleState` additionally clamps `currentState` to 0 on read if out of range (model also clamps -- action_instance.hpp:88).
- **T-32-09 (render reorders profileChanged connections):** the render is driven by a DIRECT hook from the toggle path, NOT a new `profileChanged` slot; `application.cpp:456-487` ordering is preserved.
- **T-32-SC (npm/pip/cargo installs):** zero new dependencies this plan.

## Next Phase Readiness

- BIND-05 (Toggle semantics) + BIND-07 (cycle + render + willAppear + persist) delivered at the seam + classified in the registry. The device editor remains an other-plan concern.
- **One open gate:** the live debug-channel verification (above) must be walked in the consolidated live pass before this is hardware-confirmed. ctest coverage proves the cycle-mod-N, the render-seam read of states[currentState], and persistence at the unit level.

## Self-Check: PASSED

All modified files present on disk; the new test file present; both task commits (13eb207, 2f8a0e6) exist in git history. Scoped suite (62/62) and full suite (727/727, -LE qml) green.

______________________________________________________________________

*Phase: 32-binding-layer-fix-multi-toggle-action-device-editor*
*Completed: 2026-06-08*
