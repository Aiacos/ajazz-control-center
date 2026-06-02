// SPDX-License-Identifier: GPL-3.0-or-later
//
// KeyBindingList.qml — per-key multi-action binding list editor (Phase 29, PLUGIN-23).
//
// Shows the selected key's full onPress action chain as a vertical list.
// Each row is a drag SOURCE (via DragRelay / Drag.Internal — Wayland-safe).
// MIME "application/x-ajazz-binding" with {controller:"Keypad", position:<keyIndex>,
//   actionPos:<row>} so the trash DropArea and row-to-row drop targets can
// distinguish a binding-reorder drag from a key-to-key drag.
//
// Drop rules:
//   - Row dropped onto another row -> ProfileController.reorderKeyAction()
//   - Row dropped onto the chassis trash zone -> ProfileController.removeKeyActionAt()
//   - A new library action (application/x-ajazz-action) dropped onto the list
//     -> ProfileController.appendKeyAction()  (additive, keeps existing actions)
//
// Every named control sets objectName per CLAUDE.md debug-channel rule.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AjazzControlCenter

Rectangle {
    id: root
    objectName: "keyBindingList"

    // ---- Required inputs -------------------------------------------------------
    property int keyIndex: -1          ///< 0-based key index (from DeviceView.selectedKeyIndex)
    property var actionList: []        ///< QVariantList: [{actionKind, actionId, label, iconSource}, ...]

    // ---- Appearance ------------------------------------------------------------
    color: Theme.surfaceVariant
    radius: Theme.radiusCard
    visible: root.keyIndex >= 0 && root.actionList.length > 0

    // Minimum height: title row + rows + append hint.
    implicitHeight: titleRow.height + listView.contentHeight + appendHint.height + 3 * Theme.spacingMd

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacingMd
        spacing: Theme.spacingSm

        // ---- Header ----------------------------------------------------------
        RowLayout {
            id: titleRow
            Layout.fillWidth: true
            spacing: Theme.spacingSm

            Text {
                objectName: "keyBindingListTitle"
                Layout.fillWidth: true
                text: qsTr("Actions for Key %1").arg(root.keyIndex + 1)
                font.pixelSize: Theme.fontSizeSm
                font.bold: true
                color: Theme.textSecondary
                elide: Text.ElideRight
            }

            Text {
                text: qsTr("(%1)").arg(root.actionList.length)
                font.pixelSize: Theme.fontSizeSm
                color: Theme.textTertiary
            }
        }

        // ---- Action rows (each a drag source + drop target for reorder) ------
        ListView {
            id: listView
            objectName: "keyBindingListView"
            Layout.fillWidth: true
            // Height limited to 4 rows max; scrolls beyond that.
            implicitHeight: Math.min(contentHeight, 4 * 52)
            clip: true
            model: root.actionList
            spacing: Theme.spacingXs

            delegate: Item {
                id: rowDelegate
                objectName: "keyBindingRow_" + index
                required property int index
                required property var modelData

                width: listView.width
                height: 48

                // ---- Drop target: reorder from another action row -----------
                DropArea {
                    id: rowDropArea
                    anchors.fill: parent
                    // Only accept binding drags from this same key's action list.
                    keys: ["application/x-ajazz-binding"]

                    property bool hovering: false

                    onEntered: function(drag) {
                        if (DragRelay.mimeKey !== "application/x-ajazz-binding") {
                            drag.accepted = false;
                            return;
                        }
                        try {
                            var bp = JSON.parse(DragRelay.payload);
                            // Only accept reorder drags from the same key and controller=Keypad.
                            if (bp.controller !== "Keypad" || bp.position !== root.keyIndex
                                    || bp.actionPos === undefined || bp.actionPos === rowDelegate.index) {
                                drag.accepted = false;
                                return;
                            }
                        } catch (e) {
                            drag.accepted = false;
                            return;
                        }
                        hovering = true;
                        drag.accepted = true;
                    }

                    onExited: function() {
                        hovering = false;
                    }

                    onDropped: function(drop) {
                        hovering = false;
                        if (DragRelay.mimeKey !== "application/x-ajazz-binding")
                            return;
                        try {
                            var bp2 = JSON.parse(DragRelay.payload);
                            if (bp2.controller === "Keypad" && bp2.position === root.keyIndex
                                    && bp2.actionPos !== undefined) {
                                ProfileController.reorderKeyAction(root.keyIndex, bp2.actionPos,
                                                                   rowDelegate.index);
                            }
                        } catch (e2) {
                        }
                        drop.acceptProposedAction();
                    }
                }

                // ---- Row visual -------------------------------------------
                Rectangle {
                    anchors.fill: parent
                    radius: Theme.radiusCard
                    color: rowDropArea.hovering
                        ? Theme.primaryContainer
                        : (rowMouseArea.containsMouse ? Theme.hoverOverlay : Theme.surfaceVariant)
                    border.width: rowDropArea.hovering ? 1 : 0
                    border.color: Theme.accent

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: Theme.spacingSm
                        anchors.rightMargin: Theme.spacingSm
                        spacing: Theme.spacingSm

                        // ---- Action icon ----------------------------------
                        Item {
                            width: 32
                            height: 32

                            Image {
                                anchors.fill: parent
                                source: rowDelegate.modelData ? rowDelegate.modelData.iconSource
                                                              : ""
                                fillMode: Image.PreserveAspectFit
                                visible: source.toString() !== ""
                            }

                            Text {
                                anchors.centerIn: parent
                                font.family: "Material Symbols Outlined"
                                font.pixelSize: 20
                                text: "bolt"
                                color: Theme.textSecondary
                                visible: !parent.children[0].visible
                                        || parent.children[0].source.toString() === ""
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                        }

                        // ---- Label ----------------------------------------
                        Text {
                            Layout.fillWidth: true
                            text: rowDelegate.modelData ? (rowDelegate.modelData.label !== ""
                                      ? rowDelegate.modelData.label
                                      : rowDelegate.modelData.actionId)
                                : ""
                            font.pixelSize: Theme.fontSizeSm
                            color: Theme.textPrimary
                            elide: Text.ElideRight
                        }

                        // ---- Drag handle icon -----------------------------
                        Text {
                            font.family: "Material Symbols Outlined"
                            font.pixelSize: 18
                            text: "drag_handle"
                            color: Theme.textSecondary
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                }

                // ---- Hover detection for cursor ---------------------------
                MouseArea {
                    id: rowMouseArea
                    anchors.fill: parent
                    acceptedButtons: Qt.NoButton
                    hoverEnabled: true
                    cursorShape: rowDragHandler.active ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                }

                // ---- Drag SOURCE: reorder this action row -----------------
                // Uses DragRelay (Drag.Internal via Main.qml ghost) — same pattern
                // as KeyCell.qml to remain Wayland-safe (Drag.Automatic dead on niri).
                DragHandler {
                    id: rowDragHandler
                    target: null
                    acceptedButtons: Qt.LeftButton
                    dragThreshold: 8

                    readonly property string _payload: JSON.stringify({
                        controller: "Keypad",
                        position: root.keyIndex,
                        actionPos: rowDelegate.index
                    })

                    onActiveChanged: {
                        if (active)
                            DragRelay.begin("application/x-ajazz-binding", _payload,
                                            rowDelegate.modelData ? rowDelegate.modelData.iconSource
                                                                  : "",
                                            "bolt",
                                            rowDelegate.modelData ? rowDelegate.modelData.label : "",
                                            centroid.scenePosition.x,
                                            centroid.scenePosition.y);
                        else
                            DragRelay.finish();
                    }
                }

                // Feed live cursor position to the relay ghost while dragging.
                Binding {
                    target: DragRelay
                    property: "hotspot"
                    value: rowDragHandler.centroid.scenePosition
                    when: rowDragHandler.active
                    restoreMode: Binding.RestoreNone
                }
            }
        }

        // ---- "Drop here to add" affordance for library-action drags ----------
        // Accepts application/x-ajazz-action (key affordanceMask & 1) and calls
        // appendKeyAction to ADD the action without overwriting existing entries.
        Rectangle {
            id: appendHint
            objectName: "keyBindingListAppendZone"
            Layout.fillWidth: true
            height: 36
            radius: Theme.radiusCard
            color: appendDropArea.containsDrag ? Theme.primaryContainer : "transparent"
            border.width: 1
            border.color: appendDropArea.containsDrag ? Theme.accent : Theme.divider

            Text {
                anchors.centerIn: parent
                text: qsTr("Drop here to add action")
                font.pixelSize: Theme.fontSizeXs
                color: appendDropArea.containsDrag ? Theme.textPrimary : Theme.textTertiary
                horizontalAlignment: Text.AlignHCenter
            }

            DropArea {
                id: appendDropArea
                anchors.fill: parent
                // Accept only library-action drags (not binding-move drags).
                keys: ["application/x-ajazz-action"]

                onEntered: function(drag) {
                    if (DragRelay.mimeKey !== "application/x-ajazz-action") {
                        drag.accepted = false;
                        return;
                    }
                    // Affordance gate: key bit (bit 1) must be set (mirrors KeyCell.qml:220-234).
                    try {
                        var ap = JSON.parse(DragRelay.payload);
                        var mask = ap.affordanceMask !== undefined ? ap.affordanceMask : 0;
                        if ((mask & 1) === 0) {
                            drag.accepted = false;
                            return;
                        }
                    } catch (e) {
                        drag.accepted = false;
                        return;
                    }
                    drag.accepted = true;
                }

                onDropped: function(drop) {
                    if (DragRelay.mimeKey !== "application/x-ajazz-action")
                        return;
                    try {
                        var ap2 = JSON.parse(DragRelay.payload);
                        var mask2 = ap2.affordanceMask !== undefined ? ap2.affordanceMask : 0;
                        if ((mask2 & 1) !== 0) {
                            // Additive append — keeps existing actions.
                            ProfileController.appendKeyAction(root.keyIndex,
                                                              ap2.actionKind !== undefined
                                                                  ? ap2.actionKind : 0,
                                                              "",
                                                              ap2.actionId !== undefined
                                                                  ? ap2.actionId : "");
                        }
                    } catch (e2) {
                    }
                    drop.acceptProposedAction();
                }
            }
        }
    }
}
