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
        id: bg
        radius: Theme.radiusMd
        clip: true                   // contains the Ripple's expansion within button bounds
        color: root.down ? Qt.darker(Theme.accent, 1.2)
                          : (root.hovered ? Qt.lighter(Theme.accent, 1.1) : Theme.accent)
        border.width: root.activeFocus ? Theme.focusRingWidth : 0
        border.color: Theme.fgPrimary

        Ripple {
            id: ripple
            anchors.fill: parent
            rippleColor: "#0e1011"   // matches contentItem text color
            rippleOpacity: 0.16       // M3 on-primary @ pressed
            duration: Theme.durationMedium
        }
    }

    Connections {
        target: root
        function onPressedChanged() {
            if (root.pressed)
                ripple.trigger(ripple.width / 2, ripple.height / 2)
        }
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
