# Supported Devices

<!--
  All tables on this page are regenerated from `docs/_data/devices.yaml`.
  Do NOT edit AUTOGEN blocks by hand — run `make docs` at the repo root
  (pre-commit and CI also do this automatically).
-->

<!-- BEGIN AUTOGEN: stats -->
**13 devices** across 3 keyboard, 6 mouse, 4 streamdeck — 6 functional, 7 scaffolded.
<!-- END AUTOGEN: stats -->

Support levels:

<!-- BEGIN AUTOGEN: legend -->
🟢 **stable** — every vendor feature works · 🟢 **functional** — core I/O works (keys/LEDs) · 🟡 **scaffolded** — enumerated, protocol being mapped · 🟠 **requested** — captures wanted
<!-- END AUTOGEN: legend -->

Because protocols are reverse-engineered, support levels can change with
each release. See [CHANGELOG.md](https://github.com/Aiacos/ajazz-control-center/blob/main/CHANGELOG.md).

## Device matrix

<!-- BEGIN AUTOGEN: devices-table -->
| Family | Device | VID:PID | Keys | Encoders | Status | Capabilities |
|--------|--------|---------|:----:|:--------:|--------|--------------|
| streamdeck | [AJAZZ AKP153 / Mirabox HSV293S](docs/protocols/streamdeck/akp153.md) | `0x0300:0x1001` | 15 | 0 | 🟢 functional | display, rgb, macros, firmware |
| streamdeck | [AJAZZ AKP153E (China variant)](docs/protocols/streamdeck/akp153.md) | `0x0300:0x1002` | 15 | 0 | 🟢 functional | display, rgb, macros, firmware |
| streamdeck | [AJAZZ AKP03 / Mirabox N3](docs/protocols/streamdeck/akp03.md) | `0x0300:0x3001` | 6 | 1 | 🟢 functional | display, encoder, macros |
| streamdeck | [AJAZZ AKP05 / AKP05E (knobs)](docs/protocols/streamdeck/akp05.md) | `0x0300:0x5001` | 15 | 4 | 🟢 functional | display, encoder, touch, macros |
| keyboard | [AJAZZ AK series (QMK/VIA-compatible)](docs/protocols/keyboard/via.md) | `0x3151:various` | — | — | 🟢 functional | rgb, macros, layers, firmware |
| keyboard | [AJAZZ AK series (proprietary)](docs/protocols/keyboard/proprietary.md) | `0x3151:various` | — | — | 🟢 functional | rgb, macros, layers, firmware |
| keyboard | [AJAZZ AK980 PRO](docs/protocols/keyboard/proprietary.md) | `0x0c45:0x8009` | — | — | 🟡 scaffolded | rgb, macros, layers |
| mouse | [AJAZZ AJ-series wired (primary)](docs/protocols/mouse/aj_series.md) | `0x248A:0x5C2E` | — | — | 🟡 scaffolded | rgb, dpi, firmware |
| mouse | [AJAZZ AJ-series wired (alt mode 5D2E)](docs/protocols/mouse/aj_series.md) | `0x248A:0x5D2E` | — | — | 🟡 scaffolded | rgb, dpi, firmware |
| mouse | [AJAZZ AJ-series wired (alt mode 5E2E)](docs/protocols/mouse/aj_series.md) | `0x248A:0x5E2E` | — | — | 🟡 scaffolded | rgb, dpi, firmware |
| mouse | [AJAZZ AJ-series 2.4GHz dongle](docs/protocols/mouse/aj_series.md) | `0x248A:0x5C2F` | — | — | 🟡 scaffolded | rgb, dpi, firmware |
| mouse | [AJAZZ AJ199 family (wired)](docs/protocols/mouse/aj_series.md) | `0x3554:0xF500` | — | — | 🟡 scaffolded | rgb, dpi, firmware |
| mouse | [AJAZZ AJ199 family (2.4GHz dongle)](docs/protocols/mouse/aj_series.md) | `0x3554:0xF501` | — | — | 🟡 scaffolded | rgb, dpi, firmware |
<!-- END AUTOGEN: devices-table -->

## By family

<!-- BEGIN AUTOGEN: devices-by-family -->
### Stream decks

| Device | USB | Status | Features | Notes |
|--------|-----|--------|----------|-------|
| [AJAZZ AKP153 / Mirabox HSV293S](docs/protocols/streamdeck/akp153.md) | `0x0300:0x1001` | 🟢 functional | Per-key display, RGB backlight, Macros, Firmware version | Reference implementation. JPEG 85×85 per-key images. |
| [AJAZZ AKP153E (China variant)](docs/protocols/streamdeck/akp153.md) | `0x0300:0x1002` | 🟢 functional | Per-key display, RGB backlight, Macros, Firmware version | Same protocol family as AKP153. |
| [AJAZZ AKP03 / Mirabox N3](docs/protocols/streamdeck/akp03.md) | `0x0300:0x3001` | 🟢 functional | Per-key display, Encoder / dial, Macros | 6 keys + knob; PNG 72×72 image codec. Image/encoder I/O wired. |
| [AJAZZ AKP05 / AKP05E (knobs)](docs/protocols/streamdeck/akp05.md) | `0x0300:0x5001` | 🟢 functional | Per-key display, Encoder / dial, Touch strip, Macros | Stream Dock Plus class; key/encoder/touch events and per-encoder screen I/O implemented. |

### Keyboards

| Device | USB | Status | Features | Notes |
|--------|-----|--------|----------|-------|
| [AJAZZ AK series (QMK/VIA-compatible)](docs/protocols/keyboard/via.md) | `0x3151:various` | 🟢 functional | RGB backlight, Macros, Layers, Firmware version | Any VIA JSON layout is supported. |
| [AJAZZ AK series (proprietary)](docs/protocols/keyboard/proprietary.md) | `0x3151:various` | 🟢 functional | RGB backlight, Macros, Layers, Firmware version | Clean-room backend; RGB zones, keymap and macro upload wired. |
| [AJAZZ AK980 PRO](docs/protocols/keyboard/proprietary.md) | `0x0c45:0x8009` | 🟡 scaffolded | RGB backlight, Macros, Layers | Microdia-chipset wireless mech; enumerated, routed through the proprietary backend. Protocol mapping in progress — captures welcome (#issue). |

### Mice

| Device | USB | Status | Features | Notes |
|--------|-----|--------|----------|-------|
| [AJAZZ AJ-series wired (primary)](docs/protocols/mouse/aj_series.md) | `0x248A:0x5C2E` | 🟡 scaffolded | RGB backlight, DPI stages, Firmware version | Covers AJ139 PRO / AJ159 / AJ159 MC / AJ159P MC / AJ179 / AJ139 V2 PRO / AJ179 V2 / AJ179 V2 MAX in their canonical USB-wired mode. PAW3395 / PAW3311 / PAW3370 / PAW3335 / PAW3950 sensor depending on SKU. HID configuration interface is `MI_02`. Wire-format reconciliation against the vendor driver still pending — see TODO `AJ-series wire format reconciliation` and `vendor-protocol-notes.md` Finding 11. |
| [AJAZZ AJ-series wired (alt mode 5D2E)](docs/protocols/mouse/aj_series.md) | `0x248A:0x5D2E` | 🟡 scaffolded | RGB backlight, DPI stages, Firmware version | Same SKUs as `aj_series_wired_primary`; the vendor's `config.xml` lists three USB PIDs per device (5C2E / 5D2E / 5E2E) representing different USB-side mode descriptors. |
| [AJAZZ AJ-series wired (alt mode 5E2E)](docs/protocols/mouse/aj_series.md) | `0x248A:0x5E2E` | 🟡 scaffolded | RGB backlight, DPI stages, Firmware version | Same SKUs as `aj_series_wired_primary`; third USB-mode descriptor. |
| [AJAZZ AJ-series 2.4GHz dongle](docs/protocols/mouse/aj_series.md) | `0x248A:0x5C2F` | 🟡 scaffolded | RGB backlight, DPI stages, Firmware version | Same SKUs in 2.4GHz wireless mode through the bundled USB dongle. Vendor also exposes the same dongle under VID `0x249A` PID `0x5C2F` — reason unclear (likely USB stack path difference). For udev coverage, include both `idVendor=="248a"` and `idVendor=="249a"`. |
| [AJAZZ AJ199 family (wired)](docs/protocols/mouse/aj_series.md) | `0x3554:0xF500` | 🟡 scaffolded | RGB backlight, DPI stages, Firmware version | AJ199 / AJ199 Max / AJ199 Carbon Fiber. Wired-mode primary PID per AJ199 Max `Config.ini` `M_PID` (`F500`); the same family also enumerates under PIDs `F546` and `F566` for variant SKUs. AJ199 Max wire format is structurally different from AJ199 V1.0 (offset-based struct vs flat report) — see `vendor-protocol-notes.md` Finding 11.B. |
| [AJAZZ AJ199 family (2.4GHz dongle)](docs/protocols/mouse/aj_series.md) | `0x3554:0xF501` | 🟡 scaffolded | RGB backlight, DPI stages, Firmware version | AJ199 family in 2.4GHz mode. Vendor `D_PID` list is `F501,F564,F567,F545,F547,F5D5` — six dongle-mode PIDs distinguishing variant SKUs at the USB layer. We register the first one as canonical here; future work may broaden coverage with a runtime probe. |

<!-- END AUTOGEN: devices-by-family -->

References:

- [AKP153 protocol notes](https://github.com/Aiacos/ajazz-control-center/blob/main/docs/protocols/streamdeck/akp153.md)
- [AJ-series protocol notes](https://github.com/Aiacos/ajazz-control-center/blob/main/docs/protocols/mouse/aj_series.md)

VIA keyboards are the easiest to support — just drop the `via.json`
layout into `~/.config/ajazz-control-center/via/` and the keyboard is
fully configurable.

## Adding your device

If your AJAZZ device is not listed:

1. Capture USB traffic with Wireshark + `usbmon` (Linux) or USBPcap
   (Windows) while the vendor app changes every setting.
1. Save the `.pcapng` and a short README of what you changed.
1. Open a
   [Device Request](https://github.com/Aiacos/ajazz-control-center/issues/new?template=device_request.yml).

See [Reverse Engineering](Reverse-Engineering) for the methodology.
