// SPDX-License-Identifier: GPL-3.0-or-later
//
// TouchStripLane.qml -- horizontal row of touch-zone cells for DeviceView.
//
// Each zone cell is a discrete drop target (not one wide cell). The lane
// renders `touchZoneCount` cells side-by-side with `cellSpacing` gaps. When the
// parent constrains the lane width the zones stretch to share it evenly, so the
// strip can be sized to span the key grid and align with the dial lane below.
//
// LED-strip hint: top border is thickened to 4px Theme.accent to suggest
// the physical LED bar above the touch strip (OpenDeck Key.svelte pattern).
//
// Drag-drop (Phase 26 / REQ-26-B):
//   * Each zone cell is a drag SOURCE when occupied (iconSource non-empty).
//   * Each zone cell is ALWAYS a drop TARGET accepting "application/x-ajazz-action"
//     (library -> zone) and "application/x-ajazz-binding" (same-controller swap).
//
// Accessibility: outer Item has Accessible.Group; per-cell Accessible.Button.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import AjazzControlCenter

Item {
    id: root

    required property int touchZoneCount
    // Arrays of url and string, one entry per zone (index 0..touchZoneCount-1).
    // Parent passes empty arrays when bindings are not yet loaded; cells fall
    // back to the empty-state placeholder glyph.
    required property var zoneIconSources
    required property var zoneLabels

    // Inter-zone gap. The parent (DeviceCanvas) sets this to the SAME value the
    // dial lane uses so zone N lines up exactly above dial N (Stream Dock Plus).
    property int cellSpacing: Theme.spacingXs

    // Zones share the lane width evenly: when the parent constrains `width`
    // (e.g. Layout.preferredWidth = the key-grid content width), each zone
    // stretches to fill its share so the strip spans the grid and aligns with
    // the dials below. Falls back to a sensible minimum touch target.
    readonly property real _cellWidth: touchZoneCount > 0
        ? Math.max(Theme.minTouchTarget,
                   (width - (touchZoneCount - 1) * cellSpacing) / touchZoneCount)
        : width

    signal zoneSwapRequested(int srcIndex, int dstIndex)
    /// Emitted when the user taps (clicks) a zone cell; carries the 0-based zone index.
    /// Connected by DeviceView.qml to update selectedZoneIndex and drive the Inspector.
    signal zoneTapped(int idx)
    /// Emitted when any zone cell drag starts (true) or all zone drags end (false).
    /// DeviceView.qml watches this to maintain anyDragActive (WR-02).
    signal zoneDragActiveChanged(bool active)

    // Implicit size: fits the row of cells.
    implicitWidth:  touchZoneCount * 120 + Math.max(0, touchZoneCount - 1) * Theme.spacingXs
    implicitHeight: Math.max(Theme.minTouchTarget, 48)

    Row {
        id: row
        anchors.fill: parent
        spacing: root.cellSpacing

        Repeater {
            model: root.touchZoneCount
            delegate: TouchZoneCell {
                required property int index

                zoneIndex:   index
                iconSource:  index < root.zoneIconSources.length
                             ? root.zoneIconSources[index] : ""
                zoneLabel:   index < root.zoneLabels.length
                             ? root.zoneLabels[index]      : ""
                width:       root._cellWidth
                height:      row.height

                onZoneSwapRequested: function(src, dst) {
                    root.zoneSwapRequested(src, dst);
                }
                onZoneTapped: function(tappedIdx) {
                    root.zoneTapped(tappedIdx);
                }
                onDragActiveChanged: function(active) {
                    root.zoneDragActiveChanged(active);
                }
            }
        }
    }

    Accessible.role: Accessible.Grouping
    Accessible.name: qsTr("Touch strip")

    // -------------------------------------------------------------------------
    // TouchZoneCell -- inline private component (no separate file needed; it is
    // only ever instantiated by TouchStripLane).
    // -------------------------------------------------------------------------
    component TouchZoneCell: Item {
        id: zoneCell

        property int    zoneIndex:  0
        property url    iconSource: ""
        property string zoneLabel:  ""
        /// True while this zone cell is being dragged (WR-02).
        readonly property bool dragActive: Drag.active

        signal zoneSwapRequested(int srcIndex, int dstIndex)
        /// Emitted when the user taps this zone cell; forwarded by TouchStripLane to DeviceView.
        signal zoneTapped(int idx)

        width:  Math.max(Theme.minTouchTarget, 120)
        height: Math.max(Theme.minTouchTarget, 48)

        // ----- Drag-over scale animation (D-10) ---------------------------
        transform: Scale {
            id: zoneCellScale
            origin.x: zoneCell.width / 2
            origin.y: zoneCell.height / 2
            xScale: 1.0
            yScale: 1.0
            Behavior on xScale {
                NumberAnimation { duration: 100; easing.type: Easing.OutQuad }
            }
            Behavior on yScale {
                NumberAnimation { duration: 100; easing.type: Easing.OutQuad }
            }
        }

        // Background rectangle with LED-strip hint (thickened top border).
        Rectangle {
            id: bg
            anchors.fill: parent
            radius: Theme.radiusSm
            color: zoneDelegate.hovered ? Theme.tileHover : Theme.tile
            // UI-REVIEW.md fix: drag-rejected state shows errorAccent border
            // for cross-controller drag-over.
            border.width: zoneDropArea.dragRejected
                ? Theme.focusRingWidth
                : 1
            border.color: zoneDropArea.dragRejected
                ? Theme.errorAccent
                : (zoneDelegate.activeFocus || zoneDelegate.selected
                   ? Theme.accent : Theme.borderSubtle)
        }

        // Thickened top accent border for LED-strip visual hint.
        Rectangle {
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: 4
            color: Theme.accent
        }

        // Icon layer.
        Image {
            anchors.centerIn: parent
            width: parent.width - Theme.spacingMd * 2
            height: parent.height - Theme.spacingMd * 2 - 4 // subtract LED strip
            source: zoneCell.iconSource
            fillMode: Image.PreserveAspectFit
            smooth: true
            asynchronous: true
            visible: zoneCell.iconSource.toString() !== ""
        }

        // Default "view_column" glyph when empty.
        Text {
            anchors.centerIn: parent
            font.family: "Material Symbols Outlined"
            font.pixelSize: 20
            text: "view_column"
            color: Qt.rgba(Theme.fgPrimary.r, Theme.fgPrimary.g, Theme.fgPrimary.b, 0.5)
            visible: zoneCell.iconSource.toString() === ""
        }

        // Label overlay.
        Text {
            anchors.bottom: parent.bottom
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottomMargin: Theme.spacingXs
            text: zoneCell.zoneLabel
            color: Theme.fgPrimary
            font.pixelSize: Theme.typeLabelMedium.pixelSize
            font.weight: Theme.typeLabelMedium.weight
            horizontalAlignment: Text.AlignHCenter
            visible: zoneCell.zoneLabel !== ""
        }

        // ItemDelegate for focus + pressed states.
        ItemDelegate {
            id: zoneDelegate
            anchors.fill: parent
            background: Item {}   // visual is handled by bg above
            property bool selected: false
            // CR-03: wire click to emit zoneTapped so DeviceView can update
            // selectedZoneIndex and drive the Inspector pane with zone-specific data.
            onClicked: zoneCell.zoneTapped(zoneCell.zoneIndex)
        }

        // ----- Drag source (when occupied) ---------------------------------
        DragHandler {
            id: zoneDragHandler
            target: null
            acceptedButtons: Qt.LeftButton
            dragThreshold: 8
            enabled: zoneCell.iconSource.toString() !== ""
        }

        Drag.active: zoneDragHandler.active
        Drag.dragType: Drag.Automatic
        Drag.mimeData: ({
            "application/x-ajazz-binding": JSON.stringify({
                controller: "TouchZone",
                position: zoneCell.zoneIndex
            })
        })

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.NoButton
            hoverEnabled: true
            cursorShape: zoneCell.iconSource.toString() !== ""
                ? (zoneDragHandler.active ? Qt.ClosedHandCursor : Qt.OpenHandCursor)
                : Qt.ArrowCursor
        }

        // ----- Drop target (always) ----------------------------------------
        DropArea {
            id: zoneDropArea
            anchors.fill: parent
            keys: ["application/x-ajazz-action", "application/x-ajazz-binding"]

            // UI-REVIEW.md fix: dragRejected drives the no-go visual on the
            // zone background (errorAccent border) for cross-controller drag-over.
            property bool dragRejected: false

            onEntered: function(drag) {
                if (drag.hasFormat("application/x-ajazz-binding")) {
                    var ok = false;
                    try {
                        var p = JSON.parse(drag.getDataAsString("application/x-ajazz-binding"));
                        ok = (p.controller === "TouchZone");
                    } catch (e) {
                        ok = false;
                    }
                    if (!ok) {
                        dragRejected = true;
                        drag.accepted = false;
                        return;
                    }
                }
                // PLUGIN-20: strict affordance gate for library-action drags.
                // TouchZone requires affordanceMask bit 4. Zero/missing mask
                // (e.g. Information-only) also fails -- fail-safe per T-28-07.
                if (drag.hasFormat("application/x-ajazz-action")) {
                    var ok2 = false;
                    try {
                        var ap2 = JSON.parse(drag.getDataAsString("application/x-ajazz-action"));
                        var mask = ap2.affordanceMask !== undefined ? ap2.affordanceMask : 0;
                        ok2 = ((mask & 4) !== 0);  // TouchZone bit
                    } catch (e2) {
                        ok2 = false;
                    }
                    if (!ok2) {
                        dragRejected = true;
                        drag.accepted = false;
                        return;
                    }
                }
                dragRejected = false;
                zoneCellScale.xScale = 1.05;
                zoneCellScale.yScale = 1.05;
                drag.accepted = true;
            }

            onExited: function() {
                dragRejected = false;
                zoneCellScale.xScale = 1.0;
                zoneCellScale.yScale = 1.0;
            }

            onDropped: function(drop) {
                zoneCellScale.xScale = 1.0;
                zoneCellScale.yScale = 1.0;
                dragRejected = false;

                if (drop.hasFormat("application/x-ajazz-action")) {
                    var ap = JSON.parse(drop.getDataAsString("application/x-ajazz-action"));
                    // PLUGIN-19: pass actionId as 6th arg (was dropped in 5-arg call).
                    // Seed defaultSettings from payload if available (RESEARCH §Q3).
                    var aid = ap.actionId ? ap.actionId : "";
                    var defaults = ap.defaultSettings ? ap.defaultSettings : "";
                    ProfileController.commitTouchZoneBinding(zoneCell.zoneIndex, "",
                                                            ap.label, ap.actionKind,
                                                            defaults, aid);
                    drop.acceptProposedAction();
                    return;
                }

                if (drop.hasFormat("application/x-ajazz-binding")) {
                    var bp = JSON.parse(drop.getDataAsString("application/x-ajazz-binding"));
                    if (bp.controller !== "TouchZone") {
                        drop.accepted = false;
                        return;
                    }
                    if (bp.position !== zoneCell.zoneIndex) {
                        zoneCell.zoneSwapRequested(bp.position, zoneCell.zoneIndex);
                    }
                    drop.acceptProposedAction();
                    return;
                }

                drop.accepted = false;
            }
        }

        Accessible.role: Accessible.Button
        Accessible.name: zoneCell.iconSource.toString() !== ""
            ? (zoneCell.zoneLabel !== ""
               ? qsTr("Touch zone %1: %2").arg(zoneCell.zoneIndex + 1).arg(zoneCell.zoneLabel)
               : qsTr("Touch zone %1").arg(zoneCell.zoneIndex + 1))
            : qsTr("Touch zone %1 -- empty").arg(zoneCell.zoneIndex + 1)
        Accessible.description: zoneCell.iconSource.toString() !== ""
            ? qsTr("Press Space to select, Delete to clear")
            : qsTr("Drop a touch zone action here")
    }
}
