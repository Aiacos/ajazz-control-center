// SPDX-License-Identifier: GPL-3.0-or-later
//
// DeviceRow.qml — list-item row for the device sidebar.
//
// Replaces the inline Rectangle/MouseArea pair previously hard-coded in
// DeviceList.qml. Uses ItemDelegate so the row is keyboard-focusable and gets
// hover/press effects from QtQuick.Controls.
//
// Properties:
//   * `modelName` — primary line (human-readable device model).
//   * `codename`  — secondary line component (machine identifier).
//   * `connected` — secondary line component (online state).
//
// Emits:
//   * `clicked` — already provided by ItemDelegate.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AjazzControlCenter

ItemDelegate {
    id: root

    property string modelName: ""
    property string codename: ""
    property bool connected: false

    height: 56

    background: Rectangle {
        radius: Theme.radiusMd
        color: root.hovered ? Theme.bgRowHover : Theme.tile
        border.width: root.activeFocus ? Theme.focusRingWidth : 0
        border.color: Theme.accent
    }

    contentItem: ColumnLayout {
        spacing: 2
        Text {
            Layout.fillWidth: true
            text: root.modelName
            color: Theme.fgPrimary
            font.pixelSize: Theme.fontMd
            elide: Text.ElideRight
        }
        Text {
            Layout.fillWidth: true
            text: "%1 · %2".arg(root.codename)
                           .arg(root.connected ? qsTr("connected") : qsTr("offline"))
            color: Theme.fgMuted
            font.pixelSize: Theme.fontXs
            elide: Text.ElideRight
        }
    }

    Accessible.role: Accessible.ListItem
    Accessible.name: root.modelName
    Accessible.description: root.connected ? qsTr("Connected device %1").arg(root.codename)
                                            : qsTr("Offline device %1").arg(root.codename)
}
