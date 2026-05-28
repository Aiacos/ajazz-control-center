// SPDX-License-Identifier: GPL-3.0-or-later
//
// EncoderDial.qml -- round encoder cell delegate for DeviceView.
//
// Visual: full-circle tile with the user-assigned icon centered, or a
// default Material Symbols "tune" glyph when unoccupied. Label below icon.
//
// Drag-drop (Phase 26 / REQ-26-B):
//   * When occupied (iconSource non-empty), the cell is a drag SOURCE via
//     DragHandler with MIME key "application/x-ajazz-binding" carrying
//     {controller: "Encoder", position: index}.
//   * The cell is ALWAYS a drop TARGET accepting "application/x-ajazz-action"
//     (library -> encoder) and "application/x-ajazz-binding" (same-controller swap).
//   * Drop-over triggers a 1.05x scale animation (D-10).
//
// Accessibility: ItemDelegate gives focus + key handling for free.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import AjazzControlCenter

ItemDelegate {
    id: root

    required property int    index
    required property url    iconSource
    property string          label: ""
    property bool            selected: false
    /// True while this encoder cell is being dragged. DeviceView watches this
    /// to set anyDragActive (WR-02: trash-zone 100% opacity during any cell drag).
    readonly property bool dragActive: Drag.active

    // Emitted when a same-controller cell-to-cell binding arrives.
    signal encoderSwapRequested(int srcIndex, int dstIndex)

    // Default visual size; parent DeviceView may override via Layout.* or anchors.
    width:  120
    height: 120

    // ----- Drag-over scale animation (D-10) ----------------------------------
    transform: Scale {
        id: cellScale
        origin.x: root.width / 2
        origin.y: root.height / 2
        xScale: 1.0
        yScale: 1.0
        Behavior on xScale {
            NumberAnimation { duration: 100; easing.type: Easing.OutQuad }
        }
        Behavior on yScale {
            NumberAnimation { duration: 100; easing.type: Easing.OutQuad }
        }
    }

    background: Rectangle {
        // Full-circle background -- matches EncoderCard.qml pattern.
        radius: Math.min(width, height) / 2
        color: root.hovered ? Theme.tileHover : Theme.tile
        // UI-REVIEW.md fix: drag-rejected state overrides the default accent
        // border with errorAccent during cross-controller drag-over.
        border.width: dropArea.dragRejected
            ? Theme.focusRingWidth
            : (root.activeFocus || root.selected ? Theme.focusRingWidth : 2)
        border.color: dropArea.dragRejected ? Theme.errorAccent : Theme.accent
    }

    contentItem: Item {
        // Icon or default glyph, centered.
        Image {
            anchors.centerIn: parent
            width: 28
            height: 28
            source: root.iconSource
            fillMode: Image.PreserveAspectFit
            smooth: true
            asynchronous: true
            visible: root.iconSource.toString() !== ""
        }

        // Material Symbols "tune" glyph when unoccupied.
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.verticalCenter: parent.top
            anchors.verticalCenterOffset: parent.height * 0.35
            font.family: "Material Symbols Outlined"
            font.pixelSize: 28
            text: "tune"
            color: Qt.rgba(Theme.fgPrimary.r, Theme.fgPrimary.g, Theme.fgPrimary.b,
                           root.hovered ? 1.0 : 0.7)
            visible: root.iconSource.toString() === ""
        }

        // Label below the icon/glyph.
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: Theme.spacingXs
            text: root.label !== "" ? root.label : qsTr("Dial %1").arg(root.index + 1)
            color: Theme.fgPrimary
            font.pixelSize: Theme.typeLabelMedium.pixelSize
            font.weight: Theme.typeLabelMedium.weight
            horizontalAlignment: Text.AlignHCenter
        }
    }

    // ----- Drag source (when occupied) --------------------------------------
    DragHandler {
        id: dragHandler
        target: null
        acceptedButtons: Qt.LeftButton
        dragThreshold: 8
        enabled: root.iconSource.toString() !== ""
    }

    Drag.active: dragHandler.active
    Drag.dragType: Drag.Automatic
    Drag.mimeData: ({
        "application/x-ajazz-binding": JSON.stringify({
            controller: "Encoder",
            position: root.index
        })
    })

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.NoButton
        hoverEnabled: true
        cursorShape: root.iconSource.toString() !== ""
            ? (dragHandler.active ? Qt.ClosedHandCursor : Qt.OpenHandCursor)
            : Qt.ArrowCursor
    }

    // ----- Drop target (always) --------------------------------------------
    DropArea {
        id: dropArea
        anchors.fill: parent
        keys: ["application/x-ajazz-action", "application/x-ajazz-binding"]

        // UI-REVIEW.md fix: dragRejected exposes a reject state so the encoder
        // dial can show the errorAccent border + no scale-up for cross-controller
        // drags, instead of falsely accepting then failing silently in onDropped.
        property bool dragRejected: false

        onEntered: function(drag) {
            if (drag.hasFormat("application/x-ajazz-binding")) {
                var ok = false;
                try {
                    var p = JSON.parse(drag.getDataAsString("application/x-ajazz-binding"));
                    ok = (p.controller === "Encoder");
                } catch (e) {
                    ok = false;
                }
                if (!ok) {
                    dragRejected = true;
                    drag.accepted = false;
                    return;
                }
            }
            dragRejected = false;
            cellScale.xScale = 1.05;
            cellScale.yScale = 1.05;
            drag.accepted = true;
        }

        onExited: function() {
            dragRejected = false;
            cellScale.xScale = 1.0;
            cellScale.yScale = 1.0;
        }

        onDropped: function(drop) {
            cellScale.xScale = 1.0;
            cellScale.yScale = 1.0;
            dragRejected = false;

            if (drop.hasFormat("application/x-ajazz-action")) {
                var ap = JSON.parse(drop.getDataAsString("application/x-ajazz-action"));
                // Library -> encoder: commit binding; iconPath empty in v1.
                ProfileController.commitEncoderBinding(root.index, "", ap.label,
                                                       ap.actionKind, "");
                drop.acceptProposedAction();
                return;
            }

            if (drop.hasFormat("application/x-ajazz-binding")) {
                var bp = JSON.parse(drop.getDataAsString("application/x-ajazz-binding"));
                if (bp.controller !== "Encoder") {
                    // Cross-controller drag: reject (T-26-17).
                    drop.accepted = false;
                    return;
                }
                if (bp.position !== root.index) {
                    root.encoderSwapRequested(bp.position, root.index);
                }
                drop.acceptProposedAction();
                return;
            }

            drop.accepted = false;
        }
    }

    Accessible.role: Accessible.Button
    Accessible.name: root.iconSource.toString() !== ""
        ? (root.label !== ""
           ? qsTr("Encoder %1: %2").arg(root.index + 1).arg(root.label)
           : qsTr("Encoder %1").arg(root.index + 1))
        : qsTr("Encoder %1 -- empty").arg(root.index + 1)
    Accessible.description: root.iconSource.toString() !== ""
        ? qsTr("Press Space to select, Delete to clear")
        : qsTr("Drop an encoder action here")
}
