// SPDX-License-Identifier: GPL-3.0-or-later
//
// SecondaryButton.qml — outlined / low-emphasis button that pairs with
// PrimaryButton. Used for "Revert", "Cancel", "Restore defaults", etc.
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
        color: root.hovered ? Theme.bgRowHover : "transparent"
        border.width: root.activeFocus ? Theme.focusRingWidth : 1
        border.color: root.activeFocus ? Theme.accent : Theme.borderSubtle
    }

    contentItem: Text {
        text: root.text
        color: Theme.fgPrimary
        font.pixelSize: Theme.fontMd
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }

    Accessible.role: Accessible.Button
    Accessible.name: root.accessibleName
    Accessible.description: root.accessibleDescription
}
