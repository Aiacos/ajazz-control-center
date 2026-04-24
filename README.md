# AJAZZ Control Center

[![CI](https://github.com/Aiacos/ajazz-control-center/actions/workflows/ci.yml/badge.svg)](https://github.com/Aiacos/ajazz-control-center/actions/workflows/ci.yml)
[![Release](https://github.com/Aiacos/ajazz-control-center/actions/workflows/release.yml/badge.svg)](https://github.com/Aiacos/ajazz-control-center/actions/workflows/release.yml)
[![License](https://img.shields.io/badge/license-GPL--3.0-blue)](LICENSE)
[![Qt 6.7+](https://img.shields.io/badge/Qt-6.7%2B-41CD52?logo=qt)](https://www.qt.io/)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-00599C?logo=cplusplus)](https://isocpp.org/)
[![Python 3.11+](https://img.shields.io/badge/Python-3.11%2B-3776AB?logo=python)](https://www.python.org/)

A modern, open, cross-platform control center for AJAZZ devices — stream decks, keyboards and mice — with a clean Qt 6 / QML UI and a Python plugin system for scripting, automation and third-party integrations.

> **Status:** early alpha. Scaffolding, architecture and CI are in place. Device backends are under active development.

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

| Family       | Device                               | Status       | Protocol source                    |
|--------------|--------------------------------------|--------------|------------------------------------|
| Stream deck  | AJAZZ AKP153 / Mirabox HSV293S       | 🟡 planned   | USB capture + published notes      |
| Stream deck  | AJAZZ AKP153E (China variant)        | 🟡 planned   | USB capture                        |
| Stream deck  | AJAZZ AKP03 / Mirabox N3             | 🟡 planned   | USB capture                        |
| Stream deck  | AJAZZ AKP05 / AKP05E (knobs)         | 🟠 research  | USB capture required               |
| Stream deck  | AJAZZ AKP815                         | 🟠 research  | USB capture required               |
| Keyboards    | AJAZZ AK series (QMK/VIA-compatible) | 🟡 planned   | VIA JSON layer                     |
| Keyboards    | AJAZZ AK series (proprietary)        | 🟠 research  | USB capture required               |
| Mice         | AJAZZ AJ199 / AJ159 / AJ series      | 🟠 research  | USB capture + manufacturer utils   |

Legend: 🟢 working · 🟡 in progress · 🟠 research phase

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

## Building from source

### Prerequisites

| Platform | Tools                                                                |
|----------|----------------------------------------------------------------------|
| Linux    | GCC 13+ or Clang 17+, CMake 3.28+, Qt 6.7+, Python 3.11+, libudev, libusb-1.0, libhidapi |
| Windows  | Visual Studio 2022, CMake 3.28+, Qt 6.7+, Python 3.11+               |
| macOS    | Xcode 15+, CMake 3.28+, Qt 6.7+, Python 3.11+                        |

### Quick start

```bash
git clone git@github.com:Aiacos/ajazz-control-center.git
cd ajazz-control-center
cmake --preset linux-release        # or windows-release / macos-release
cmake --build --preset linux-release
ctest --preset linux-release
```

Detailed platform-specific instructions are in [`docs/guides/BUILDING.md`](docs/guides/BUILDING.md).

### Linux permissions

AJAZZ devices need udev rules to be accessible without root. After the first build, install them:

```bash
sudo install -m 644 resources/udev/99-ajazz.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules && sudo udevadm trigger
```

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
