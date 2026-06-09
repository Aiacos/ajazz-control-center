---
phase: 32-binding-layer-fix-multi-toggle-action-device-editor
reviewed: 2026-06-08T00:00:00Z
depth: standard
files_reviewed: 14
files_reviewed_list:
  - src/core/include/ajazz/core/action_chain_adapter.hpp
  - src/core/src/action_chain_adapter.cpp
  - src/core/include/ajazz/core/action_instance.hpp
  - src/core/src/profile.cpp
  - src/core/include/ajazz/core/builtin_action_registry.hpp
  - src/app/src/builtin_actions_service.cpp
  - src/app/src/builtin_actions_service.hpp
  - src/app/src/stream_dock_input_service.cpp
  - src/app/src/stream_dock_input_service.hpp
  - src/app/src/profile_controller.cpp
  - src/app/src/profile_controller.hpp
  - src/app/src/plugin_device_bridge.cpp
  - src/app/qml/DeviceView.qml
  - src/app/qml/components/DeviceCanvas.qml
findings:
  critical: 2
  warning: 4
  info: 3
  total: 9
status: issues_found
---

# Phase 32: Code Review Report

**Reviewed:** 2026-06-08
**Depth:** standard
**Files Reviewed:** 14
**Status:** issues_found

## Summary

Phase 32 adds Multi Action + Toggle Action dispatch on top of the Phase 31 `ActionInstance` model, plus a scrollable device editor. The COD-031 boundary is clean (`grep -rn '#include.*nlohmann' src/core/include/` and `src/core/src/` both return 0; the two new public headers are Qt-free and nlohmann-free). The recursion depth cap on `instanceChildrenToChain` is present and correct (bounded by nesting depth, not sibling count). `delayMs` serialization (omit-when-zero, reader-tolerant, signed-reject, range-check) is sound. `cycleInstanceState` correctly guards `states.size() <= 1` before the modulo, so there is no modulo-by-zero, and N>2 cycling + persistence are correct and test-covered end-to-end (reload-from-disk).

However, the toggle render hook carries a **key-index base mismatch** that paints the wrong physical key and notifies the wrong plugin context — exactly the integration-wiring class of bug that unit tests + grep miss (CLAUDE.md's "verify live through the debug channel" rule). A second BLOCKER: `dispatchToggle` is invoked with inconsistent index bases across its call sites (1-based for keypad/encoder, 0-based for touch-zone), so the touch-zone toggle path mis-targets. Both should be confirmed live on the AKP05E before this ships.

## Critical Issues

### CR-01: Toggle render hook double-converts the key index (paints wrong key, wrong plugin context)

**File:** `src/app/src/plugin_device_bridge.cpp:1054-1057, 1080-1081` (called from `src/app/src/stream_dock_input_service.cpp:256, 460`)

**Issue:** `StreamDockInputService::dispatch` calls `dispatchToggle("Keypad", ev.index)` where `ev.index` is the **1-based** device key number (device.hpp:146) — and crucially the SAME value the service uses for `prof.keys.find(ev.index)` and that `cycleInstanceState` uses for `m_profile.keys.find(idx)`. So within the input/profile path, `prof.keys` is treated as keyed by the 1-based device index. But `renderToggleState` then treats that index as **0-based**:

```cpp
// plugin_device_bridge.cpp:1055-1056
std::uint8_t const keyIndex =
    static_cast<std::uint8_t>(index + 1); // 0-based -> 1-based device index
...
// plugin_device_bridge.cpp:1080
auto const gc = coordsForKeyIndex(static_cast<std::uint8_t>(index + 1), kDefaultKeyCols);
```

The `+1` is applied to a value that is already the 1-based device index, so the repaint hits `keyIndex+1` (the next physical key) and the `byCoord` lookup resolves the wrong coordinate — the state-change `willAppear` goes to the wrong context (or none). The unit test masks this because it installs a *stub* render hook (`test_toggle_dispatch.cpp:239`) that only records `index` and never exercises the production `renderToggleState` conversion; the cycle-and-persist half (which IS correct) is what the test actually validates.

Note the ambiguity is partly inherited: `populateContextsForActivePage` (plugin_device_bridge.cpp:1124,1143) documents `Profile::keys` as 0-based and registers contexts at `keyIdx0 + 1`, which contradicts the input service treating `prof.keys`/`ev.index` as the same 1-based value. The two halves of the codebase disagree on the base. The toggle render hook must use the SAME convention the dispatch path uses end-to-end.

**Fix:** Resolve the single source of truth for the `Profile::keys` key base, then make `renderToggleState` agree with `dispatchToggle`'s caller. If `prof.keys` is keyed by the 1-based device index (as the input service + `cycleInstanceState` assume), drop the `+1`:

```cpp
// renderToggleState, Keypad branch: index is already the 1-based device index
std::uint8_t const keyIndex = static_cast<std::uint8_t>(index);
m_control->assignKeyImage(keyIndex, composited);
...
// willAppear resolution: convert the 1-based device index directly
auto const gc = coordsForKeyIndex(static_cast<std::uint8_t>(index), kDefaultKeyCols);
```

Then add a render-hook test that drives the REAL `renderToggleState` (not a stub) and asserts the painted key index / resolved context, and verify live via `scripts/ajazz-debug input.key` on the AKP05E that the *pressed* key (not its neighbour) repaints.

### CR-02: `dispatchToggle` receives inconsistent index bases across call sites (touch-zone toggle mis-targets)

**File:** `src/app/src/stream_dock_input_service.cpp:256, 286, 344`

**Issue:** `dispatchToggle(QString controller, std::uint16_t index)` is called with three different index conventions:

- KeyPressed: `dispatchToggle("Keypad", ev.index)` — 1-based device key (line 256)
- EncoderPressed: `dispatchToggle("Encoder", ev.index)` — encoder index per the device event (line 286)
- TouchUp tap: `dispatchToggle("Encoder", zone)` — `zone` is the **0-based** result of `zoneForX()` (line 339-344)

`dispatchToggle` then forwards `index` unchanged to both `cycleInstanceState` and the render hook. The render hook's `Encoder` branch (`plugin_device_bridge.cpp:1061`) uses `index` directly as the 0-based encoder index, and its `byCoord` lookup uses `(... "Encoder", 0, index)`. So for the encoder-press path the base may line up, but a touch-zone tap that resolves `zone` and an encoder press that resolves `ev.index` cannot both be the correct base for the same `cycleInstanceState`/`renderToggleState` consumers unless `ev.index` for encoders is also 0-based AND `prof.encoders` is 0-based. The keypad vs encoder/touch mismatch means at least one of the three paths cycles/renders the wrong control. This is unverifiable from tests alone (the encoder test at `test_toggle_dispatch.cpp:291` uses a stub hook and a single index), and is the same wiring-bug class CLAUDE.md calls out.

**Fix:** Normalize all three call sites to one documented base before calling `dispatchToggle`, mirroring whatever `cycleInstanceState` + `renderToggleState` expect. Add an explicit comment on `dispatchToggle`'s signature stating the base (e.g. "index is the same key used by Profile::keys/encoders"), and convert at each call site so keypad, encoder, and touch-zone all pass that base. Confirm each path live (key press, dial press, strip tap) repaints the correct control.

## Warnings

### WR-01: Nested Multi Action node is emitted as a Plugin step AND its children are flattened (double dispatch)

**File:** `src/core/src/action_chain_adapter.cpp:29-40`

**Issue:** When a child is itself a Multi Action (has `id == kMultiActionId` and non-empty `children`), `flatten` pushes it as a `Plugin` Action (so the engine will dispatch `kMultiActionId` → the registry fallback handler, which runs `decodeActionChain` on the child's `settings` string) AND then recurses to flatten that child's typed `children` inline. For a nested Multi Action whose semantics live in `children` (not in a `settings.actions` array), the emitted parent step is a redundant no-op at best; if the nested node also carries a `settings.actions` array, those actions fire IN ADDITION to the flattened typed children — a double execution. The pre-order flatten does not skip container nodes.

**Fix:** When a child is a container (its `id` is `kMultiActionId`, or more generally it has non-empty `children`), either skip emitting the container step and only flatten its children, or decide a single canonical source (typed `children` XOR `settings.actions`) and document it. At minimum add a test with a nested Multi Action asserting the grandchildren run exactly once.

### WR-02: `DeviceCanvas` reads `bindings.get(index)` per-delegate without a `count`/null re-evaluation guard

**File:** `src/app/qml/components/DeviceCanvas.qml:125-128`

**Issue:** The KeyCell delegate sets `iconSource`/`label` from `canvas.bindings.get(index)` guarded by `index < canvas.bindings.count`. The `bindings` model is mutated by `_syncFromProfile()` (DeviceView.qml `bindings.set`) and by the `onKeyImageAssigned` handler (`bindings.setProperty`). `ListModel.get(index)` returns a snapshot object; the binding only re-evaluates when `bindings` (the model reference) or `index` changes, not when a row's properties change via `setProperty`. The live-render mirror relies on `onKeyImageAssigned` calling `setProperty`, which DeviceView wires to drive `iconSource` — but the canvas delegate's `iconSource` binding reads `bindings.get(index).iconSource` and may not refresh on `setProperty` of the same row. Verify the on-canvas tile actually updates when a plugin/toggle repaints; this is the exact "looks bound, is a no-op" trap.

**Fix:** Verify live (debug channel `device.renderTest` / a plugin setImage) that the on-screen cell updates after `setProperty`. If it does not refresh, bind the cell's `iconSource` to a property that changes identity on update (e.g. drive KeyCell from a role via a DelegateModel, or re-`set` the whole row rather than `setProperty`).

### WR-03: `multiactions.LunBo` decodes the entire action array twice per press

**File:** `src/app/src/builtin_actions_service.cpp:504-512`

**Issue:** `cursor = cursor % chainSize` then `decodeActionChain(arr)[cursor]` materializes the full chain only to take one element, and `cursor = (cursor + 1) % chainSize` re-derives it. `m_lunboCursors[bindingId]` default-constructs to 0 on first access (correct), but the pre-advance `cursor % chainSize` is redundant with the post-advance modulo and the double decode is wasteful. More importantly, `decodeActionChain(arr)[cursor]` indexes a freshly built vector — correct only because `cursor < chainSize` is guaranteed by the modulo; if `arr` were empty the early `return` covers it, so no OOB. Functionally correct but fragile and duplicated. (Pre-existing, touched by Phase 32 context — flagging for the multi-action surface.)

**Fix:** Decode once, guard the index, and advance:

```cpp
auto chain = decodeActionChain(arr);
auto& cursor = m_lunboCursors[bindingId];
cursor %= chain.size();
core::ActionChain step{chain[cursor]};
if (m_engine) m_engine->run(step);
cursor = (cursor + 1) % chain.size();
```

### WR-04: `renderToggleState` willAppear gate silently no-ops when `m_activeDeviceId` is empty

**File:** `src/app/src/plugin_device_bridge.cpp:1073`

**Issue:** The state-change `willAppear` is gated on `m_activeDeviceId.isEmpty()` returning early. `m_activeDeviceId` is only set in `onDeviceConnected` (plugin_device_bridge.cpp:1356). If a toggle is pressed (e.g. via `input.key` debug injection or a synthetic event) before a real device-connect has populated `m_activeDeviceId`, the repaint half runs but the plugin never receives the state-change `willAppear` — a partial, silent failure. There is no log on this path.

**Fix:** Add an `AJAZZ_LOG_DEBUG`/`WARN` when the willAppear is skipped due to empty `m_activeDeviceId`, so the partial dispatch is observable, and confirm `m_activeDeviceId` is set on every path that can fire a toggle (including synthetic-event injection used for autonomous testing).

## Info

### IN-01: `runPressBinding` checks `isMultiAction` but a Toggle binding never reaches it (dead branch ordering is correct but undocumented)

**File:** `src/app/src/stream_dock_input_service.cpp:85-94, 251-261`

**Issue:** In `dispatch`, `isToggleAction` is checked first and short-circuits to `dispatchToggle`, so `runPressBinding` only ever sees non-toggle bindings. A binding whose instance id is neither toggle nor multi falls through to the legacy `onPress` chain — correct. The precedence (toggle wins over multi if both ids somehow matched) is fine but relies on the early branch; worth a one-line comment that toggle is mutually exclusive with the multi/legacy path.

**Fix:** Add a comment in `dispatch` noting Toggle is resolved before `runPressBinding` and is never a Multi Action.

### IN-02: Encoder `EncoderTurned` path has no Toggle/Multi handling (CW/CCW only run legacy chains)

**File:** `src/app/src/stream_dock_input_service.cpp:272-273, 385-408`

**Issue:** `EncoderTurned` → `onEncoderTurned` → `drainCoalescedRotation` runs `onCw`/`onCcw` only; a Toggle or Multi Action bound to encoder *rotation* (as opposed to encoder *press*) is not dispatched. This may be intentional (toggle-on-rotate is unusual), but it is an asymmetry vs the press path. Document the scope so it is not mistaken for a regression.

**Fix:** Add a comment that rotation dispatch is legacy-chain-only by design; toggle/multi apply to press/tap only.

### IN-03: `kDefaultKeyCols = 5` hardcoded in three render/coordinate sites

**File:** `src/app/src/plugin_device_bridge.cpp:605, 885, 1030, 1111` and `renderToggleState:1030`

**Issue:** The AKP05E 2x5 `keyCols=5` constant is duplicated across `onAction`, `onDeviceEvent`, `renderToggleState`, and `populateContextsForActivePage`. For any non-5-column SKU these paths mis-convert coordinates. This is a known documented Phase-23 follow-up, but `renderToggleState` (new in Phase 32) adds a fourth copy and inherits the same SKU-specific limitation despite BIND-06's "no SKU branching" intent.

**Fix:** Source `keyCols` from the active device descriptor (the documented Phase-23 refactor) and have `renderToggleState` use it too, so the toggle render is geometry-driven rather than AKP05E-pinned.

______________________________________________________________________

_Reviewed: 2026-06-08_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
