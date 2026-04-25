// SPDX-License-Identifier: GPL-3.0-or-later
//
// Key designer tab: renders a 5 × 3 grid of clickable key slots.
// Selecting a slot will open the keycode assignment popover (not yet implemented).
// Exposes no custom signals or properties; the parent ProfileEditor controls
// visibility via StackLayout currentIndex.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    GridLayout {
        anchors.centerIn: parent
        columns: 5
        rowSpacing: 8
        columnSpacing: 8

        Repeater {
            model: 15
            delegate: Rectangle {
                required property int index
                width: 96
                height: 96
                radius: 8
                color: "#2a2a32"
                border.color: "#3a3a44"
                border.width: 1
                Text {
                    anchors.centerIn: parent
                    text: parent.index + 1
                    color: "#aaa"
                    font.pixelSize: 18
                }
            }
        }
    }
}
