# Device Layouts and Photo Attribution

Per-SKU layout JSON files for the OpenDeck-shaped device editor introduced
in Phase 26 (plans D-04 through D-07). Each JSON file declares the visual
geometry of one AJAZZ or Mirabox LCD-key SKU: a viewBox, key-cell positions,
optional encoder-dial positions, and optional touch-zone positions.

The layout JSON `photo` field references an existing vendor product image
stored in `resources/devices/products/`. Those images are vendor marketing
photographs used under fair use for this control-software UI (see
Fair-use Rationale below).

## Layout JSON Files

Each `<codename>.json` file follows this schema:

- `codename` -- machine codename matching the device descriptor row
- `photo` -- filename of the product photo (in `resources/devices/products/`)
- `viewBox` -- `{w, h}` coordinate space for cell positioning
- `keys[]` -- array of `{i, x, y, w, h}` for each LCD key (i is 1-indexed)
- `encoders[]` -- array of `{i, x, y, w, h}` for each encoder (empty when none)
- `touchZones[]` -- array of `{i, x, y, w, h}` for each touch zone (empty when none)

If a SKU has no layout JSON (e.g. AKP815, deferred per D-13), DeviceView
falls back to an outline-frame rendering using only the C++ DeviceDescriptor
geometry fields. No layout JSON is required for the editor to function.

## Vendor Photo Attribution

The product photos in `resources/devices/products/` are vendor marketing
imagery reproduced under fair use for control-software UI purposes.

| File                   | Subject      | Copyright Holder        | Source                      |
| ---------------------- | ------------ | ----------------------- | --------------------------- |
| product-akp05e.png     | AJAZZ AKP05E | AJAZZ / Shenzhen Anhao  | AJAZZ vendor product page   |
| product-akp05.png      | AJAZZ AKP05  | AJAZZ / Shenzhen Anhao  | AJAZZ vendor product page   |
| product-akp153.png     | AJAZZ AKP153 | AJAZZ / Shenzhen Anhao  | AJAZZ vendor product page   |
| product-akp03.png      | AJAZZ AKP03  | AJAZZ / Shenzhen Anhao  | AJAZZ vendor product page   |
| product-mirabox_n4.png | Mirabox N4   | Mirabox / ShenZhen Mira | Mirabox vendor product page |

The layout JSON files themselves (the `.json` files in this directory) are
original project work authored by AJAZZ Control Center contributors and are
licensed under GPL-3.0-or-later along with the rest of the project.

## Fair-use Rationale

Use of vendor product photographs in this project meets standard fair-use
criteria:

1. **Purpose and character of use.** The images appear inside a UI editor
   where the user configures the exact physical device pictured. This is a
   functional, transformative use -- the photos serve as a chassis overlay
   for drop-target cells, not as marketing or decorative content. The project
   is non-commercial and open-source.

1. **Nature of the copyrighted work.** The images are marketing photographs
   of commercially available hardware products. They are factual depictions
   of physical devices, not highly creative works.

1. **Amount used.** Thumbnail-scale product photos cropped to show the
   device face; no high-resolution press assets or studio source files.

1. **Effect on the market.** The use does not substitute for nor harm the
   market for the vendor's photos. Users who see the image in this software
   already own the device; the photo reinforces brand recognition.

## Replacement Policy

If a vendor objects to the use of their product photo:

1. Remove the `photo` field from the relevant layout JSON file.
1. DeviceView automatically falls back to the outline-frame renderer
   (D-07), which derives geometry purely from the C++ DeviceDescriptor.
1. No functional regression: the device editor continues to work with
   all drag-drop and binding features intact.

This replacement can be done per-SKU with no impact on other SKUs.
