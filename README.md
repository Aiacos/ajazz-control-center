<p align="center">
  <img src="resources/icons/hicolor/app-256.png" alt="AJAZZ Control Center icon" width="160" height="160">
</p>

<h1 align="center">AJAZZ Control Center</h1>

[![CI](https://img.shields.io/github/actions/workflow/status/Aiacos/ajazz-control-center/ci.yml?branch=main&label=CI&logo=github)](https://github.com/Aiacos/ajazz-control-center/actions/workflows/ci.yml)
[![Lint](https://img.shields.io/github/actions/workflow/status/Aiacos/ajazz-control-center/lint.yml?branch=main&label=lint&logo=github)](https://github.com/Aiacos/ajazz-control-center/actions/workflows/lint.yml)
[![Nightly](https://img.shields.io/github/actions/workflow/status/Aiacos/ajazz-control-center/nightly.yml?branch=main&label=nightly&logo=github)](https://github.com/Aiacos/ajazz-control-center/releases/tag/nightly)
[![Release](https://img.shields.io/github/v/release/Aiacos/ajazz-control-center?include_prereleases&logo=github&color=blueviolet)](https://github.com/Aiacos/ajazz-control-center/releases)
[![License](https://img.shields.io/badge/license-GPL--3.0-blue)](LICENSE)
[![Qt 6.7+](https://img.shields.io/badge/Qt-6.7%2B-41CD52?logo=qt)](https://www.qt.io/)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-00599C?logo=cplusplus)](https://isocpp.org/)
[![Python 3.11+](https://img.shields.io/badge/Python-3.11%2B-3776AB?logo=python)](https://www.python.org/)
[![pre-commit](https://img.shields.io/badge/pre--commit-enabled-brightgreen?logo=pre-commit)](https://pre-commit.com/)

<p align="center">A modern, open, cross-platform control center for AJAZZ devices — Stream Dock macropads, keyboards and mice — with a clean Qt 6 / QML UI and a Python plugin system for scripting, automation and third-party integrations.</p>

<p align="center">
  <img alt="AJAZZ Control Center — Plugin Store with three connected devices in the sidebar" src="docs/screenshots/main-dark.png" width="900">
</p>

> **Status:** alpha. Hot-plug detection live with three device families connected today (Stream Dock, AK980 PRO, 2.4G 8K). Out-of-process sandboxed Python plugin host shipped. In-app Plugin Store with real one-click install + on-disk extraction (via QZipReader) mirroring the official AJAZZ Streamdock catalogue (~160 plugins) and the OpenDeck archive (~320 plugins). Cross-platform autostart + native notifications on Linux/macOS/Windows. AK980 PRO 20-mode firmware RGB picker (5-packet envelope with `CMD_FINISH 0xF0`) + AJ-series mouse TFT clock+DPI face + AK980 PRO settings-batch wire surface all landed 2026-05-18.

## Recent highlights

### What's working today

- **Hot-plug detection (2026-05-13).** The sidebar surfaces only currently-connected devices; reconnects rebind silently. Driven by `HotplugMonitor` on Linux udev, `WM_DEVICECHANGE` on Win32, and IOKit notifications on macOS.
- **Out-of-process sandboxed Python plugin host (SEC-003).** Each plugin runs in a child process isolated by the platform's native sandbox (`bwrap` on Linux, `sandbox-exec` on macOS, AppContainer on Windows). The host gates loading on a signed-manifest verification against bundled trust roots.
- **QML singleton invariant locked at build-time.** `QML_SINGLETON` services co-locate a `static_assert(!std::is_default_constructible_v<T>)` next to the class declaration — the long-standing dual-instance pattern (silent duplicate from Qt 6 SFINAE) becomes a compile error rather than a runtime mystery.
- **In-app Plugin Store with real one-click install + on-disk extraction (2026-05-18).** Live mirrors of the AJAZZ Streamdock (~160 plugins) + OpenDeck (~320 plugins) catalogues, browsable through tabbed search. `Install` fires an HTTPS GET against the upstream CDN (`cdn1.key123.vip` for Streamdock), shows inline download progress in the tile, saves the `.sdPlugin` archive under the per-user plugins directory (`AppDataLocation/plugins/`), **extracts it in place to an `<id>.sdPlugin/` directory tree via QZipReader** (single-folder wrappers are stripped automatically), and flips the row to `Installed`. A first-launch sweep also rounds up any legacy archive files left by older installs. Entries without a direct download URL gracefully fall back to opening the upstream catalogue in the system browser.
- **VIA / QMK keyboard support.** Any VIA layout JSON works out of the box; the proprietary backend covers AK-series RGB + macros + layers.
- **AK980 PRO 20-mode firmware RGB picker (2026-05-18).** The RGB tab exposes the keyboard's twenty vendor-built-in lighting effects (Static / Glittering / Rainbow blanket / Dynamic breath / Spectrum rings / Rolling glow / Rotating accents / Press burst / Launch trail / Ripples / etc.) via the new `IFirmwareLightingCapable` capability + `LightingService` QML singleton. Picking a mode sends the **five-packet** `0x18 → 0x13 → DATA → 0x02 → 0xF0` envelope to the device — `CMD_FINISH 0xF0` was added per vendor §13.7 so non-RTC commits match the reference behaviour exactly.
- **AK980 PRO settings batch C++ surface (2026-05-18).** New `ISettingsCapable` capability lets the host commit Fn-layer / sleep-timer / key-response-time in one shot via opcode `0x07 0x10`. Wire format pinned by byte-level tests; QML row pending — exposed today through the C++ device API for Python plugins and scriptable workflows.
- **AJ-series mouse TFT basetta clock + DPI face (2026-05-18).** The wireless dock LCD on the 2.4G 8K and AJ199 family now shows the host time + active DPI value. Host-renders a Format_RGB16 face, slices into 55-byte chunks, and uploads via opcode `0x25` (`FEA_CMD_SETTFTLCDDATA`). Driven by the same `IClockCapable` surface as the keyboard.
- **Native desktop notifications + autostart on every OS (2026-05-18).** Notifications dispatch through `notify-send` on Linux, `osascript` on macOS, PowerShell BurntToast / Forms-balloon fallback on Windows. Autostart wires a `.desktop` (Linux), a LaunchAgent plist (macOS), or an `HKCU\…\Run` registry value (Windows). 8 of 13 cross-platform features at full parity.
- **15-minute periodic clock auto-sync (2026-05-18).** `TimeSyncService` runs a coarse-timer tick that re-pushes the host time to every connected `IClockCapable` device, so the AK980 PRO RTC and the mouse TFT face stay accurate across DST transitions and long sessions without user intervention.

### Coming next (v1.2 — active milestone)

v1.0 (Phases 1-2) shipped 2026-05-13 and v1.1 (Phases 3-8) shipped 2026-05-14. v1.2 ("Connected-Device Capability Parity") is in flight — see [`.planning/ROADMAP.md`](.planning/ROADMAP.md) and [`.planning/STATE.md`](.planning/STATE.md) for live status.

- **Phase 9.x captures (gating)** — sanitised Wireshark/usbmon captures for the 4 connected devices + ratify ARCH-04/05/06 from default verdict to FINAL. Requires the developer to physically interact with the devices; runbook in [`docs/protocols/CAPTURING.md`](docs/protocols/CAPTURING.md).
- **Phase 10 — AKP03 `0x3004` promotion** — one-line `PacketSize 512→1024` fix unblocks 13 sibling SKUs; real `setKeyImage` / encoder / brightness wired to the LCD. ARCH-04 host-side image pipeline already landed (2026-05-17).
- **Phase 11 — AJAZZ 2.4G 8K mouse probe-and-confirm** — zero-OSS-corpus session on `3151:5007`: DPI cycle, polling-rate, LOD, per-zone RGB. Mouse OLED basetta clock+DPI face already landed (2026-05-18).
- **Phase 12 — AK980 PRO promotion** — sleep-timer, brightness/speed/direction, host-save-vs-flash UX separation. Firmware RTC (ARCH-05.1) + 20-mode RGB picker already landed (2026-05-17 / 2026-05-18).
- **Phase 13 — catalogue + v1.1 UI verifies back-fill** — `microdia_dongle_7016` at `probed`, real-hardware visual verifies for the Sync button / Settings auto-sync / glyph-only-no-toast / MaturityRole tooltip.

### Open work (where to look)

- **[`TODO.md`](TODO.md)** — living checklist sorted by effort (quick wins, medium fixes, multi-day refactors). Includes the cleanup backlog (dead code, magic numbers, consistency).
- **[GitHub issues](https://github.com/Aiacos/ajazz-control-center/issues)** — 10 open as of 2026-05-18 (down from 13 earlier today after #58 / #62 closed and #57 C++ surface landed): 5 Phase 3 follow-ups (AK980 PRO settings batch QML row, AKP05 9 host-side effects port, AK980 macro record/assign, plugin store bundling, profile auto-switch runtime) and 5 product-roadmap items (OpenRGB adapter, OBS plugins, live key remap editor, profile auto-switch on focused app, i18n sweep).
- **[`.planning/HANDOFF-2026-05-18.md`](.planning/HANDOFF-2026-05-18.md)** — snapshot of this development cycle (CI hardening + audits 1/2/3 + audit-3 user-facing landings) with explicit remaining-gaps punch list.

## Try it now

Don't want to build from source? Pre-built installers for every push to `main` are published as a rolling pre-release:

👉 **[Download the latest nightly](https://github.com/Aiacos/ajazz-control-center/releases/tag/nightly)** — `.deb`, `.rpm`, `.flatpak` (Linux), `.msi` / portable `.zip` (Windows), universal `.dmg` (macOS).

For stable, signed, slow-moving builds, see the [tagged releases](https://github.com/Aiacos/ajazz-control-center/releases). The release process is documented in the [Release Process wiki page](docs/wiki/Release-Process.md).

<!-- BEGIN AUTOGEN: stats -->
**31 devices** across 1 dongle, 3 keyboard, 10 mouse, 17 streamdeck — 10 functional, 16 scaffolded, 2 probed, 3 partial.
<!-- END AUTOGEN: stats -->

______________________________________________________________________

## Why another tool?

AJAZZ (and its OEM partner Mirabox) ships device-specific Windows-only utilities that rarely see updates, do not run on Linux or macOS, and cannot be scripted. The community has produced several excellent per-device projects — [OpenDeck](https://github.com/nekename/OpenDeck), [`elgato-streamdeck`](https://github.com/OpenActionAPI/rust-elgato-streamdeck), [`mirajazz`](https://crates.io/crates/mirajazz), [`opendeck-akp03`](https://github.com/4ndv/opendeck-akp03), [`opendeck-akp153`](https://github.com/4ndv/opendeck-akp153), [`ajazz-sdk`](https://github.com/mishamyrt/ajazz-sdk), [`ajazz-aj199-official-software`](https://github.com/progzone122/ajazz-aj199-official-software) — but each covers only a narrow subset of the hardware catalog.

**AJAZZ Control Center** unifies these efforts under one roof:

- A single desktop application on **Linux, Windows and macOS**.
- **Modular device backends**: each product family (Stream Dock macropad, keyboard, mouse) is an independent C++ module loaded at runtime.
- **Hybrid Qt 6 stack**: performance-critical code in C++20, extensibility in Python 3.11+ via an embedded interpreter.
- **Legally clean-room** approach: protocols are documented from USB captures and reimplemented in-house; open-source references are cited but not vendored.
- **First-class developer experience**: CMake presets, CI matrix on three OSes, packaged releases.

## Supported (and planned) devices

<!--
  The tables below are generated from `docs/_data/devices.yaml`.
  Do NOT edit them by hand — run `make docs` (or let pre-commit/CI do it).
-->

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
| [AJAZZ Stream Dock (PID 0x3004)](docs/protocols/streamdeck/akp03.md) | `0x0300:0x3004` | 🟡 scaffolded | Per-key display, Encoder / dial, Host-settable clock (scaffolded) | Hot-plug capture 2026-05-13 surfaced this PID as 'Ajazz HOTSPOTEKUSB HID DEMO'. Routed through the AKP03 factory; exact retail SKU to confirm against vendor manifests. |

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
| [AJAZZ 2.4G 8K](docs/protocols/mouse/aj_series_vendor.md) | `0x3151:0x5007` | 🟠 partial | DPI stages, RGB backlight, Per-key display, Host-settable clock (scaffolded) | ✓ Descriptor + factory wired in register.cpp (3151:5007); P0 safety guard: damaging commit() helper (opcode 0x50) removed; no longer corrupts button slot 0 · ✗ Wire-format rewrite per docs/protocols/mouse/aj_series_vendor.md (§11.5 roadmap, ~600 LOC code + ~400 LOC test, largest single commit in upcoming batch); Real-device round-trip witness once rewrite lands (promotes scaffolded → partial); 8 onboard profiles, 8 DPI stages (not 6), 20 macros, OTA, TFT LCD widgets — all v1.3+ deferred · 8KHz-polling wireless mouse on SONiX VID prefix. Has TFT LCD basetta showing clock + DPI (2026-05-18 implementation: opcode 0x25 SETTFTLCDDATA chunked RGB565 with host-side QImage renderer, wired to IClockCapable so TimeSyncService drives it). Wire-format rewrite landed in P3.12.1/.2 (656fb1c, 0e9bb04). |
| [AJAZZ AJ199 family (wired)](docs/protocols/mouse/aj_series.md) | `0x3554:0xF500` | 🟡 scaffolded | RGB backlight, DPI stages, Firmware version, Per-key display, Host-settable clock (scaffolded) | AJ199 / AJ199 Max / AJ199 Carbon Fiber. Wired-mode primary PID per AJ199 Max `Config.ini` `M_PID` (`F500`). Has TFT LCD basetta wired to the shared 0x25 chunked clock+DPI renderer (2026-05-18). AJ199 Max wire format is structurally different from AJ199 V1.0 (offset-based struct vs flat report) — see `vendor-protocol-notes.md` Finding 11.B. |
| [AJAZZ AJ199 family (2.4GHz dongle)](docs/protocols/mouse/aj_series.md) | `0x3554:0xF501` | 🟡 scaffolded | RGB backlight, DPI stages, Firmware version, Per-key display, Host-settable clock (scaffolded) | AJ199 family in 2.4GHz mode. Vendor `D_PID` list is `F501,F564,F567,F545,F547,F5D5` — six dongle-mode PIDs distinguishing variant SKUs at the USB layer. Has TFT LCD basetta wired to the shared 0x25 chunked clock+DPI renderer (2026-05-18). |

### Dongles

| Device | USB | Status | Features | Notes |
|--------|-----|--------|----------|-------|
| [AJAZZ AK980 PRO 2.4GHz USB Receiver](docs/protocols/keyboard/proprietary.md) | `0x0c45:0x7016` | 🔵 probed |  | ✓ Identified via HID report-descriptor parsing on dev box hardware (2026-05-15); Topology evidence cited inline (Phase 9 ARCH-06 cross-reference) · ✗ Runtime entry in `src/devices/keyboard/src/register.cpp` (or new `src/devices/dongle/`) — this is catalog-only for now; sidebar visibility requires register.cpp + `DeviceFamily::Dongle` enum addition; Captures-confirmation 2-minute physical unplug test (unplug `ak980pro` USB-C and verify the dongle does NOT simultaneously disappear) — Phase 9.x deferred · 2.4GHz USB receiver dongle paired with AJAZZ AK980 PRO (`ak980pro`, VID:PID 0c45:8009). Identified 2026-05-15 via HID report-descriptor parsing on real hardware: Interface 0 advertises a 6KRO boot keyboard + 64-byte vendor Feature Report on Consumer Page — the exact control-channel signature that TaxMachine documented for AK820 Pro / AK980 PRO at the wired VID:PID. Interface 1 is a 4-in-1 composite (mouse + system + consumer/media + 120-key NKRO keyboard) typical of SONiX wireless mech-keyboard receivers. When the host has both the USB-C wired connection AND the 2.4GHz dongle active, the same physical keyboard enumerates twice — once as `ak980pro` (wired path) and once via this dongle (RF-tunneled). No standalone capabilities advertised; the dongle is a transport, not a device — capability work belongs on the `ak980pro` codename and applies regardless of connection path. ARCH-06 default verdict (Phase 9 partial-scope, 2026-05-15) ratified composite-HID dedup NOT firing — topology (Full-Speed 12Mbps vs the AK980 PRO's High-Speed 480Mbps, separate USB bus branch) refutes the composite-interface hypothesis. |

<!-- END AUTOGEN: devices-by-family -->

<!-- BEGIN AUTOGEN: legend -->
🟡 **scaffolded** — descriptor + factory exist; backend compiles but does not exercise the device · 🔵 **probed** — device enumerates and descriptor populated; no protocol writes confirmed · 🟠 **partial** — some features work end-to-end; advertised capability set incomplete or untested · 🟢 **functional** — all advertised capabilities work in practice; tested manually or in CI · ✅ **verified** — functional + automated CI on real hardware OR sustained user-confirmed reliability
<!-- END AUTOGEN: legend -->

## Architecture at a glance

```
┌──────────────────────────────────────────────────────────────────────────┐
│                          Qt 6 / QML Desktop UI                           │
│        (device browser · profile editor · button/key designer)           │
└──────────────────────────────────────────────────────────────────────────┘
                                     │
┌──────────────────────────────────────────────────────────────────────────┐
│                         Application Layer (C++20)                        │
│     Profile engine · Action dispatcher · Plugin host · Event bus         │
└──────────────────────────────────────────────────────────────────────────┘
              │                           │                   │
┌──────────────────────┐   ┌─────────────────────┐   ┌─────────────────────┐
│   Device Core (C++)  │   │  OOP Plugin Host    │   │   Persistence (C++) │
│  HID + USB + hidapi  │   │  bwrap/sb-exec/AC   │   │   JSON / SQLite     │
│  + Hot-plug monitor  │   │  signed-mfst gate   │   │   QSettings         │
└──────────────────────┘   └─────────────────────┘   └─────────────────────┘
              │
┌──────────────────────────────────────────────────────────────────────────┐
│                      Device Modules (C++, plug-in)                       │
│      streamdeck_akp153 · streamdeck_akp03 · keyboard_via · mouse_aj      │
└──────────────────────────────────────────────────────────────────────────┘
```

See [`docs/architecture/ARCHITECTURE.md`](docs/architecture/ARCHITECTURE.md) for the full design.

## Install (end users)

One command, any platform. No group membership, no logout, no replug.

### Linux / macOS

```bash
curl -fsSL https://raw.githubusercontent.com/Aiacos/ajazz-control-center/main/scripts/install.sh | bash
```

The installer auto-detects your distro and uses the native package
manager (Flatpak / `dnf` / `apt` / Homebrew). On Linux it installs a
udev rule that uses `TAG+="uaccess"` so systemd-logind grants your user
device access automatically — no `plugdev` group, no logout, no replug.

### Windows

```powershell
winget install Aiacos.AjazzControlCenter
```

Or grab the `.msi` from the [latest release](https://github.com/Aiacos/ajazz-control-center/releases/latest).

### Manual downloads

Every release on the [Releases page](https://github.com/Aiacos/ajazz-control-center/releases)
ships `.deb`, `.rpm`, `.flatpak`, `.msi` and a universal `.dmg`.

## Build from source (developers)

### Happy path — `make bootstrap`

```bash
git clone https://github.com/Aiacos/ajazz-control-center.git
cd ajazz-control-center
make bootstrap          # installs deps + udev rule + builds
make run                # launches the app
```

`make bootstrap` detects Fedora / RHEL / openSUSE / Debian / Ubuntu /
Arch / macOS and installs every build dependency via the native package
manager. After that, `make build` / `make test` / `make package` /
`make doctor` do the obvious thing. Run `make help` for the full list.

### Per-platform CMake recipes

If you'd rather drive CMake directly (Windows, custom toolchain, IDE
integration), each platform has a dedicated preset already wired in
[`CMakePresets.json`](CMakePresets.json). All recipes assume Qt 6.7+
with `qtwebsockets` and `qtshadertools` modules installed.

#### Linux (Ubuntu 24.04 / Fedora 40+ / Arch)

```bash
# 1. Install system deps (Ubuntu/Debian)
sudo apt install -y build-essential cmake ninja-build pkg-config \
    libudev-dev libusb-1.0-0-dev libhidapi-dev \
    libgl1-mesa-dev libxkbcommon-dev libxcb1-dev libxcb-cursor-dev \
    libxkbcommon-x11-dev python3-dev python3-pip

#    Fedora/RHEL equivalent: sudo dnf install gcc-c++ cmake ninja-build \
#    systemd-devel libusb1-devel hidapi-devel mesa-libGL-devel libxkbcommon-devel

# 2. Install Qt 6.7+ via aqtinstall (cross-distro) OR your distro's qt6-base
pip install aqtinstall
aqt install-qt linux desktop 6.8.3 gcc_64 -m qtwebsockets qtshadertools

# 3. Configure + build (Release)
cmake --preset linux-release
cmake --build --preset linux-release

# 4. Test
ctest --preset linux-release --output-on-failure

# 5. Package (.deb / .rpm / .flatpak)
cmake --build --preset linux-release --target package    # CPack → .deb + .rpm
make flatpak                                              # → .flatpak (uses flatpak-builder)
```

The udev rule at `resources/linux/99-ajazz.rules` uses `TAG+="uaccess"`
so systemd-logind grants your user device access automatically — no
`plugdev` group, no logout, no replug. `make udev` installs it without
a full build.

#### macOS (14+, Apple Silicon or Intel)

```bash
# 1. Install Xcode CLT + Homebrew deps
xcode-select --install
brew install cmake ninja hidapi pkg-config

# 2. Install Qt 6.7+ (universal binary)
brew install qt@6
#    or:  aqt install-qt mac desktop 6.8.3 clang_64 -m qtwebsockets qtshadertools

# 3. Configure + build
cmake --preset macos-release
cmake --build --preset macos-release

# 4. Test
ctest --preset macos-release --output-on-failure

# 5. Package universal .dmg (arm64 + x86_64)
cmake --build --preset macos-release --target package
```

The release pipeline produces a universal binary via two single-arch
builds + `lipo`; CI runs on `macos-14` (Apple Silicon) and `macos-13`
(Intel) and merges. For local development a single-arch build is fine.

#### Windows (10/11, MSVC 2022)

```powershell
# 1. Open "x64 Native Tools Command Prompt for VS 2022" so cl.exe is on PATH
# 2. Install Qt 6.7+ (via the official online installer's MaintenanceTool,
#    OR via aqtinstall)
pip install aqtinstall
aqt install-qt windows desktop 6.8.3 win64_msvc2022_64 -m qtwebsockets qtshadertools
$env:Path = "C:\Qt\6.8.3\msvc2022_64\bin;$env:Path"

# 3. Configure + build (Release)
cmake --preset windows-release
cmake --build --preset windows-release

# 4. Test
ctest --preset windows-release --output-on-failure

# 5. Package .msi + portable .zip
cmake --build --preset windows-release --target package    # CPack → WiX .msi
```

**Qt 6.11.1 dev-box gotcha**: Qt 6.11 headers emit `C4702` (unreachable
code) warnings that fail `/WX`. Until upstream fixes it or our matrix
adds `/wd4702`, configure with `-DAJAZZ_ENABLE_WERROR=OFF` for local
Qt 6.11+ builds. CI uses Qt 6.8.3 where the warnings don't fire, so
`/WX` stays on.

### Useful debug builds

```bash
cmake --preset dev                                    # Debug + sanitizers
cmake --build --preset dev
cmake --preset coverage                               # gcov instrumentation
ctest --test-dir build-cov && lcov --capture ...      # see Makefile `make coverage`
```

The full reference (every preset, every `-D` option, every CPack
generator) lives in [`docs/guides/BUILDING.md`](docs/guides/BUILDING.md).

### Platform support matrix

<!-- BEGIN AUTOGEN: platform-matrix -->
| Platform | Build | Install | Notes |
|----------|-------|---------|-------|
| Linux | ✅ first-class | .rpm / .deb / .flatpak | udev `TAG+=uaccess` — no group, no logout |
| macOS | ✅ universal | .dmg (arm64 + x86_64) | Grant Input Monitoring on first launch |
| Windows | ✅ MSVC 2022 | .msi (winget) | No drivers required |
<!-- END AUTOGEN: platform-matrix -->

## Python plugins

Plugins are pure Python packages loaded by the embedded interpreter. Minimal example:

```python
# ~/.config/ajazz-control-center/plugins/hello/plugin.py
from ajazz import Plugin, action

class HelloPlugin(Plugin):
    id = "com.example.hello"
    name = "Hello world"

    @action(id="say-hi", label="Say hi")
    def say_hi(self, ctx):
        ctx.notify("Hello from Python!")
```

See [`docs/guides/PLUGIN_DEVELOPMENT.md`](docs/guides/PLUGIN_DEVELOPMENT.md) for the full plugin API.

## Reverse engineering methodology

All device protocol work follows a documented clean-room procedure (Wireshark + `usbmon` capture, protocol notes in `docs/protocols/`, in-house reimplementation). See [`docs/protocols/REVERSE_ENGINEERING.md`](docs/protocols/REVERSE_ENGINEERING.md).

## Contributing

Pull requests, bug reports and new device captures are very welcome. Read [`CONTRIBUTING.md`](CONTRIBUTING.md), [`GUIDELINES.md`](GUIDELINES.md) (project-wide engineering rules) and [`docs/guides/ADDING_A_DEVICE.md`](docs/guides/ADDING_A_DEVICE.md) before you start.

## License

GPL-3.0-or-later. AJAZZ Control Center is a clean-room implementation and is not affiliated with, endorsed by, or sponsored by AJAZZ, Mirabox, or Elgato.

## Acknowledgements

This project stands on the shoulders of the reverse-engineering community, in particular the authors of [OpenDeck](https://github.com/nekename/OpenDeck), [`elgato-streamdeck`](https://github.com/OpenActionAPI/rust-elgato-streamdeck), [`mirajazz`](https://crates.io/crates/mirajazz), [ZCube's AKP153 protocol notes](https://gist.github.com/ZCube/430fab6039899eaa0e18367f60d36b3c), [Den Delimarsky's Stream Deck Plus write-up](https://den.dev/blog/reverse-engineer-stream-deck-plus/), [`opendeck-akp03`](https://github.com/4ndv/opendeck-akp03) and [`ajazz-aj199-official-software`](https://github.com/progzone122/ajazz-aj199-official-software). None of their code is vendored; all protocol knowledge was re-derived from captures and their public notes.
