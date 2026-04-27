// SPDX-License-Identifier: GPL-3.0-or-later
//
// Theme.qml — central design-token singleton for AJAZZ Control Center.
//
// All QML files MUST consume colors and spacing from this singleton instead
// of using hard-coded literals.  The singleton is a thin facade over the
// `Branding` QML singleton (BrandingService — registered via QML_SINGLETON
// in branding_service.hpp) that:
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
import AjazzControlCenter

QtObject {
    id: theme

    // ---- Branding-driven base palette --------------------------------------
    // The Branding singleton is registered before the QML engine loads (see
    // Application::exposeToQml), so these bindings always resolve. The
    // BrandingService constructor seeds every field via loadEmbeddedDefaults,
    // so the canonical AJAZZ palette is the value when no override file is
    // active.

    /// Primary application background — large surfaces, content area.
    readonly property color bgBase:     Branding.bgBase
    /// Sidebar / navigation rail background.
    readonly property color bgSidebar:  Branding.bgSidebar
    /// Row hover highlight (sidebar list, table rows).
    readonly property color bgRowHover: Branding.bgRowHover

    /// Primary foreground (titles, headings).
    readonly property color fgPrimary:  Branding.fgPrimary
    /// Muted foreground (secondary labels, captions).
    /// WCAG 2.1 AA: "#aaaaaa" on "#1e1e23" sidebar = 7.5:1; on "#14141a" base = 9.0:1.
    readonly property color fgMuted:    Branding.fgMuted
    /// Tertiary foreground for de-emphasized helper text — ≥4.5:1 on dark surfaces.
    readonly property color fgFaint:    "#bdbdbd"

    /// Brand accent (primary CTAs, focus rings, key indicators).
    readonly property color accent:     Branding.accent
    /// Secondary accent (hover/pressed states, links, info badges).
    readonly property color accent2:    Branding.accent2
    /// Error accent (destructive states, error toasts, validation
    /// failures). Not part of the branding contract — the same
    /// medium-red works on both light and dark surfaces.
    readonly property color errorAccent: "#e34c4c"

    // ---- Derived semantic tokens -------------------------------------------
    // These are not part of the branding contract; they are derived from
    // `bgSidebar` + `fgPrimary` so they follow theme polarity automatically:
    // dark themes get lightened (fgPrimary is near-white), light themes get
    // darkened (fgPrimary is near-black). The α factors below were chosen so
    // the dark-theme hex output matches the previous hardcoded literals
    // (#24242a / #2a2a32 / #3a3a44) within < 1% per channel.
    //
    // Why: pre-2026-04 these were hardcoded dark literals, which painted
    // every card/tile/border dark even when the user picked the light theme
    // (visible as black device-list rows on a near-white sidebar — see
    // `docs/screenshots/main-light.png` from the screenshots cycle).

    /// Idle tile / card surface used by KeyDesigner, EncoderPanel, MousePanel,
    /// DeviceRow, AppHeader search field, PluginStore tiles, Settings page.
    readonly property color tile:
        Qt.tint(bgSidebar, Qt.rgba(fgPrimary.r, fgPrimary.g, fgPrimary.b, 0.03))
    /// Hovered tile surface — slightly more elevated than `tile`.
    readonly property color tileHover:
        Qt.tint(bgSidebar, Qt.rgba(fgPrimary.r, fgPrimary.g, fgPrimary.b, 0.06))
    /// Subtle border for tiles, separators, divider lines.
    readonly property color borderSubtle:
        Qt.tint(bgSidebar, Qt.rgba(fgPrimary.r, fgPrimary.g, fgPrimary.b, 0.13))

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

    // F-03: bumped fontXs from 11→12 to keep contrast ≥ 4.5:1 at small sizes.
    readonly property int fontXs:  12
    readonly property int fontSm:  13
    readonly property int fontMd:  14
    readonly property int fontLg:  18
    readonly property int fontXl:  20

    // ---- Focus / accessibility --------------------------------------------
    /// Focus ring width (px) for keyboard navigation — see F-04 in ui-review.
    readonly property int focusRingWidth: 2
    /// Minimum interactive target size (px) — GUIDELINES says ≥44×44.
    readonly property int minTouchTarget: 44
}
