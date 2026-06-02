<!-- refreshed: 2026-06-02 -->

# Architecture

**Analysis Date:** 2026-06-02

## System Overview

```text
┌─────────────────────────────────────────────────────────────────────┐
│                         QML UI Layer                                │
│  Main.qml / DeviceView.qml / ProfileEditor.qml / PluginStore.qml   │
│              `src/app/qml/`                                         │
└────────────────────────┬────────────────────────────────────────────┘
                         │
┌─────────────────────────┴────────────────────────────────────────────┐
│                    Qt Application Layer                              │
├────────────────────────────────────────────────────────────────────┤
│                                                                    │
│ Application (main controller)  ← main.cpp (`src/app/src/`)       │
│ ├─ ProfileController (load/save)                                 │
│ ├─ DeviceModel (connected devices list)                          │
│ ├─ StreamDockControlService (render, brightness)                 │
│ ├─ StreamDockInputService (key/encoder/touch input dispatch)     │
│ ├─ PluginManager / SdPluginServer (.sdPlugin runtime host)       │
│ ├─ PluginDeviceBridge (plugin action ↔ device I/O)              │
│ ├─ DebugControlServer (opt-in JSON-RPC via Unix socket)         │
│ ├─ Services: ThemeService, BrandingService, LightingService,    │
│ │  TimeSyncService, BatteryService, SettingsService, ...        │
│ └─ TrayController (system tray icon + menu)                      │
│                                                                    │
│ DeviceRegistry (thread-safe; owns device factories)              │
│ └─ Opened devices (stream deck, keyboard, mouse)                 │
│                                                                    │
│ HotplugMonitor + debouncer (USB enumeration events)              │
│ RingBufferSink (in-memory log ring for debug channel)            │
│                                                                    │
└────────────────┬─────────────────────────────┬────────────────────┘
                 │                             │
┌────────────────┴─────────────┐   ┌───────────┴──────────────────────┐
│   Core Library Layer          │   │  Device Backend Modules          │
│  (Hardware-agnostic)          │   │  (Transport + Protocol)          │
│                               │   │                                  │
│ `src/core/include/` → API     │   │ `src/devices/*/`                │
│ • IDevice / ITransport        │   │                                  │
│ • IDisplayCapable (per-key)   │   │ Stream Deck family:             │
│ • IEncoderCapable             │   │ • AKP05/N4/AKP03/AKP153         │
│ • ITouchStripDisplayCapable   │   │   (proxied via sidecar)         │
│ • IRgbCapable                 │   │ • AKP815 (custom C++ backend)   │
│ • IBatteryCapable             │   │                                  │
│ • IClockCapable / ISettings   │   │ Keyboard: AK980 (custom)         │
│ • ActionEngine (key-press →   │   │ Mouse: AJ-series (custom)        │
│   plugin/sleep/url/command)   │   │                                  │
│ • Profile (device bindings +  │   │ All backends: HidTransport       │
│   pages + actions)            │   │ (libhidapi wrapper)              │
│                               │   │                                  │
└───────────────┬───────────────┘   └────────────────────────────────┘
                │
        ┌───────┴────────────────────┐
        │                            │
        │     Out-of-Process         │
        │    Rust Sidecar            │
        │                            │
        │  streamdock-host/          │
        │  (Mirajazz-based)          │
        │                            │
        │  AKP05/N4/AKP03/           │
        │  AKP153 device handling    │
        │  JSON over stdin/stdout    │
        │  (one persistent handle)   │
        │                            │
        └────────────────────────────┘
                │
        ┌───────┴─────────────┐
        │   Plugin Layer      │
        │                     │
        │ Out-of-Process      │
        │ Python Host         │
        │ (POSIX subprocess)  │
        │                     │
        │ .sdPlugin runtime   │
        │ (Node.js WebSocket) │
        │                     │
        └─────────────────────┘
```

## Component Responsibilities

| Component                     | Responsibility                                                                                      | File                                               |
| ----------------------------- | --------------------------------------------------------------------------------------------------- | -------------------------------------------------- |
| **Application**               | Top-level lifecycle; owns registry, device model, services; wires QML engine                        | `src/app/src/application.hpp/.cpp`                 |
| **DeviceRegistry**            | Maps USB VID/PID to device factories; thread-safe; injects into all `registerAll()` calls           | `src/core/include/ajazz/core/device_registry.hpp`  |
| **DeviceModel**               | Qt list model for the sidebar; emits device added/removed signals                                   | `src/app/src/device_model.hpp/.cpp`                |
| **ProfileController**         | Loads/saves profiles from disk; bridges QML ↔ `core::readProfileFromDisk`                           | `src/app/src/profile_controller.hpp/.cpp`          |
| **StreamDockControlService**  | Converts display actions → HID writes; brightness/image render                                      | `src/app/src/stream_dock_control_service.hpp/.cpp` |
| **StreamDockInputService**    | Polls devices; converts HID input reports → `DeviceEvent`; invokes action chains                    | `src/app/src/stream_dock_input_service.hpp/.cpp`   |
| **SidecarStreamDockDevice**   | `core::IDevice` implementation; spawns mirajazz sidecar process; persistent handle lifecycle        | `src/app/src/sidecar_stream_dock_device.hpp/.cpp`  |
| **SdPluginServer**            | WebSocket server hosting `.sdPlugin` plugins; routes action events → **PluginDeviceBridge**         | `src/app/src/sd_plugin_server.hpp/.cpp`            |
| **PluginDeviceBridge**        | Converges SdPluginServer ↔ StreamDockControl/Input; context registry (page/row/col ↔ plugin action) | `src/app/src/plugin_device_bridge.hpp/.cpp`        |
| **PluginManager**             | Discovers and launches `.sdPlugin` bundles (WebSocket subprocesses)                                 | `src/app/src/plugin_manager.hpp/.cpp`              |
| **DebugControlServer**        | Opt-in Unix socket JSON-RPC channel for `scripts/ajazz-debug` (log/state/QML introspection)         | `src/app/src/debug_control_server.hpp/.cpp`        |
| **IDevice**                   | Abstract interface: every backend (Stream Deck/keyboard/mouse) implements this                      | `src/core/include/ajazz/core/device.hpp`           |
| **IDisplayCapable**           | Mixin: per-key LCD or main display (setKeyImage, setBrightness, flush)                              | `src/core/include/ajazz/core/capabilities.hpp`     |
| **IEncoderCapable**           | Mixin: rotary encoders with image rendering (AKP05)                                                 | `src/core/include/ajazz/core/capabilities.hpp`     |
| **ITouchStripDisplayCapable** | Mixin: addressable touch-strip zones (AKP05 encoder strip)                                          | `src/core/include/ajazz/core/capabilities.hpp`     |
| **ActionEngine**              | Executes action chains (sleep → defer, plugin → callback, key press, command, URL, folder nav)      | `src/core/include/ajazz/core/action_engine.hpp`    |
| **ITransport**                | Abstract USB transport; default is **HidTransport** (libhidapi)                                     | `src/core/include/ajazz/core/transport.hpp`        |
| **HotplugMonitor**            | Cross-platform USB enumeration watcher; dispatches events to debouncer                              | `src/core/include/ajazz/core/hotplug_monitor.hpp`  |
| **makeAkp815**                | Stream Deck AKP815 factory (5×3 grid, 800×480 strip, custom C++ backend)                            | `src/devices/streamdeck/src/akp815.cpp`            |
| **makeSidecarStreamDock**     | Stream Deck AKP05/N4/AKP03/AKP153 factory (mirajazz sidecar proxy)                                  | `src/app/src/sidecar_stream_dock_device.hpp`       |

## Pattern Overview

**Overall:** Layered architecture with clear separation of concerns:

1. **Core library** (hardware-agnostic, no Qt, no nlohmann::json — COD-031 boundary)
1. **Device backends** (transport + capability implementations)
1. **Qt application** (UI, services, IPC, hot-plug)
1. **Out-of-process subsystems** (Rust sidecar, Python plugin host, Node.js `.sdPlugin` runtime)

**Key Characteristics:**

- **Device abstraction** via `IDevice` + capability mix-ins (dynamic_cast); no inheritance hierarchy
- **Dependency injection** for testing: `DeviceRegistry::HidEnumerator`, `SidecarBinaryResolver`, injected `ITransport`
- **Single-threaded Qt GUI thread** for all QObject slots; device I/O thread pools or dedicated monitor threads marshal events back via `QMetaObject::invokeMethod`
- **Persistent IPC handles** (sidecar, WebSocket plugins) to avoid re-spawning overhead
- **Action-driven dispatch** via `ActionEngine` callbacks; the engine itself has no Qt dependency

## Layers

**Core Library (`src/core/`):**

- Purpose: Device abstractions, transport interface, profile I/O, action engine, logging
- Location: `src/core/include/ajazz/core/` (headers), `src/core/src/` (implementation)
- Contains: Pure C++20, no external dependencies except std
- Depends on: C++20 stdlib, libhidapi headers (optional, for compile-time transport validation)
- Used by: Device backends, Qt app, Python plugin host

**Device Backends (`src/devices/`):**

- Purpose: USB protocol drivers for Stream Deck, keyboard, mouse families
- Location: `src/devices/{streamdeck,keyboard,mouse}/`
- Contains:
  - `streamdeck/src/akp815.cpp` — AKP815 custom C++ backend
  - `streamdeck/src/register.cpp` — AKP815 + sidecar descriptor registration
  - `keyboard/src/ak980.cpp` — Keyboard custom backend
  - `mouse/src/aj_series.cpp` — Mouse custom backend
  - Wire protocol headers (`*_protocol.hpp`, `*_wire.hpp`)
- Depends on: Core library, libhidapi at runtime
- Used by: Qt application via `DeviceRegistry`

**Qt Application (`src/app/src/`):**

- Purpose: GUI, user-facing services, plugin runtime, IPC infrastructure
- Location: `src/app/src/` (Qt objects), `src/app/qml/` (QML UI)
- Contains:
  - Sidecar proxy (`SidecarStreamDockDevice`, `sidecar_protocol.*`)
  - Plugin runtime (`SdPluginServer`, `PluginManager`, `PluginDeviceBridge`)
  - Device services (`StreamDockControl/Input`, `LightingService`, `TimeSyncService`, etc.)
  - Debug channel (`DebugControlServer`, `DebugControlFacade`)
  - Profile persistence (`ProfileController`)
  - Tray integration (`TrayController`)
- Depends on: Core library, Qt6 (Core, Gui, Quick, WebSockets, WebEngine), libhidapi at runtime
- Used by: QML UI, external debug clients

**Out-of-Process Sidecar (`streamdock-host/`):**

- Purpose: AKP05/N4/AKP03/AKP153 device I/O via mirajazz crate
- Language: Rust
- Protocol: Newline-delimited JSON over stdin/stdout
- Spawned by: `SidecarStreamDockDevice::open()` as a persistent subprocess
- Implements: One persistent HID handle per device (fixes per-interaction open/close wedge)

**Python Plugin Host (`src/plugins/`):**

- Purpose: Out-of-process OOP Python plugin runner (abstracting away the in-process pybind11 era)
- Location: `src/plugins/include/ajazz/plugins/` (C++ interface), `python/` (Python runtime)
- Implements: `IPluginHost` interface; dispatches actions via JSON IPC to subprocess
- Depends on: C++ for interface + launcher; Python 3.x at runtime for plugin execution

## Data Flow

### Primary Request Path: Device Input → Plugin Action → Device Output

1. **Input Polling** (`StreamDockInputService::run()` on poll thread)

   - Calls `device->poll()` in a loop
   - Device (or sidecar) decodes HID input reports → `DeviceEvent` (key/encoder/touch)
   - `onEvent()` callback fires → `StreamDockInputService::handleEvent()` (`src/app/src/stream_dock_input_service.cpp:71`)

1. **Profile Navigation & Action Lookup**

   - Navigation context: current device codename + active page id (starts at "root")
   - Lookup device's active profile → find page by id → find action at (row, col)
   - If action.kind == `Kind::Plugin`: extract `plugin.action` id
   - If action.kind == `Kind::Folder`: push new page onto stack
   - If action.kind == `Kind::BackToParent`: pop stack

1. **Plugin Action Dispatch** (if applicable)

   - `PluginDeviceBridge::onActionReceived()` (Phase 19 seam; SdPluginServer→bridge→plugin)
   - OR `ActionEngine::execute()` with plugin callback wired to `PluginManager::dispatch()`
   - Plugin UUID + action id → WebSocket plugin subprocess → handler invokes user code

1. **Action Execution**

   - **Sleep:** Deferred via `Executor` (Qt event loop)
   - **KeyPress:** Calls `InputSynthesizer::synthesizeKeyPress()` → OS key event (if enabled)
   - **RunCommand:** Spawns shell subprocess
   - **OpenUrl:** Launches system browser
   - **OpenFolder:** Pushes page onto navigation stack (updates page id context)
   - **BackToParent:** Pops stack

1. **Device Output (Render Loop)**

   - `StreamDockControlService::assignKeyImage()` queues image assignment
   - When page changes or action context updates, UI calls `device->setKeyImage(index, rgba, w, h)`
   - For mirajazz sidecar: `SidecarStreamDockDevice` encodes JSON `set_image` command → QProcess stdin
   - Device encodes to JPEG/PNG, applies Rot180, sends HID output report
   - `device->flush()` ensures batch writes are pushed
   - `device->keepAlive()` sends keep-alive heartbeat (CRT CONNECT)

### Secondary Flow: Hot-plug → Device Registry Update

1. **HotplugMonitor** spawns OS-specific watcher (udev on Linux, IOKit on macOS, WMI on Windows)
1. Emits `HotplugEvent` (device added / removed)
1. **HotplugDebouncer** coalesces rapid events (e.g., re-enumeration storms)
1. Invoked on main thread → `DeviceModel::onHotplugEvent()` → `Application::bootstrap()` re-scans registry
1. QML sidebar refreshes bound device list

**State Management:**

- **Registry state:** Thread-safe mutex-protected map of factories; never changes after bootstrap
- **Device state:** Per-device (open/closed, cached firmware version, image buffer); owned by service layer
- **Profile state:** `ProfileController` holds in-memory `ajazz::core::Profile`; persists to disk
- **Navigation state:** `ActionEngine::NavigationContext` (page stack); mutated only on input thread
- **Plugin context:** `PluginDeviceBridge::ContextRegistry` (opaque context id ↔ ActionContext); guarded by Qt GUI thread

## Key Abstractions

**IDevice:**

- Purpose: Common interface for all device backends
- Examples: `SidecarStreamDockDevice` (AKP05), `Akp815Device` (AKP815), `Ak980Keyboard`, `AjMouseDevice`
- Pattern: Implement + register factory with `DeviceRegistry::registerDevice()`
- Key methods: `open()`, `close()`, `poll()`, `onEvent()`, `capabilities()`

**ITransport:**

- Purpose: Byte-stream transport abstraction (USB, mock, capture-replay)
- Examples: `HidTransport` (real libhidapi), `MockTransport` (unit tests), `CaptureReplayTransport` (wire-format audit)
- Pattern: Constructor-injected into device factory; devices never hard-code hidapi calls

**Capability Mix-ins:**

- `IDisplayCapable` → per-key LCDs or main display
- `IEncoderCapable` → rotary encoders
- `ITouchStripDisplayCapable` → touch-strip zones
- `IRgbCapable` → per-key or global RGB lighting
- `IBatteryCapable` → wireless device charge level
- `IClockCapable` → device RTC
- Pattern: `dynamic_cast<IDisplayCapable*>(device)` to check; `capabilities()` bitset for quick tests

**ActionEngine:**

- Purpose: Lightweight interpreter for action chains (sequences of steps: plugin, sleep, key press, etc.)
- Callback-driven (no Qt dependency); injected executors for OS operations
- Pattern: Application creates once at startup with Qt executor; every key press walks its chain

**Profile / ProfilePage / Action:**

- Purpose: Device configuration persisted to JSON
- Structure: Profile → PageMap (named pages) → KeyGrid (per-key actions) → ActionChain
- Wire format: JSON; schema defined in `docs/schemas/profile.schema.json`
- Lifetime: Loaded by `ProfileController::loadProfile()`, edited in QML, saved by `ProfileController::saveProfile()`

## Entry Points

**Application Bootstrap** (`src/app/src/main.cpp:46`)

- Sets Qt app metadata (name, org, version, desktop file)
- Creates single-instance guard (prevents duplicate launches)
- Constructs `Application` controller
- Calls `Application::bootstrap()` → registers all device backends
- Loads QML engine with `Main.qml` root component
- Calls `Application::startBackgroundServices()` → starts tray + hot-plug monitor
- Enters event loop

**Device Registry Bootstrap** (`src/app/src/application.cpp:bootstrap()`, `src/devices/streamdeck/src/register.cpp`)

- Called once from `Application::bootstrap()`
- Each device module calls `registerAll(DeviceRegistry&)`:
  - `ajazz::streamdeck::registerAll()` → registers AKP815 factory + sidecar descriptors
  - `ajazz::keyboard::registerAll()` → registers AK980 factory
  - `ajazz::mouse::registerAll()` → registers AJ-series factory
- Result: `DeviceRegistry` maps VID/PID → factory callable

**Input Poll Loop** (`src/app/src/stream_dock_input_service.cpp:122`)

- Spawned on a worker thread in `StreamDockInputService::start()`
- Repeatedly calls `device->poll()` (blocking read from HID)
- On event: decodes `DeviceEvent` → `ActionEngine::execute()` → invokes bound action

**Hot-plug Monitor** (`src/core/include/ajazz/core/hotplug_monitor.hpp`)

- Spawned on a worker thread; OS-specific watcher (udev / IOKit / WMI)
- Emits `HotplugEvent` → debounced → `Application::onHotplugEvent()` on GUI thread
- Triggers device re-enumeration and model refresh

## Architectural Constraints

- **Threading:** Single-threaded Qt GUI thread for all QObject operations; device I/O threads (poll, hot-plug monitor) marshal events back via `QMetaObject::invokeMethod` or signals/slots
- **Global state:** None; `Application` owns the `DeviceRegistry` (was a Meyers singleton pre-Audit A1, now constructor-injected)
- **Circular imports:** None enforced by CMake; headers are vigilant about forward declarations
- **COD-031 boundary:** Core library headers (`src/core/include/ajazz/core/`) must NOT include nlohmann::json (PRIVATE-linked to plugins layer only); QJsonDocument is used in app layer
- **Sidecar persistence:** One persistent `QProcess` per mirajazz device; closing device → sending `CRT DIS` → process exit
- **Plugin isolation:** Each `.sdPlugin` runs in its own WebSocket subprocess; plugin crash does not crash the app

## Anti-Patterns

### Per-Interaction Open/Close (Removed)

**What happens:** Legacy code (removed in mirajazz migration) opened the AKP05 device, sent one command, closed it. This churn caused the panel to wedge (DIS → LIG → DIS sequence disabled input).

**Why it's wrong:** USB devices have state; repeated open/close cycles can desynchronize firmware or hardware state, especially on Stream Decks with stateful displays.

**Do this instead:** Hold a persistent `QProcess` for the sidecar or a persistent `ITransport` handle. See `SidecarStreamDockDevice::open()` (opens once, holds handle until `close()` is called). `device->keepAlive()` sends periodic `CRT CONNECT` to prevent idle timeouts.

### Hardcoded Device Constants in UI (Partially Mitigated)

**What happens:** Early QML layouts used magic numbers (15 keys, 4 encoders, etc.) instead of reading from `DeviceDescriptor` or runtime `displayInfo()`.

**Why it's wrong:** Adding a new device requires editing multiple QML files; descriptor changes don't auto-reflect in the UI.

**Do this instead:** Read `DeviceDescriptor.keyCount`, `gridColumns`, `encoderCount` at startup; bind QML item counts to model properties. Query `displayInfo()` at runtime for sizing. See `DeviceView.qml` and `stream_dock_control_service.cpp` for the pattern.

### nlohmann::json in Core Public Headers (Strictly Prevented)

**What happens:** Early code leaked JSON parsing into `src/core/include/` headers, breaking COD-031 (the nlohmann boundary).

**Why it's wrong:** Core library is embedded in headless/embedded devices; nlohmann is only used by the app layer and plugin host. Leaking it into core forces the JSON dependency everywhere.

**Do this instead:** Parse JSON at the app layer (e.g., `SidecarStreamDockDevice::handleLine()` uses `QJsonDocument`). Core returns POD structs (e.g., `DeviceEvent`, `DisplayInfo`). If core needs config, pass it as constructor args, not JSON strings.

## Error Handling

**Strategy:** Exceptions for hard failures; optional/result types for soft failures.

**Patterns:**

- **Transport errors:** `ITransport::write()` throws `std::runtime_error` if HID write fails
- **Device open failure:** `IDevice::open()` throws `std::runtime_error` with OS error details
- **Profile I/O:** `readProfileFromDisk()` throws `std::exception` on JSON parse or file errors; caught by `ProfileController` and emitted as `loadFailed(QString)`
- **Plugin dispatch:** `IPluginHost::dispatch()` returns `bool` (soft failure: unknown action, handler error) or throws `std::runtime_error` (hard failure: subprocess died)
- **Action engine:** `ActionEngine::execute()` calls callbacks; callbacks may throw (caller responsibility to catch)

## Cross-Cutting Concerns

**Logging:** `ajazz::core::Logger` (compile-time log level; thread-safe sink pattern). Sinks include:

- File sink (append-only `.log` file)
- Ring buffer sink (in-memory ring, accessible via debug channel)
- Qt sink (bridges `qDebug()` / `qCWarning()` into the logger)

**Validation:** Profile JSON validated against schema at load time. Action parameters (sleep duration, command string, etc.) validated during action execution (not at profile load).

**Authentication:** Plugin host (Python) validates manifest Ed25519 signature via `scripts/sign-plugin-manifest.py` subprocess. Plugins declared permissions are surfaced at install but NOT enforced at runtime (Phase 3a read-only; future work).

**SdPlugin Isolation:** Each `.sdPlugin` manifest declares which properties/actions it exposes; SdPluginServer dispatches only to actions registered in that manifest. Unknown actions silently fail (soft error).

______________________________________________________________________

*Architecture analysis: 2026-06-02*
