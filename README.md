<p align="center">
  <img src="resources/icons/hicolor/app-256.png" alt="AJAZZ Control Center icon" width="160" height="160">
</p>

<h1 align="center">AJAZZ Control Center</h1>

<p align="center"><em>One open, cross-platform control center for every AJAZZ device.</em></p>

<p align="center">
  <a href="https://github.com/Aiacos/ajazz-control-center/actions/workflows/ci.yml"><img src="https://img.shields.io/github/actions/workflow/status/Aiacos/ajazz-control-center/ci.yml?branch=main&label=CI&logo=github" alt="CI"></a>
  <a href="https://github.com/Aiacos/ajazz-control-center/releases"><img src="https://img.shields.io/github/v/release/Aiacos/ajazz-control-center?include_prereleases&logo=github&color=blueviolet" alt="Release"></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-GPL--3.0-blue" alt="License"></a>
  <a href="https://www.qt.io/"><img src="https://img.shields.io/badge/Qt-6.7%2B-41CD52?logo=qt" alt="Qt 6.7+"></a>
</p>

<p align="center">
  <img alt="AJAZZ Control Center — main window" src="docs/screenshots/main-dark.png" width="860">
</p>

**AJAZZ Control Center** is a modern, open, cross-platform control center for AJAZZ
devices — Stream Dock macropads/docks, keyboards and mice. It runs natively on
**Linux, Windows and macOS** with a clean Qt 6 / QML interface, and is extensible
through a sandboxed, out-of-process **Python plugin system** with compatibility for
Stream Deck (`.sdPlugin`) plugins. Performance-critical code is C++20; each device
family is an independent backend loaded at runtime.

> **Status:** alpha. Hot-plug detection, an in-app Plugin Store, periodic clock
> auto-sync, and three device families working today (Stream Dock, AK980 PRO,
> 2.4G 8K mouse). See [`.planning/STATE.md`](.planning/STATE.md) for live status.

______________________________________________________________________

## Installation

Download the latest build from the **[Releases page](https://github.com/Aiacos/ajazz-control-center/releases)**.
Rolling pre-release builds for every push to `main` are published as the
**[nightly release](https://github.com/Aiacos/ajazz-control-center/releases/tag/nightly)**.

### Linux

```bash
# Debian / Ubuntu (.deb)
sudo apt install ./ajazz-control-center_*.deb      # or: sudo dpkg -i ajazz-control-center_*.deb

# Fedora / RHEL / openSUSE (.rpm)
sudo dnf install ./ajazz-control-center-*.rpm

# Any distro (.flatpak)
flatpak install --user ./ajazz-control-center.flatpak
```

> **Device access:** the project ships a udev rule
> [`resources/linux/70-ajazz.rules`](resources/linux/70-ajazz.rules) using
> `TAG+="uaccess"`, so systemd-logind grants your user access automatically —
> no `plugdev` group, no logout. The `.deb`/`.rpm` packages install it for you;
> the Flatpak prompts you to install it on first run.

### Windows

Download the `.msi` installer (or the portable `.zip`) from the
[latest release](https://github.com/Aiacos/ajazz-control-center/releases/latest)
and run it — no drivers required. SmartScreen may warn while the installer is
unsigned: choose **More info → Run anyway**.

`winget` and `Chocolatey` packages are prepared
([`packaging/`](packaging/PUBLISHING.md)) and pending submission/approval:

```powershell
winget install Aiacos.AjazzControlCenter   # coming soon (pending winget-pkgs)
choco  install ajazz-control-center        # coming soon (pending moderation)
```

### macOS

Download the universal `.dmg` (Apple Silicon + Intel) from the
[latest release](https://github.com/Aiacos/ajazz-control-center/releases/latest),
drag the app to **Applications**, and grant **Input Monitoring** on first launch.

> Prefer to compile it yourself? See [Build from source](#build-from-source) below.

______________________________________________________________________

## Features

| Feature | Description | Status |
|---------|-------------|--------|
| Per-key / LCD-key config | Set images, labels and actions on Stream Dock LCD keys | 🟢 working |
| RGB lighting | Static, effect and per-LED control; 20 firmware modes on AK980 PRO | 🟢 working |
| Encoders / dials | Rotation + press handling on Stream Dock Plus-class devices | 🟢 working |
| Macros | Record / assign macros across keyboards and macropads | 🟢 working |
| Profiles | Switch device profiles; per-app auto-switch planned | 🟢 working |
| Time-sync | Push host time to firmware RTCs; 15-min periodic auto-sync | 🟢 working |
| Battery level | Wireless charge readout (see device notes for caveats) | 🟠 partial |
| Firmware updates | Auto-download + delegate flashing to the vendor tool | 🟡 planned |
| Python plugins | Out-of-process, sandboxed (`bwrap` / `sandbox-exec` / AppContainer) | 🟢 working |
| Stream Deck plugins | In-app store mirrors AJAZZ Streamdock (~160) + OpenDeck (~320) catalogues | 🟢 working |
| Cross-platform UX | Native notifications + autostart on Linux / macOS / Windows | 🟢 working |

______________________________________________________________________

## Supported devices

<!-- BEGIN AUTOGEN: legend -->
🟡 **scaffolded** — descriptor + factory exist; backend compiles but does not exercise the device · 🔵 **probed** — device enumerates and descriptor populated; no protocol writes confirmed · 🟠 **partial** — some features work end-to-end; advertised capability set incomplete or untested · 🟢 **functional** — all advertised capabilities work in practice; tested manually or in CI · ✅ **verified** — functional + automated CI on real hardware OR sustained user-confirmed reliability
<!-- END AUTOGEN: legend -->

The tables below are generated from [`docs/_data/devices.yaml`](docs/_data/devices.yaml)
by `make docs` — edit the YAML, not this section. Full per-device protocol notes
live under [`docs/protocols/`](docs/protocols/).

<!-- BEGIN AUTOGEN: devices-by-family -->
### Stream Dock macropads

| Device | USB | Status | Features | Notes |
|--------|-----|--------|----------|-------|
| [AJAZZ AKP153 / Mirabox HSV293S](docs/protocols/streamdeck/akp153.md) | `0x0300:0x1001` | 🟢 functional | Per-key display, RGB backlight, Macros, Firmware version, Host-settable clock (scaffolded) | Reference 3x5 grid; 85x85 JPEG keys (Rot90+mirror). Legacy VID:PID kept for compatibility; the canonical Mirabox V1 pair is 0x5548:0x6674 (also registered). |
| [AJAZZ AKP153 (Mirabox V1 firmware)](docs/protocols/streamdeck/akp153.md) | `0x5548:0x6674` | 🟢 functional | Per-key display, RGB backlight, Macros, Firmware version, Host-settable clock (scaffolded) | Canonical USB pair per ajazz-sdk for the AKP153 with Mirabox V1 firmware. |
| [AJAZZ AKP153E (China variant)](docs/protocols/streamdeck/akp153.md) | `0x0300:0x1002` | 🟢 functional | Per-key display, RGB backlight, Macros, Firmware version, Host-settable clock (scaffolded) | Legacy VID:PID; canonical AKP153E PID per ajazz-sdk is 0x1010 (also registered as `akp153e_v2`). |
| [AJAZZ AKP153E (Mirabox V2 firmware)](docs/protocols/streamdeck/akp153.md) | `0x0300:0x1010` | 🟢 functional | Per-key display, RGB backlight, Macros, Firmware version, Host-settable clock (scaffolded) | Canonical AKP153E PID per ajazz-sdk; same protocol as AKP153. |
| [AJAZZ AKP153R](docs/protocols/streamdeck/akp153.md) | `0x0300:0x1020` | 🟡 scaffolded | Per-key display, RGB backlight, Macros, Firmware version, Host-settable clock (scaffolded) | Regional revision per ajazz-sdk. Protocol identical to AKP153; capture pending to confirm firmware quirks. |
| [AJAZZ AKP815](docs/protocols/streamdeck/akp815.md) | `0x5548:0x6672` | 🔵 probed | Per-key display, Macros, Firmware version, Host-settable clock (scaffolded) | ✓ Descriptor + factory wired in register.cpp (0x5548:0x6672); Protocol artefact (akp815.md) with byte-0 Report ID convention; Per-key image upload path via the v1-API builders in akp815_wire.{hpp,cpp} · ⚠ 100x100 Rot180 image transform — implementation present, no real-device capture confirms byte output · ✗ Real-device capture to promote probed -> partial; 800x480 strip image upload validation · 15-key 5x3 grid; 100x100 JPEG keys (Rot180) plus an 800x480 LCD strip. Custom C++ carve-out (NOT a mirajazz device); owns its v1-API wire builders in akp815_wire.{hpp,cpp} with a different DisplayInfo. Per-revision image pipeline tracked in TODO.md. |
| [AJAZZ AKP03 (legacy 0x3001 firmware)](docs/protocols/streamdeck/akp03.md) | `0x0300:0x3001` | 🟢 functional | Per-key display, Encoder / dial, Macros, Host-settable clock (scaffolded) | 6 LCD keys (2x3) + 3 pressable encoders + 3 non-LCD side buttons. JPEG 60x60 (Rot0) keys. Earliest AKP03 firmware PID; same backend as the rest of the AKP03 family. |
| [AJAZZ AKP03E](docs/protocols/streamdeck/akp03.md) | `0x0300:0x3002` | 🟢 functional | Per-key display, Encoder / dial, Macros, Host-settable clock (scaffolded) | AKP03 with Mirabox V2 firmware (1024-byte packets per mirajazz). Same wire-format family. |
| [AJAZZ AKP03R](docs/protocols/streamdeck/akp03.md) | `0x0300:0x1003` | 🟢 functional | Per-key display, Encoder / dial, Macros, Host-settable clock (scaffolded) | Regional revision; same protocol as AKP03 per ajazz-sdk. |
| [AJAZZ AKP03R rev. 2](docs/protocols/streamdeck/akp03.md) | `0x0300:0x3003` | 🟡 scaffolded | Per-key display, Encoder / dial, Macros, Host-settable clock (scaffolded) | Per mirajazz this is a protocol_version 3 device (full press/release states + GIF support). Per-key images are 64x64 (Rot90) instead of 60x60 (Rot0). |
| [Mirabox N3 (rev. 1)](docs/protocols/streamdeck/mirabox_n3.md) | `0x6602:0x1002` | 🟠 partial | Per-key display, Encoder / dial, Macros, Host-settable clock (scaffolded) | ✓ Descriptor wired via streamDockSidecarDescriptors() (0x6602:0x1002), driven by the mirajazz sidecar; Inherited from akp03: per-key image upload, brightness, encoder rotation/press; Cross-reference protocol doc (mirabox_n3.md) citing akp03.md · ⚠ Inherited from akp03 functional tier — no first-hand Mirabox capture confirms Mirabox V1 firmware behaves identically · ✗ Real Mirabox N3 (rev. 1) capture to promote partial -> functional; USB-suspend handling on bus reset (Mirabox-specific quirks if any) · Mirabox-branded sibling of AJAZZ AKP03 (opendeck-akp03 catalogue). Same backend; Phase 8 DEVICES-04 promotion #2. |
| [Mirabox N3E (rev. 1)](docs/protocols/streamdeck/akp03.md) | `0x6602:0x1003` | 🟡 scaffolded | Per-key display, Encoder / dial, Macros, Host-settable clock (scaffolded) | Mirabox-branded AKP03E sibling (opendeck-akp03 catalogue, 0x6602:0x1003). Same backend; registered in register.cpp but was previously missing from this table. |
| [Mirabox N3 (rev. 3)](docs/protocols/streamdeck/akp03.md) | `0x6603:0x1002` | 🟡 scaffolded | Per-key display, Encoder / dial, Macros, Host-settable clock (scaffolded) | Newer Mirabox N3 hardware revision; same backend. |
| [Mirabox N3EN](docs/protocols/streamdeck/akp03.md) | `0x6603:0x1003` | 🟡 scaffolded | Per-key display, Encoder / dial, Macros, Host-settable clock (scaffolded) | Mirabox SKU variant per opendeck-akp03 udev rules. |
| [AJAZZ AKP05 / AKP05E (provisional)](docs/protocols/streamdeck/akp05.md) | `0x0300:0x5001` | 🟡 scaffolded | Per-key display, Encoder / dial, Touch strip, Macros, Host-settable clock (scaffolded) | Stream Dock Plus-class: 10 LCD keys (2x5) + 4 endless encoders + LCD touchscreen strip (4 zones). VID:PID is a pre-2026-05-14 placeholder; canonical is `mirabox_n4`. Layout corrected from 15->10 keys after the 2026-05-14 research pass. |
| [Mirabox N4 / AJAZZ AKP05 family](docs/protocols/streamdeck/akp05.md) | `0x6603:0x1007` | 🟡 scaffolded | Per-key display, Encoder / dial, Touch strip, Macros, Host-settable clock (scaffolded) | Canonical USB ID from opendeck-akp05. 2x5 LCD keys, 4 encoders with touchscreen-strip overlays (110x14mm physical, 800x480 panel), built-in USB-2 hub (2xUSB-A + 2xUSB-C). Per mirajazz this is a protocol_version 3 device. |
| [AJAZZ AKP05E (Stream Dock Plus)](docs/protocols/streamdeck/akp05.md) | `0x0300:0x3004` | 🟠 partial | Per-key display, Encoder / dial, Touch strip, Macros | White-label demo AKP05E ('Ajazz HOTSPOTEKUSB HID DEMO' dev string; briefly mis-filed as 6-key 'akp03_variant_3004'). Live CRT VER 2026-05-20 returned 'V3.AKP05E.01.007' — AKP05E, protocol_version 3 (1024-byte packets), 10 keys (2x5) + 4 encoders + LCD touch strip. Output FULLY works (2026-05-31): keys + the 4 strip zones render via BAT (wire 6..15 keys, 1..4 strip zones), Rot180; brightness live. Input (key/encoder/touch) UNREACHABLE on this demo SKU -> maturity 'partial'. See docs/protocols/streamdeck/akp05.md. Driven by the out-of-process mirajazz sidecar (the C++ wire backend was removed in experiment/mirajazz Slice D). |

### Keyboards

| Device | USB | Status | Features | Notes |
|--------|-----|--------|----------|-------|
| [AJAZZ AK series (QMK/VIA-compatible)](docs/protocols/keyboard/via.md) | `0x3151:various` | 🟢 functional | RGB backlight, Macros, Layers, Firmware version | Any VIA JSON layout is supported. |
| [AJAZZ AK series (proprietary)](docs/protocols/keyboard/proprietary.md) | `0x3151:various` | 🟢 functional | RGB backlight, Macros, Layers, Firmware version | Clean-room backend; RGB zones, keymap and macro upload wired. |
| [AJAZZ AK980 PRO](docs/protocols/keyboard/proprietary.md) | `0x0c45:0x8009` | 🟢 functional | RGB backlight, Macros, Layers, Host-settable clock (scaffolded), Battery charge level (wireless) | ✓ Descriptor + factory wired in register.cpp (0c45:8009); Time-sync HARDWARE-CONFIRMED working (2026-05-21): IClockCapable::setTime emits the 4-packet 0x18/0x28/data/0x02 envelope on the 0xFF13 control collection — but the envelope ALONE is a silent no-op; the device needs a request/RESPONSE handshake, so setTime does per packet a ~30ms settle delay + a best-effort GET_REPORT readback (writeFeature then readFeature), with the year-2000 byte encoding + 100ms SAVE settle. The on-device TFT clock follows the injected time (round-trip witness OBTAINED); Battery charge level reads on hardware (2026-05-21): query opcode 0x20 sub 0x01, report id 0x00, reply via GET_FEATURE into a 65-byte buffer (64-byte fails on Windows) — opcode echo at resp[1], charge percent at resp[4] (0xFF wired+full clamps to 100, 0 = no battery); RGB static / effect / per-LED buffer / brightness builders inherited from proprietary backend (AK680/AK510 wire format); Macro chunked upload + key remap + layer switch inherited from proprietary backend · ⚠ RGB / macros / layers compile against the AK680/AK510 wire format — same chipset family is assumed but not field-confirmed on AK980 PRO specifically · ✗ Negative witness for setTime: year 2099 produces visible-but-wrong display, proving firmware parses the field; TFT 1.14" image upload (cmd 0x72) — DISPLAY-05 deferred to v1.2.x; 20-mode RGB enum expansion (TaxMachine corpus has 20; our RgbEffect enum has 6); Sleep-timer wire format (cmd 0x17 4-state enum per gohv) — capture per-byte layout · Microdia/Sonix SN32F299 wireless mech; routed through the proprietary backend. ARCH-05.1 (2026-05-17) located the firmware RTC wire format: 4-packet HID Feature Report envelope (START 0x18 + PREAMBLE 0x28 + DATA magic 0x5A + SAVE 0x02), sent via writeFeature() (NOT write()), corroborated by gohv/EPOMAKER-Ajazz-AK820-Pro + KyleBoyer/TFTTimeSync-node + Agent B vendor binary disassembly; the per-packet delay+readback handshake was the missing piece, pinned on live hardware 2026-05-21 (commit 807b250). |

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
| [AJAZZ AJ159 APEX](docs/protocols/mouse/aj_series_vendor.md) | `0x3151:0x5007` | 🟢 functional | DPI stages, RGB backlight, Per-key display, Host-settable clock (scaffolded), Battery charge level (wireless) | ✓ Descriptor + factory wired in register.cpp (3151:5007); P0 safety guard: damaging commit() helper (opcode 0x50) removed; no longer corrupts button slot 0 · ✗ Wire-format rewrite per docs/protocols/mouse/aj_series_vendor.md (§11.5 roadmap, ~600 LOC code + ~400 LOC test, largest single commit in upcoming batch); Real-device round-trip witness once rewrite lands (promotes scaffolded → partial); 8 onboard profiles, 8 DPI stages (not 6), 20 macros, OTA, TFT LCD widgets — all v1.3+ deferred · 8KHz-polling wireless mouse on SONiX VID prefix. Has an OLED basetta showing clock + DPI. The clock is a firmware RTC (HARDWARE-CONFIRMED 2026-05-21, commit 0a1952e): opcode 0x28 SET_OLEDCLOCK feature report with a required 0xD7 marker at byte 8, big-endian year, no checksum, sent via HidD_SetFeature; wired to IClockCapable so TimeSyncService drives it. (Supersedes the earlier opcode-0x25 SETTFTLCDDATA host-rendered RGB565 face, which never actually set the clock — the 0x25 render pipeline is kept only for a future custom-image feature.) Battery now reads on hardware via vendor status report 0x05 byte 3 (0..100, 0x64=100%) over GET_FEATURE on the 0xFFFF (usage 0x02) control collection, with reconnect frame-validation (a valid frame has bytes 1/2 zero; malformed transient frames after a replug are rejected so there is no spurious 1% flash — commit 1f2be0c). Wire-format rewrite landed in P3.12.1/.2 (656fb1c, 0e9bb04). |
| [AJAZZ AJ199 family (wired)](docs/protocols/mouse/aj_series.md) | `0x3554:0xF500` | 🟡 scaffolded | RGB backlight, DPI stages, Firmware version, Per-key display, Host-settable clock (scaffolded) | AJ199 / AJ199 Max / AJ199 Carbon Fiber. Wired-mode primary PID per AJ199 Max `Config.ini` `M_PID` (`F500`). Has an OLED basetta whose clock is a firmware RTC driven by opcode 0x28 SET_OLEDCLOCK (the same mechanism hardware-confirmed on the 2.4G 8K, 2026-05-21 — NOT the earlier host-rendered 0x25 face). AJ199 Max wire format is structurally different from AJ199 V1.0 (offset-based struct vs flat report) — see `vendor-protocol-notes.md` Finding 11.B. |
| [AJAZZ AJ199 family (2.4GHz dongle)](docs/protocols/mouse/aj_series.md) | `0x3554:0xF501` | 🟡 scaffolded | RGB backlight, DPI stages, Firmware version, Per-key display, Host-settable clock (scaffolded) | AJ199 family in 2.4GHz mode. Vendor `D_PID` list is `F501,F564,F567,F545,F547,F5D5` — six dongle-mode PIDs distinguishing variant SKUs at the USB layer. Has an OLED basetta whose clock is a firmware RTC driven by opcode 0x28 SET_OLEDCLOCK (the same mechanism hardware-confirmed on the 2.4G 8K, 2026-05-21 — NOT the earlier host-rendered 0x25 face). |

### Dongles

| Device | USB | Status | Features | Notes |
|--------|-----|--------|----------|-------|
| [Microdia / SONiX 2.4GHz USB Receiver (0c45:7016)](docs/protocols/keyboard/microdia_dongle.md) | `0x0c45:0x7016` | 🔵 probed |  | ✓ Identified via HID report-descriptor parsing on dev box hardware (2026-05-15); Topology evidence cited inline (Phase 9 ARCH-06 cross-reference) · ✗ Runtime entry in `src/devices/keyboard/src/register.cpp` (or new `src/devices/dongle/`) — this is catalog-only for now; sidebar visibility requires register.cpp + `DeviceFamily::Dongle` enum addition; Captures-confirmation 2-minute physical unplug test (unplug the `ak980pro` 2.4G receiver and verify the dongle does NOT simultaneously disappear) — Phase 9.x deferred; gates ARCH-06 promotion from DEFAULT VERDICT to Locked · Separate wireless 2.4GHz Microdia/SONiX receiver dongle — NOT a composite/secondary interface of `ak980pro` (the ARCH-06 separate-dongle verdict). Topology evidence from live `lsusb` 2026-05-15: different USB bus branch (`usb1/1-13/1-13.1/1-13.1.2` vs ak980pro's `usb1/1-10`); Full-Speed 12 Mbps link speed; two boot-keyboard HID interfaces with 8-byte EPs (IF0 + IF1); `iManufacturer="SONiX"` / `iProduct="USB DEVICE"` / `bcdDevice 1.03` — the canonical signature of a SONiX OEM 2.4 GHz receiver. Paired downstream input device unknown — pending an `evtest` / `/dev/hidrawN` session per the DEVICES-09 identification methodology. Existing codename `ak980pro_dongle_24g` retained for catalogue/AUTOGEN stability; ARCH-06 §Binding records the canonical alias `microdia_dongle_7016` (used as the stub doc filename for forward-discoverability by anyone keying off the ADR). ARCH-06 status: **DEFAULT VERDICT (PENDING CAPTURE CONFIRMATION)** — composite-HID dedup NOT firing in `DeviceRegistry::enumerate`. Finalization gate: 2-minute physical unplug test (unplug the `ak980pro` 2.4G receiver; confirm `0c45:7016` does NOT disappear simultaneously) — Phase 9.x deferred. |

<!-- END AUTOGEN: devices-by-family -->

______________________________________________________________________

## Architecture

```
┌────────────────────────────────────────────────────────────┐
│                   Qt 6 / QML Desktop UI                      │
│      device browser · profile editor · key designer          │
└────────────────────────────────────────────────────────────┘
                            │
┌────────────────────────────────────────────────────────────┐
│                  Application Layer (C++20)                   │
│   Profile engine · Action dispatcher · Plugin host · Bus     │
└────────────────────────────────────────────────────────────┘
        │                   │                    │
┌───────────────┐  ┌─────────────────┐  ┌──────────────────┐
│  Device Core  │  │ OOP Plugin Host │  │   Persistence    │
│ HID + hidapi  │  │ sandboxed +     │  │  JSON / SQLite   │
│ + hot-plug    │  │ signed-manifest │  │  QSettings       │
└───────────────┘  └─────────────────┘  └──────────────────┘
        │
┌────────────────────────────────────────────────────────────┐
│                       Device backends                       │
│  Stream Docks (AKP03/05/153) ─► mirajazz Rust sidecar       │
│  AKP815 · keyboard (AK980) · mouse_aj  ─► custom C++ modules │
└────────────────────────────────────────────────────────────┘
```

The AKP03 / AKP05-N4 / AKP153 Stream Dock families are driven by an
out-of-process Rust sidecar built on [`mirajazz`](https://github.com/4ndv/mirajazz)
(JSON over stdio), proxied in by `SidecarStreamDockDevice`. The AKP815 (a
non-mirajazz 800×480-strip device), keyboards and mice keep custom in-tree C++
backends. Full design in [`docs/architecture/ARCHITECTURE.md`](docs/architecture/ARCHITECTURE.md).

______________________________________________________________________

## Python plugins

Plugins are pure Python packages loaded by the embedded interpreter, each isolated
in its own sandboxed child process. Minimal example:

```python
# ~/.local/share/ajazz-control-center/plugins/hello/plugin.py
from ajazz import Plugin, action

class HelloPlugin(Plugin):
    id = "com.example.hello"
    name = "Hello world"

    @action(id="say-hi", label="Say hi")
    def say_hi(self, ctx):
        ctx.notify("Hello from Python!")
```

Full API in [`docs/guides/PLUGIN_DEVELOPMENT.md`](docs/guides/PLUGIN_DEVELOPMENT.md).

______________________________________________________________________

## Build from source

```bash
git clone https://github.com/Aiacos/ajazz-control-center.git
cd ajazz-control-center
make bootstrap     # detects your OS, installs deps + udev rule, builds
make run           # launches the app
```

Or drive CMake directly via the per-platform presets in
[`CMakePresets.json`](CMakePresets.json) (requires Qt 6.7+ with `qtwebsockets`):

```bash
cmake --preset linux-release          # also: macos-release / windows-release
cmake --build --preset linux-release
ctest --preset linux-release --output-on-failure
```

The full reference — every preset, `-D` option and CPack generator — lives in
[`docs/guides/BUILDING.md`](docs/guides/BUILDING.md). Protocol work follows a
documented clean-room procedure: see
[`docs/protocols/REVERSE_ENGINEERING.md`](docs/protocols/REVERSE_ENGINEERING.md).

______________________________________________________________________

## Contributing

Pull requests, bug reports and new device captures are very welcome. Read
[`CONTRIBUTING.md`](CONTRIBUTING.md), [`GUIDELINES.md`](GUIDELINES.md) and
[`docs/guides/ADDING_A_DEVICE.md`](docs/guides/ADDING_A_DEVICE.md) before you start.

## License

GPL-3.0-or-later. AJAZZ Control Center is a clean-room implementation and is not
affiliated with, endorsed by, or sponsored by AJAZZ, Mirabox, or Elgato. It stands
on the work of the reverse-engineering community — including
[OpenDeck](https://github.com/nekename/OpenDeck),
[`elgato-streamdeck`](https://github.com/OpenActionAPI/rust-elgato-streamdeck),
[`mirajazz`](https://crates.io/crates/mirajazz),
[`opendeck-akp03`](https://github.com/4ndv/opendeck-akp03) and
[`ajazz-aj199-official-software`](https://github.com/progzone122/ajazz-aj199-official-software) —
none of whose code is vendored.
