// SPDX-License-Identifier: GPL-3.0-or-later
//
// ActionLibraryPane.qml -- left-panel drag-source library for DeviceView.
//
// Shows 5 built-in ActionKind tiles (D-02). Each tile is draggable via
// DragHandler (acceptedButtons: Qt.LeftButton, dragThreshold: 8) to avoid
// conflicting with ListView flick (UI-SPEC Qt6 constraint 4 / T-26-16).
//
// MIME: "application/x-ajazz-action" with payload
//       { actionKind: int, label: string, iconName: string }.
//
// Width: fixed 240px (configurable via Layout.preferredWidth on the parent).
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AjazzControlCenter

Rectangle {
    id: root

    color: Theme.bgBase

    Layout.preferredWidth: 240
    implicitWidth:          240
    implicitHeight:         360

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Header row.
        Item {
            Layout.fillWidth: true
            implicitHeight: headerLabel.implicitHeight + Theme.spacingMd * 2

            Text {
                id: headerLabel
                anchors {
                    left: parent.left
                    right: parent.right
                    verticalCenter: parent.verticalCenter
                    leftMargin: Theme.spacingLg
                }
                text: qsTr("Actions")
                color: Theme.fgPrimary
                font.pixelSize: Theme.typeTitleSmall.pixelSize
                font.weight: Theme.typeTitleSmall.weight
            }
        }

        // Divider.
        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: Theme.borderSubtle
        }

        // Tile list.
        ListView {
            id: actionList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 0

            model: ListModel {
                ListElement { actionLabel: "Plugin action";  kind: 0; iconName: "extension"  }
                ListElement { actionLabel: "Key macro";      kind: 2; iconName: "keyboard"   }
                ListElement { actionLabel: "Launch command"; kind: 3; iconName: "terminal"   }
                ListElement { actionLabel: "Open URL";       kind: 4; iconName: "link"       }
                ListElement { actionLabel: "Open folder";    kind: 5; iconName: "folder_open" }
            }

            delegate: LibraryTile {}

            Accessible.role: Accessible.List
            Accessible.name: qsTr("Action library")
        }

        Item { Layout.fillHeight: true }
    }

    // -------------------------------------------------------------------------
    // LibraryTile -- inline private component for each action row.
    // -------------------------------------------------------------------------
    component LibraryTile: ItemDelegate {
        id: tile

        required property string actionLabel
        required property int    kind
        required property string iconName

        width:  ListView.view ? ListView.view.width : root.implicitWidth
        height: 48

        // ----- Drag source --------------------------------------------------
        DragHandler {
            id: tileDragHandler
            target: null
            acceptedButtons: Qt.LeftButton
            // dragThreshold prevents accidental drag during list flick.
            dragThreshold: 8
        }

        Drag.active: tileDragHandler.active
        Drag.dragType: Drag.Automatic
        Drag.mimeData: ({
            "application/x-ajazz-action": JSON.stringify({
                actionKind: tile.kind,
                label: tile.actionLabel,
                iconName: tile.iconName
            })
        })

        // Cursor affordance.
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.NoButton
            hoverEnabled: true
            cursorShape: Qt.OpenHandCursor
        }

        background: Rectangle {
            color: tile.hovered ? Theme.bgRowHover : Theme.bgBase
        }

        contentItem: RowLayout {
            anchors {
                left: parent.left
                right: parent.right
                verticalCenter: parent.verticalCenter
                leftMargin: Theme.spacingLg
                rightMargin: Theme.spacingLg
            }
            spacing: Theme.spacingSm

            // Material Symbols icon.
            Text {
                font.family: "Material Symbols Outlined"
                font.pixelSize: 20
                text: tile.iconName
                color: Theme.accent
                Layout.alignment: Qt.AlignVCenter
            }

            // Action label.
            Text {
                Layout.fillWidth: true
                text: tile.actionLabel
                color: Theme.fgPrimary
                font.pixelSize: Theme.typeBodyMedium.pixelSize
                font.weight: Theme.typeBodyMedium.weight
                elide: Text.ElideRight
                Layout.alignment: Qt.AlignVCenter
            }
        }

        Accessible.role: Accessible.Button
        Accessible.name: qsTr("%1 action").arg(tile.actionLabel)
        Accessible.description: qsTr("Drag to assign %1 to a key").arg(tile.actionLabel)
    }
}
