// SPDX-License-Identifier: GPL-3.0-or-later
//
// PrimaryButton.qml — solid-fill brand-accent button (CTA) used in the
// sticky Apply/Revert footer and elsewhere where a single dominant action
// should stand out.
import QtQuick
import QtQuick.Controls
import AjazzControlCenter

Button {
    id: root

    property string accessibleName: text
    property string accessibleDescription: ""

    implicitHeight: Math.max(36, Theme.minTouchTarget - 8)
    leftPadding: Theme.spacingLg
    rightPadding: Theme.spacingLg

    background: Rectangle {
        radius: Theme.radiusMd
        color: root.down ? Qt.darker(Theme.accent, 1.2)
                          : (root.hovered ? Qt.lighter(Theme.accent, 1.1) : Theme.accent)
        border.width: root.activeFocus ? Theme.focusRingWidth : 0
        border.color: Theme.fgPrimary
    }

    contentItem: Text {
        text: root.text
        color: "#0e1011"
        font.pixelSize: Theme.fontMd
        font.bold: true
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }

    Accessible.role: Accessible.Button
    Accessible.name: root.accessibleName
    Accessible.description: root.accessibleDescription
}
