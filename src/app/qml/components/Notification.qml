// SPDX-License-Identifier: GPL-3.0-or-later
//
// Notification.qml — compact transient notification anchored top-right.
//
// Smaller and less intrusive than the bottom-centred Toast (M3 Snackbar): a
// pill that slides down from the top-right corner, dwells briefly, then fades.
// Used for lightweight confirmations like a successful time-sync.
//
// Usage:
//     Notification { id: note }
//     note.show("Time synced")                       // success green (default)
//     note.show("Sync failed", Theme.errorAccent)    // custom colour
import QtQuick
import QtQuick.Effects
import AjazzControlCenter

Item {
    id: root
    visible: opacity > 0
    opacity: 0
    z: 1000

    width: pill.width
    height: pill.height
    anchors.top: parent ? parent.top : undefined
    anchors.right: parent ? parent.right : undefined
    anchors.topMargin: Theme.spacingLg + slideOffset
    anchors.rightMargin: Theme.spacingLg

    property real slideOffset: -16
    property string _message: ""
    property color _color: Theme.successAccent

    /// Show the notification with the given message and (optional) accent colour.
    function show(message, color) {
        root._message = message;
        root._color = color || Theme.successAccent;
        opacity = 1;
        slideOffset = 0;
        hideTimer.restart();
    }

    Rectangle {
        id: pill
        radius: Theme.radiusMd
        color: root._color
        width: label.implicitWidth + Theme.spacingMd * 2
        height: 32
        layer.enabled: true
        layer.effect: MultiEffect {
            shadowEnabled: true
            shadowColor: Theme.elevationShadowColor
            shadowVerticalOffset: Theme.elevation3.offsetY
            shadowBlur: Theme.elevation3.blur
            shadowOpacity: Theme.elevation3.opacity
        }

        Text {
            id: label
            anchors.centerIn: parent
            text: root._message
            // Success green / status accents sit above the luminance flip, so
            // dark text reads best.
            color: "#0e1011"
            font.pixelSize: Theme.fontSm
            font.weight: Font.Medium
        }
    }

    Behavior on opacity {
        NumberAnimation { duration: Theme.durationMedium; easing.type: Theme.easingStandard }
    }
    Behavior on slideOffset {
        NumberAnimation { duration: Theme.durationMedium; easing.type: Theme.easingStandard }
    }

    Timer {
        id: hideTimer
        interval: 2500
        onTriggered: {
            root.opacity = 0;
            root.slideOffset = -16;
        }
    }
}
