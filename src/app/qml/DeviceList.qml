// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls

Rectangle {
    id: root
    color: "#1e1e23"
    signal deviceSelected(string codename)

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
