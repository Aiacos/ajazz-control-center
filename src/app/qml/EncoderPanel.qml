// SPDX-License-Identifier: GPL-3.0-or-later
//
// Encoder configuration tab: displays up to four rotary-encoder dials.
// Each dial shows its ordinal label and will eventually support binding
// CW/CCW rotation and press to actions.
// Exposes no custom signals or properties.
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
