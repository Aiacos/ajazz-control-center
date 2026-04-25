// SPDX-License-Identifier: GPL-3.0-or-later
//
// EncoderCard.qml — circular dial used by EncoderPanel for each rotary encoder.
//
// Properties:
//   * `index` — 0-based encoder index used for the default label.
//   * `label` — overridable user-visible string.
//
// Accessibility: the dial is an ItemDelegate so it can be focused via keyboard
// and exposes a meaningful Accessible name/description.
import QtQuick
import QtQuick.Controls
import AjazzControlCenter

ItemDelegate {
    id: root

    property int index: 0
    property string label: qsTr("Dial %1").arg(index + 1)

    width: 120
    height: 120

    background: Rectangle {
        radius: 60
        color: root.hovered ? Theme.tileHover : Theme.tile
        border.width: root.activeFocus ? Theme.focusRingWidth : 2
        border.color: root.activeFocus ? Theme.accent : Theme.accent
    }

    contentItem: Text {
        text: root.label
        color: Theme.fgPrimary
        font.pixelSize: Theme.fontMd
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }

    Accessible.role: Accessible.Button
    Accessible.name: root.label
    Accessible.description: qsTr("Configures the rotary encoder %1").arg(index + 1)
}
