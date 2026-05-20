# Supported Devices

<!--
  All tables on this page are regenerated from `docs/_data/devices.yaml`.
  Do NOT edit AUTOGEN blocks by hand — run `make docs` at the repo root
  (pre-commit and CI also do this automatically).
-->

<!-- BEGIN AUTOGEN: stats -->
**31 devices** across 1 dongle, 3 keyboard, 10 mouse, 17 streamdeck — 10 functional, 16 scaffolded, 2 probed, 3 partial.
<!-- END AUTOGEN: stats -->

Support levels:

<!-- BEGIN AUTOGEN: legend -->
🟡 **scaffolded** — descriptor + factory exist; backend compiles but does not exercise the device · 🔵 **probed** — device enumerates and descriptor populated; no protocol writes confirmed · 🟠 **partial** — some features work end-to-end; advertised capability set incomplete or untested · 🟢 **functional** — all advertised capabilities work in practice; tested manually or in CI · ✅ **verified** — functional + automated CI on real hardware OR sustained user-confirmed reliability
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
| streamdeck | [AJAZZ AKP815](docs/protocols/streamdeck/akp815.md) | `0x5548:0x6672` | 15 | 0 | 🔵 probed | display, macros, firmware, clock |
| streamdeck | [AJAZZ AKP03 / Mirabox N3](docs/protocols/streamdeck/akp03.md) | `0x0300:0x1001` | 6 | 3 | 🟢 functional | display, encoder, macros, clock |
| streamdeck | [AJAZZ AKP03 (legacy 0x3001 firmware)](docs/protocols/streamdeck/akp03.md) | `0x0300:0x3001` | 6 | 3 | 🟢 functional | display, encoder, macros, clock |
| streamdeck | [AJAZZ AKP03E](docs/protocols/streamdeck/akp03.md) | `0x0300:0x3002` | 6 | 3 | 🟢 functional | display, encoder, macros, clock |
| streamdeck | [AJAZZ AKP03R](docs/protocols/streamdeck/akp03.md) | `0x0300:0x1003` | 6 | 3 | 🟢 functional | display, encoder, macros, clock |
| streamdeck | [AJAZZ AKP03R rev. 2](docs/protocols/streamdeck/akp03.md) | `0x0300:0x3003` | 6 | 3 | 🟡 scaffolded | display, encoder, macros, clock |
| streamdeck | [Mirabox N3 (rev. 1)](docs/protocols/streamdeck/mirabox_n3.md) | `0x6602:0x1002` | 6 | 3 | 🟠 partial | display, encoder, macros, clock |
| streamdeck | [Mirabox N3 (rev. 3)](docs/protocols/streamdeck/akp03.md) | `0x6603:0x1002` | 6 | 3 | 🟡 scaffolded | display, encoder, macros, clock |
| streamdeck | [Mirabox N3EN](docs/protocols/streamdeck/akp03.md) | `0x6603:0x1003` | 6 | 3 | 🟡 scaffolded | display, encoder, macros, clock |
| streamdeck | [AJAZZ AKP05 / AKP05E (provisional)](docs/protocols/streamdeck/akp05.md) | `0x0300:0x5001` | 10 | 4 | 🟡 scaffolded | display, encoder, touch, macros, clock |
| streamdeck | [Mirabox N4 / AJAZZ AKP05 family](docs/protocols/streamdeck/akp05.md) | `0x6603:0x1007` | 10 | 4 | 🟡 scaffolded | display, encoder, touch, macros, clock |
| streamdeck | [AJAZZ AKP05E (Stream Dock Plus)](docs/protocols/streamdeck/akp05.md) | `0x0300:0x3004` | 10 | 4 | 🟡 scaffolded | display, encoder, touch, macros, clock |
| keyboard | [AJAZZ AK series (QMK/VIA-compatible)](docs/protocols/keyboard/via.md) | `0x3151:various` | — | — | 🟢 functional | rgb, macros, layers, firmware |
| keyboard | [AJAZZ AK series (proprietary)](docs/protocols/keyboard/proprietary.md) | `0x3151:various` | — | — | 🟢 functional | rgb, macros, layers, firmware |
| keyboard | [AJAZZ AK980 PRO](docs/protocols/keyboard/proprietary.md) | `0x0c45:0x8009` | — | — | 🟠 partial | rgb, macros, layers, clock, battery |
| mouse | [AJAZZ AJ-series wired (primary)](docs/protocols/mouse/aj_series.md) | `0x248A:0x5C2E` | — | — | 🟡 scaffolded | rgb, dpi, firmware |
| mouse | [AJAZZ AJ-series wired (alt mode 5D2E)](docs/protocols/mouse/aj_series.md) | `0x248A:0x5D2E` | — | — | 🟡 scaffolded | rgb, dpi, firmware |
| mouse | [AJAZZ AJ-series wired (alt mode 5E2E)](docs/protocols/mouse/aj_series.md) | `0x248A:0x5E2E` | — | — | 🟡 scaffolded | rgb, dpi, firmware |
| mouse | [AJAZZ AJ-series 2.4GHz dongle](docs/protocols/mouse/aj_series.md) | `0x248A:0x5C2F` | — | — | 🟡 scaffolded | rgb, dpi, firmware |
| mouse | [AJAZZ AJ159 APEX (wired) / AJ179 APEX](docs/protocols/mouse/aj_series_opcode_table.md) | `0x3151:0x5008` | — | — | 🟡 scaffolded | dpi, rgb |
| mouse | [AJAZZ AJ159 APEX (2.4G)](docs/protocols/mouse/aj_series_opcode_table.md) | `0x3151:0x4026` | — | — | 🟡 scaffolded | dpi, rgb |
| mouse | [AJAZZ AJ159 APEX 2.4G dongle (paired kbd+mouse)](docs/protocols/mouse/aj_series_opcode_table.md) | `0x3151:0x4027` | — | — | 🟡 scaffolded | dpi, rgb |
| mouse | [AJAZZ 2.4G 8K](docs/protocols/mouse/aj_series_vendor.md) | `0x3151:0x5007` | — | — | 🟠 partial | dpi, rgb, display, clock |
| mouse | [AJAZZ AJ199 family (wired)](docs/protocols/mouse/aj_series.md) | `0x3554:0xF500` | — | — | 🟡 scaffolded | rgb, dpi, firmware, display, clock |
| mouse | [AJAZZ AJ199 family (2.4GHz dongle)](docs/protocols/mouse/aj_series.md) | `0x3554:0xF501` | — | — | 🟡 scaffolded | rgb, dpi, firmware, display, clock |
| dongle | [AJAZZ AK980 PRO 2.4GHz USB Receiver](docs/protocols/keyboard/proprietary.md) | `0x0c45:0x7016` | — | — | 🔵 probed |  |
<!-- END AUTOGEN: devices-table -->

## By family

<!-- BEGIN AUTOGEN: devices-by-family -->
### Stream Dock macropads

| Device | USB | Status | Features | Notes |
|--------|-----|--------|----------|-------|
| [AJAZZ AKP153 / Mirabox HSV293S](docs/protocols/streamdeck/akp153.md) | `0x0300:0x1001` | 🟢 functional | Per-key display, RGB backlight, Macros, Firmware version, Host-settable clock (scaffolded) | Reference 3x5 grid; 85x85 JPEG keys (Rot90+mirror). Legacy VID:PID kept for compatibility; the canonical Mirabox V1 pair is 0x5548:0x6674 (also registered). |
| [AJAZZ AKP153 (Mirabox V1 firmware)](docs/protocols/streamdeck/akp153.md) | `0x5548:0x6674` | 🟢 functional | Per-key display, RGB backlight, Macros, Firmware version, Host-settable clock (scaffolded) | Canonical USB pair per ajazz-sdk for the AKP153 with Mirabox V1 firmware. |
| [AJAZZ AKP153E (China variant)](docs/protocols/streamdeck/akp153.md) | `0x0300:0x1002` | 🟢 functional | Per-key display, RGB backlight, Macros, Firmware version, Host-settable clock (scaffolded) | Legacy VID:PID; canonical AKP153E PID per ajazz-sdk is 0x1010 (also registered as `akp153e_v2`). |
| [AJAZZ AKP153E (Mirabox V2 firmware)](docs/protocols/streamdeck/akp153.md) | `0x0300:0x1010` | 🟢 functional | Per-key display, RGB backlight, Macros, Firmware version, Host-settable clock (scaffolded) | Canonical AKP153E PID per ajazz-sdk; same protocol as AKP153. |
| [AJAZZ AKP153R](docs/protocols/streamdeck/akp153.md) | `0x0300:0x1020` | 🟡 scaffolded | Per-key display, RGB backlight, Macros, Firmware version, Host-settable clock (scaffolded) | Regional revision per ajazz-sdk. Protocol identical to AKP153; capture pending to confirm firmware quirks. |
| [AJAZZ AKP815](docs/protocols/streamdeck/akp815.md) | `0x5548:0x6672` | 🔵 probed | Per-key display, Macros, Firmware version, Host-settable clock (scaffolded) | ✓ Descriptor + factory wired in register.cpp (0x5548:0x6672); Protocol artefact (akp815.md) with byte-0 Report ID convention; Per-key image upload path inherited from AKP153 v1-API state machine · ⚠ 100x100 Rot180 image transform — implementation present, no real-device capture confirms byte output · ✗ Real-device capture to promote probed -> partial; 800x480 strip image upload validation · 15-key 5x3 grid; 100x100 JPEG keys (Rot180) plus an 800x480 LCD strip. Backend reuses the AKP153 v1-API state machine with a different DisplayInfo. Per-revision image pipeline tracked in TODO.md. |
| [AJAZZ AKP03 / Mirabox N3](docs/protocols/streamdeck/akp03.md) | `0x0300:0x1001` | 🟢 functional | Per-key display, Encoder / dial, Macros, Host-settable clock (scaffolded) | 6 LCD keys (2x3) + 3 pressable encoders + 3 non-LCD side buttons. JPEG 60x60 (Rot0) keys. Canonical PID per ajazz-sdk; legacy 0x3001 kept registered for compatibility. |
| [AJAZZ AKP03 (legacy 0x3001 firmware)](docs/protocols/streamdeck/akp03.md) | `0x0300:0x3001` | 🟢 functional | Per-key display, Encoder / dial, Macros, Host-settable clock (scaffolded) | Pre-2026-05-14 placeholder PID. Same backend as `akp03`; retained until removed in a future cleanup. |
| [AJAZZ AKP03E](docs/protocols/streamdeck/akp03.md) | `0x0300:0x3002` | 🟢 functional | Per-key display, Encoder / dial, Macros, Host-settable clock (scaffolded) | AKP03 with Mirabox V2 firmware (1024-byte packets per mirajazz). Same wire-format family. |
| [AJAZZ AKP03R](docs/protocols/streamdeck/akp03.md) | `0x0300:0x1003` | 🟢 functional | Per-key display, Encoder / dial, Macros, Host-settable clock (scaffolded) | Regional revision; same protocol as AKP03 per ajazz-sdk. |
| [AJAZZ AKP03R rev. 2](docs/protocols/streamdeck/akp03.md) | `0x0300:0x3003` | 🟡 scaffolded | Per-key display, Encoder / dial, Macros, Host-settable clock (scaffolded) | Per mirajazz this is a protocol_version 3 device (full press/release states + GIF support). Per-key images are 64x64 (Rot90) instead of 60x60 (Rot0). |
| [Mirabox N3 (rev. 1)](docs/protocols/streamdeck/mirabox_n3.md) | `0x6602:0x1002` | 🟠 partial | Per-key display, Encoder / dial, Macros, Host-settable clock (scaffolded) | ✓ Descriptor + factory wired in register.cpp (0x6602:0x1002 via akp03_descriptor); Inherited from akp03: per-key image upload, brightness, encoder rotation/press; Cross-reference protocol doc (mirabox_n3.md) citing akp03.md · ⚠ Inherited from akp03 functional tier — no first-hand Mirabox capture confirms Mirabox V1 firmware behaves identically · ✗ Real Mirabox N3 (rev. 1) capture to promote partial -> functional; USB-suspend handling on bus reset (Mirabox-specific quirks if any) · Mirabox-branded sibling of AJAZZ AKP03 (opendeck-akp03 catalogue). Same backend; Phase 8 DEVICES-04 promotion #2. |
| [Mirabox N3 (rev. 3)](docs/protocols/streamdeck/akp03.md) | `0x6603:0x1002` | 🟡 scaffolded | Per-key display, Encoder / dial, Macros, Host-settable clock (scaffolded) | Newer Mirabox N3 hardware revision; same backend. |
| [Mirabox N3EN](docs/protocols/streamdeck/akp03.md) | `0x6603:0x1003` | 🟡 scaffolded | Per-key display, Encoder / dial, Macros, Host-settable clock (scaffolded) | Mirabox SKU variant per opendeck-akp03 udev rules. |
| [AJAZZ AKP05 / AKP05E (provisional)](docs/protocols/streamdeck/akp05.md) | `0x0300:0x5001` | 🟡 scaffolded | Per-key display, Encoder / dial, Touch strip, Macros, Host-settable clock (scaffolded) | Stream Dock Plus-class: 10 LCD keys (2x5) + 4 endless encoders + LCD touchscreen strip (4 zones). VID:PID is a pre-2026-05-14 placeholder; canonical is `mirabox_n4`. Layout corrected from 15->10 keys after the 2026-05-14 research pass. |
| [Mirabox N4 / AJAZZ AKP05 family](docs/protocols/streamdeck/akp05.md) | `0x6603:0x1007` | 🟡 scaffolded | Per-key display, Encoder / dial, Touch strip, Macros, Host-settable clock (scaffolded) | Canonical USB ID from opendeck-akp05. 2x5 LCD keys, 4 encoders with touchscreen-strip overlays (110x14mm physical, 800x480 panel), built-in USB-2 hub (2xUSB-A + 2xUSB-C). Per mirajazz this is a protocol_version 3 device. |
| [AJAZZ AKP05E (Stream Dock Plus)](docs/protocols/streamdeck/akp05.md) | `0x0300:0x3004` | 🟡 scaffolded | Per-key display, Encoder / dial, Touch strip, Macros, Host-settable clock (scaffolded) | Real retail AKP05E. Hot-plug capture 2026-05-13 surfaced this PID as 'Ajazz HOTSPOTEKUSB HID DEMO' (white-label dev string), which led to a brief mis-filing as the 6-key 'akp03_variant_3004'. A live CRT VER handshake on 2026-05-20 returned firmware string 'V3.AKP05E.01.007' — confirming AKP05E, protocol_version 3 (1024-byte packets), 10 keys (2x5) + 4 encoders + LCD touch strip. Routed through makeAkp05. |

### Keyboards

| Device | USB | Status | Features | Notes |
|--------|-----|--------|----------|-------|
| [AJAZZ AK series (QMK/VIA-compatible)](docs/protocols/keyboard/via.md) | `0x3151:various` | 🟢 functional | RGB backlight, Macros, Layers, Firmware version | Any VIA JSON layout is supported. |
| [AJAZZ AK series (proprietary)](docs/protocols/keyboard/proprietary.md) | `0x3151:various` | 🟢 functional | RGB backlight, Macros, Layers, Firmware version | Clean-room backend; RGB zones, keymap and macro upload wired. |
| [AJAZZ AK980 PRO](docs/protocols/keyboard/proprietary.md) | `0x0c45:0x8009` | 🟠 partial | RGB backlight, Macros, Layers, Host-settable clock (scaffolded), Battery charge level (wireless) | ✓ Descriptor + factory wired in register.cpp (0c45:8009); Time-sync wire format implemented (ARCH-05.1): IClockCapable::setTime emits the 4-packet 0x18/0x28/data/0x02 envelope via HID Feature Reports with local-time year-2000 byte encoding + 100ms settle window; RGB static / effect / per-LED buffer / brightness builders inherited from proprietary backend (AK680/AK510 wire format); Macro chunked upload + key remap + layer switch inherited from proprietary backend · ⚠ RGB / macros / layers compile against the AK680/AK510 wire format — same chipset family is assumed but not field-confirmed on AK980 PRO specifically · ✗ Phase 9.x physical round-trip witness for setTime (witness 2): TFT clock widget shows the time we sent; Phase 9.x negative witness for setTime (witness 3): year 2099 produces visible-but-wrong display, proving firmware parses the field; TFT 1.14" image upload (cmd 0x72) — DISPLAY-05 deferred to v1.2.x; 20-mode RGB enum expansion (TaxMachine corpus has 20; our RgbEffect enum has 6); Sleep-timer wire format (cmd 0x17 4-state enum per gohv) — capture per-byte layout · Microdia/Sonix SN32F299 wireless mech; routed through the proprietary backend. ARCH-05.1 (2026-05-17) located the firmware RTC wire format: 4-packet HID Feature Report envelope (START 0x18 + PREAMBLE 0x28 + DATA magic 0x5A + SAVE 0x02), sent via writeFeature() (NOT write()), corroborated by gohv/EPOMAKER-Ajazz-AK820-Pro + KyleBoyer/TFTTimeSync-node + Agent B vendor binary disassembly. setTime() implemented end-to-end; promotion partial -> functional gates on Phase 9.x physical round-trip witness (TFT clock widget shows the time we sent). |

### Mice

| Device | USB | Status | Features | Notes |
|--------|-----|--------|----------|-------|
| [AJAZZ AJ-series wired (primary)](docs/protocols/mouse/aj_series.md) | `0x248A:0x5C2E` | 🟡 scaffolded | RGB backlight, DPI stages, Firmware version | Covers AJ139 PRO / AJ159 / AJ159 MC / AJ159P MC / AJ179 / AJ139 V2 PRO / AJ179 V2 / AJ179 V2 MAX in their canonical USB-wired mode. PAW3395 / PAW3311 / PAW3370 / PAW3335 / PAW3950 sensor depending on SKU. HID configuration interface is `MI_02`. Wire-format reconciliation against the vendor driver still pending — see TODO `AJ-series wire format reconciliation` and `vendor-protocol-notes.md` Finding 11. |
| [AJAZZ AJ-series wired (alt mode 5D2E)](docs/protocols/mouse/aj_series.md) | `0x248A:0x5D2E` | 🟡 scaffolded | RGB backlight, DPI stages, Firmware version | Same SKUs as `aj_series_wired_primary`; the vendor's `config.xml` lists three USB PIDs per device (5C2E / 5D2E / 5E2E) representing different USB-side mode descriptors. |
| [AJAZZ AJ-series wired (alt mode 5E2E)](docs/protocols/mouse/aj_series.md) | `0x248A:0x5E2E` | 🟡 scaffolded | RGB backlight, DPI stages, Firmware version | Same SKUs as `aj_series_wired_primary`; third USB-mode descriptor. |
| [AJAZZ AJ-series 2.4GHz dongle](docs/protocols/mouse/aj_series.md) | `0x248A:0x5C2F` | 🟡 scaffolded | RGB backlight, DPI stages, Firmware version | Same SKUs in 2.4GHz wireless mode through the bundled USB dongle. Vendor also exposes the same dongle under VID `0x249A` PID `0x5C2F` — reason unclear (likely USB stack path difference). For udev coverage, include both `idVendor=="248a"` and `idVendor=="249a"`. |
| [AJAZZ AJ159 APEX (wired) / AJ179 APEX](docs/protocols/mouse/aj_series_opcode_table.md) | `0x3151:0x5008` | 🟡 scaffolded | DPI stages, RGB backlight | ✓ Descriptor + factory wired in register.cpp (3151:5008) · ✗ P3.12 aj_series.cpp wire-format rewrite per docs/protocols/mouse/aj_series_opcode_table.md (~600 LOC code + ~400 LOC test, feature-flagged AJAZZ_AJ_SERIES_WIRE_REWRITE); Real-device round-trip witness for scaffolded → partial promotion · AJ159 APEX in USB wired mode + AJ179 APEX alias (disambiguated by descriptor strings, not by PID). 8 onboard DPI stages, 8 KHz polling. Wire format documented in aj_series_opcode_table.md (deep RE 2026-05-17). Promotion to partial requires P3.12 wire-format rewrite + real-device round-trip witness. |
| [AJAZZ AJ159 APEX (2.4G)](docs/protocols/mouse/aj_series_opcode_table.md) | `0x3151:0x4026` | 🟡 scaffolded | DPI stages, RGB backlight | AJ159 APEX in 2.4G wireless mode (capped at 1 KHz per vendor JS UI). Same wire format as wired sibling. Promotion blocked on P3.12 + real-device test. |
| [AJAZZ AJ159 APEX 2.4G dongle (paired kbd+mouse)](docs/protocols/mouse/aj_series_opcode_table.md) | `0x3151:0x4027` | 🟡 scaffolded | DPI stages, RGB backlight | Paired 2.4G dongle exposing both keyboard + mouse child interfaces (dongle_common path per aj_series_device_matrix.md §1.2). Promotion blocked on P3.12. |
| [AJAZZ 2.4G 8K](docs/protocols/mouse/aj_series_vendor.md) | `0x3151:0x5007` | 🟠 partial | DPI stages, RGB backlight, Per-key display, Host-settable clock (scaffolded) | ✓ Descriptor + factory wired in register.cpp (3151:5007); P0 safety guard: damaging commit() helper (opcode 0x50) removed; no longer corrupts button slot 0 · ✗ Wire-format rewrite per docs/protocols/mouse/aj_series_vendor.md (§11.5 roadmap, ~600 LOC code + ~400 LOC test, largest single commit in upcoming batch); Real-device round-trip witness once rewrite lands (promotes scaffolded → partial); 8 onboard profiles, 8 DPI stages (not 6), 20 macros, OTA, TFT LCD widgets — all v1.3+ deferred · 8KHz-polling wireless mouse on SONiX VID prefix. Has TFT LCD basetta showing clock + DPI (2026-05-18 implementation: opcode 0x25 SETTFTLCDDATA chunked RGB565 with host-side QImage renderer, wired to IClockCapable so TimeSyncService drives it — clock surface ratified ARCH-05.2, host-rendered, NOT a firmware RTC). Wire-format rewrite landed in P3.12.1/.2 (656fb1c, 0e9bb04). |
| [AJAZZ AJ199 family (wired)](docs/protocols/mouse/aj_series.md) | `0x3554:0xF500` | 🟡 scaffolded | RGB backlight, DPI stages, Firmware version, Per-key display, Host-settable clock (scaffolded) | AJ199 / AJ199 Max / AJ199 Carbon Fiber. Wired-mode primary PID per AJ199 Max `Config.ini` `M_PID` (`F500`). Has TFT LCD basetta wired to the shared 0x25 chunked clock+DPI renderer (2026-05-18). AJ199 Max wire format is structurally different from AJ199 V1.0 (offset-based struct vs flat report) — see `vendor-protocol-notes.md` Finding 11.B. |
| [AJAZZ AJ199 family (2.4GHz dongle)](docs/protocols/mouse/aj_series.md) | `0x3554:0xF501` | 🟡 scaffolded | RGB backlight, DPI stages, Firmware version, Per-key display, Host-settable clock (scaffolded) | AJ199 family in 2.4GHz mode. Vendor `D_PID` list is `F501,F564,F567,F545,F547,F5D5` — six dongle-mode PIDs distinguishing variant SKUs at the USB layer. Has TFT LCD basetta wired to the shared 0x25 chunked clock+DPI renderer (2026-05-18). |

### Dongles

| Device | USB | Status | Features | Notes |
|--------|-----|--------|----------|-------|
| [AJAZZ AK980 PRO 2.4GHz USB Receiver](docs/protocols/keyboard/proprietary.md) | `0x0c45:0x7016` | 🔵 probed |  | ✓ Identified via HID report-descriptor parsing on dev box hardware (2026-05-15); Topology evidence cited inline (Phase 9 ARCH-06 cross-reference) · ✗ Runtime entry in `src/devices/keyboard/src/register.cpp` (or new `src/devices/dongle/`) — this is catalog-only for now; sidebar visibility requires register.cpp + `DeviceFamily::Dongle` enum addition; Captures-confirmation 2-minute physical unplug test (unplug `ak980pro` USB-C and verify the dongle does NOT simultaneously disappear) — Phase 9.x deferred · 2.4GHz USB receiver dongle paired with AJAZZ AK980 PRO (`ak980pro`, VID:PID 0c45:8009). Identified 2026-05-15 via HID report-descriptor parsing on real hardware: Interface 0 advertises a 6KRO boot keyboard + 64-byte vendor Feature Report on Consumer Page — the exact control-channel signature that TaxMachine documented for AK820 Pro / AK980 PRO at the wired VID:PID. Interface 1 is a 4-in-1 composite (mouse + system + consumer/media + 120-key NKRO keyboard) typical of SONiX wireless mech-keyboard receivers. When the host has both the USB-C wired connection AND the 2.4GHz dongle active, the same physical keyboard enumerates twice — once as `ak980pro` (wired path) and once via this dongle (RF-tunneled). No standalone capabilities advertised; the dongle is a transport, not a device — capability work belongs on the `ak980pro` codename and applies regardless of connection path. ARCH-06 default verdict (Phase 9 partial-scope, 2026-05-15) ratified composite-HID dedup NOT firing — topology (Full-Speed 12Mbps vs the AK980 PRO's High-Speed 480Mbps, separate USB bus branch) refutes the composite-interface hypothesis. |

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
