// SPDX-License-Identifier: GPL-3.0-or-later
//
// Encoder configuration tab: displays one circular dial per rotary encoder.
// The number of dials is data-driven from `encoderCount`.
//
// Properties (set by the parent ProfileEditor):
//   * `encoderCount` — number of encoders to render (default 0).
//
// Emits:
//   * `encoderActivated(int index)` — user activated a dial.
import QtQuick
import QtQuick.Layouts
import AjazzControlCenter
import "components"

Item {
    id: root

    property int encoderCount: 0
    property int selectedIndex: -1

    signal encoderActivated(int index)

    EmptyState {
        anchors.centerIn: parent
        visible: root.encoderCount === 0
        title: qsTr("No encoders")
        body: qsTr("This device does not expose rotary encoders.")
    }

    GridLayout {
        anchors.centerIn: parent
        visible: root.encoderCount > 0
        columns: Math.min(root.encoderCount, 4)
        rowSpacing: Theme.spacingLg
        columnSpacing: Theme.spacingLg

        Repeater {
            model: root.encoderCount
            delegate: EncoderCard {
                required property int index
                index: index
                onClicked: {
                    root.selectedIndex = index;
                    root.encoderActivated(index);
                }
            }
        }
    }
}
