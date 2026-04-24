# Architecture

AJAZZ Control Center is a Qt 6 desktop application backed by a C++20 core library and an embedded Python 3 plugin host. This document describes how the pieces fit together, what lives where, and which invariants the modules guarantee to their callers.

## Layered view

```
┌──────────────────────────────────────────────────────────────────────┐
│                          Qt 6 / QML UI                               │
│   DeviceList · ProfileEditor · KeyDesigner · RgbPicker · ...         │
└──────────────────────────────────────────────────────────────────────┘
                                  │  Q_PROPERTY / Q_INVOKABLE
┌──────────────────────────────────────────────────────────────────────┐
│                        Application Layer (C++)                       │
│   DeviceModel · ProfileController · ActionDispatcher · PluginHost    │
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

Modules talk to each other exclusively through the headers in `src/core/include/ajazz/core/`. No device backend includes headers from another; no UI code reaches into protocol internals; no plugin sees raw transports.

## Capability-based device model

Each concrete backend implements `IDevice` and opts into any subset of the capability interfaces in `capabilities.hpp`:

- `IDisplayCapable` — per-key LCDs and main screens.
- `IRgbCapable` — single-zone, multi-zone or per-LED RGB.
- `IEncoderCapable` — endless rotary encoders with optional screens.
- `IKeyRemappable` — QMK/VIA-style remapping plus macros.
- `IMouseCapable` — DPI stages, polling, lift-off, button bindings, battery.
- `IFirmwareCapable` — version query and update.

The UI and Python SDK discover capabilities at runtime via `dynamic_cast`. This lets one backend selectively grow — e.g. AKP05 initially implements `IDisplayCapable` + `IEncoderCapable` and adds `IRgbCapable` later without touching the UI.

## Threading model

- The **main thread** runs the Qt event loop and the QML UI. It never blocks on I/O.
- Each open device owns a **reader thread** started by `IDevice::open()`. It pumps HID input reports and emits `DeviceEvent`s through `EventCallback`. Callbacks must be cheap; they are forwarded to the main thread via `QMetaObject::invokeMethod(..., Qt::QueuedConnection)`.
- The **plugin host** holds the GIL while dispatching actions. Long-running Python work must spawn a Python thread or use `asyncio` — the host does not offload automatically.
- The **event bus** snapshots its subscriber list under a short lock and releases it before dispatch, so subscribers can (un)subscribe during handling without deadlocking.

## Data flow

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
    → <Backend>::sendImage (jpeg encode, chunk, hidapi write)
```

## Profile engine

Profiles are JSON documents (schema in `docs/protocols/../PROFILE_SCHEMA.md`) that map physical controls to action chains. Key design points:

- **Triggering** — per-application hints automatically switch the active profile when the OS focus changes.
- **Stacking** — temporary profiles can be pushed/popped for modal interactions (e.g. scene-switcher overlay).
- **Portability** — profiles are device-family agnostic at the schema level; device-specific fields live in typed sub-objects so profiles authored for one device can be partially reused on another.

## Python plugin host

- The interpreter is started once at app startup and lives for the process's lifetime.
- The host exposes the `ajazz` runtime module (written in C++ with pybind11) — plugins import it to get access to `Plugin`, `action`, and eventually `Device`, `RGB`, etc.
- Plugin directories scanned at startup:
  - Linux: `~/.config/ajazz-control-center/plugins`
  - Windows: `%APPDATA%\ajazz-control-center\plugins`
  - macOS: `~/Library/Application Support/ajazz-control-center/plugins`
- Plugin crashes are caught at the host boundary and surfaced in the UI, not propagated into the event loop.

## Error handling conventions

- Transport errors throw `std::runtime_error`. Device backends decide whether to close the handle or retry.
- Protocol decoders return `std::optional<T>` and never throw on bad input.
- Public UI code never throws; exceptions are converted to `QMessageBox`-friendly notifications at the controller layer.

## Why this architecture

- **Capability interfaces** let us grow device support incrementally without breaking the UI.
- **Clean-room protocol modules** keep reverse-engineered knowledge isolated and well-documented.
- **Embedded Python** gives power users scripting parity with the Stream Deck SDK without tying the core to a JS runtime.
- **Three-OS CI matrix from day one** avoids the "works on my Linux" drift common in hardware tooling.
