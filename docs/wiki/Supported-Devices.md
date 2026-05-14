# Supported Devices

<!--
  All tables on this page are regenerated from `docs/_data/devices.yaml`.
  Do NOT edit AUTOGEN blocks by hand — run `make docs` at the repo root
  (pre-commit and CI also do this automatically).
-->

<!-- BEGIN AUTOGEN: stats -->
**27 devices** across 3 keyboard, 7 mouse, 17 streamdeck — 10 functional, 17 scaffolded.
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
| streamdeck | [AJAZZ AKP153 / Mirabox HSV293S](docs/protocols/streamdeck/akp153.md) | `0x0300:0x1001` | 15 | 0 | 🟢 functional | display, rgb, macros, firmware, clock |
| streamdeck | [AJAZZ AKP153 (Mirabox V1 firmware)](docs/protocols/streamdeck/akp153.md) | `0x5548:0x6674` | 15 | 0 | 🟢 functional | display, rgb, macros, firmware, clock |
| streamdeck | [AJAZZ AKP153E (China variant)](docs/protocols/streamdeck/akp153.md) | `0x0300:0x1002` | 15 | 0 | 🟢 functional | display, rgb, macros, firmware, clock |
| streamdeck | [AJAZZ AKP153E (Mirabox V2 firmware)](docs/protocols/streamdeck/akp153.md) | `0x0300:0x1010` | 15 | 0 | 🟢 functional | display, rgb, macros, firmware, clock |
| streamdeck | [AJAZZ AKP153R](docs/protocols/streamdeck/akp153.md) | `0x0300:0x1020` | 15 | 0 | 🟡 scaffolded | display, rgb, macros, firmware, clock |
| streamdeck | [AJAZZ AKP815](docs/protocols/streamdeck/akp815.md) | `0x5548:0x6672` | 15 | 0 | 🟡 scaffolded | display, macros, firmware, clock |
| streamdeck | [AJAZZ AKP03 / Mirabox N3](docs/protocols/streamdeck/akp03.md) | `0x0300:0x1001` | 6 | 3 | 🟢 functional | display, encoder, macros, clock |
| streamdeck | [AJAZZ AKP03 (legacy 0x3001 firmware)](docs/protocols/streamdeck/akp03.md) | `0x0300:0x3001` | 6 | 3 | 🟢 functional | display, encoder, macros, clock |
| streamdeck | [AJAZZ AKP03E](docs/protocols/streamdeck/akp03.md) | `0x0300:0x3002` | 6 | 3 | 🟢 functional | display, encoder, macros, clock |
| streamdeck | [AJAZZ AKP03R](docs/protocols/streamdeck/akp03.md) | `0x0300:0x1003` | 6 | 3 | 🟢 functional | display, encoder, macros, clock |
| streamdeck | [AJAZZ AKP03R rev. 2](docs/protocols/streamdeck/akp03.md) | `0x0300:0x3003` | 6 | 3 | 🟡 scaffolded | display, encoder, macros, clock |
| streamdeck | [Mirabox N3 (rev. 1)](docs/protocols/streamdeck/akp03.md) | `0x6602:0x1002` | 6 | 3 | 🟡 scaffolded | display, encoder, macros, clock |
| streamdeck | [Mirabox N3 (rev. 3)](docs/protocols/streamdeck/akp03.md) | `0x6603:0x1002` | 6 | 3 | 🟡 scaffolded | display, encoder, macros, clock |
| streamdeck | [Mirabox N3EN](docs/protocols/streamdeck/akp03.md) | `0x6603:0x1003` | 6 | 3 | 🟡 scaffolded | display, encoder, macros, clock |
| streamdeck | [AJAZZ AKP05 / AKP05E (provisional)](docs/protocols/streamdeck/akp05.md) | `0x0300:0x5001` | 10 | 4 | 🟡 scaffolded | display, encoder, touch, macros, clock |
| streamdeck | [Mirabox N4 / AJAZZ AKP05 family](docs/protocols/streamdeck/akp05.md) | `0x6603:0x1007` | 10 | 4 | 🟡 scaffolded | display, encoder, touch, macros, clock |
| streamdeck | [AJAZZ Stream Dock (PID 0x3004)](docs/protocols/streamdeck/akp03.md) | `0x0300:0x3004` | 6 | 3 | 🟡 scaffolded | display, encoder, clock |
| keyboard | [AJAZZ AK series (QMK/VIA-compatible)](docs/protocols/keyboard/via.md) | `0x3151:various` | — | — | 🟢 functional | rgb, macros, layers, firmware |
| keyboard | [AJAZZ AK series (proprietary)](docs/protocols/keyboard/proprietary.md) | `0x3151:various` | — | — | 🟢 functional | rgb, macros, layers, firmware |
| keyboard | [AJAZZ AK980 PRO](docs/protocols/keyboard/proprietary.md) | `0x0c45:0x8009` | — | — | 🟡 scaffolded | rgb, macros, layers, clock |
| mouse | [AJAZZ AJ-series wired (primary)](docs/protocols/mouse/aj_series.md) | `0x248A:0x5C2E` | — | — | 🟡 scaffolded | rgb, dpi, firmware |
| mouse | [AJAZZ AJ-series wired (alt mode 5D2E)](docs/protocols/mouse/aj_series.md) | `0x248A:0x5D2E` | — | — | 🟡 scaffolded | rgb, dpi, firmware |
| mouse | [AJAZZ AJ-series wired (alt mode 5E2E)](docs/protocols/mouse/aj_series.md) | `0x248A:0x5E2E` | — | — | 🟡 scaffolded | rgb, dpi, firmware |
| mouse | [AJAZZ AJ-series 2.4GHz dongle](docs/protocols/mouse/aj_series.md) | `0x248A:0x5C2F` | — | — | 🟡 scaffolded | rgb, dpi, firmware |
| mouse | [AJAZZ 2.4G 8K](docs/research/vendor-protocol-notes.md) | `0x3151:0x5007` | — | — | 🟡 scaffolded | dpi, rgb |
| mouse | [AJAZZ AJ199 family (wired)](docs/protocols/mouse/aj_series.md) | `0x3554:0xF500` | — | — | 🟡 scaffolded | rgb, dpi, firmware |
| mouse | [AJAZZ AJ199 family (2.4GHz dongle)](docs/protocols/mouse/aj_series.md) | `0x3554:0xF501` | — | — | 🟡 scaffolded | rgb, dpi, firmware |
<!-- END AUTOGEN: devices-table -->

## By family

<!-- BEGIN AUTOGEN: devices-by-family -->
### Stream decks

| Device | USB | Status | Features | Notes |
|--------|-----|--------|----------|-------|
| [AJAZZ AKP153 / Mirabox HSV293S](docs/protocols/streamdeck/akp153.md) | `0x0300:0x1001` | 🟢 functional | Per-key display, RGB backlight, Macros, Firmware version, Host-settable clock (scaffolded) | Reference 3x5 grid; 85x85 JPEG keys (Rot90+mirror). Legacy VID:PID kept for compatibility; the canonical Mirabox V1 pair is 0x5548:0x6674 (also registered). |
| [AJAZZ AKP153 (Mirabox V1 firmware)](docs/protocols/streamdeck/akp153.md) | `0x5548:0x6674` | 🟢 functional | Per-key display, RGB backlight, Macros, Firmware version, Host-settable clock (scaffolded) | Canonical USB pair per ajazz-sdk for the AKP153 with Mirabox V1 firmware. |
| [AJAZZ AKP153E (China variant)](docs/protocols/streamdeck/akp153.md) | `0x0300:0x1002` | 🟢 functional | Per-key display, RGB backlight, Macros, Firmware version, Host-settable clock (scaffolded) | Legacy VID:PID; canonical AKP153E PID per ajazz-sdk is 0x1010 (also registered as `akp153e_v2`). |
| [AJAZZ AKP153E (Mirabox V2 firmware)](docs/protocols/streamdeck/akp153.md) | `0x0300:0x1010` | 🟢 functional | Per-key display, RGB backlight, Macros, Firmware version, Host-settable clock (scaffolded) | Canonical AKP153E PID per ajazz-sdk; same protocol as AKP153. |
| [AJAZZ AKP153R](docs/protocols/streamdeck/akp153.md) | `0x0300:0x1020` | 🟡 scaffolded | Per-key display, RGB backlight, Macros, Firmware version, Host-settable clock (scaffolded) | Regional revision per ajazz-sdk. Protocol identical to AKP153; capture pending to confirm firmware quirks. |
| [AJAZZ AKP815](docs/protocols/streamdeck/akp815.md) | `0x5548:0x6672` | 🟡 scaffolded | Per-key display, Macros, Firmware version, Host-settable clock (scaffolded) | 15-key 5x3 grid; 100x100 JPEG keys (Rot180) plus an 800x480 LCD strip. Backend reuses the AKP153 v1-API state machine with a different DisplayInfo. Per-revision image pipeline tracked in TODO.md. |
| [AJAZZ AKP03 / Mirabox N3](docs/protocols/streamdeck/akp03.md) | `0x0300:0x1001` | 🟢 functional | Per-key display, Encoder / dial, Macros, Host-settable clock (scaffolded) | 6 LCD keys (2x3) + 3 pressable encoders + 3 non-LCD side buttons. JPEG 60x60 (Rot0) keys. Canonical PID per ajazz-sdk; legacy 0x3001 kept registered for compatibility. |
| [AJAZZ AKP03 (legacy 0x3001 firmware)](docs/protocols/streamdeck/akp03.md) | `0x0300:0x3001` | 🟢 functional | Per-key display, Encoder / dial, Macros, Host-settable clock (scaffolded) | Pre-2026-05-14 placeholder PID. Same backend as `akp03`; retained until removed in a future cleanup. |
| [AJAZZ AKP03E](docs/protocols/streamdeck/akp03.md) | `0x0300:0x3002` | 🟢 functional | Per-key display, Encoder / dial, Macros, Host-settable clock (scaffolded) | AKP03 with Mirabox V2 firmware (1024-byte packets per mirajazz). Same wire-format family. |
| [AJAZZ AKP03R](docs/protocols/streamdeck/akp03.md) | `0x0300:0x1003` | 🟢 functional | Per-key display, Encoder / dial, Macros, Host-settable clock (scaffolded) | Regional revision; same protocol as AKP03 per ajazz-sdk. |
| [AJAZZ AKP03R rev. 2](docs/protocols/streamdeck/akp03.md) | `0x0300:0x3003` | 🟡 scaffolded | Per-key display, Encoder / dial, Macros, Host-settable clock (scaffolded) | Per mirajazz this is a protocol_version 3 device (full press/release states + GIF support). Per-key images are 64x64 (Rot90) instead of 60x60 (Rot0). |
| [Mirabox N3 (rev. 1)](docs/protocols/streamdeck/akp03.md) | `0x6602:0x1002` | 🟡 scaffolded | Per-key display, Encoder / dial, Macros, Host-settable clock (scaffolded) | Mirabox-branded sibling of AJAZZ AKP03 (opendeck-akp03 catalogue). Same backend. |
| [Mirabox N3 (rev. 3)](docs/protocols/streamdeck/akp03.md) | `0x6603:0x1002` | 🟡 scaffolded | Per-key display, Encoder / dial, Macros, Host-settable clock (scaffolded) | Newer Mirabox N3 hardware revision; same backend. |
| [Mirabox N3EN](docs/protocols/streamdeck/akp03.md) | `0x6603:0x1003` | 🟡 scaffolded | Per-key display, Encoder / dial, Macros, Host-settable clock (scaffolded) | Mirabox SKU variant per opendeck-akp03 udev rules. |
| [AJAZZ AKP05 / AKP05E (provisional)](docs/protocols/streamdeck/akp05.md) | `0x0300:0x5001` | 🟡 scaffolded | Per-key display, Encoder / dial, Touch strip, Macros, Host-settable clock (scaffolded) | Stream Dock Plus-class: 10 LCD keys (2x5) + 4 endless encoders + LCD touchscreen strip (4 zones). VID:PID is a pre-2026-05-14 placeholder; canonical is `mirabox_n4`. Layout corrected from 15->10 keys after the 2026-05-14 research pass. |
| [Mirabox N4 / AJAZZ AKP05 family](docs/protocols/streamdeck/akp05.md) | `0x6603:0x1007` | 🟡 scaffolded | Per-key display, Encoder / dial, Touch strip, Macros, Host-settable clock (scaffolded) | Canonical USB ID from opendeck-akp05. 2x5 LCD keys, 4 encoders with touchscreen-strip overlays (110x14mm physical, 800x480 panel), built-in USB-2 hub (2xUSB-A + 2xUSB-C). Per mirajazz this is a protocol_version 3 device. |
| [AJAZZ Stream Dock (PID 0x3004)](docs/protocols/streamdeck/akp03.md) | `0x0300:0x3004` | 🟡 scaffolded | Per-key display, Encoder / dial, Host-settable clock (scaffolded) | Hot-plug capture 2026-05-13 surfaced this PID as 'Ajazz HOTSPOTEKUSB HID DEMO'. Routed through the AKP03 factory; exact retail SKU to confirm against vendor manifests. |

### Keyboards

| Device | USB | Status | Features | Notes |
|--------|-----|--------|----------|-------|
| [AJAZZ AK series (QMK/VIA-compatible)](docs/protocols/keyboard/via.md) | `0x3151:various` | 🟢 functional | RGB backlight, Macros, Layers, Firmware version | Any VIA JSON layout is supported. |
| [AJAZZ AK series (proprietary)](docs/protocols/keyboard/proprietary.md) | `0x3151:various` | 🟢 functional | RGB backlight, Macros, Layers, Firmware version | Clean-room backend; RGB zones, keymap and macro upload wired. |
| [AJAZZ AK980 PRO](docs/protocols/keyboard/proprietary.md) | `0x0c45:0x8009` | 🟡 scaffolded | RGB backlight, Macros, Layers, Host-settable clock (scaffolded) | Microdia-chipset wireless mech; enumerated, routed through the proprietary backend. Protocol mapping in progress — captures welcome (#issue). Time-sync scaffolded (Phase 5): TimeSyncService::setSystemTimeOn returns NotImplemented honestly until vendor wire format is captured. |

### Mice

| Device | USB | Status | Features | Notes |
|--------|-----|--------|----------|-------|
| [AJAZZ AJ-series wired (primary)](docs/protocols/mouse/aj_series.md) | `0x248A:0x5C2E` | 🟡 scaffolded | RGB backlight, DPI stages, Firmware version | Covers AJ139 PRO / AJ159 / AJ159 MC / AJ159P MC / AJ179 / AJ139 V2 PRO / AJ179 V2 / AJ179 V2 MAX in their canonical USB-wired mode. PAW3395 / PAW3311 / PAW3370 / PAW3335 / PAW3950 sensor depending on SKU. HID configuration interface is `MI_02`. Wire-format reconciliation against the vendor driver still pending — see TODO `AJ-series wire format reconciliation` and `vendor-protocol-notes.md` Finding 11. |
| [AJAZZ AJ-series wired (alt mode 5D2E)](docs/protocols/mouse/aj_series.md) | `0x248A:0x5D2E` | 🟡 scaffolded | RGB backlight, DPI stages, Firmware version | Same SKUs as `aj_series_wired_primary`; the vendor's `config.xml` lists three USB PIDs per device (5C2E / 5D2E / 5E2E) representing different USB-side mode descriptors. |
| [AJAZZ AJ-series wired (alt mode 5E2E)](docs/protocols/mouse/aj_series.md) | `0x248A:0x5E2E` | 🟡 scaffolded | RGB backlight, DPI stages, Firmware version | Same SKUs as `aj_series_wired_primary`; third USB-mode descriptor. |
| [AJAZZ AJ-series 2.4GHz dongle](docs/protocols/mouse/aj_series.md) | `0x248A:0x5C2F` | 🟡 scaffolded | RGB backlight, DPI stages, Firmware version | Same SKUs in 2.4GHz wireless mode through the bundled USB dongle. Vendor also exposes the same dongle under VID `0x249A` PID `0x5C2F` — reason unclear (likely USB stack path difference). For udev coverage, include both `idVendor=="248a"` and `idVendor=="249a"`. |
| [AJAZZ 2.4G 8K](docs/research/vendor-protocol-notes.md) | `0x3151:0x5007` | 🟡 scaffolded | DPI stages, RGB backlight | 8KHz-polling wireless mouse on SONiX VID prefix. Surfaced via real-device hot-plug capture 2026-05-13. Wire format reuses AJ-series backend pending reconciliation. |
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
