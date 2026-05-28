---
phase: 26-opendeck-shaped-device-editor
fixed_at: 2026-05-28T18:30:00Z
review_path: .planning/phases/26-opendeck-shaped-device-editor/26-REVIEW.md
iteration: 1
findings_in_scope: 10
fixed: 10
skipped: 0
status: all_fixed
---

# Phase 26: Code Review Fix Report

**Fixed at:** 2026-05-28T18:30:00Z
**Source review:** `.planning/phases/26-opendeck-shaped-device-editor/26-REVIEW.md`
**Iteration:** 1

**Summary:**

- Findings in scope: 10 (4 Critical + 6 Warning)
- Fixed: 10
- Skipped: 0

## Fixed Issues

### CR-02: resetActiveProfile() silently omits touchZones from clear

**Files modified:** `src/app/src/profile_controller.cpp`
**Commit:** 7dc7407
**Applied fix:** Added `m_profile.touchZones.clear()` alongside the existing
`keys`, `encoders`, and `mouseButtons` clears in `resetActiveProfile()`. The
Phase 26 D-11 touchZones map was the only binding store not cleared, meaning
"Restore defaults" silently preserved stale touch-strip bindings.

______________________________________________________________________

### CR-04: commitEncoderBinding called from QML without existing on ProfileController

**Files modified:** `src/app/src/profile_controller.hpp`,
`src/app/src/profile_controller.cpp`, `tests/qml/test_device_view_tests.cpp`
**Commit:** 0e443ae
**Applied fix:** Added `commitEncoderBinding(int encoderIndex, QString iconPath, QString label, int actionKind, QString settingsJson)` Q_INVOKABLE to
`ProfileController`, following the exact pattern of `commitKeyBinding` and
`commitTouchZoneBinding` (range validation, state mutation, `profileChanged()`
emission). Updated the stub test that previously expected zero signal fires to
now call the method and assert one fire.

______________________________________________________________________

### WR-03: AKP153 + AKP03 test geometry transposed from register.cpp descriptors

**Files modified:** `tests/qml/test_device_view_geometry.qml`,
`tests/qml/test_device_view_tests.cpp`
**Commit:** a4f74be
**Applied fix:** Corrected `keyRows` and `gridColumns` arguments to match
`akp153_descriptor` (keyRows=3, gridColumns=5) and `akp03_descriptor`
(keyRows=2, gridColumns=3) in both test files. The previous transposed values
still produced correct key counts but would return wrong `keyRowsResolved` under
production code.

______________________________________________________________________

### CR-03: selectedZoneIndex is never set positive; Inspector zone selection broken

**Files modified:** `src/app/qml/components/TouchStripLane.qml`,
`src/app/qml/DeviceView.qml`
**Commit:** b7f6f5f
**Applied fix:**

1. Added `signal zoneTapped(int idx)` to `TouchZoneCell`; wired
   `ItemDelegate.onClicked: zoneCell.zoneTapped(zoneCell.zoneIndex)`.
1. Bubbled the signal through `TouchStripLane.zoneTapped`.
1. Added `onZoneTapped` handler in `DeviceView.qml` to clear other selection
   indices, set `selectedZoneIndex = idx`, and emit `zoneSelected(idx)` —
   matching the existing key/encoder click handler pattern.

______________________________________________________________________

### WR-04: DeviceModel roleNames does not expose keyRows or touchZoneCount as model roles

**Files modified:** `src/app/src/device_model.hpp`, `src/app/src/device_model.cpp`
**Commit:** 52d4ef7
**Applied fix:** Added `KeyRowsRole`, `TouchZoneCountRole`, `MainScreenWidthPxRole`,
and `MainScreenHeightPxRole` to the `Roles` enum in `device_model.hpp`. Wired the
new cases in `data()` and added the corresponding names to `roleNames()`. Names
match the keys already exposed by `capabilitiesFor()` for consistency.

______________________________________________________________________

### CR-01 + WR-06: JSON reader silently discards touchZones when key precedes \_schemaVersion

**Files modified:** `src/core/src/profile.cpp`,
`tests/unit/test_profile_serialization.cpp`
**Commit:** 6ac5a24
**Applied fix:**

- Removed the `if (schemaVersion >= 2)` guard from the `"touchZones"` branch in
  `profileFromJson()`. Parse the key unconditionally when present; genuine v1 files
  have no `"touchZones"` key so the branch is never reached for them.
- Marked `schemaVersion` `[[maybe_unused]]` to suppress `-Werror=unused-but-set-variable`
  (the variable is still read from the stream for future dispatch).
- Added regression test: "v2 profile with touchZones key before \_schemaVersion
  round-trips" — asserts `p.touchZones.size() == 1` when `"touchZones"` precedes
  `"_schemaVersion"` in the JSON literal.

______________________________________________________________________

### WR-02: anyDragActive is declared but never set; trash-zone opacity always 0.3

**Files modified:** `src/app/qml/DeviceView.qml`,
`src/app/qml/components/KeyCell.qml`, `src/app/qml/components/EncoderDial.qml`,
`src/app/qml/components/TouchStripLane.qml`
**Commit:** c257678
**Applied fix:**

1. Added `readonly property bool dragActive: Drag.active` to `KeyCell` and
   `EncoderDial`.
1. Added `readonly property bool dragActive: Drag.active` to `TouchZoneCell`;
   bubbled through a new `zoneDragActiveChanged(bool)` signal on `TouchStripLane`.
1. Replaced `property bool anyDragActive: false` in `DeviceView` with a counter
   `_activeDragCount: int` + `readonly anyDragActive: _activeDragCount > 0`.
1. Added `onDragActiveChanged` handlers on each delegate and on `TouchStripLane`
   to increment/decrement the counter.

______________________________________________________________________

### WR-01 + WR-05: Synchronous XHR in loadLayout; \_hasPhoto dead property

**Files modified:** `src/app/qml/DeviceView.qml`
**Commit:** 8eede93
**Applied fix:**

- Switched `xhr.open("GET", url, false)` to `xhr.open("GET", url, true)` with
  `xhr.onreadystatechange` callback pattern — avoids Qt 6.8+ deprecation warning
  and removes the main-thread block.
- Replaced `Qt.createQmlObject('import QtQml 2.15; XMLHttpRequest {}', root)`
  with the standard `new XMLHttpRequest()` constructor (no leaked QObject child;
  no per-call component compile overhead).
- Added a `TODO(Phase 26 Plan 26-06)` comment to `_hasPhoto` documenting that it
  will gate per-SKU photo rendering when device-layouts JSONs with a `"photo"` key
  land. The property is preserved so photo activation requires zero DeviceView
  structural changes.

## Skipped Issues

None — all 10 in-scope findings were fixed.

______________________________________________________________________

_Fixed: 2026-05-28T18:30:00Z_
_Fixer: Claude (gsd-code-fixer)_
_Iteration: 1_
