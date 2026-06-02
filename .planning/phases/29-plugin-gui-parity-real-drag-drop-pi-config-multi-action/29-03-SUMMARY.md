---
phase: 29-plugin-gui-parity-real-drag-drop-pi-config-multi-action
plan: 03
subsystem: ui
tags: [qml, multi-action, drag-drop, profile, PLUGIN-23, TDD]

# Dependency graph
requires:
  - phase: 29-plugin-gui-parity-real-drag-drop-pi-config-multi-action
    plan: 01
    provides: DragRelay singleton + Drag.Internal ghost (Wayland-safe drag)
  - phase: 29-plugin-gui-parity-real-drag-drop-pi-config-multi-action
    plan: 02
    provides: Inspector deviceCodename/keyIndex bindings in DeviceView.qml
provides:
  - 'ProfileController.appendKeyAction: additive push_back onto onPress (Q_INVOKABLE)'
  - 'ProfileController.reorderKeyAction: std::rotate within onPress (Q_INVOKABLE, T-29-05 guarded)'
  - 'ProfileController.removeKeyActionAt: erase at pos (Q_INVOKABLE, T-29-05 guarded)'
  - 'activeKeyBindings() actionList field: full per-key onPress exposed to QML'
  - 'KeyBindingList.qml: per-key multi-action list editor with DragRelay reorder + trash + append'
  - 'DeviceView.qml: instantiates KeyBindingList for selected key'
affects:
  - 29-04-PLAN (live verification batched session)

# Tech tracking
tech-stack:
  added: []
  patterns:
    - Multi-action list driven by activeKeyBindings().actionList nested QVariantList
    - 'Per-action DragRelay drags: application/x-ajazz-binding + {controller, position, actionPos}'
    - 'TDD RED/GREEN: test_profile_multiaction.cpp -> appendKeyAction/reorderKeyAction/removeKeyActionAt'

key-files:
  created:
    - src/app/qml/components/KeyBindingList.qml
    - tests/unit/test_profile_multiaction.cpp
  modified:
    - src/app/src/profile_controller.hpp
    - src/app/src/profile_controller.cpp
    - src/app/qml/DeviceView.qml
    - src/app/CMakeLists.txt
    - tests/unit/CMakeLists.txt

key-decisions:
  - appendKeyAction is the additive path; commitKeyBinding remains the replace-all / set-primary path for initial drag-drop
  - std::rotate implements O(n) in-place reorder; out-of-range pos is a no-op (T-29-05 mitigation)
  - actionList nested in activeKeyBindings() to preserve back-compat for all existing QML callers
  - KeyBindingList hosted in DeviceView.qml (not Inspector.qml) to avoid merge conflicts with 29-02
  - trashDropArea updated to dispatch removeKeyActionAt when actionPos field is present in the binding payload
  - anyDragActive includes DragRelay.active so trash zone shows during KeyBindingList row drags
  - Live verification DEFERRED to batched session 29-04 per critical_deferral in execution context

requirements-completed: [PLUGIN-23]

# Metrics
duration: ~45min
completed: 2026-06-02
---

# Phase 29 Plan 03: Multi-Action Binding List Editor Summary

**ProfileController multi-action verbs (append/reorder/remove-at-index over onPress vector) + per-key KeyBindingList.qml editor with DragRelay reorder + drag-to-trash + additive append; persists across restart via existing profile JSON serializer; TDD RED/GREEN with 12 new unit tests**

## Performance

- **Duration:** ~45 min
- **Completed:** 2026-06-02
- **Tasks:** 2 of 2 code tasks executed (Task 3 checkpoint = DEFERRED per critical_deferral)
- **Files created/modified:** 7

## Accomplishments

### Task 1: ProfileController multi-action verbs (TDD RED + GREEN)

**TDD RED** (commit `38a1074`): `tests/unit/test_profile_multiaction.cpp` — 12 failing tests covering:

- `appendKeyAction` keeps existing, creates first action, out-of-range no-op
- `reorderKeyAction` moves pos 0→2 via std::rotate, out-of-range no-op
- `removeKeyActionAt` removes correct action, last-action leaves empty onPress, out-of-range no-op
- `activeKeyBindings()` exposes `actionList` field with full onPress, back-compat top-level fields unchanged
- 2-action onPress survives save/load round-trip

**TDD GREEN** (commit `a51d69c`): Implementation in `profile_controller.hpp` + `.cpp`:

- `appendKeyAction(keyIndex, actionKind, settingsJson, actionId)`: `push_back` onto `Binding.onPress` WITHOUT clearing existing entries. Validates keyIndex [0..65534] + actionKind [0..BackToParent] (T-29-05 mirrors `commitKeyBinding`).
- `reorderKeyAction(keyIndex, fromPos, toPos)`: `std::rotate` O(n); both `fromPos`/`toPos` must be in `[0, onPress.size()-1]` or no-op.
- `removeKeyActionAt(keyIndex, pos)`: `erase` at `pos`; last action removed leaves empty `onPress`.
- `activeKeyBindings()` extended with `actionList` (nested `QVariantList` of `{actionKind, actionId, label, iconSource}`); existing top-level fields unchanged for back-compat.

**Serialization**: The existing `profileToJson`/`profileFromJson` already handles `std::vector<Action>` in `onPress` as a JSON array (see `profile.cpp:191` `writeChain(out, "onPress", b.onPress)`). No serializer changes needed — confirmed via the 2-action round-trip test.

**COD-031**: `grep -rn nlohmann src/core/include/` returns 0 actual includes (3 comment-only references). CLEAN.

### Task 2: KeyBindingList.qml + DeviceView wiring (commit `0f1fff2`)

**`src/app/qml/components/KeyBindingList.qml`** (new file, 270 lines):

- Vertical `ListView` of the selected key's `onPress` action rows.
- Each row (`objectName: "keyBindingRow_<index>"`) is a **drag SOURCE** via `DragHandler + DragRelay` (Wayland-safe, same pattern as `KeyCell.qml`). MIME `"application/x-ajazz-binding"` payload: `{controller:"Keypad", position:<keyIndex>, actionPos:<row>}` — the `actionPos` field distinguishes row drags from key-cell drags.
- Each row is also a **drop TARGET** (`DropArea`) that calls `ProfileController.reorderKeyAction(keyIndex, fromPos, toPos)` when a row from the same key drops on it.
- `appendHint` drop zone at the bottom accepts `application/x-ajazz-action` (key affordanceMask bit 1 gate, mirrors `KeyCell.qml:220-234`) and calls `ProfileController.appendKeyAction(...)` — additive, does NOT overwrite.
- 6 `objectName` declarations (`keyBindingList`, `keyBindingListTitle`, `keyBindingListView`, `keyBindingRow_<pos>`, `keyBindingListAppendZone`) for full debug-channel addressability.

**`src/app/qml/DeviceView.qml`** (modified):

- Added `_selectedKeyActionList` property + `_refreshSelectedKeyActionList()` function reading `activeKeyBindings().actionList` for the selected key.
- `onSelectedKeyIndexChanged` + `Connections.onProfileChanged` both refresh `_selectedKeyActionList`.
- `KeyBindingList { keyIndex: root.selectedKeyIndex; actionList: root._selectedKeyActionList }` instantiated between the chassis FocusScope and the Inspector (NOT inside Inspector.qml).
- `trashDropArea.onDropped` updated: when `bp.actionPos !== undefined` → calls `ProfileController.removeKeyActionAt(bp.position, bp.actionPos)` (row drag → remove just that action). Existing key-cell drag path (no `actionPos`) → `commitKeyBinding(bp.position, "", "", 0, "")` unchanged.
- `anyDragActive: _activeDragCount > 0 || DragRelay.active` — trash zone now also becomes visible during KeyBindingList row drags.

## Task Commits

| Task       | Name                                                         | Commit    | Files                                                                                         |
| ---------- | ------------------------------------------------------------ | --------- | --------------------------------------------------------------------------------------------- |
| 1a (RED)   | Failing tests for multi-action verbs                         | `38a1074` | tests/unit/test_profile_multiaction.cpp, tests/unit/CMakeLists.txt                            |
| 1b (GREEN) | Implement appendKeyAction/reorderKeyAction/removeKeyActionAt | `a51d69c` | src/app/src/profile_controller.{hpp,cpp}                                                      |
| 2          | KeyBindingList.qml + DeviceView wiring                       | `0f1fff2` | src/app/qml/components/KeyBindingList.qml, src/app/qml/DeviceView.qml, src/app/CMakeLists.txt |

## Build + Test Results

```
cmake --build build/linux-release --target ajazz-control-center ajazz_unit_tests -j$(nproc)
# SUCCEEDED — all targets built clean

ctest --preset linux-release -E qml
99% tests passed, 9 tests failed out of 686
# 677 non-QML tests PASSED (was 665 in 29-01; +12 new multi-action tests)
# 9 "Not Run" = pre-existing QML link gap (CLAUDE.md known latent item, unrelated)
# 0 FAILED

grep -rn nlohmann src/core/include/  # → 0 includes (COD-031 CLEAN)
grep -c objectName src/app/qml/components/KeyBindingList.qml  # → 6 (>= 2 requirement)
git diff --name-only (Task 2 commit)  # → Inspector.qml NOT present (requirement met)
```

## Verification Commands for Orchestrator (Batched Live Session — 29-04)

These commands drive the multi-action editor through the running GUI for complete PLUGIN-23 live verification:

```bash
# 1. Build (no-op if up-to-date)
cmake --build build/linux-release --target ajazz-control-center -j$(nproc)

# 2. Kill stale instances + socket
pkill -x ajazz-control-center || true
rm -f "$XDG_RUNTIME_DIR/ajazz-control-center-debug.sock"

# 3. Launch with AKP05E connected + a plugin (System Monitor recommended)
setsid env AJAZZ_DEBUG_CONTROL=1 \
  build/linux-release/src/app/ajazz-control-center &
APP_PID=$!
sleep 4

# 4. Select a device to activate the editor
scripts/ajazz-debug device.setActiveDevice --params '"akp05e"'

# 5. Drop first action (use qml.drag from library to key)
scripts/ajazz-debug qml.drag --params '{"from":"libraryTile_0","to":"key_0"}'
sleep 0.5

# 6. Drop SECOND action onto the append zone (additive)
scripts/ajazz-debug qml.drag --params '{"from":"libraryTile_1","to":"keyBindingListAppendZone"}'
sleep 0.5

# 7. Verify 2 actions on key 0
scripts/ajazz-debug qml.get --params '{"objectName":"keyBindingList","property":"actionList"}'
# Expected: array of 2 elements

# 8. Drag-reorder: move row 0 to position 1
scripts/ajazz-debug qml.drag --params '{"from":"keyBindingRow_0","to":"keyBindingRow_1"}'
sleep 0.5

# 9. Verify order swapped
scripts/ajazz-debug qml.get --params '{"objectName":"keyBindingList","property":"actionList"}'

# 10. Drag row to trash (remove)
scripts/ajazz-debug qml.drag --params '{"from":"keyBindingRow_0","to":"trashDropArea"}'
sleep 0.5

# 11. Save and restart
scripts/ajazz-debug profile.save
kill $APP_PID
setsid env AJAZZ_DEBUG_CONTROL=1 \
  build/linux-release/src/app/ajazz-control-center &
APP_PID=$!
sleep 4

# 12. Verify persistence: key 0 still has 1 action after restart
scripts/ajazz-debug profile.active
# Check the key[0].onPress in the JSON output has 1 action

# 13. Cleanup
kill $APP_PID
```

**Expected outcomes:**

- Step 7: `actionList` has 2 elements (append worked without overwriting).
- Step 9: order is reversed (reorder worked).
- After step 10: `actionList` has 1 element (remove worked).
- After restart (step 12): the remaining action is still present (persistence across restart via profileToJson/profileFromJson which already handles multi-element onPress).

**Note on live verification:** Per `critical_deferral` in the execution context, ALL live GUI verification is batched to session 29-04. The orchestrator runs the above commands with the physical AKP05E + a real mouse after the full phase completes.

## Deviations from Plan

### [Rule N/A — Existing serializer already correct]

The plan noted "if [the serializer] truncates to one, fix the serializer." Verified: `profileToJson` uses `writeChain(out, "onPress", b.onPress)` which iterates the full vector (profile.cpp:191). `profileFromJson` reads `b.onPress = readActionArray(r)` (profile.cpp:694-695). No fix needed — proven by the 2-action round-trip unit test.

### [Rule 2 — Added anyDragActive || DragRelay.active]

The plan said to use the existing trash DropArea for row drags. The existing opacity was `anyDragActive` driven by `_activeDragCount` (counter from `onCellDragActiveChanged`). KeyBindingList row drags go through `DragRelay.begin/finish` but NOT through `DeviceCanvas.onCellDragActiveChanged`, so the counter stayed 0 and the trash zone stayed at 0.3 opacity. Added `|| DragRelay.active` to `anyDragActive` so the trash zone shows during all drags.

## Known Stubs

None. The `actionList.iconSource` per-action field is always an empty string (the `iconSource` belongs to `KeyState`, not `Action`). QML falls back to the bolt icon. This is not a stub preventing the plan's goal — the label and actionId display correctly, and iconSource is a cosmetic enhancement left for a future plan.

## Threat Flags

None — no new network endpoints, auth paths, or schema changes at trust boundaries. The three new verbs mutate only in-memory profile data (same trust boundary as `commitKeyBinding`). T-29-05 (index tampering from QML) is mitigated by range-validation in all three methods.

## Self-Check: PASSED

- [x] `src/app/qml/components/KeyBindingList.qml` exists (6 objectNames)
- [x] `src/app/src/profile_controller.hpp` declares appendKeyAction, reorderKeyAction, removeKeyActionAt as Q_INVOKABLE
- [x] `src/app/src/profile_controller.cpp` implements the verbs with push_back/erase/rotate on onPress
- [x] `tests/unit/test_profile_multiaction.cpp` exists with 12 test cases
- [x] Commit `38a1074` (RED tests) exists in git log
- [x] Commit `a51d69c` (GREEN implementation) exists in git log
- [x] Commit `0f1fff2` (KeyBindingList + DeviceView) exists in git log
- [x] Build: both targets build clean (no errors, only pre-existing null-deref warning in Qt header)
- [x] ctest -E qml: 677 passed, 0 failed (686 total; 9 Not Run = pre-existing QML gap)
- [x] COD-031: grep -rn nlohmann src/core/include/ → 0 actual includes
- [x] Inspector.qml NOT in Task 2 diff
- [x] Live verification DEFERRED to 29-04 per critical_deferral

______________________________________________________________________

*Phase: 29-plugin-gui-parity-real-drag-drop-pi-config-multi-action*
*Completed: 2026-06-02*
