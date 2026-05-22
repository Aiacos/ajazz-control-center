# UI icon set

Clean-room **original** vector icons for the AJAZZ Control Center UI. Drawn from
scratch in the project's visual language (brand accent `#E63946`, dark Material
surfaces); **no vendor artwork is traced or reused** — these are functional
symbols (battery, gear, folder, …) and generic device-class illustrations, not
copies of any vendor icon.

## How they're themed (white ⇄ black, automatically)

Every line glyph and action icon uses `stroke="currentColor"` with a default
`color` set on the root `<svg>`. In QML, set the `color` on the consuming
`Image`/icon (or drive it from `Theme`) and the icon renders **white on the dark
theme and near-black on the light theme from the same file** — no per-theme
duplicate assets to maintain. Semantic accents (the brand-red highlight, and the
inherently-colourful `glyph-rgb` / `glyph-colorpicker` / `glyph-weather`) are
explicit colours and stay constant on both themes.

> Static white/black raster/SVG exports are not committed (they would be 2× dead
> files); the tintable source above is the single source of truth. Render at any
> size — SVG scales cleanly from 16 px up.

## Loading from QML

Aliased under `icons/` by `src/app/CMakeLists.txt` (a `CONFIGURE_DEPENDS` glob —
just drop a new SVG in and re-configure):

```qml
Image { source: "qrc:/qt/qml/AjazzControlCenter/icons/glyphs/glyph-sync.svg" }
// action icon:  .../icons/actions/action-website.svg
// device art:   .../icons/devices/device-mouse.svg
```

## Inventory

### `glyphs/` — feature icons (24)
`sync` · `battery` · `rgb` · `dpi` · `profile` · `settings` · `brightness` ·
`macro` · `layer` · `keybind` · `wireless` (2.4 GHz) · `display` (TFT/screen) ·
`firmware` · `power` (sleep) · `pollrate` · `lod` (lift-off) · `anglesnap` ·
`bluetooth` · `wired` (USB) · `colorpicker` · `record` · `delay` (debounce) ·
`clock` · `weather`.

### `actions/` — key/button assignment actions (40)
- **Mouse:** `click-left` `click-right` `click-middle` `scroll`
  `dpi-up` `dpi-down` `dpi-cycle` `dpi-shift` `button-back` `button-forward`
- **Keys:** `key` `multikey` `text` `mic-mute`
- **Media:** `play-pause` `stop` `next` `prev` `volume-up` `volume-down` `mute`
- **Launch:** `launch-app` `website` `email` `folder` `home` `favorites`
  `calculator` `search`
- **Edit/system:** `copy` `paste` `cut` `undo` `redo` `screenshot` `lock`
- **Window/special:** `show-desktop` `task-view` `disable` `default`

### `../devices/` — per-family illustrations (3)
`device-keyboard` (AK980-class) · `device-mouse` (AJ-series) ·
`device-streamdock` (AKP). Inherently coloured (dark surfaces + RGB hints), not
theme-tinted.

## Conventions for new icons
- 24×24 viewBox for glyphs/actions; `fill="none"`, `stroke="currentColor"`,
  `stroke-width="2"`, round caps/joins; one brand-red accent where it adds
  meaning.
- Put a default `color="#E8E8EE"` on the root `<svg>` so it previews on dark.
- Name `glyph-<feature>.svg` or `action-<verb-noun>.svg`; the CMake glob aliases
  it automatically.
