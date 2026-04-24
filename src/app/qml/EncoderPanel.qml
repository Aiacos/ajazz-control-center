// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    GridLayout {
        anchors.centerIn: parent
        columns: 4
        rowSpacing: 16
        columnSpacing: 16

        Repeater {
            model: 4
            delegate: Rectangle {
                required property int index
                width: 120; height: 120; radius: 60
                color: "#2a2a32"
                border.color: "#ff5722"
                border.width: 2
                Text {
                    anchors.centerIn: parent
                    text: qsTr("Dial %1").arg(parent.index + 1)
                    color: "#e0e0e0"
                }
            }
        }
    }
}
