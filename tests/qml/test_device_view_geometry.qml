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
import QtQuick
import QtTest
import AjazzControlCenter

TestCase {
    id: root
    name: "DeviceViewGeometry"
    // The window must be visible (even on offscreen) so items polish and their
    // Repeaters actually complete model delegation before we read back counts.
    when: windowShown

    // ---- AKP05E: 5 columns x 2 rows (10 keys) + 4 encoders + 4 touch zones -

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

    // ---- AKP153: 3 columns x 5 rows (15 keys) + 0 encoders + 0 touch zones --

    DeviceView {
        id: dvAkp153
        visible: false
        width: 800
        height: 480
        keyCount:      15
        keyRows:        5
        gridColumns:    3
        encoderCount:   0
        touchZoneCount: 0
        codename:      "akp153"
    }

    function test_akp153_renders_3x5_grid_no_dials_no_zones() {
        compare(dvAkp153.keyCellsRendered,     15, "AKP153 key cells")
        compare(dvAkp153.encoderDialsRendered,  0, "AKP153 encoder dials")
        compare(dvAkp153.touchZonesRendered,    0, "AKP153 touch zones")
    }

    // ---- AKP03: 2 columns x 3 rows (6 keys) + 3 encoders + 0 touch zones ----

    DeviceView {
        id: dvAkp03
        visible: false
        width: 800
        height: 480
        keyCount:       6
        keyRows:        3
        gridColumns:    2
        encoderCount:   3
        touchZoneCount: 0
        codename:      "akp03"
    }

    function test_akp03_renders_2x3_grid_plus_3_dials_no_zones() {
        compare(dvAkp03.keyCellsRendered,     6, "AKP03 key cells")
        compare(dvAkp03.encoderDialsRendered, 3, "AKP03 encoder dials")
        compare(dvAkp03.touchZonesRendered,   0, "AKP03 touch zones")
    }
}
