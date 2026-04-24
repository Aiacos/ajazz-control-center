# AJAZZ Control Center

[![CI](https://img.shields.io/github/actions/workflow/status/Aiacos/ajazz-control-center/ci.yml?branch=main&label=CI&logo=github)](https://github.com/Aiacos/ajazz-control-center/actions/workflows/ci.yml)
[![Lint](https://img.shields.io/github/actions/workflow/status/Aiacos/ajazz-control-center/lint.yml?branch=main&label=lint&logo=github)](https://github.com/Aiacos/ajazz-control-center/actions/workflows/lint.yml)
[![Release](https://img.shields.io/github/v/release/Aiacos/ajazz-control-center?include_prereleases&logo=github&color=blueviolet)](https://github.com/Aiacos/ajazz-control-center/releases)
[![License](https://img.shields.io/badge/license-GPL--3.0-blue)](LICENSE)
[![Qt 6.7+](https://img.shields.io/badge/Qt-6.7%2B-41CD52?logo=qt)](https://www.qt.io/)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-00599C?logo=cplusplus)](https://isocpp.org/)
[![Python 3.11+](https://img.shields.io/badge/Python-3.11%2B-3776AB?logo=python)](https://www.python.org/)
[![pre-commit](https://img.shields.io/badge/pre--commit-enabled-brightgreen?logo=pre-commit)](https://pre-commit.com/)

A modern, open, cross-platform control center for AJAZZ devices — stream decks, keyboards and mice — with a clean Qt 6 / QML UI and a Python plugin system for scripting, automation and third-party integrations.

> **Status:** early alpha. Scaffolding, architecture and CI are in place. Device backends are under active development.

<!-- BEGIN AUTOGEN: stats -->
**10 devices** across 2 keyboard, 4 mouse, 4 streamdeck — 9 functional, 1 scaffolded.
<!-- END AUTOGEN: stats -->

---

## Why another tool?

AJAZZ (and its OEM partner Mirabox) ships device-specific Windows-only utilities that rarely see updates, do not run on Linux or macOS, and cannot be scripted. The community has produced several excellent per-device projects — [OpenDeck](https://github.com/nekename/OpenDeck), [`elgato-streamdeck`](https://github.com/OpenActionAPI/rust-elgato-streamdeck), [`mirajazz`](https://crates.io/crates/mirajazz), [`opendeck-akp03`](https://github.com/4ndv/opendeck-akp03), [`opendeck-akp153`](https://github.com/4ndv/opendeck-akp153), [`ajazz-sdk`](https://github.com/mishamyrt/ajazz-sdk), [`ajazz-aj199-official-software`](https://github.com/progzone122/ajazz-aj199-official-software) — but each covers only a narrow subset of the hardware catalog.

**AJAZZ Control Center** unifies these efforts under one roof:

- A single desktop application on **Linux, Windows and macOS**.
- **Modular device backends**: each product family (stream deck, keyboard, mouse) is an independent C++ module loaded at runtime.
- **Hybrid Qt 6 stack**: performance-critical code in C++20, extensibility in Python 3.11+ via an embedded interpreter.
- **Legally clean-room** approach: protocols are documented from USB captures and reimplemented in-house; open-source references are cited but not vendored.
- **First-class developer experience**: CMake presets, CI matrix on three OSes, packaged releases.

## Supported (and planned) devices

<!--
  The tables below are generated from `docs/_data/devices.yaml`.
  Do NOT edit them by hand — run `make docs` (or let pre-commit/CI do it).
-->

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

### Mice

| Device | USB | Status | Features | Notes |
|--------|-----|--------|----------|-------|
| [AJAZZ AJ159](docs/protocols/mouse/aj_series.md) | `0x3554:0xf51a` | 🟢 functional | RGB backlight, DPI stages, Firmware version | PAW3395 sensor. |
| [AJAZZ AJ199](docs/protocols/mouse/aj_series.md) | `0x3554:0xf51b` | 🟢 functional | RGB backlight, DPI stages, Firmware version | PAW3395 sensor. |
| [AJAZZ AJ339](docs/protocols/mouse/aj_series.md) | `0x3554:0xf51c` | 🟢 functional | RGB backlight, DPI stages, Firmware version | PAW3395 sensor, wireless variant pending. |
| [AJAZZ AJ380](docs/protocols/mouse/aj_series.md) | `0x3554:0xf51d` | 🟡 scaffolded | RGB backlight, DPI stages, Firmware version | PAW3950 sensor. |

<!-- END AUTOGEN: devices-by-family -->

<!-- BEGIN AUTOGEN: legend -->
🟢 **stable** — every vendor feature works · 🟢 **functional** — core I/O works (keys/LEDs) · 🟡 **scaffolded** — enumerated, protocol being mapped · 🟠 **requested** — captures wanted
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
│   Device Core (C++)  │   │  Python Plugin Host │   │   Persistence (C++) │
│  HID + USB + hidapi  │   │  pybind11 embedded  │   │   JSON / SQLite     │
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

```bash
git clone https://github.com/Aiacos/ajazz-control-center.git
cd ajazz-control-center
make bootstrap          # installs deps + udev rule + builds
make run                # launches the app
```

`make bootstrap` detects Fedora / RHEL / openSUSE / Debian / Ubuntu /
Arch / macOS and installs every build dependency via the native package
manager. After that, `make build` / `make test` / `make package` do the
obvious thing. Run `make help` for the full list.

Prefer pure CMake? `cmake --preset dev && cmake --build --preset dev`
works too. Full reference in [`docs/guides/BUILDING.md`](docs/guides/BUILDING.md).

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

Pull requests, bug reports and new device captures are very welcome. Read [`CONTRIBUTING.md`](CONTRIBUTING.md) and [`docs/guides/ADDING_A_DEVICE.md`](docs/guides/ADDING_A_DEVICE.md) before you start.

## License

GPL-3.0-or-later. AJAZZ Control Center is a clean-room implementation and is not affiliated with, endorsed by, or sponsored by AJAZZ, Mirabox, or Elgato.

## Acknowledgements

This project stands on the shoulders of the reverse-engineering community, in particular the authors of [OpenDeck](https://github.com/nekename/OpenDeck), [`elgato-streamdeck`](https://github.com/OpenActionAPI/rust-elgato-streamdeck), [`mirajazz`](https://crates.io/crates/mirajazz), [ZCube's AKP153 protocol notes](https://gist.github.com/ZCube/430fab6039899eaa0e18367f60d36b3c), [Den Delimarsky's Stream Deck Plus write-up](https://den.dev/blog/reverse-engineer-stream-deck-plus/), [`opendeck-akp03`](https://github.com/4ndv/opendeck-akp03) and [`ajazz-aj199-official-software`](https://github.com/progzone122/ajazz-aj199-official-software). None of their code is vendored; all protocol knowledge was re-derived from captures and their public notes.
