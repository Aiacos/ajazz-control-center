// SPDX-License-Identifier: GPL-3.0-or-later
//
// KeyCell.qml — single AKP key tile used by KeyDesigner.
//
// Replaces the inline `Rectangle { … }` previously hard-coded in KeyDesigner.
// Exposes:
//   * `index`     — 0-based key index used for the default placeholder label.
//   * `label`     — overridable user-visible string (defaults to index+1).
//   * `selected`  — when true, draws an accent outline (F-26 selected outline).
//
// Emits:
//   * `clicked`   — when the user activates the cell (mouse or keyboard).
//
// Accessibility: the cell uses an ItemDelegate so it gets full focus + key
// handling for free, and exposes Accessible.role/name/description.
import QtQuick
import QtQuick.Controls
import AjazzControlCenter

ItemDelegate {
    id: root

    property int index: 0
    property string label: (index + 1).toString()
    property bool selected: false

    width: 96
    height: 96

    background: Rectangle {
        radius: Theme.radiusLg
        color: root.hovered ? Theme.tileHover : Theme.tile
        border.width: root.activeFocus || root.selected ? Theme.focusRingWidth : 1
        border.color: root.activeFocus || root.selected ? Theme.accent : Theme.borderSubtle
    }

    contentItem: Text {
        text: root.label
        color: Theme.fgPrimary
        font.pixelSize: Theme.fontLg
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }

    Accessible.role: Accessible.Button
    Accessible.name: qsTr("Key %1").arg(index + 1)
    Accessible.description: qsTr("Configures action bound to key %1").arg(index + 1)
}
