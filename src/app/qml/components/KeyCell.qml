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
    // Stable handle for the out-of-process debug control channel
    // (qml.click/get on "key_0".."key_N"). Harmless in normal use.
    objectName: "key_" + index
    required property url iconSource
    required property string label
    property bool selected: false
    /// True while this cell is being dragged. DeviceView watches this to set
    /// anyDragActive (WR-02: trash-zone 100% opacity during any cell drag).
    /// Driven by the local DragHandler now that the drag is routed through the
    /// DragRelay singleton (Phase 29) instead of a native Drag.Automatic.
    readonly property bool dragActive: dragHandler.active

    // Emitted when a cell-to-cell binding arrives; the DeviceView parent
    // orchestrates the actual swap (it owns both source and destination state).
    signal cellSwapRequested(int srcIndex, int dstIndex)

    // Emitted when a library action is dropped on this cell. The DeviceView
    // parent owns the binding ListModel, so it (not the cell) updates the live
    // preview AND commits to the profile — keeping the dropped action's icon,
    // label and plugin actionId in sync. `payload` is the parsed
    // application/x-ajazz-action object.
    signal cellActionDropped(int index, var payload)

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
        // UI-REVIEW.md fix: drag-rejected state uses errorAccent border so the
        // user sees a clear "no-go" signal during cross-controller drag-over.
        border.width: dropArea.dragRejected
            ? Theme.focusRingWidth
            : (root.activeFocus || root.selected ? Theme.focusRingWidth : 1)
        border.color: dropArea.dragRejected
            ? Theme.errorAccent
            : (root.activeFocus || root.selected ? Theme.accent : Theme.borderSubtle)
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
                // Phase 29 (OpenDeck parity): a live device render (image://livekey)
                // already bakes the plugin's title into the frame, so suppress the
                // separate label overlay to avoid double text -- show only the
                // rendered key, like OpenDeck. Static icons keep their label.
                visible: root.iconSource.toString().indexOf("image://livekey") !== 0
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
    // DragHandler grabs the pointer + enforces the drag threshold; the actual
    // drag is carried by the shared overlay ghost via the DragRelay singleton
    // (Phase 29 — native Drag.Automatic is not delivered on Wayland/niri).
    // MIME "application/x-ajazz-binding" carries {controller, position} for the
    // cell-to-cell move ("controller" drag in OpenDeck terms).
    DragHandler {
        id: dragHandler
        target: null
        acceptedButtons: Qt.LeftButton
        // dragThreshold 8px prevents accidental drag during a quick tap.
        dragThreshold: 8
        enabled: root.iconSource.toString() !== ""

        readonly property string _payload: JSON.stringify({
            controller: "Keypad",
            position: root.index
        })
        onActiveChanged: {
            if (active)
                DragRelay.begin("application/x-ajazz-binding", _payload,
                                root.iconSource, "", root.label,
                                centroid.scenePosition.x, centroid.scenePosition.y);
            else
                DragRelay.finish();
        }
    }
    // Feed the live cursor position to the ghost while dragging.
    Binding {
        target: DragRelay
        property: "hotspot"
        value: dragHandler.centroid.scenePosition
        when: dragHandler.active
        restoreMode: Binding.RestoreNone
    }

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

        // UI-REVIEW.md fix: dragRejected exposes a reject state so the cell can
        // render the no-go visual (dashed errorAccent border, no scale-up)
        // instead of falsely accepting a cross-controller drop and then failing
        // silently in onDropped. Cleared on exited / dropped.
        property bool dragRejected: false

        // Drag data source: with Drag.Internal (the Wayland-safe relay path), the
        // drop event's mimeData/formats/getDataAsString are EMPTY (those populate
        // only for Drag.Automatic/native drags). The active drag's format + JSON
        // payload live on the DragRelay singleton; read them from there. The
        // DropArea `keys` property still hover-filters via Drag.keys (which IS
        // populated for internal drags), so onEntered/onDropped only fire for a
        // matching drag.
        onEntered: function(drag) {
            // For a "binding" drag (cell-to-cell), reject up front if the source
            // controller is not "Keypad" -- this is the cross-controller drag path.
            if (DragRelay.mimeKey === "application/x-ajazz-binding") {
                var ok = false;
                try {
                    ok = (JSON.parse(DragRelay.payload).controller === "Keypad");
                } catch (e) {
                    ok = false;
                }
                if (!ok) {
                    dragRejected = true;
                    drag.accepted = false;  // visually signal reject
                    return;
                }
            }
            // PLUGIN-20: strict affordance gate for library-action drags.
            // Key cell requires affordanceMask bit 1. Zero/missing mask
            // (e.g. Information-only, affordanceMask=0) also fails -- fail-safe per T-28-07.
            if (DragRelay.mimeKey === "application/x-ajazz-action") {
                var ok2 = false;
                try {
                    var ap2 = JSON.parse(DragRelay.payload);
                    var mask = ap2.affordanceMask !== undefined ? ap2.affordanceMask : 0;
                    ok2 = ((mask & 1) !== 0);  // Key bit
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

            if (DragRelay.mimeKey === "application/x-ajazz-action") {
                var actionPayload = JSON.parse(DragRelay.payload);
                // Library -> cell: let DeviceView update the preview model AND
                // commit (it owns the bindings model; committing here would skip
                // the live preview and drop the plugin actionId).
                root.cellActionDropped(root.index, actionPayload);
                drop.acceptProposedAction();
                return;
            }

            if (DragRelay.mimeKey === "application/x-ajazz-binding") {
                var bindPayload = JSON.parse(DragRelay.payload);
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
