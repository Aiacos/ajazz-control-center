// SPDX-License-Identifier: GPL-3.0-or-later
//
// Theme.qml — central design-token singleton for AJAZZ Control Center.
//
// All QML files MUST consume colors and spacing from this singleton instead
// of using hard-coded literals.  The singleton is a thin facade over the
// `branding` context property (BrandingService) that:
//
//   * proxies colors that are already part of the branding contract
//     (accent, accent2, bgBase, bgSidebar, bgRowHover, fgPrimary, fgMuted)
//     so a custom theme.json or `AJAZZ_BRAND_DIR` build flag re-skins the
//     entire UI without touching QML;
//   * derives semantic tokens (tile, tileHover, borderSubtle, …) from the
//     branding palette so we have a single, named source of truth for every
//     surface, border and text role used by the app;
//   * provides safe fallbacks when `branding` is unavailable (e.g. unit tests
//     or QML hot-reload scenarios) so the UI never crashes on a null deref.
//
// Usage from any QML file:
//
//     import AjazzControlCenter
//     Rectangle { color: Theme.tile; border.color: Theme.borderSubtle }
//
// See docs/analysis/ui-review.md (F-01, F-02, F-14, F-15) for context.
pragma Singleton
import QtQuick

QtObject {
    id: theme

    // ---- Branding-driven base palette --------------------------------------
    // Each of these falls back to the canonical AJAZZ Control Center palette
    // (#14141a + #41CD52 accent) when `branding` is not yet initialized.

    /// Primary application background — large surfaces, content area.
    readonly property color bgBase:     branding ? branding.bgBase     : "#14141a"
    /// Sidebar / navigation rail background.
    readonly property color bgSidebar:  branding ? branding.bgSidebar  : "#1e1e23"
    /// Row hover highlight (sidebar list, table rows).
    readonly property color bgRowHover: branding ? branding.bgRowHover : "#2c2c34"

    /// Primary foreground (titles, headings).
    readonly property color fgPrimary:  branding ? branding.fgPrimary  : "#f0f0f0"
    /// Muted foreground (secondary labels, captions).
    readonly property color fgMuted:    branding ? branding.fgMuted    : "#888888"
    /// Tertiary foreground for de-emphasized helper text.
    readonly property color fgFaint:    "#aaaaaa"

    /// Brand accent (primary CTAs, focus rings, key indicators).
    readonly property color accent:     branding ? branding.accent     : "#41CD52"
    /// Secondary accent (hover/pressed states, links, info badges).
    readonly property color accent2:    branding ? branding.accent2    : "#0A82FA"

    // ---- Derived semantic tokens -------------------------------------------
    // These are not part of the branding contract; they are computed locally
    // so the look stays consistent regardless of which accent ships.

    /// Idle tile / card surface used by KeyDesigner, EncoderPanel, MousePanel.
    readonly property color tile:        "#24242a"
    /// Hovered tile surface — slightly lighter than `tile`.
    readonly property color tileHover:   "#2a2a32"
    /// Subtle border for tiles, separators, divider lines.
    readonly property color borderSubtle: "#3a3a44"

    // ---- Spacing / radius scale -------------------------------------------
    // Lightweight 4-point scale, matches what the QML tree already uses.

    readonly property int spacingXs:  4
    readonly property int spacingSm:  8
    readonly property int spacingMd:  12
    readonly property int spacingLg:  16
    readonly property int spacingXl:  24

    readonly property int radiusSm:   4
    readonly property int radiusMd:   6
    readonly property int radiusLg:   8

    // ---- Typography scale --------------------------------------------------

    readonly property int fontXs:  11
    readonly property int fontSm:  12
    readonly property int fontMd:  14
    readonly property int fontLg:  18
    readonly property int fontXl:  20
}
