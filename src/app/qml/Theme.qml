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
    /// Tertiary foreground for de-emphasized helper text — derived from
    /// `fgPrimary` at 55% opacity so it follows theme polarity (dark themes
    /// get a lightened tone, light themes get a darkened tone). Pre-2026-05
    /// this was a hardcoded "#bdbdbd" which inverted polarity in light mode.
    readonly property color fgFaint:
        Qt.rgba(fgPrimary.r, fgPrimary.g, fgPrimary.b, 0.55)

    /// Brand accent (primary CTAs, focus rings, key indicators).
    readonly property color accent:     Branding.accent
    /// Secondary accent (hover/pressed states, links, info badges).
    readonly property color accent2:    Branding.accent2
    /// Error accent (destructive states, error toasts, validation
    /// failures). Not part of the branding contract — the same
    /// medium-red works on both light and dark surfaces.
    readonly property color errorAccent: "#e34c4c"
    /// Warning accent (signed-but-untrusted plugins, deprecated APIs,
    /// reversible-but-unusual operations). Amber 500 — works on both
    /// themes by sitting in the same value/saturation band as
    /// `errorAccent`.
    readonly property color warningAccent: "#f59e0b"

    // ---- Semantic chip tokens (status badges) ----------------------------
    // Used by trust-status chips (unsigned / self-signed plugins), validation
    // badges, deprecated-API tooltips. The "bg" tint of the accent gives a
    // soft pill background that reads on both dark and light surfaces;
    // "fg" is the accent itself (high-contrast text), and "border" is a
    // saturated variant for definition.

    /// Background for `errorAccent` chips — bgBase tinted ~15% with the accent.
    readonly property color chipBgError:
        Qt.tint(bgBase, Qt.rgba(errorAccent.r, errorAccent.g, errorAccent.b, 0.15))
    /// Foreground (text) for `errorAccent` chips.
    readonly property color chipFgError: errorAccent
    /// Border for `errorAccent` chips.
    readonly property color chipBorderError: errorAccent
    /// Background for `warningAccent` chips.
    readonly property color chipBgWarning:
        Qt.tint(bgBase, Qt.rgba(warningAccent.r, warningAccent.g, warningAccent.b, 0.15))
    /// Foreground for `warningAccent` chips.
    readonly property color chipFgWarning: warningAccent
    /// Border for `warningAccent` chips.
    readonly property color chipBorderWarning: warningAccent

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

    // ---- M3 surface tint scale --------------------------------------------
    // Material 3 elevation surfaces — `bgBase` tinted with `accent` at the
    // M3-spec opacity ladder (5 / 8 / 11 / 14 / 16 %). Unlike `tile` (which
    // uses `fgPrimary` for polarity-flip neutral elevation), these are
    // *brand-flavored* surfaces — a warmth shift toward the accent color
    // that reads as the same family in both dark and light themes because
    // the low alpha keeps `bgBase` dominant.
    //
    // Wire from any surface that takes an `elevation: int` property:
    //
    //     Rectangle { color: Theme.surfaceAt(elevation) }
    //
    // Or pick a named token directly when the elevation is fixed:
    //
    //     Rectangle { color: Theme.surfaceContainer }   // level 3

    /// Surface Container Lowest — level 1.
    readonly property color surfaceContainerLowest:
        Qt.tint(bgBase, Qt.rgba(accent.r, accent.g, accent.b, 0.05))
    /// Surface Container Low — level 2.
    readonly property color surfaceContainerLow:
        Qt.tint(bgBase, Qt.rgba(accent.r, accent.g, accent.b, 0.08))
    /// Surface Container — level 3 (default container surface).
    readonly property color surfaceContainer:
        Qt.tint(bgBase, Qt.rgba(accent.r, accent.g, accent.b, 0.11))
    /// Surface Container High — level 4.
    readonly property color surfaceContainerHigh:
        Qt.tint(bgBase, Qt.rgba(accent.r, accent.g, accent.b, 0.14))
    /// Surface Container Highest — level 5.
    readonly property color surfaceContainerHighest:
        Qt.tint(bgBase, Qt.rgba(accent.r, accent.g, accent.b, 0.16))

    /// Resolve an elevation level to its surface-tint token. Levels 0 and
    /// negative resolve to `bgBase` (no tint); levels above 5 clamp to
    /// `surfaceContainerHighest`.
    function surfaceAt(level) {
        if (level <= 0) return bgBase
        if (level === 1) return surfaceContainerLowest
        if (level === 2) return surfaceContainerLow
        if (level === 3) return surfaceContainer
        if (level === 4) return surfaceContainerHigh
        return surfaceContainerHighest
    }

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

    // ---- Material 3 typography ramp ---------------------------------------
    // The legacy `fontXs/Sm/Md/Lg/Xl` aliases above stay in place so existing
    // consumers keep working; new code (and gradual migrations) can adopt the
    // M3-named roles below. M3 ramp ref:
    //   https://m3.material.io/styles/typography/type-scale-tokens
    //
    // We expose `pixelSize`, `weight` and `letterSpacing` for each role; the
    // density-aware `lineHeight` is intentionally elided — Qt's Text element
    // handles line height via `lineHeightMode + lineHeight` and most call
    // sites benefit from the QtQuick default.
    //
    // Subset is task-scaled to a desktop control-center: skipping the
    // five `display*` roles (used only for very large hero numerals) and
    // capping `headline*` at the medium tier. Add the missing tiers if a
    // future page needs them.

    /// Headline Large — top of a primary page (32 px / 400 weight).
    readonly property var typeHeadlineLarge:
        ({ pixelSize: 32, weight: Font.Normal,   letterSpacing: 0     })
    /// Headline Medium — empty-state hero, dialog heading (28 px / 400).
    readonly property var typeHeadlineMedium:
        ({ pixelSize: 28, weight: Font.Normal,   letterSpacing: 0     })
    /// Title Large — section heading (22 px / 400).
    readonly property var typeTitleLarge:
        ({ pixelSize: 22, weight: Font.Normal,   letterSpacing: 0     })
    /// Title Medium — card title, sub-section heading (16 px / 500).
    readonly property var typeTitleMedium:
        ({ pixelSize: 16, weight: Font.Medium,   letterSpacing: 0.15  })
    /// Title Small — list-row title, encoder label (14 px / 500).
    readonly property var typeTitleSmall:
        ({ pixelSize: 14, weight: Font.Medium,   letterSpacing: 0.1   })
    /// Body Large — primary read-text (16 px / 400).
    readonly property var typeBodyLarge:
        ({ pixelSize: 16, weight: Font.Normal,   letterSpacing: 0.5   })
    /// Body Medium — default body copy (14 px / 400).
    readonly property var typeBodyMedium:
        ({ pixelSize: 14, weight: Font.Normal,   letterSpacing: 0.25  })
    /// Body Small — captions, helper text (12 px / 400).
    readonly property var typeBodySmall:
        ({ pixelSize: 12, weight: Font.Normal,   letterSpacing: 0.4   })
    /// Label Large — button text, prominent labels (14 px / 500).
    readonly property var typeLabelLarge:
        ({ pixelSize: 14, weight: Font.Medium,   letterSpacing: 0.1   })
    /// Label Medium — chip text, dense labels (12 px / 500).
    readonly property var typeLabelMedium:
        ({ pixelSize: 12, weight: Font.Medium,   letterSpacing: 0.5   })
    /// Label Small — counters, badges (11 px / 500).
    readonly property var typeLabelSmall:
        ({ pixelSize: 11, weight: Font.Medium,   letterSpacing: 0.5   })

    // ---- Motion tokens -----------------------------------------------------
    // Material 3 motion durations (subset of the 16-token spec, scaled to the
    // few cadences this app actually animates: hover + focus, transient
    // surfaces like Snackbar, modal sheets / drawers).
    //
    //   short  ≈ 100-200 ms — hover, ripple, micro-interactions
    //   medium ≈ 250-400 ms — surface enter/exit, tab switch
    //   long   ≈ 500-700 ms — modal sheet/drawer slide, large surface change
    //
    // Easings: M3 specifies cubic-bezier curves that we approximate with the
    // closest QtQuick `Easing.Type` (Out*** for decelerate-to-rest, In***
    // for accelerate-from-rest, InOut*** for symmetric). Bind via
    //
    //     NumberAnimation { duration: Theme.durationMedium; easing.type: Theme.easingStandard }

    /// Short motion — hover, focus, button press feedback.
    readonly property int durationShort:  150
    /// Medium motion — surface enter/exit, tab content swap, snackbar.
    readonly property int durationMedium: 280
    /// Long motion — drawer open, modal sheet, large layout reflow.
    readonly property int durationLong:   500

    /// Standard easing — surface enter/exit at rest. Closest M3 match.
    readonly property int easingStandard:   Easing.OutCubic
    /// Decelerate easing — element entering its resting state.
    readonly property int easingDecelerate: Easing.OutQuint
    /// Accelerate easing — element leaving the screen.
    readonly property int easingAccelerate: Easing.InCubic

    // ---- Elevation tokens --------------------------------------------------
    // Material 3 levels 0-3 expressed as a `QtQuick.Effects.MultiEffect`-
    // friendly bag: vertical offset (px), blur radius (0..1, the MultiEffect
    // scale), shadow opacity (0..1). MultiEffect supports one shadow per
    // pass; the values below pick a single-shadow tuning that approximates
    // each level's combined key + ambient shadow.
    //
    // Wire from any surface:
    //
    //     Rectangle { id: card; ... }
    //     MultiEffect {
    //         source: card
    //         anchors.fill: card
    //         shadowEnabled: true
    //         shadowVerticalOffset: Theme.elevation1.offsetY
    //         shadowBlur: Theme.elevation1.blur
    //         shadowColor: Theme.elevationShadowColor
    //         shadowOpacity: Theme.elevation1.opacity
    //     }
    //
    // Designers think in `level: 0..3` integers; `elevationOf(level)`
    // resolves to the right token bag so call-sites can stay terse.

    /// Shadow color — black at theme-appropriate intensity. The opacity
    /// in the elevation bag scales this further per level.
    readonly property color elevationShadowColor: "#000000"

    /// Level 0 — flat (no shadow). Mostly here so `elevationOf(0)` works.
    readonly property var elevation0: ({ offsetY: 0, blur: 0,    opacity: 0    })
    /// Level 1 — at-rest cards, list rows, tray-like surfaces.
    readonly property var elevation1: ({ offsetY: 1, blur: 0.10, opacity: 0.18 })
    /// Level 2 — hover state for level-1 surfaces, FAB at rest.
    readonly property var elevation2: ({ offsetY: 2, blur: 0.18, opacity: 0.22 })
    /// Level 3 — Snackbar, dropdown menu, picked-up card.
    readonly property var elevation3: ({ offsetY: 4, blur: 0.30, opacity: 0.28 })

    /// Resolve an elevation level to its token bag. Out-of-range levels
    /// (negative, > 3) clamp to the closest valid one.
    function elevationOf(level) {
        if (level <= 0) return elevation0
        if (level === 1) return elevation1
        if (level === 2) return elevation2
        return elevation3
    }

    // ---- Focus / accessibility --------------------------------------------
    /// Focus ring width (px) for keyboard navigation — see F-04 in ui-review.
    readonly property int focusRingWidth: 2
    /// Minimum interactive target size (px) — GUIDELINES says ≥44×44.
    readonly property int minTouchTarget: 44
}
