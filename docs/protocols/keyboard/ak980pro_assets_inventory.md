# AJAZZ AK980 PRO — vendor asset inventory

Inventory of every non-code asset bundled with `DeviceDriver.exe`
(Beta 1.0.0.6, 2025-01-02), with format identification and intended use.
Clean-room: only file metadata + magic-byte hex-dumps were used; no
asset was executed or rendered.

> Path prefix for everything below:
> `C:/Users/unilo/reverse-eng-workdir/ak980pro/inno_extracted/app/`

______________________________________________________________________

## 1. `device/` — device-card preview images (~668 KB total)

| File                              | Bytes  | Format                         | Use                                                        |
| --------------------------------- | ------ | ------------------------------ | ---------------------------------------------------------- |
| `home_keyboard_connected.png`     | 348 467 | PNG, 1000×400, 8-bit RGBA      | Hero illustration of the keyboard shown on the "Home" tab **when connected** |
| `home_keyboard_disconnected.png`  | 319 590 | PNG, 1000×400, 8-bit RGBA      | Hero illustration **when disconnected** (greyed-out variant) |

Both PNGs are referenced from `config.xml`'s
`<keyboard … img_connected="…" img_disconnected="…">` attributes.

**Naming convention**: literal file names (no per-device codename) —
this driver bundle ships only one keyboard variant (AK980 PRO + XS75T
re-badge), so the names are flat. If additional devices were added,
they'd presumably follow `home_<deviceCodename>_connected.png` /
`…_disconnected.png`.

______________________________________________________________________

## 2. `firmware/` — empty (in this build)

The directory exists but is **empty**. Confirms the prior finding: the
vendor downloads `FirmwareUpdateTool.zip` over HTTP at update time
(via `FUN_0045a520` in DeviceDriver.exe) — no firmware blob is bundled
inside the driver installer.

**Implication**: we have no firmware reference for the AK980 PRO MCU
SKU or version. Static RE of `DeviceDriver.exe` alone **cannot** recover
the DFU opcode or bootloader command set.

______________________________________________________________________

## 3. `font/` — fallback fonts (~884 KB total)

| File                       | Bytes   | Format                                       | Use |
| -------------------------- | ------- | -------------------------------------------- | --- |
| `OpenSans-Regular.ttf`     | 217 360 | TrueType, digitally signed, Google Open Sans | UI body text fallback |
| `OpenSans-Semibold.ttf`    | 221 328 | TrueType                                     | UI semibold (headings) |
| `OpenSans-ExtraBold.ttf`   | 222 584 | TrueType                                     | UI extra-bold (emphasis) |
| `OpenSans-Light.ttf`       | 222 412 | TrueType                                     | UI light (subtitle) |

All four are standard **Open Sans 1.10** TTF files (Google Fonts /
Apache-2.0 license). The vendor ships them so the UI looks consistent
even on systems without Open Sans installed.

**For us**: Qt 6 ships with its own bundled fonts and respects system
fallback chains. We don't need to bundle Open Sans unless we
specifically want to match the vendor look — and that's not part of our
brand. **No copying these into our repo.**

`mui.dll` references font names like `Open Sans`, `Microsoft YaHei` (CN
default), `MS Shell Dlg`. Our `QFontDatabase` + system fallback handles
this.

______________________________________________________________________

## 4. `gif/AJAZZ AK980 三模RGB带屏 Keyboard/` — default LCD content

| File   | Bytes   | Format                                   | Use |
| ------ | ------- | ---------------------------------------- | --- |
| `0.gif` | 238 733 | GIF89a, 240×135 px (matches LCD geometry) | Default GIF uploaded to the on-keyboard TFT screen when the user resets to defaults |

The hex header confirms standard GIF89a with NetscapeApp / Animation
extension (`!ÿ\x0bNETSCAPE2.0…` typically present in the trailer; not
explicitly verified, but the size and 240×135 dimensions match the
firmware's LCD).

**Naming convention**: the parent directory name **is the keyboard's
Chinese long-product-name** ("AJAZZ AK980 三模 RGB 带屏 Keyboard" =
"AJAZZ AK980 Tri-mode RGB Screened Keyboard"). The file inside is
zero-indexed by GIF slot — `0.gif`, `1.gif`, … up to 9 (since
`config.xml` declares `gif_count="1"`, only `0.gif` is bundled, but
the slot model is multi-GIF capable).

**Replay path**: the vendor reads this file, splits it into ≤140 frames,
encodes each frame to RGB565, and ships them via opcode `0x7F 0x03`
(see `ak980pro_tft_protocol.md`).

**For us**: bundle our own default GIF (one that doesn't infringe on
AJAZZ's branding) under
`resources/keyboards/ak980pro/default-screen.gif`, ship it as a
QRC-embedded resource, and decode via `QMovie` → cooked RGB565 frames.

______________________________________________________________________

## 5. `language/` — UI string catalogs

| File         | Encoding              | Locale            | Notes |
| ------------ | --------------------- | ----------------- | ----- |
| `1033.lan`   | UTF-16 LE with BOM    | en-US             | English |
| `2052.lan`   | UTF-16 LE with BOM    | zh-CN simplified  | **Primary source** |
| `1028.lan`   | UTF-16 LE with BOM    | zh-TW traditional |       |
| `1049.lan`   | UTF-16 LE with BOM    | ru-RU             |       |

Format: simple line-delimited `id\tstring` (already mined in
`ak980pro_vendor.md` §1.3). UI string IDs 50-887 are documented there.

______________________________________________________________________

## 6. `layouts/` — device-keymap definitions

| File                | Format | Use |
| ------------------- | ------ | --- |
| `rgb-keyboard.xml`  | XML    | Per-device keymap: declares `<key>` entries with `code` (HID usage), `name`, `desc`, `row_col`, `rect_*` (UI positioning), `key_index`, `light_index`, `fnlayer_disable` |

**Per-device per-layout convention**: `<layouts>/<deviceCodename>.xml`.
The AK980 PRO uses **one** layout `rgb-keyboard.xml` because all three
device modes (USB / 2.4 GHz / XS75T) share the same physical key
matrix. Multi-layout devices would have multiple XML files.

The layout XML defines:

```xml
<KeyItems>
  <key code="0x29" name="Esc" desc="Esc"
       key_index="1" light_index="1"
       row_col="0#0"
       rect_left="24" rect_top="40" rect_width="34" rect_height="33"
       fnlayer_disable="0"/>
  …
</KeyItems>
```

- **97 `<key>` entries**, light_indexes 1..123 (sparse)
- col_count = 19, row_count = 6 (78-key + numpad + arrow-cluster
  variant)
- `key_index` ≠ `light_index` in some rows (e.g. row 1 key 14 has
  key_index 103 / light_index 103 — they happen to match, but the
  fields are independently addressable)

**For us**: we already consume `rgb-keyboard.xml` via the layout loader.
Confirm that `light_index` is the **byte offset** into the per-key RGB
buffer (1-byte/key wired or 4-byte/key wireless — see
`ak980pro_perkey_rgb_protocol.md`).

______________________________________________________________________

## 7. `skins/theme1/` — UI sprite assets (~330 KB total)

Total **57 PNG files + 1 GIF (loading spinner) + 1 ICO + 3 subfolders
(icon/, keyboard/, logo/)**.

Notable sprites:

| File                                | Purpose                                |
| ----------------------------------- | -------------------------------------- |
| `DeviceDriver.ico`                  | Application icon (67 KB; 32x32, 48x48, 256x256) |
| `img_circlepalette.png` (17 KB)     | HSV circle backdrop for `CirclePalette` |
| `img_menubkg.png`                   | Sidebar menu background                |
| `loading.gif`                       | Loading spinner GIF                    |
| `imgbtn_lcd_add/copy/del/edit/erase/revoke.png` | LCD editor toolbar icons |
| `imgbtn_lcd_addtext.png`            | Add-text tool icon                     |
| `img_lcdedit_bkg.png`               | LCD canvas backdrop                    |
| `btn_record_up/down/del/time.png`   | Macro-recorder buttons                 |
| `main_sysbtn_close/min/store/website.png` | Window-frame title-bar buttons    |
| `img_btn_layer_n.png` / `img_btn_layer_p.png` | Layer-switch buttons         |
| `imageview_item_normal/hover.png`   | List-item backgrounds                  |
| `lcdlist_item_normal/hover.png`     | LCD-list item backgrounds              |
| `img_combobox.png`                  | Combobox dropdown arrow                |
| `img_direction_top/bottom/left/right.png` | Direction-picker arrows         |
| `img_scroll_v.png`, `img_slider_back.png`, `img_thumb.png` | Scrollbar / slider sprites |
| `img_tip_bk.png`                    | Tooltip background                     |

### 7.1 `skins/theme1/icon/` (65 files)

Per-feature sidebar icons: `icon_home.png`, `icon_config.png`,
`icon_customlayout.png`, `icon_customlight.png`, `icon_input_delay.png`,
`icon_input_down.png`, etc. Plus connectivity icons (`24g_mode.png`,
`bt_mode.png`, `icon_bluetooth.png`), battery state
(`icon_battery_bkg.png`, `icon_batteryvalue.png`, `icon_charge.png`),
brightness controls (`icon_brightness_up.png`,
`icon_brightness_down.png`), colors (`icon_colors.png`,
`icon_circle.png`), data file icons (`icon_file.png`, `icon_email.png`,
`icon_favorites.png`), cal/calc (`icon_cal.png`).

### 7.2 `skins/theme1/keyboard/`, `skins/theme1/logo/`

(Not enumerated yet — likely the keyboard hero illustrations sliced
into per-row sprites, plus the AJAZZ corporate logo.)

**For us**: all sprite assets are vendor-branded. We bundle our own UI
art under `resources/icons/` and `resources/keyboards/<codename>/`.
We **never** copy or ship vendor sprites — copyright and brand
ambiguity. Material Icons + our own SVGs replace this entire tree.

______________________________________________________________________

## 8. Summary cross-reference: code path → asset

| Code path                                 | Asset(s) it loads                                            |
| ----------------------------------------- | ------------------------------------------------------------ |
| `FUN_004231c0` / `FUN_00422920` (TFT upload) | `gif/<deviceLongName>/<n>.gif` (frames split + RGB565-encoded) |
| `FUN_004358c0` (battery query → `BatteryCtrl::SetBatteryInfo`) | `skins/theme1/icon/icon_battery_bkg.png`, `icon_batteryvalue.png`, `icon_charge.png` |
| Home tab (connected/disconnected toggle)  | `device/home_keyboard_connected.png`, `device/home_keyboard_disconnected.png` |
| Window-frame (close/min/store/website)    | `skins/theme1/main_sysbtn_*.png`                             |
| LCD editor toolbar                        | `skins/theme1/imgbtn_lcd_*.png`                              |
| Macro recorder toolbar                    | `skins/theme1/btn_record_*.png`                              |
| Profile-list / device-list                | `skins/theme1/imageview_item_*.png`, `lcdlist_item_*.png`    |
| `CGifPicture` (animated previews)         | `skins/theme1/loading.gif` + user-uploaded GIFs              |
| All UI text                               | `language/{1033,2052,1028,1049}.lan`                         |
| Keyboard hero sprite                      | `skins/theme1/keyboard/*` (suspected slices)                 |
| Brand mark                                | `skins/theme1/logo/*` (suspected)                            |
| Application icon                          | `skins/theme1/DeviceDriver.ico`                              |

______________________________________________________________________

## 9. Code corrections required

None for our repo — there's no asset-loading code in our tree that
depends on vendor assets, and we explicitly target Qt 6 / QML / our own
QRC-embedded sprites.

**Future asset budget** (recommendation for v1.2 keyboard MVP):

1. **`resources/keyboards/ak980pro/`**: hero illustration (commission
   a fresh SVG/PNG, do NOT trace the vendor PNG), layout XML (already
   importable from `rgb-keyboard.xml` — but we own it), default LCD
   GIF (commission a fresh one).
2. **`resources/icons/keyboard/`**: feature icons for the sidebar
   (Material Symbols are fine; we don't need vendor icons).
3. **No bundled fonts.** Qt's font fallback handles everything.
4. **No bundled GIFs.** `QMovie` handles the user's own GIFs.

Estimated bundle size for our equivalent: **<150 KB** vs the vendor's
**~1.4 MB** of assets (driven by the four Open Sans variants + the
massive home-page hero PNGs).
