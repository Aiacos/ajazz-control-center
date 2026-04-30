// SPDX-License-Identifier: GPL-3.0-or-later
//
// Card.qml — generic surface container.
//
// Uses Theme tokens so the card automatically follows the current branding /
// light-vs-dark mode. Children are placed in the default `data` slot; size is
// controlled by the parent via implicit/explicit width and height.
//
// Optional elevation (Material 3 levels 0-3): set the `elevation` property
// to opt the card into a drop shadow. Default is 0 (flat — preserves the
// pre-2026-04 visual contract for the dozens of consumers that already use
// Card without expecting a shadow). Levels 1-3 wire `layer.enabled` plus a
// `MultiEffect` shadow sized via `Theme.elevationOf(level)`.
//
// `pragma ComponentBehavior: Bound` is required so qmllint can statically
// resolve the `root` id reads inside the nested `layer.effect` Component
// (without it, the MultiEffect's `shadowOpacity` etc. trip an `[unqualified]`
// warning despite the binding being valid at runtime).

pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Effects
import AjazzControlCenter

Rectangle {
    id: root
    color: root.elevation === 0 ? Theme.tile : Theme.surfaceAt(root.elevation)
    border.color: Theme.borderSubtle
    border.width: 1
    radius: Theme.radiusLg

    /// Material 3 elevation level (0 flat … 3 picked-up). Default 0 so
    /// existing call-sites render exactly as before; raise to 1 or 2 for
    /// a tactile lift on cards that benefit from depth.
    property int elevation: 0

    // `layer.effect` replaces the Rectangle's direct draw with the effect's
    // output (no double-rendering), so the shadow + content come out as a
    // single composited surface. We only enable the layer when there is a
    // shadow to draw — `layer.enabled: true` has measurable cost (offscreen
    // FBO allocation per Card) so the default-zero path stays free.
    layer.enabled: root.elevation > 0
    layer.effect: MultiEffect {
        shadowEnabled: root.elevation > 0
        shadowColor: Theme.elevationShadowColor
        shadowVerticalOffset: Theme.elevationOf(root.elevation).offsetY
        shadowBlur: Theme.elevationOf(root.elevation).blur
        shadowOpacity: Theme.elevationOf(root.elevation).opacity
    }
}
