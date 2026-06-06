---
phase: 26-opendeck-shaped-device-editor
reviewed: 2026-05-28T20:00:00Z
depth: standard
files_reviewed: 23
files_reviewed_list:
  - src/app/CMakeLists.txt
  - src/app/qml/ActionLibraryPane.qml
  - src/app/qml/DeviceView.qml
  - src/app/qml/Main.qml
  - src/app/qml/ProfileEditor.qml
  - src/app/qml/components/EncoderDial.qml
  - src/app/qml/components/KeyCell.qml
  - src/app/qml/components/TouchStripLane.qml
  - src/app/src/device_model.cpp
  - src/app/src/profile_controller.cpp
  - src/app/src/profile_controller.hpp
  - src/app/src/stream_dock_control_service.hpp
  - src/core/include/ajazz/core/device.hpp
  - src/core/include/ajazz/core/profile.hpp
  - src/core/src/profile.cpp
  - src/devices/streamdeck/src/register.cpp
  - tests/qml/CMakeLists.txt
  - tests/qml/test_device_view_drag_drop.qml
  - tests/qml/test_device_view_geometry.qml
  - tests/qml/test_device_view_tests.cpp
  - tests/qml/test_qml_smoke.cpp
  - tests/unit/CMakeLists.txt
  - tests/unit/test_profile_serialization.cpp
  - tests/unit/test_streamdeck_register_geometry.cpp
findings:
  critical: 1
  warning: 1
  info: 3
  total: 5
status: issues_found
---

# Phase 26: Code Review Report (Iteration 2)

**Reviewed:** 2026-05-28T20:00:00Z
**Depth:** standard
**Files Reviewed:** 23
**Status:** issues_found

## Summary

This is the second-pass review following 8 fix commits (7dc7407 -> 8eede93) that
addressed 4 Critical + 6 Warning findings from iteration 1. Eight of the ten
prior findings are confirmed closed. One new Critical regression was introduced
by the CR-04 fix: the QML drag-drop test that covered the encoder-binding path
was not updated after `commitEncoderBinding` became a real Q_INVOKABLE, producing
an inverted assertion that will cause the QML test to fail at runtime. One residual
Warning remains open (WR-02 counter underflow on cell destroy). Three Info items
carry over unchanged.

COD-031 invariant: clean (no nlohmann in core/include headers).
QML_SINGLETON static_assert: present on both ProfileController and
StreamDockControlService.
ASCII test names: all test names inspected are ASCII-clean.

______________________________________________________________________

## Closure Verification — Prior Findings

| ID    | Status   | Evidence                                                                                                                                                                                                                                                                                                                                       |
| ----- | -------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| CR-01 | CLOSED   | `profile.cpp:868` — `touchZones` branch is now unconditional; `schemaVersion` marked `[[maybe_unused]]`. New test `"v2 profile with touchZones key before _schemaVersion round-trips"` passes (line 272 of `test_profile_serialization.cpp`).                                                                                                  |
| CR-02 | CLOSED   | `profile_controller.cpp:310` — `m_profile.touchZones.clear()` added with an explanatory comment.                                                                                                                                                                                                                                               |
| CR-03 | CLOSED   | `TouchStripLane.qml:175` — `onClicked: zoneCell.zoneTapped(zoneCell.zoneIndex)` added to ItemDelegate; `zoneTapped` signal propagated through `TouchStripLane`; `DeviceView.qml:313` connects `onZoneTapped` and sets `selectedZoneIndex`.                                                                                                     |
| CR-04 | CLOSED\* | `profile_controller.cpp:190-230` — `commitEncoderBinding` Q_INVOKABLE added, mirrors `commitKeyBinding` exactly (same arg order, validation range, signal). `EncoderDial.qml:152` calls it. *HOWEVER: the QML-level test was not updated — see new CR-01 below.*                                                                               |
| WR-01 | CLOSED   | `DeviceView.qml:131` — `new XMLHttpRequest()` (standard constructor); `xhr.open("GET", url, true)` (async=true). `_hasPhoto` kept with a TODO comment per D-07. IN-04 collapsed into this fix.                                                                                                                                                 |
| WR-02 | PARTIAL  | `_activeDragCount` counter wired from all three cell types (lines 233-235, 284-286, 320-322 in DeviceView.qml). Counter is correct for normal operation. Residual underflow risk on cell destroy — see new WR-01 below.                                                                                                                        |
| WR-03 | CLOSED   | `test_device_view_geometry.qml:60-84` — AKP153 now `keyRows:3, gridColumns:5`; AKP03 now `keyRows:2, gridColumns:3`. Both align with `register.cpp` descriptors.                                                                                                                                                                               |
| WR-04 | CLOSED   | `device_model.cpp:134-141` — four geometry roles (`KeyRowsRole`, `TouchZoneCountRole`, `MainScreenWidthPxRole`, `MainScreenHeightPxRole`) added to `data()`. `roleNames()` exposes all four (lines 169-172). `dataChanged` is fired for `ConnectedRole` only (correct: geometry roles are static per-descriptor and do not change at runtime). |
| WR-05 | CLOSED   | Collapsed into WR-01 fix — async XHR.                                                                                                                                                                                                                                                                                                          |
| WR-06 | CLOSED   | Collapsed into CR-01 fix.                                                                                                                                                                                                                                                                                                                      |

______________________________________________________________________

## Narrative Findings (AI reviewer)

## Critical Issues

### CR-01: QML drag-drop test for commitEncoderBinding still asserts the stub no-op; will fail now that Q_INVOKABLE is real

**File:** `tests/qml/test_device_view_drag_drop.qml:63-83`

**Issue:** `test_drop_library_tile_on_encoder_calls_commitEncoderBinding` (lines
69-83) was written when `commitEncoderBinding` was a known absent stub and the
correct expectation was zero `profileChanged` emissions. The test body explicitly
comments "Phase 26 v1: ProfileController.commitEncoderBinding does not exist" (line
71\) and asserts `profileChangedSpy.count == before` (i.e. zero new emissions, line
82).

The CR-04 fix added `commitEncoderBinding` as a real `Q_INVOKABLE` that emits
`profileChanged` on success (`profile_controller.cpp:229`). The QML test now
calls a live method that fires `profileChanged`, but the assertion still demands
zero emissions. The test will therefore fail at runtime.

The C++ companion test in `test_device_view_tests.cpp:153-171` **was** correctly
updated to expect `fired == 1`, so the C++ side is sound. Only the QML test
is inverted.

The test also wraps the call in a `try/catch` to absorb a "is not a function"
error — this guard is now dead code because the method exists, but it silently
swallows any future runtime exception from that call path.

**Fix:** Update the QML test to match the C++ companion test:

```qml
function test_drop_library_tile_on_encoder_calls_commitEncoderBinding() {
    profileChangedSpy.clear()
    // Phase 26 CR-04: commitEncoderBinding is now a real Q_INVOKABLE.
    // Calling it with a valid index and actionKind must emit profileChanged.
    ProfileController.commitEncoderBinding(1, "", "Key macro", 2, "")
    compare(profileChangedSpy.count, 1,
            "commitEncoderBinding must emit profileChanged once a Q_INVOKABLE exists")
}
```

Remove the `try/catch` wrapper — it hides real exceptions from the live method.

______________________________________________________________________

## Warnings

### WR-01: \_activeDragCount can underflow to negative if a cell is destroyed while its drag is in flight

**File:** `src/app/qml/DeviceView.qml:233-235, 284-286, 320-322`

**Issue:** The `_activeDragCount` counter is incremented (`+1`) when a cell's
`onDragActiveChanged(true)` fires and decremented (`-1`) when
`onDragActiveChanged(false)` fires. The increment and decrement are wired
through the delegate's `onDragActiveChanged` handler.

If the user changes the active device (i.e. `root.codename` changes) while a
drag is in progress, `_ensureBindings()` is called, which calls
`bindings.remove()` on cells. Removing a cell from the `bindings` ListModel
destroys its delegate instance. In Qt Quick, a destroyed delegate does NOT emit
a final `Drag.active = false` change notification on the parent's handler —
the parent's `_activeDragCount` never receives the decrement. The counter stays
positive, `anyDragActive` stays `true`, and the trash button remains fully opaque
forever until the next drag cycle, which sends a spurious decrement that takes
the counter to -1 (since `anyDragActive` is `_activeDragCount > 0`, a -1 value
reads as false, so the visual is eventually corrected, but the counter state is
corrupted).

The same scenario applies for `encoderCount` or `touchZoneCount` changes (which
rebuild the Repeater model).

**Fix:** Reset the counter to zero whenever the bindings model or geometry changes:

```qml
onKeyCountChanged: {
    _activeDragCount = 0;   // reset: any in-flight drag is now orphaned
    _ensureBindings();
}

onEncoderCountChanged: {
    _activeDragCount = 0;
}

onTouchZoneCountChanged: {
    _activeDragCount = 0;
}
```

Alternatively, clamp the counter to non-negative at every decrement:

```qml
root._activeDragCount = Math.max(0, root._activeDragCount - 1);
```

The clamp is simpler and more robust: it cannot go negative regardless of event
ordering, but it does not reset a counter that was artificially elevated. The
explicit zero-reset on geometry change is the correct semantic fix.

______________________________________________________________________

## Info

### IN-01: akp03_descriptor omits explicit touchZoneCount=0 (carries over from prior review; unchanged)

**File:** `src/devices/streamdeck/src/register.cpp:90-103`

**Issue:** `akp03_descriptor` does not set `touchZoneCount` explicitly, relying
on the zero default in `DeviceDescriptor`. The zero-default is correct (AKP03
has no touch strip), but the absence of an explicit field leaves a future
contributor uncertain whether the field was intentionally zeroed or forgotten,
in contrast to the AKP05 descriptors which comment their Phase 26 geometry
fields explicitly.

**Fix:** Add `.touchZoneCount = 0, // AKP03 has no touch strip (REQ-26-C)` to the
`akp03_descriptor` helper.

______________________________________________________________________

### IN-02: test_device_view_drag_drop.qml test comment block is stale after CR-04 fix

**File:** `tests/qml/test_device_view_drag_drop.qml:63-68`

**Issue:** Even after the fix in CR-01 above, the surrounding comment block (lines
63-68) will still say "commitEncoderBinding does not yet exist on ProfileController
(Phase 26 v1 known stub)" and "when commitEncoderBinding is added in a follow-up
plan this test must be updated to expect count == 1". This comment block will be
factually wrong after the test is corrected — it will describe a condition that is
already resolved.

**Fix:** After applying the CR-01 fix, replace the comment block with a forward-
looking comment that describes the current (post-fix) behavior:

```qml
// Phase 26 CR-04: commitEncoderBinding is a real Q_INVOKABLE.
// Verify that a library tile drop emits profileChanged.
```

______________________________________________________________________

### IN-03: test_device_view_drag_drop.qml test 2 dead try/catch is a test-reliability hazard

**File:** `tests/qml/test_device_view_drag_drop.qml:77-81`

**Issue:** The `try { ProfileController.commitEncoderBinding(...) } catch (e) {}`
wrapper was added to absorb the "is not a function" error from calling a
non-existent method. After CR-04 the method is real; the try/catch now silently
swallows any actual runtime exception thrown from within `commitEncoderBinding`'s
execution path (e.g., a QML binding loop, a type conversion fault). A test that
swallows exceptions will pass even when the method under test throws.

**Fix:** Remove the try/catch entirely (the method is real; let it throw if it
fails):

```qml
ProfileController.commitEncoderBinding(1, "", "Key macro", 2, "")
```

This item is separately listed from IN-02 because it is a test-reliability bug
distinct from the stale comment.

______________________________________________________________________

_Reviewed: 2026-05-28T20:00:00Z_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
_Iteration: 2 of 3_
