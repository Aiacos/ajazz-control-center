# Architecture

AJAZZ Control Center is a Qt 6 desktop application backed by a C++20 core library and an embedded Python 3 plugin host. This document is the entry-point — it gives a 10-minute overview and links to deeper documents on individual subsystems.

## Documents in this folder

| Topic                              | Document                             |
| ---------------------------------- | ------------------------------------ |
| 10-minute high-level overview      | This page                            |
| Threading and synchronization      | [THREADING.md](THREADING.md)         |
| Hot-plug + auto-discovery          | [HOTPLUG.md](HOTPLUG.md)             |
| Plugin system (Python)             | [PLUGIN-SYSTEM.md](PLUGIN-SYSTEM.md) |
| Protocol layering and capabilities | [PROTOCOLS.md](PROTOCOLS.md)         |
| Branding / white-labeling          | [BRANDING.md](BRANDING.md)           |
| Per-device wire protocols          | [`../protocols/`](../protocols/)     |

## Layered view

```
┌──────────────────────────────────────────────────────────────────────┐
│                          Qt 6 / QML UI                               │
│   DeviceList · ProfileEditor · KeyDesigner · RgbPicker · TrayIcon    │
└──────────────────────────────────────────────────────────────────────┘
                                  │  Q_PROPERTY / Q_INVOKABLE
┌──────────────────────────────────────────────────────────────────────┐
│                        Application Layer (C++)                       │
│   DeviceModel · ProfileController · ActionDispatcher · PluginHost    │
│   HotplugMonitor · BrandingService · TrayController                  │
└──────────────────────────────────────────────────────────────────────┘
            │                          │                │
┌───────────────────────┐  ┌───────────────────────┐  ┌──────────────────┐
│ Device Core           │  │ Python Plugin Host    │  │ Persistence      │
│ IDevice · IRegistry   │  │ pybind11 embedded     │  │ QSettings + JSON │
│ ITransport (HID/…)    │  │ `ajazz` runtime mod.  │  │                  │
└───────────────────────┘  └───────────────────────┘  └──────────────────┘
            │
┌──────────────────────────────────────────────────────────────────────┐
│                      Device Modules (C++, plug-in)                   │
│   streamdeck_akp153 · _akp03 · _akp05 · keyboard_via · mouse_aj      │
└──────────────────────────────────────────────────────────────────────┘
```

## Module boundaries

| Module              | Directory                | Depends on        | Public surface                                                            |
| ------------------- | ------------------------ | ----------------- | ------------------------------------------------------------------------- |
| `ajazz::core`       | `src/core`               | Qt6::Core, hidapi | `IDevice`, `IRegistry`, `ITransport`, capability mix-ins, profile, logger |
| `ajazz::streamdeck` | `src/devices/streamdeck` | `core`            | `registerAll()`, per-model factories                                      |
| `ajazz::keyboard`   | `src/devices/keyboard`   | `core`            | `registerAll()`, VIA + proprietary factories                              |
| `ajazz::mouse`      | `src/devices/mouse`      | `core`            | `registerAll()`, `makeAjSeries`                                           |
| `ajazz::plugins`    | `src/plugins`            | `core`, pybind11  | `PluginHost`, `ajazz` runtime module                                      |
| `ajazz::app`        | `src/app`                | all of the above  | Qt/QML entry point                                                        |

Modules communicate exclusively through the headers in `src/core/include/ajazz/core/`. No device backend includes headers from another; no UI code reaches into protocol internals; no plugin sees raw transports.

## Capability-based device model

Each concrete backend implements `IDevice` and opts into any subset of the capability interfaces in `capabilities.hpp`. The full catalog and contracts are documented in [PROTOCOLS.md](PROTOCOLS.md).

The UI and Python SDK discover capabilities at runtime via `dynamic_cast`. This lets a backend selectively grow — e.g. AKP05 initially implements `IDisplayCapable` + `IEncoderCapable` and later adds `IRgbCapable` without touching the UI.

## Data flow at a glance

### Input: key press → action

```
hidapi → HidTransport::read
       → <Backend>::poll → parseInputReport
       → EventCallback(DeviceEvent)
       → EventBus::publish
       → ProfileEngine::dispatch
       → PluginHost::dispatch ( action id → Python handler )
       → OS side-effect (notification, scene switch, shell command, …)
```

### Output: UI change → device update

```
QML → ProfileController::setKeyImage
    → DeviceController::device()->setKeyImage(...)
    → <Backend>::sendImage (encode, chunk, hidapi write)
```

## Profile engine

Profiles are JSON documents (schema in `docs/protocols/PROFILE_SCHEMA.md`) that map physical controls to action chains. Key design points:

- **Triggering** — per-application hints automatically switch the active profile when the OS focus changes.
- **Stacking** — temporary profiles can be pushed/popped for modal interactions (e.g. scene-switcher overlay).
- **Portability** — profiles are device-family agnostic at the schema level; device-specific fields live in typed sub-objects so profiles authored for one device can be partially reused on another.

## Error handling conventions

- Transport errors throw `std::runtime_error`. Device backends decide whether to close the handle or retry.
- Protocol decoders return `std::optional<T>` and never throw on bad input.
- Public UI code never throws; exceptions are converted to `QMessageBox`-friendly notifications at the controller layer.

## Why this architecture

- **Capability interfaces** let us grow device support incrementally without breaking the UI.
- **Clean-room protocol modules** keep reverse-engineered knowledge isolated and well-documented.
- **Embedded Python** gives power users scripting parity with the Stream Deck SDK without tying the core to a JS runtime.
- **Three-OS CI matrix from day one** avoids the "works on my Linux" drift common in hardware tooling.
- **Hot-plug + tray-first** UX mirrors what users expect from a "set it and forget it" device companion.
