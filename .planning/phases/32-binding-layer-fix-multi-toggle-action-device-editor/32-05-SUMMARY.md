---
phase: 32
plan: 05
subsystem: device-editor-ui
tags: [edit-01, scrollview, device-view, qml, qt6]
requires:
  - DeviceView.qml geometry properties (keyRowsResolved, _keyColumnsResolved, encoderCount, touchZoneCount)
  - DeviceCanvas.qml content-sized device frame
provides:
  - ScrollView#deviceCanvasScroll vertical wrapper around the device grid
  - deviceCanvasFlick (contentItem) debug-addressable scroll surface
  - DeviceView._gridOverflows readonly overflow predicate
affects:
  - src/app/qml/DeviceView.qml
  - src/app/qml/components/DeviceCanvas.qml
  - tests/qml/test_device_view_tests.cpp
  - tests/qml/test_device_view_geometry.qml
  - tests/qml/CMakeLists.txt
tech-stack:
  added: [Qt6::Test (test target link, for QTest::qWait)]
  patterns: [vertical-only ScrollView (content width = availableWidth), offscreen QQuickWindow + qWait for layout-dependent QML tests]
key-files:
  created: []
  modified:
    - src/app/qml/DeviceView.qml
    - src/app/qml/components/DeviceCanvas.qml
    - tests/qml/test_device_view_tests.cpp
    - tests/qml/test_device_view_geometry.qml
    - tests/qml/CMakeLists.txt
decisions:
  - Vertical-only enforced by binding DeviceCanvas.implicitWidth to width (not frame.width) + ScrollBar.horizontal.policy AlwaysOff; a ScrollView sizes contentWidth from the contentItem's implicit size, so reporting frame.width would force a phantom horizontal scrollbar.
  - EDIT-01 scroll assertions live in C++ Catch2 (test_device_view_tests.cpp) because that is the path ctest actually runs; the .qml QtTest spec was also extended per plan but is not wired to a QtTest runner.
  - Geometry/Repeater-dependent assertions host DeviceView in a real offscreen QQuickWindow + QTest::qWait — a bare QQmlComponent::create() never runs a layout pass, so Flickable contentHeight and Repeater delegates do not settle.
metrics:
  duration: ~50m
  completed: 2026-06-08
---

# Phase 32 Plan 05: Scrollable Device-View Editor (EDIT-01) Summary

Wrapped the device grid (DeviceCanvas) in a vertical-only, debug-addressable
`ScrollView#deviceCanvasScroll` inside `chassisArea` so oversized SKU grids
scroll instead of clipping/overflowing, per 32-UI-SPEC. Container-level change
only: no delegate redesign, no new theming/colors/copy, no new QML import.

## What was built

- **`src/app/qml/DeviceView.qml`**

  - New `ScrollView { id: deviceCanvasScroll; objectName: "deviceCanvasScroll" }`
    as a sibling of `chassisPhoto` / `trashBtn` / `EmptyState` inside
    `chassisArea`. It takes over the canvas's former `anchors.fill` +
    `anchors.margins: Theme.spacingMd`.
  - `clip: true`, `ScrollBar.horizontal.policy: ScrollBar.AlwaysOff`,
    `ScrollBar.vertical.policy: ScrollBar.AsNeeded`,
    `visible: !chassisPhoto.visible`.
  - `contentItem.objectName: "deviceCanvasFlick"` so `qml.get deviceCanvasFlick`
    exposes `contentY` / `contentHeight`.
  - `DeviceCanvas` moved inside as the content child:
    `width: deviceCanvasScroll.availableWidth` (vertical-only),
    `height: Math.max(implicitHeight, deviceCanvasScroll.availableHeight)`
    (fills viewport and centers when shorter; overflows when taller).
  - New readonly `_gridOverflows` predicate:
    `(_keyColumnsResolved > 8) || ((keyRowsResolved + (encoderCount>0?1:0) + (touchZoneCount>0?1:0)) > 4)`.
  - `trashBtn` + `EmptyState` + `chassisPhoto` remain pinned `chassisArea`
    siblings (NOT inside the scroll). Delegates untouched.

- **`src/app/qml/components/DeviceCanvas.qml`**

  - Exposed `implicitHeight: frame.height` as the scrollable content extent.
  - `implicitWidth: width` (deliberately NOT `frame.width`) so the wrapping
    ScrollView's contentWidth never exceeds the viewport — the vertical-only
    guarantee.

- **`tests/qml/test_device_view_tests.cpp`** (the ctest-run path)

  - 8 new `DeviceViewScroll::*` Catch2 cases: addressable scroll container +
    flickable; over-threshold `_gridOverflows` true + `contentHeight > viewport`;
    under-threshold not-scrollable; no horizontal scroll
    (`contentWidth <= viewport`); trash `RoundButton` count inside the ScrollView
    is 0 while the DeviceView still contains it; key/encoder/touch delegates stay
    distinct (`KeyCell` / `EncoderDial` / `TouchStripLane` all > 0).
  - `createDeviceViewWindow()` hosts DeviceView in an offscreen `QQuickWindow` +
    `QTest::qWait(50)`; `countByTypePrefix` walks both QObject children and visual
    `childItems()` (Repeater delegates are reparented as childItems).

- **`tests/qml/test_device_view_geometry.qml`** (QtTest spec, extended per plan)

  - Added mirror EDIT-01 cases (over/under-threshold `_gridOverflows` +
    `ScrollBar.vertical.size`, horizontal AlwaysOff, trash-pinned,
    delegate-distinct) with ASCII-only titles and a `QtQuick.Controls` import.

- **`tests/qml/CMakeLists.txt`**: link `Qt6::Test` for `QTest::qWait`.

## Verification (run by executor, green)

- `ctest --preset linux-release -L qml` -> **17/17 passed** (was 9; +8 EDIT-01).
- `ctest --preset linux-release -LE qml` -> **715/715 passed** (no regression).
- Full app target (`ajazz-control-center`) builds clean (shared DeviceView.qml /
  DeviceCanvas.qml recompiled by qmlcachegen).
- Acceptance greps all satisfied: `deviceCanvasScroll`, `deviceCanvasFlick`,
  `ScrollBar.horizontal.policy: ScrollBar.AlwaysOff`, `availableWidth` in
  DeviceView.qml; `_gridOverflows` (>=2), `AlwaysOff`/`horizontal`, `trashBtn`/
  `RoundButton` in the .qml test.

## Deviations from Plan

### Auto-fixed / adjusted (Rule 3 - blocking)

1. **[Rule 3] EDIT-01 assertions implemented in C++ Catch2, not only the .qml.**

   - **Found during:** Task 2.
   - **Issue:** `tests/qml/test_device_view_geometry.qml` is bundled in the QML
     module but is NOT wired to any QtTest runner — the `DeviceViewGeometry`
     ctest cases are actually C++ Catch2 cases in `test_device_view_tests.cpp`.
     Extending only the .qml would have left the EDIT-01 contract unexercised by
     ctest (the project's real gate).
   - **Fix:** Added the load-bearing assertions as 8 `DeviceViewScroll::*` Catch2
     cases in `test_device_view_tests.cpp` (so ctest runs them), AND extended the
     `.qml` spec as the plan literally requested (acceptance-grep + parity).
   - **Files:** tests/qml/test_device_view_tests.cpp, tests/qml/CMakeLists.txt.
   - **Commit:** 791b47c.

1. **[Rule 3] DeviceCanvas.implicitWidth must track width, not frame.width.**

   - **Found during:** Task 1 test bring-up — a first cut set
     `implicitWidth: frame.width`, which made the ScrollView's contentWidth equal
     the frame width (796px) and produced a phantom horizontal scrollbar
     (`contentWidth=796 > viewport=36`).
   - **Fix:** Bound `implicitWidth: width`; a ScrollView sizes content from the
     contentItem's implicit size, so the content width must equal the viewport.
   - **Files:** src/app/qml/components/DeviceCanvas.qml.
   - **Commit:** 535dde3.

1. **[Rule 3] Layout-dependent QML tests need a shown QQuickWindow + qWait.**

   - **Found during:** Task 2 — a bare `QQmlComponent::create()` never runs a
     layout pass, so Flickable `contentHeight` was 0/stale and Repeater
     delegates (`KeyCell`/`EncoderDial`) were not instantiated.
   - **Fix:** `createDeviceViewWindow()` parents DeviceView into an offscreen
     `QQuickWindow`, shows it, and `QTest::qWait(50)`s; linked `Qt6::Test`.
   - **Files:** tests/qml/test_device_view_tests.cpp, tests/qml/CMakeLists.txt.
   - **Commit:** 791b47c.

No architectural (Rule 4) changes. No new dependencies (T-32-SC mitigation
holds: zero new QML imports; `Qt6::Test` is a test-only link, not a runtime dep).

## Known Stubs

None. The wrapper surfaces existing strings/controls only; no placeholder data.

## Pending live verification

Task 3 of the plan is a `checkpoint:human-verify` live debug-channel pass. Per
executor instruction the GUI app was NOT launched; ctest is necessary but not
sufficient (CLAUDE.md). The orchestrator's consolidated live pass should run:

1. **Launch headless:**
   ```
   AJAZZ_DEBUG_CONTROL=1 nohup build/linux-release/src/app/ajazz-control-center &
   ```
1. **Addressability:**
   ```
   scripts/ajazz-debug qml.get deviceCanvasScroll
   scripts/ajazz-debug qml.get deviceCanvasFlick
   ```
   Expect: `deviceCanvasScroll` returns an object including
   `ScrollBar.vertical.size` / `.position` and `ScrollBar.horizontal.policy`;
   `deviceCanvasFlick` returns `contentY` + `contentHeight`.
1. **Over-threshold (vertical scrollbar present, none horizontal):** drive an
   over-threshold SKU (>8 cols OR keyRows+encoderRow+touchRow>4), e.g.
   ```
   scripts/ajazz-debug device.setActiveDevice --params '{"codename":"<over-threshold-sku>"}'
   scripts/ajazz-debug qml.get deviceCanvasScroll      # expect ScrollBar.vertical.size < 1.0
   scripts/ajazz-debug screenshot                       # expect a vertical scrollbar, NO horizontal
   ```
   (If no built-in SKU exceeds the threshold, use `device.renderTest` with an
   over-threshold geometry to populate the canvas.)
1. **Under-threshold (no vertical scrollbar, frame centered):**
   ```
   scripts/ajazz-debug device.setActiveDevice --params '{"codename":"akp03"}'   # 2x3 + 3 enc
   scripts/ajazz-debug qml.get deviceCanvasScroll      # expect ScrollBar.vertical.size >= 1.0
   scripts/ajazz-debug screenshot                       # expect no vertical scrollbar, centered
   ```
1. **Scroll position changes + trash pinned:**
   ```
   scripts/ajazz-debug qml.set deviceCanvasFlick --params '{"contentY": 120}'
   scripts/ajazz-debug qml.get deviceCanvasFlick       # expect contentY ~= 120
   scripts/ajazz-debug screenshot                       # trashBtn still anchored top-right
   ```
1. **Delegates distinct (all-three-controller SKU):** `screenshot` shows square
   key tiles, a horizontal touch strip, and circular dials as three lanes;
   selecting each updates `selectedKeyIndex` / `selectedEncoderIndex` /
   `selectedZoneIndex` on DeviceView via `qml.get`.

Resume signal: "approved" once `qml.get` returns scroll-position props,
over/under threshold behaves, no horizontal scrollbar appears, trash stays
pinned, and delegates look distinct.

## Self-Check: PASSED
