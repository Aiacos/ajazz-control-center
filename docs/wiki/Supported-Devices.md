# Supported Devices

<!--
  All tables on this page are regenerated from `docs/_data/devices.yaml`.
  Do NOT edit AUTOGEN blocks by hand — run `make docs` at the repo root
  (pre-commit and CI also do this automatically).
-->

<!-- BEGIN AUTOGEN: stats -->
**10 devices** across 2 keyboard, 4 mouse, 4 streamdeck — 6 functional, 4 scaffolded.
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
| streamdeck | [AJAZZ AKP03 / Mirabox N3](docs/protocols/streamdeck/akp03.md) | `0x0300:0x3001` | 6 | 1 | 🟡 scaffolded | display, rgb, encoder, macros |
| streamdeck | [AJAZZ AKP05 / AKP05E (knobs)](docs/protocols/streamdeck/akp05.md) | `0x0300:0x5001` | 15 | 4 | 🟡 scaffolded | display, rgb, encoder, touch, macros |
| keyboard | [AJAZZ AK series (QMK/VIA-compatible)](docs/protocols/keyboard/via.md) | `0x3151:various` | — | — | 🟢 functional | rgb, macros, layers, firmware |
| keyboard | [AJAZZ AK series (proprietary)](docs/protocols/keyboard/proprietary.md) | `0x3151:various` | — | — | 🟡 scaffolded | rgb, macros, layers |
| mouse | [AJAZZ AJ159](docs/protocols/mouse/aj_series.md) | `0x3554:0xf51a` | — | — | 🟢 functional | rgb, dpi, firmware |
| mouse | [AJAZZ AJ199](docs/protocols/mouse/aj_series.md) | `0x3554:0xf51b` | — | — | 🟢 functional | rgb, dpi, firmware |
| mouse | [AJAZZ AJ339](docs/protocols/mouse/aj_series.md) | `0x3554:0xf51c` | — | — | 🟢 functional | rgb, dpi, firmware |
| mouse | [AJAZZ AJ380](docs/protocols/mouse/aj_series.md) | `0x3554:0xf51d` | — | — | 🟡 scaffolded | rgb, dpi, firmware |
<!-- END AUTOGEN: devices-table -->

## By family

<!-- BEGIN AUTOGEN: devices-by-family -->
### Stream decks

| Device | USB | Status | Features | Notes |
|--------|-----|--------|----------|-------|
| [AJAZZ AKP153 / Mirabox HSV293S](docs/protocols/streamdeck/akp153.md) | `0x0300:0x1001` | 🟢 functional | Per-key display, RGB backlight, Macros, Firmware version | Reference implementation. JPEG 85×85 per-key images. |
| [AJAZZ AKP153E (China variant)](docs/protocols/streamdeck/akp153.md) | `0x0300:0x1002` | 🟢 functional | Per-key display, RGB backlight, Macros, Firmware version | Same protocol family as AKP153. |
| [AJAZZ AKP03 / Mirabox N3](docs/protocols/streamdeck/akp03.md) | `0x0300:0x3001` | 🟡 scaffolded | Per-key display, RGB backlight, Encoder / dial, Macros | 6-key + knob variant. PNG 72×72 image codec. |
| [AJAZZ AKP05 / AKP05E (knobs)](docs/protocols/streamdeck/akp05.md) | `0x0300:0x5001` | 🟡 scaffolded | Per-key display, RGB backlight, Encoder / dial, Touch strip, Macros | Stream Dock Plus class: encoders + touch strip + main LCD. |

### Keyboards

| Device | USB | Status | Features | Notes |
|--------|-----|--------|----------|-------|
| [AJAZZ AK series (QMK/VIA-compatible)](docs/protocols/keyboard/via.md) | `0x3151:various` | 🟢 functional | RGB backlight, Macros, Layers, Firmware version | Any VIA JSON layout is supported. |
| [AJAZZ AK series (proprietary)](docs/protocols/keyboard/proprietary.md) | `0x3151:various` | 🟡 scaffolded | RGB backlight, Macros, Layers | Closed firmware; reverse-engineering in progress. |

### Mice

| Device | USB | Status | Features | Notes |
|--------|-----|--------|----------|-------|
| [AJAZZ AJ159](docs/protocols/mouse/aj_series.md) | `0x3554:0xf51a` | 🟢 functional | RGB backlight, DPI stages, Firmware version | PAW3395 sensor. |
| [AJAZZ AJ199](docs/protocols/mouse/aj_series.md) | `0x3554:0xf51b` | 🟢 functional | RGB backlight, DPI stages, Firmware version | PAW3395 sensor. |
| [AJAZZ AJ339](docs/protocols/mouse/aj_series.md) | `0x3554:0xf51c` | 🟢 functional | RGB backlight, DPI stages, Firmware version | PAW3395 sensor, wireless variant pending. |
| [AJAZZ AJ380](docs/protocols/mouse/aj_series.md) | `0x3554:0xf51d` | 🟡 scaffolded | RGB backlight, DPI stages, Firmware version | PAW3950 sensor. |

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
