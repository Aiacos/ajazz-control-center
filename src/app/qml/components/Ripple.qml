// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ripple.qml — Material 3 animated state-layer for press feedback.
//
// Reusable component. Anchor to the button's background `Rectangle` and
// trigger via `ripple.trigger(x, y)` from a `Connections` block on the
// button's `pressedChanged` signal.
//
// Clipping convention: the parent `Rectangle` (button background) must
// set `clip: true`. The clip is rectangular at the bounding box; the
// rounded corners on small-radius buttons (radiusMd ≈ 6 px) show a
// negligible triangular fill at the four corners during the peak of the
// ripple, but the visible expansion stays inside the button.
//
// (We tried `MultiEffect { maskEnabled: true; maskSource: Rectangle {...} }`
// for proper rounded clipping, but the inline `Rectangle` does not
// satisfy `MultiEffect.maskSource`'s contract — Qt 6 docs require a
// `ShaderEffectSource`, `Image`, or `Item` with `layer.enabled: true`.
// Using a raw Rectangle triggered SIGABRT at first paint on real GPU
// pipelines while passing the offscreen QPA. Parent-clip is the
// pragmatic alternative; proper rounded mask would need a
// `ShaderEffectSource` indirection. YAGNI for now.)
//
// API:
//   property color rippleColor    — fill color of the expanding circle
//   property real  rippleOpacity  — peak alpha during fade-in (0..1)
//   property int   duration       — total animation time (ms)
//   function trigger(x, y)        — restart animation from (x, y)
//
// Asymmetric opacity envelope (30% fade-in / 70% fade-out) is M3-spec
// — symmetric curves feel mechanical; asymmetric feels human.

pragma ComponentBehavior: Bound
import QtQuick
import AjazzControlCenter

Item {
    id: root

    /// Fill color of the expanding circle. Pick the on-container
    /// foreground color of the button it overlays.
    property color rippleColor: "white"
    /// Peak alpha during fade-in. M3 pressed state-layer: 0.12 (on
    /// neutral surfaces) — 0.16 (on saturated container colors).
    property real rippleOpacity: 0.16
    /// Total animation duration; bind to `Theme.durationMedium` (≈ 280 ms).
    property int duration: 280

    /// Restart the ripple animation from (x, y) in this Item's local
    /// coordinates. Center-origin callers pass (width/2, height/2).
    function trigger(x, y) {
        rippleCircle.originX = x
        rippleCircle.originY = y
        rippleAnim.restart()
    }

    Rectangle {
        id: rippleCircle
        property real originX: 0
        property real originY: 0
        x: rippleCircle.originX - width / 2
        y: rippleCircle.originY - height / 2
        width: 0
        height: 0
        radius: width / 2
        color: root.rippleColor
        opacity: 0
    }

    ParallelAnimation {
        id: rippleAnim
        readonly property real maxDim: Math.max(root.width, root.height) * 2.5

        NumberAnimation {
            target: rippleCircle
            property: "width"
            from: 0
            to: rippleAnim.maxDim
            duration: root.duration
            easing.type: Easing.OutCubic
        }
        NumberAnimation {
            target: rippleCircle
            property: "height"
            from: 0
            to: rippleAnim.maxDim
            duration: root.duration
            easing.type: Easing.OutCubic
        }
        SequentialAnimation {
            NumberAnimation {
                target: rippleCircle
                property: "opacity"
                from: 0
                to: root.rippleOpacity
                duration: root.duration * 0.3
            }
            NumberAnimation {
                target: rippleCircle
                property: "opacity"
                from: root.rippleOpacity
                to: 0
                duration: root.duration * 0.7
                easing.type: Easing.InCubic
            }
        }
    }
}
