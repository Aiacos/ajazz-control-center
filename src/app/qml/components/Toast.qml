// SPDX-License-Identifier: GPL-3.0-or-later
//
// Toast.qml — transient notification banner shown at the bottom of the
// window. Used by ProfileEditor to surface save success/failure and by
// future async actions.
//
// Usage:
//     Toast { id: toast }
//     toast.show("Profile saved", "success");
//
// Variants: "success" (accent), "error" (red), "info" (accent2).
import QtQuick
import AjazzControlCenter

Rectangle {
    id: root
    visible: opacity > 0
    opacity: 0

    /// Resolves the variant string to a fill color from the Theme.
    function colorFor(variant) {
        if (variant === "error") {
            return "#e34c4c";
        }
        if (variant === "info") {
            return Theme.accent2;
        }
        return Theme.accent;
    }

    width: Math.min(parent ? parent.width - 2 * Theme.spacingLg : 480, 480)
    height: 44
    radius: Theme.radiusMd
    color: colorFor(_variant)
    anchors.bottom: parent ? parent.bottom : undefined
    anchors.bottomMargin: Theme.spacingLg
    anchors.horizontalCenter: parent ? parent.horizontalCenter : undefined
    z: 1000

    property string _variant: "success"

    Text {
        id: label
        anchors.fill: parent
        anchors.leftMargin: Theme.spacingLg
        anchors.rightMargin: Theme.spacingLg
        verticalAlignment: Text.AlignVCenter
        color: "#0e1011"
        font.pixelSize: Theme.fontMd
        font.bold: true
    }

    Behavior on opacity {
        NumberAnimation { duration: 180; easing.type: Easing.OutCubic }
    }

    Timer {
        id: hideTimer
        interval: 3500
        repeat: false
        onTriggered: root.opacity = 0
    }

    /// Show the toast with the given message and variant.
    /// `variant` is one of "success" (default), "error", "info".
    function show(message, variant) {
        label.text = message;
        _variant = variant || "success";
        opacity = 1;
        hideTimer.restart();
    }
}
