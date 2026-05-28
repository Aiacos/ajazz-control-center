// SPDX-License-Identifier: GPL-3.0-or-later
//
// KeyCell.qml -- single AKP key tile used by DeviceView.
//
// Renders the visual approximation of a Stream Dock LCD key: a square tile
// showing the user-chosen icon image with an optional overlay label. When
// no icon is set, the tile falls back to the keyboard-style placeholder
// (the 1-based key index) so the grid never looks empty during initial use.
//
// Exposed:
//   * `index`       -- 0-based key index (used by the index-placeholder fallback).
//   * `iconSource`  -- url for the icon image (file:// or qrc:/). Empty = placeholder.
//   * `label`       -- overlay label text. Empty = no overlay (icon-only or placeholder).
//   * `selected`    -- when true, draws an accent outline (F-26 selected outline).
//
// Emits:
//   * `clicked`            -- when the user activates the cell (mouse or keyboard).
//   * `cellSwapRequested`  -- when a cell-to-cell binding drop arrives; parent orchestrates swap.
//
// Drag-drop (Phase 26 / REQ-26-B):
//   * When occupied (iconSource non-empty), the cell is a drag SOURCE via DragHandler
//     with MIME key "application/x-ajazz-binding" carrying {controller, position}.
//   * The cell is ALWAYS a drop TARGET via DropArea accepting both
//     "application/x-ajazz-action" (library -> cell) and
//     "application/x-ajazz-binding" (cell -> cell swap).
//   * Drop-over triggers a 1.05x scale animation (D-10).
//
// Accessibility: uses ItemDelegate so focus + key handling come for free, and
// exposes Accessible.role/name/description.
import QtQuick
import QtQuick.Controls
import AjazzControlCenter

ItemDelegate {
    id: root

    // Marked `required` at the type level so the Repeater delegate's
    // model-role auto-binding takes effect directly -- re-declaring
    // these as required on the delegate instance was hitting the
    // type-default-shadow trap (cells always rendered index "1").
    required property int index
    required property url iconSource
    required property string label
    property bool selected: false
    /// True while this cell is being dragged. DeviceView watches this to set
    /// anyDragActive (WR-02: trash-zone 100% opacity during any cell drag).
    readonly property bool dragActive: Drag.active

    // Emitted when a cell-to-cell binding arrives; the DeviceView parent
    // orchestrates the actual swap (it owns both source and destination state).
    signal cellSwapRequested(int srcIndex, int dstIndex)

    width: 96
    height: 96

    // ----- Drag-over scale animation (D-10) ----------------------------------
    // Neighbouring cells do NOT reflow; scale only affects this cell.
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
        radius: Theme.radiusLg
        color: root.hovered ? Theme.tileHover : Theme.tile
        border.width: root.activeFocus || root.selected ? Theme.focusRingWidth : 1
        border.color: root.activeFocus || root.selected ? Theme.accent : Theme.borderSubtle
    }

    contentItem: Item {
        // Inner padding so the icon and label sit inside the focus ring.
        Item {
            anchors.fill: parent
            anchors.margins: 6

            // Icon layer. PreserveAspectCrop fills the cell -- matches the
            // LCD-key visual where the image is the dominant element.
            Image {
                anchors.fill: parent
                source: root.iconSource
                fillMode: Image.PreserveAspectCrop
                smooth: true
                asynchronous: true
                visible: root.iconSource.toString() !== ""
            }

            // Overlay label. Always rendered when `label` is non-empty;
            // otherwise (for icon-less cells) falls back to the 1-based key
            // index so the grid never looks blank.
            Text {
                id: overlayText
                anchors.fill: parent
                text: root.label !== ""
                    ? root.label
                    : (root.iconSource.toString() === "" ? (root.index + 1).toString() : "")
                color: Theme.fgPrimary
                font.pixelSize: root.label !== "" ? Theme.fontSm : Theme.fontLg
                font.weight: root.label !== "" ? Font.DemiBold : Font.Normal
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: root.label !== "" ? Text.AlignBottom : Text.AlignVCenter
                wrapMode: Text.WordWrap

                // When the label sits on top of an icon, draw a subtle dark
                // shadow under the text so light foregrounds stay legible on
                // bright icons. Cheap drop-shadow via doubled Text.
                style: root.iconSource.toString() !== "" ? Text.Outline : Text.Normal
                styleColor: "#000000"
            }
        }
    }

    // ----- Drag source (when occupied) --------------------------------------
    // DragHandler sits on the cell; target:null means it does not move the
    // item visually -- Qt handles the drag image automatically.
    DragHandler {
        id: dragHandler
        target: null
        acceptedButtons: Qt.LeftButton
        // dragThreshold 8px prevents accidental drag during a quick tap.
        dragThreshold: 8
        enabled: root.iconSource.toString() !== ""
    }

    Drag.active: dragHandler.active
    Drag.dragType: Drag.Automatic
    Drag.mimeData: ({
        "application/x-ajazz-binding": JSON.stringify({
            controller: "Keypad",
            position: root.index
        })
    })

    // Cursor affordance when occupied -- use a MouseArea overlay so
    // ItemDelegate's own handling is not disrupted.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.NoButton          // never steal clicks
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

        onEntered: function(drag) {
            cellScale.xScale = 1.05;
            cellScale.yScale = 1.05;
            drag.accepted = true;
        }

        onExited: function() {
            cellScale.xScale = 1.0;
            cellScale.yScale = 1.0;
        }

        onDropped: function(drop) {
            cellScale.xScale = 1.0;
            cellScale.yScale = 1.0;

            if (drop.hasFormat("application/x-ajazz-action")) {
                var actionPayload = JSON.parse(drop.getDataAsString("application/x-ajazz-action"));
                // Library -> cell: commit the binding; iconPath is empty in v1 (user picks via Inspector).
                ProfileController.commitKeyBinding(root.index, "", actionPayload.label,
                                                   actionPayload.actionKind, "");
                drop.acceptProposedAction();
                return;
            }

            if (drop.hasFormat("application/x-ajazz-binding")) {
                var bindPayload = JSON.parse(drop.getDataAsString("application/x-ajazz-binding"));
                if (bindPayload.controller !== "Keypad") {
                    // Cross-controller drag: reject (T-26-17).
                    drop.accepted = false;
                    return;
                }
                if (bindPayload.position !== root.index) {
                    // Cell-to-cell same-controller swap: signal parent to handle.
                    root.cellSwapRequested(bindPayload.position, root.index);
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
           ? qsTr("Key %1: %2").arg(root.index + 1).arg(root.label)
           : qsTr("Key %1").arg(root.index + 1))
        : qsTr("Key %1 -- empty").arg(root.index + 1)
    Accessible.description: root.iconSource.toString() !== ""
        ? qsTr("Press Space to select, Delete to clear. Drag to move binding.")
        : qsTr("Drop an action here or press Space to assign")
}
