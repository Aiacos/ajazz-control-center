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
// Variants: "success" (Theme.accent), "error" (Theme.errorAccent),
// "info" (Theme.accent2). Text color is picked per variant via a
// luminance-based contrast helper so the message reads cleanly in
// both dark and light themes (a hardcoded near-black previously had
// only ~3:1 contrast against the light-theme accent green #2e7d32).
//
// Sizing follows the Material 3 Snackbar spec: 48 px height, slide-up
// entry from below the bottom edge alongside the opacity fade.
import QtQuick
import AjazzControlCenter

Rectangle {
    id: root
    visible: opacity > 0
    opacity: 0

    /// Resolves the variant string to a fill color from the Theme.
    function colorFor(variant) {
        if (variant === "error") {
            return Theme.errorAccent;
        }
        if (variant === "info") {
            return Theme.accent2;
        }
        return Theme.accent;
    }

    /// Pick a foreground color that contrasts cleanly with the variant
    /// background. ITU-R BT.601 luminance: bright accents (e.g. dark-
    /// theme #41CD52 green) get dark text; dark accents (e.g. light-
    /// theme #2e7d32 green) get light text.
    function textColorFor(bgColor) {
        var lum = 0.299 * bgColor.r + 0.587 * bgColor.g + 0.114 * bgColor.b;
        return lum > 0.5 ? "#0e1011" : "#f4f4f8";
    }

    width: Math.min(parent ? parent.width - 2 * Theme.spacingLg : 480, 480)
    height: 48
    radius: Theme.radiusMd
    color: colorFor(_variant)
    anchors.bottom: parent ? parent.bottom : undefined
    // `bottomOffset` slides the toast up from below the window edge
    // when shown, parking it back below the edge when hidden.
    anchors.bottomMargin: Theme.spacingLg + bottomOffset
    anchors.horizontalCenter: parent ? parent.horizontalCenter : undefined
    z: 1000

    property string _variant: "success"
    /// Vertical offset relative to the resting bottomMargin position.
    /// 0 = visible (resting), -32 = parked below the visible area.
    property real bottomOffset: -32

    Text {
        id: label
        anchors.fill: parent
        anchors.leftMargin: Theme.spacingLg
        anchors.rightMargin: Theme.spacingLg
        verticalAlignment: Text.AlignVCenter
        color: root.textColorFor(root.color)
        font.pixelSize: Theme.fontMd
        font.bold: true
    }

    Behavior on opacity {
        NumberAnimation { duration: 180; easing.type: Easing.OutCubic }
    }
    Behavior on bottomOffset {
        NumberAnimation { duration: 220; easing.type: Easing.OutCubic }
    }

    Timer {
        id: hideTimer
        interval: 3500
        repeat: false
        onTriggered: {
            root.opacity = 0;
            root.bottomOffset = -32;
        }
    }

    /// Show the toast with the given message and variant.
    /// `variant` is one of "success" (default), "error", "info".
    function show(message, variant) {
        label.text = message;
        _variant = variant || "success";
        opacity = 1;
        bottomOffset = 0;
        hideTimer.restart();
    }
}
