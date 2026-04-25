// SPDX-License-Identifier: GPL-3.0-or-later
//
// Sidebar panel showing connected and previously-seen AJAZZ devices.
// Exposes `model` (alias to the internal ListView model) for the parent to
// supply a device list model.  Emits `deviceSelected(codename)` when the
// user clicks a device row.
import QtQuick
import QtQuick.Controls

Rectangle {
    id: root
    color: "#1e1e23"

    /// Emitted when the user clicks a device row; carries the device codename.
    signal deviceSelected(string codename)

    /// Alias to the internal ListView model; set by the parent to supply devices.
    property alias model: list.model

    ListView {
        id: list
        anchors.fill: parent
        anchors.margins: 8
        spacing: 4
        delegate: Rectangle {
            required property string model
            required property string codename
            required property int    family
            required property bool   connected

            width: list.width
            height: 56
            radius: 6
            color: ma.containsMouse ? "#2c2c34" : "#24242a"

            Column {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 2
                Text {
                    text: parent.parent.model
                    color: "#f0f0f0"
                    font.pixelSize: 14
                }
                Text {
                    text: "%1 · %2".arg(parent.parent.codename)
                                    .arg(parent.parent.connected ? "connected" : "offline")
                    color: "#888"
                    font.pixelSize: 11
                }
            }

            MouseArea {
                id: ma
                anchors.fill: parent
                hoverEnabled: true
                onClicked: root.deviceSelected(parent.codename)
            }
        }
    }
}
