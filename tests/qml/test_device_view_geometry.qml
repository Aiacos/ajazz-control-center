// SPDX-License-Identifier: GPL-3.0-or-later
//
// test_device_view_geometry.qml -- offscreen geometry-rendering tests for DeviceView.
//
// REQ-26-B acceptance: validates that DeviceView renders the correct number of
// KeyCell / EncoderDial / TouchStripLane zone cells for three representative
// device SKU classes:
//
//   AKP05E: 5x2 key grid (10 keys) + 4 encoder dials + 4 touch zones
//   AKP153: 3x5 key grid (15 keys) + 0 encoder dials + 0 touch zones
//   AKP03:  2x3 key grid  (6 keys) + 3 encoder dials + 0 touch zones
//
// Run under QT_QPA_PLATFORM=offscreen; no GPU or display required.
// Test names are ASCII-only (CLAUDE.md).
//
// Geometry values match register.cpp descriptors (REQ-26-C, WR-03 fix):
//   AKP05E keyRows=2, gridColumns=5, 10 keys + 4 encoders + 4 zones
//   AKP153 keyRows=3, gridColumns=5, 15 keys + 0 encoders + 0 zones
//   AKP03  keyRows=2, gridColumns=3,  6 keys + 3 encoders + 0 zones
import QtQuick
import QtQuick.Controls
import QtTest
import AjazzControlCenter

TestCase {
    id: root
    name: "DeviceViewGeometry"
    // The window must be visible (even on offscreen) so items polish and their
    // Repeaters actually complete model delegation before we read back counts.
    when: windowShown

    // ---- AKP05E: keyRows=2, gridColumns=5 (10 keys) + 4 encoders + 4 touch zones -

    DeviceView {
        id: dvAkp05e
        visible: false
        width: 800
        height: 480
        keyCount:      10
        keyRows:        2
        gridColumns:    5
        encoderCount:   4
        touchZoneCount: 4
        codename:      "akp05e"
    }

    function test_akp05e_renders_5x2_grid_plus_4_dials_plus_4_zones() {
        compare(dvAkp05e.keyCellsRendered,    10, "AKP05E key cells")
        compare(dvAkp05e.encoderDialsRendered, 4, "AKP05E encoder dials")
        compare(dvAkp05e.touchZonesRendered,   4, "AKP05E touch zones")
    }

    // ---- AKP153: keyRows=3, gridColumns=5 (15 keys) + 0 encoders + 0 touch zones --
    // Values aligned with akp153_descriptor in register.cpp (REQ-26-C, WR-03).

    DeviceView {
        id: dvAkp153
        visible: false
        width: 800
        height: 480
        keyCount:      15
        keyRows:        3   // akp153_descriptor: keyRows=3 (3 rows x 5 cols)
        gridColumns:    5   // akp153_descriptor: gridColumns=5
        encoderCount:   0
        touchZoneCount: 0
        codename:      "akp153"
    }

    function test_akp153_renders_3x5_grid_no_dials_no_zones() {
        compare(dvAkp153.keyCellsRendered,     15, "AKP153 key cells")
        compare(dvAkp153.encoderDialsRendered,  0, "AKP153 encoder dials")
        compare(dvAkp153.touchZonesRendered,    0, "AKP153 touch zones")
    }

    // ---- AKP03: keyRows=2, gridColumns=3 (6 keys) + 3 encoders + 0 touch zones ----
    // Values aligned with akp03_descriptor in register.cpp (REQ-26-C, WR-03).

    DeviceView {
        id: dvAkp03
        visible: false
        width: 800
        height: 480
        keyCount:       6
        keyRows:        2   // akp03_descriptor: keyRows=2 (2 rows x 3 cols)
        gridColumns:    3   // akp03_descriptor: gridColumns=3
        encoderCount:   3
        touchZoneCount: 0
        codename:      "akp03"
    }

    function test_akp03_renders_2x3_grid_plus_3_dials_no_zones() {
        compare(dvAkp03.keyCellsRendered,     6, "AKP03 key cells")
        compare(dvAkp03.encoderDialsRendered, 3, "AKP03 encoder dials")
        compare(dvAkp03.touchZonesRendered,   0, "AKP03 touch zones")
    }

    // ======================================================================
    // EDIT-01 (Phase 32 Plan 32-05): scrollable device-view editor.
    //
    // The device grid (DeviceCanvas) is wrapped in a vertical-only
    // ScrollView#deviceCanvasScroll inside chassisArea. These cases assert the
    // 32-UI-SPEC acceptance contract from the descriptor geometry alone:
    //   - over-threshold SKU is scrollable (vertical ScrollBar.size < 1.0)
    //   - under-threshold SKU is not scrollable (vertical ScrollBar.size >= 1.0)
    //   - the horizontal ScrollBar policy is AlwaysOff (no horizontal scroll)
    //   - trashBtn stays a chassisArea child (pinned), NOT inside the scroll
    //   - key/encoder/touch delegates stay distinct on an all-three SKU
    // Test names are ASCII-only (CLAUDE.md).
    // ======================================================================

    // ---- helpers: walk the visual tree to find named items / type lanes ----

    function _findByObjectName(item, name) {
        if (!item)
            return null
        if (item.objectName === name)
            return item
        var kids = item.children
        for (var i = 0; i < kids.length; ++i) {
            var found = _findByObjectName(kids[i], name)
            if (found)
                return found
        }
        // ScrollView/Flickable hold their content under contentItem too.
        if (item.contentItem && item.contentItem !== item) {
            var f = _findByObjectName(item.contentItem, name)
            if (f)
                return f
        }
        return null
    }

    function _countByTypeName(item, typeName, acc) {
        if (!item)
            return acc
        // toString() looks like "TypeName_QMLTYPE_NN(0x...)" / "TypeName(0x...)".
        if (item.toString().indexOf(typeName) === 0)
            acc.n += 1
        var kids = item.children
        for (var i = 0; i < kids.length; ++i)
            _countByTypeName(kids[i], typeName, acc)
        if (item.contentItem && item.contentItem !== item)
            _countByTypeName(item.contentItem, typeName, acc)
        return acc
    }

    // ---- OVER-threshold: 9 columns (> 8) so _gridOverflows is true ----------
    // 9 columns x 5 rows = 45 keys -> definitely taller/wider than the viewport.

    DeviceView {
        id: dvOver
        visible: true
        width: 360
        height: 240
        keyCount:      45
        keyRows:        5
        gridColumns:    9
        encoderCount:   4
        touchZoneCount: 4
        codename:      "overflow"
    }

    function test_edit01_over_threshold_grid_overflows() {
        verify(dvOver._gridOverflows, "9-column grid must be over-threshold")
    }

    function test_edit01_over_threshold_is_scrollable() {
        var sv = _findByObjectName(dvOver, "deviceCanvasScroll")
        verify(sv !== null, "deviceCanvasScroll must be addressable by objectName")
        // size < 1.0 means content exceeds the viewport => vertically scrollable.
        verify(sv.ScrollBar.vertical.size < 1.0,
               "over-threshold vertical ScrollBar.size must be < 1.0 (scrollable)")
    }

    // ---- UNDER-threshold: 3x3 grid, no encoders/zones -> not scrollable -----

    DeviceView {
        id: dvUnder
        visible: true
        width: 800
        height: 600
        keyCount:      9
        keyRows:        3
        gridColumns:    3
        encoderCount:   0
        touchZoneCount: 0
        codename:      "compact"
    }

    function test_edit01_under_threshold_does_not_overflow() {
        verify(!dvUnder._gridOverflows, "3x3 grid must be under-threshold")
    }

    function test_edit01_under_threshold_is_not_scrollable() {
        var sv = _findByObjectName(dvUnder, "deviceCanvasScroll")
        verify(sv !== null, "deviceCanvasScroll must be addressable by objectName")
        // size >= 1.0 means the whole content fits the viewport (no scroll).
        verify(sv.ScrollBar.vertical.size >= 1.0,
               "under-threshold vertical ScrollBar.size must be >= 1.0 (not scrollable)")
    }

    // ---- No horizontal scroll, ever (vertical-only contract) ----------------

    function test_edit01_horizontal_scroll_is_always_off() {
        var sv = _findByObjectName(dvOver, "deviceCanvasScroll")
        verify(sv !== null, "deviceCanvasScroll must be addressable")
        compare(sv.ScrollBar.horizontal.policy, ScrollBar.AlwaysOff,
                "horizontal scroll policy must be AlwaysOff")
        // Even on the over-threshold SKU the content width is bounded to the
        // viewport, so the horizontal scrollbar must remain non-scrollable.
        verify(sv.ScrollBar.horizontal.size >= 1.0,
               "horizontal ScrollBar.size must be >= 1.0 (never horizontally scrollable)")
    }

    // ---- Scroll position is machine-readable (deviceCanvasFlick) ------------

    function test_edit01_scroll_position_is_readable() {
        var flick = _findByObjectName(dvOver, "deviceCanvasFlick")
        verify(flick !== null, "deviceCanvasFlick (contentItem) must be addressable")
        verify(flick.contentHeight > 0, "Flickable contentHeight must be readable")
        verify(flick.contentY >= 0, "Flickable contentY must be readable")
    }

    // ---- Trash zone stays pinned (chassisArea child, NOT inside the scroll) -

    function test_edit01_trash_pinned_outside_scroll() {
        // trashBtn has id but no objectName; assert it is NOT a descendant of
        // deviceCanvasScroll by walking the scroll subtree for the RoundButton.
        var sv = _findByObjectName(dvOver, "deviceCanvasScroll")
        verify(sv !== null, "deviceCanvasScroll must exist")
        var acc = { n: 0 }
        _countByTypeName(sv, "RoundButton", acc)
        compare(acc.n, 0, "trashBtn (RoundButton) must NOT live inside the ScrollView")
    }

    // ---- Delegates stay distinct on an all-three-controller SKU -------------

    function test_edit01_delegates_remain_distinct() {
        // dvOver has keys + 4 encoders + 4 touch zones: all three lanes present.
        var keys = { n: 0 }
        _countByTypeName(dvOver, "KeyCell", keys)
        verify(keys.n > 0, "KeyCell delegates must be present")

        var lanes = { n: 0 }
        _countByTypeName(dvOver, "TouchStripLane", lanes)
        verify(lanes.n > 0, "TouchStripLane (touch zones) must be present")

        var dials = { n: 0 }
        _countByTypeName(dvOver, "EncoderDial", dials)
        verify(dials.n > 0, "EncoderDial (dials) must be present")
    }
}
