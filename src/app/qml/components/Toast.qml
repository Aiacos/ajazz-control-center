// SPDX-License-Identifier: GPL-3.0-or-later
//
// Toast.qml — transient bottom-anchored notification, Material 3 Snackbar
// pattern.
//
// Usage:
//     Toast { id: toast }
//     toast.show("Profile saved", "success")
//     toast.show("Save failed: disk full", "error")
//     // With action button (M3 Snackbar with action):
//     toast.show("File deleted", "info", "Undo", function() { restore() })
//
// Variants
//   "success" — Theme.accent (brand)
//   "error"   — Theme.errorAccent
//   "info"    — Theme.accent2
//
// Geometry follows the M3 Snackbar spec:
//   * height: 48 (text-only), 56 (with action button)
//   * corner radius: Theme.radiusMd
//   * elevation level 3 — drop shadow via QtQuick.Effects.MultiEffect
//   * width: clamp(344, parent.width - 2*spacingLg, 672) — M3 min/max
//
// Animations bind to the Theme motion tokens:
//   * enter: durationMedium with easingStandard (slide up + fade in)
//   * exit:  durationMedium with easingAccelerate (slide down + fade out)
//
// Text contrast: foreground colour is picked per-variant via the same
// ITU-R BT.601 luminance helper as before — bright accents (e.g. dark-
// theme green) get dark text; dark accents get light text.

pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Effects
import AjazzControlCenter

Item {
    id: root
    visible: opacity > 0
    opacity: 0
    z: 1000

    // -------- Public API --------------------------------------------------

    /// Show the toast with the given message and variant.
    /// `variant`     one of "success" (default), "error", "info"
    /// `actionLabel` optional — text for the trailing action button (M3
    ///               Snackbar with action). Pass empty/undefined to render
    ///               a text-only snackbar.
    /// `actionCb`    optional — function called when the user clicks the
    ///               action button. The toast hides as soon as the action
    ///               fires.
    function show(message, variant, actionLabel, actionCb) {
        root._message = message
        root._variant = variant || "success"
        root._actionLabel = actionLabel || ""
        root._actionCb = actionCb || null
        opacity = 1
        bottomOffset = 0
        hideTimer.restart()
    }

    /// Hide immediately without waiting for the auto-dismiss timer.
    function dismiss() {
        opacity = 0
        bottomOffset = -32
        hideTimer.stop()
    }

    // -------- Internal state ---------------------------------------------

    property string _message: ""
    property string _variant: "success"
    property string _actionLabel: ""
    property var    _actionCb: null
    /// Vertical offset relative to the resting bottomMargin position.
    /// 0 = visible (resting), -32 = parked below the visible area.
    property real bottomOffset: -32

    // -------- Layout ------------------------------------------------------

    width: snackbar.width
    height: snackbar.height
    anchors.bottom: parent ? parent.bottom : undefined
    anchors.horizontalCenter: parent ? parent.horizontalCenter : undefined
    anchors.bottomMargin: Theme.spacingLg + bottomOffset

    // Resolves the variant string to the snackbar's fill color. Kept on
    // the root so the inner Rectangle binds reactively when `_variant`
    // changes between successive show() calls.
    function colorFor(variant) {
        if (variant === "error") {
            return Theme.errorAccent
        }
        if (variant === "info") {
            return Theme.accent2
        }
        return Theme.accent
    }

    // Pick a foreground color that contrasts cleanly with the variant
    // background. ITU-R BT.601 luminance: bright accents get dark text;
    // dark accents get light text.
    function textColorFor(bgColor) {
        const lum = 0.299 * bgColor.r + 0.587 * bgColor.g + 0.114 * bgColor.b
        return lum > 0.5 ? "#0e1011" : "#f4f4f8"
    }

    // -------- Snackbar surface -------------------------------------------

    Rectangle {
        id: snackbar
        // Layer-cached so the `layer.effect` MultiEffect below can sample
        // it for the M3 elevation-3 drop shadow without forcing a second
        // paint of the Rectangle (`layer.effect` replaces the direct draw
        // with the effect's output, no double-rendering).
        layer.enabled: true
        layer.effect: MultiEffect {
            shadowEnabled: true
            shadowColor: Theme.elevationShadowColor
            shadowVerticalOffset: Theme.elevation3.offsetY
            shadowBlur: Theme.elevation3.blur
            shadowOpacity: Theme.elevation3.opacity
        }
        radius: Theme.radiusMd
        color: root.colorFor(root._variant)

        // Width: clamp to M3 Snackbar (344..672) minus parent padding.
        width: {
            const parentW = root.parent ? root.parent.width : 480
            const max = Math.min(672, parentW - 2 * Theme.spacingLg)
            return Math.max(344, max)
        }
        height: root._actionLabel.length > 0 ? 56 : 48

        // Message + optional trailing action button.
        Item {
            anchors.fill: parent
            anchors.leftMargin: Theme.spacingLg
            anchors.rightMargin: Theme.spacingSm

            Text {
                id: messageText
                anchors.left: parent.left
                anchors.right: actionButton.visible ? actionButton.left : parent.right
                anchors.rightMargin: actionButton.visible ? Theme.spacingSm : 0
                anchors.verticalCenter: parent.verticalCenter
                color: root.textColorFor(snackbar.color)
                font.pixelSize: Theme.typeBodyMedium.pixelSize
                font.weight: Font.Medium
                font.letterSpacing: Theme.typeBodyMedium.letterSpacing
                text: root._message
                elide: Text.ElideRight
                wrapMode: Text.NoWrap
            }

            Rectangle {
                id: actionButton
                visible: root._actionLabel.length > 0
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                width: actionLabel.implicitWidth + 2 * Theme.spacingMd
                height: 36
                radius: Theme.radiusSm
                // Inline tinted button — tonal, brand-aware. Hover/press
                // are subtle on a coloured snackbar background; the
                // luminance pick ensures the label still reads.
                color: actionMouse.pressed
                    ? Qt.tint(snackbar.color, Qt.rgba(0, 0, 0, 0.20))
                    : (actionMouse.containsMouse
                        ? Qt.tint(snackbar.color, Qt.rgba(0, 0, 0, 0.10))
                        : "transparent")

                Text {
                    id: actionLabel
                    anchors.centerIn: parent
                    text: root._actionLabel
                    color: root.textColorFor(snackbar.color)
                    font.pixelSize: Theme.typeLabelLarge.pixelSize
                    font.weight: Theme.typeLabelLarge.weight
                    font.letterSpacing: Theme.typeLabelLarge.letterSpacing
                }

                MouseArea {
                    id: actionMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        if (root._actionCb) {
                            root._actionCb()
                        }
                        root.dismiss()
                    }
                }
            }
        }
    }

    Behavior on opacity {
        NumberAnimation {
            duration: Theme.durationMedium
            easing.type: Theme.easingStandard
        }
    }
    Behavior on bottomOffset {
        NumberAnimation {
            duration: Theme.durationMedium
            easing.type: Theme.easingStandard
        }
    }

    Timer {
        id: hideTimer
        // M3 default Snackbar dwell is 4-10 s; 3.5 s preserves the
        // previous behaviour so existing call-sites feel unchanged.
        interval: 3500
        repeat: false
        onTriggered: root.dismiss()
    }
}
