// SPDX-License-Identifier: GPL-3.0-or-later
//
// TouchStripLane.qml -- horizontal row of touch-strip SEGMENTS for DeviceView.
//
// US3 (002-streamdeck-plugin-ui): on Stream Deck + class dial devices the
// touch-strip segment above each dial BELONGS to that dial (one control = dial +
// segment, the Elgato model). The lane therefore renders one segment per dial,
// each MIRRORING its dial's bound action (icon + label) from `segmentModel`
// (the per-encoder binding model owned by DeviceView). The segment is READ-ONLY
// here: the dial below is the single drop target, so this lane has no drop area
// and no drag source (the former independent "TouchZone" binding is retired for
// dial devices). A tap on segment N selects dial N via `segmentTapped`.
//
// LED-strip hint: a 4px Theme.accent top border suggests the physical LED bar
// above the touch strip (OpenDeck Key.svelte pattern).
//
// Accessibility: outer Item has Accessible.Group; per-cell Accessible.Button.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import AjazzControlCenter

Item {
    id: root

    // One segment per dial. Parent (DeviceCanvas) sets this to encoderCount so
    // segment N sits exactly above dial N.
    required property int segmentCount
    // Per-encoder binding ListModel {iconSource,label,...}; row N drives segment
    // N. Null/short model => the segment renders the empty-state glyph.
    property var segmentModel: null
    // The currently-selected dial index (so the owning segment highlights too).
    property int selectedIndex: -1

    // Inter-segment gap. The parent sets this to the SAME value the dial lane
    // uses so segment N lines up exactly above dial N (Stream Dock Plus).
    property int cellSpacing: Theme.spacingXs

    // Segments share the lane width evenly so the strip spans the key grid and
    // aligns with the dials below.
    readonly property real _cellWidth: segmentCount > 0
        ? Math.max(Theme.minTouchTarget,
                   (width - (segmentCount - 1) * cellSpacing) / segmentCount)
        : width

    /// Emitted when the user taps segment N; DeviceCanvas maps it to selecting
    /// dial N (the segment is owned by its dial).
    signal segmentTapped(int idx)

    implicitWidth:  segmentCount * 120 + Math.max(0, segmentCount - 1) * Theme.spacingXs
    implicitHeight: Math.max(Theme.minTouchTarget, 48)

    Row {
        id: row
        anchors.fill: parent
        spacing: root.cellSpacing

        Repeater {
            model: root.segmentCount
            delegate: SegmentCell {
                required property int index

                segmentIndex: index
                iconSource: (root.segmentModel && index < root.segmentModel.count)
                    ? root.segmentModel.get(index).iconSource : ""
                segmentLabel: (root.segmentModel && index < root.segmentModel.count)
                    ? root.segmentModel.get(index).label : ""
                selected: root.selectedIndex === index
                width:  root._cellWidth
                height: row.height

                onSegmentTapped: (idx) => root.segmentTapped(idx)
            }
        }
    }

    Accessible.role: Accessible.Grouping
    Accessible.name: qsTr("Touch strip")

    // -------------------------------------------------------------------------
    // SegmentCell -- inline private component. Read-only mirror of its dial's
    // bound action; selecting it selects the dial (no drag, no drop here).
    // -------------------------------------------------------------------------
    component SegmentCell: Item {
        id: segCell

        property int    segmentIndex: 0
        property url    iconSource:   ""
        property string segmentLabel: ""
        property bool   selected:     false

        signal segmentTapped(int idx)

        width:  Math.max(Theme.minTouchTarget, 120)
        height: Math.max(Theme.minTouchTarget, 48)

        // Background with LED-strip hint (thickened top border).
        Rectangle {
            id: bg
            anchors.fill: parent
            radius: Theme.radiusSm
            color: segDelegate.hovered ? Theme.tileHover : Theme.tile
            border.width: (segDelegate.activeFocus || segCell.selected)
                ? Theme.focusRingWidth : 1
            border.color: (segDelegate.activeFocus || segCell.selected)
                ? Theme.accent : Theme.borderSubtle
        }

        // Thickened top accent border for the LED-strip visual hint.
        Rectangle {
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: 4
            color: Theme.accent
        }

        // Icon layer — the dial's bound-action icon (mirrors the dial).
        Image {
            anchors.centerIn: parent
            width: parent.width - Theme.spacingMd * 2
            height: parent.height - Theme.spacingMd * 2 - 4 // subtract LED strip
            source: segCell.iconSource
            fillMode: Image.PreserveAspectFit
            smooth: true
            asynchronous: true
            visible: segCell.iconSource.toString() !== ""
        }

        // Default glyph when the dial has no bound action.
        Text {
            anchors.centerIn: parent
            font.family: "Material Symbols Outlined"
            font.pixelSize: 20
            text: "view_column"
            color: Qt.rgba(Theme.fgPrimary.r, Theme.fgPrimary.g, Theme.fgPrimary.b, 0.5)
            visible: segCell.iconSource.toString() === ""
        }

        // Label overlay (the dial's action title).
        Text {
            anchors.bottom: parent.bottom
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottomMargin: Theme.spacingXs
            text: segCell.segmentLabel
            color: Theme.fgPrimary
            font.pixelSize: Theme.typeLabelMedium.pixelSize
            font.weight: Theme.typeLabelMedium.weight
            horizontalAlignment: Text.AlignHCenter
            visible: segCell.segmentLabel !== ""
        }

        // ItemDelegate for focus + click → select the owning dial.
        ItemDelegate {
            id: segDelegate
            objectName: "touchSegment_" + segCell.segmentIndex
            anchors.fill: parent
            background: Item {}   // visual handled by bg above
            onClicked: segCell.segmentTapped(segCell.segmentIndex)
        }

        Accessible.role: Accessible.Button
        Accessible.name: segCell.segmentLabel !== ""
            ? qsTr("Dial %1 touch segment: %2").arg(segCell.segmentIndex + 1).arg(segCell.segmentLabel)
            : qsTr("Dial %1 touch segment").arg(segCell.segmentIndex + 1)
        Accessible.description: qsTr("Press Space to select this dial")
    }
}
