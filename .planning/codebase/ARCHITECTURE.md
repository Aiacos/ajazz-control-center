# Architecture

**Analysis Date:** 2026-05-22

## System Overview

AJAZZ Control Center is a Qt 6 desktop application with a modular, capability-driven device backend system backed by a C++20 core library and an out-of-process Python 3 plugin host for scripting and third-party integrations.

```text
┌─────────────────────────────────────────────────────────────────────────────┐
│                            Qt 6 / QML UI Layer                              │
│  Main.qml · DeviceList · ProfileEditor · KeyDesigner · RgbPicker · Tray     │
│  `src/app/qml/` — Material Design 3 theme, Material attached properties     │
└─────────────────────────────────────────────────────────────────────────────┘
                           │ Q_PROPERTY / Q_INVOKABLE
┌─────────────────────────────────────────────────────────────────────────────┐
│                         Application Services (C++)                          │
│  `src/app/src/` — Application · DeviceModel · ProfileController · Services  │
│  · TimeSyncService · SettingsService · LightingService · PluginHost        │
│  · HotplugMonitor · BrandingService · TrayController · BatteryService      │
└─────────────────────────────────────────────────────────────────────────────┘
        │                          │                    │
┌───────────────────┐  ┌──────────────────────────┐  ┌─────────────────┐
│ Device Core       │  │ Python Plugin Host       │  │ Persistence     │
│ `src/core/`       │  │ `src/plugins/` +         │  │ QSettings +     │
│ ─────────────── │  │ `python/`                │  │ Profile JSON    │
│ IDevice          │  │ ─────────────────────── │  │                 │
│ ITransport (HID) │  │ OutOfProcessPluginHost   │  │ ProfileIO       │
│ Capabilities     │  │ Manifest signing         │  │                 │
│ DeviceRegistry   │  │ Sandboxing (bwrap/exec) │  │ Trust roots     │
│ EventBus         │  │ pybind11 `ajazz` module │  │                 │
└───────────────────┘  └──────────────────────────┘  └─────────────────┘
        │
┌─────────────────────────────────────────────────────────────────────────────┐
│                   Device Backend Modules (C++, pluggable)                   │
│  streamdeck_akp153/akp03/akp05/akp815 · keyboard_via/proprietary · mouse_aj │
│  `src/devices/{streamdeck,keyboard,mouse}/src/`                             │
│  Protocol builders, wire-format decoders, capability implementations        │
└─────────────────────────────────────────────────────────────────────────────┘
```

## Component Responsibilities

| Component                  | Responsibility                                                                                      | File(s)                                                |
| -------------------------- | --------------------------------------------------------------------------------------------------- | ------------------------------------------------------ |
| **Application**            | Top-level controller; owns registry, device model, services; bootstraps backends; wires QML context | `src/app/src/application.{hpp,cpp}`                    |
| **DeviceModel**            | List model exposed to QML; tracks connected devices; handles hot-plug events                        | `src/app/src/device_model.{hpp,cpp}`                   |
| **ProfileController**      | Load/save profile JSON; dispatch actions to plugin host or system                                   | `src/app/src/profile_controller.{hpp,cpp}`             |
| **DeviceRegistry**         | Maps USB VID/PID → factory functions; owns flyweight shared_ptr cache per device class              | `src/core/include/ajazz/core/device_registry.hpp`      |
| **IDevice**                | Abstract interface; every backend inherits; lifecycle is open/close/poll                            | `src/core/include/ajazz/core/device.hpp`               |
| **ITransport**             | Abstract USB I/O; HidTransport is default (libhidapi); MockTransport for tests                      | `src/core/include/ajazz/core/transport.hpp`            |
| **Capability Mix-ins**     | IDisplayCapable, IRgbCapable, IEncoderCapable, IClockCapable, IBatteryCapable, etc.                 | `src/core/include/ajazz/core/capabilities.hpp` (68 KB) |
| **HotplugMonitor**         | Polls libhidapi; fires Connected/Disconnected events; triggers DeviceModel refresh                  | `src/core/include/ajazz/core/hotplug_monitor.hpp`      |
| **Backend: StreamDeck**    | AKP153/03/05/815 (6–15 LCD keys + encoders); JPEG display encoding; touch strip                     | `src/devices/streamdeck/src/`                          |
| **Backend: Keyboard**      | VIA-compatible (AK820 Pro) + proprietary (AK980 PRO); RGB modes; macro playback; RTC                | `src/devices/keyboard/src/`                            |
| **Backend: Mouse**         | AJ-series wired + 2.4GHz dongle; DPI stages; RGB zones; battery; TFT clock host-render              | `src/devices/mouse/src/`                               |
| **OutOfProcessPluginHost** | Spawns Python child; gates plugin loading via manifest signatures; IPC bridge                       | `src/plugins/src/out_of_process_plugin_host*.cpp`      |
| **TimeSyncService**        | QML singleton; exposes per-device `setTime()` via dynamic_cast to IClockCapable                     | `src/app/src/time_sync_service.{hpp,cpp}`              |
| **SettingsService**        | AK980 PRO settings batch (opcode 0x07 sub 0x10); ISettingsCapable bridge                            | `src/app/src/settings_service.{hpp,cpp}`               |
| **LightingService**        | AK980 PRO 20-mode firmware RGB; IFirmwareLightingCapable bridge                                     | `src/app/src/lighting_service.{hpp,cpp}`               |
| **BatteryService**         | Polls IBatteryCapable for charge level; emits toast notifications on low                            | `src/app/src/battery_service.{hpp,cpp}`                |

## Pattern Overview

**Overall:** Layered architecture with three horizontal planes (UI ↔ Services ↔ Backends) and per-device-family vertical slices.

**Key Characteristics:**

- **Capability-driven:** UI discovers features via `dynamic_cast<ICapability*>` at runtime; no compile-time coupling to specific backends.
- **Pluggable backends:** Each device family (streamdeck, keyboard, mouse) is a separate CMake target linked at build time; new backends add themselves via `registerAll(DeviceRegistry&)` at startup.
- **Shared ownership:** `DevicePtr = std::shared_ptr<IDevice>`; flyweight cache in `DeviceRegistry` ensures one backend instance per (vendorId, productId) pair, shareable across multiple consumers (D-06 flyweight contract).
- **Out-of-process plugins:** Python plugins run in a sandboxed child process; manifest signatures gate loading (SEC-003); IPC bridge routes action dispatch via JSON wire protocol.
- **Hot-plug correct:** `HotplugMonitor` fires events → `DeviceModel` updates → UI refreshes; devices are marked offline rather than removed; reconnect is automatic (HOTPLUG contract).
- **COD-031 boundary:** `nlohmann::json` PRIVATE-linked to `ajazz_plugins` only; never in `ajazz_core` public headers — trust-roots parsing lives inside the sandbox.

## Layers

**QML UI Layer:**

- Purpose: Render device list, profile editor, key designer, RGB picker, system tray; Material Design 3.
- Location: `src/app/qml/`
- Contains: Main.qml, component library, Material-attached property bindings.
- Depends on: Application context properties (DeviceModel, ProfileController) and C++ services exposed via `qmlRegisterSingletonInstance`.
- Used by: End users clicking keys, editing profiles, syncing time, etc.

**Application Services Layer:**

- Purpose: Bridge the UI to device backends; implement cross-device logic (hot-plug debouncing, profile dispatch, plugin IPC).
- Location: `src/app/src/`
- Contains: Application (top-level controller), DeviceModel, ProfileController, TimeSyncService, SettingsService, LightingService, BatteryService, HotplugDebouncer, TrayController, BrandingService, AutostartService, FirmwareUpdateService, AppUpdateService.
- Depends on: Device core, plugin host, persistence (QSettings, JSON profile I/O).
- Used by: QML UI (via Q_PROPERTY / Q_INVOKABLE) and background daemons (tray, hot-plug, plugin host).

**Device Core Layer:**

- Purpose: Abstract device interface, transport, registry, events, profiles, logging.
- Location: `src/core/include/ajazz/core/` + `src/core/src/`
- Contains: IDevice, ITransport (HidTransport, MockTransport), DeviceRegistry, HotplugMonitor, capability mix-ins (IDisplayCapable, IRgbCapable, etc.), DeviceEvent, profile I/O, logger, event bus, action engine.
- Depends on: Qt6::Core, hidapi, standard C++20.
- Used by: Application services, device backends, tests.

**Device Backend Layer:**

- Purpose: Implement per-family protocol handlers and USB wire-format encoding/decoding.
- Location: `src/devices/{streamdeck,keyboard,mouse}/src/`
- Contains: Per-model concrete device classes (Akp153Device, Akp03Device, Akp05Device, ProprietaryKeyboard, ViaKeyboard, AjSeriesMouse); protocol builders; register.cpp files.
- Depends on: Device core layer (IDevice, capabilities, ITransport).
- Used by: DeviceRegistry (via factory functions); application services.

**Plugin Host Layer:**

- Purpose: Spawn and sandbox Python plugin processes; manage manifest signatures; route action dispatch via IPC.
- Location: `src/plugins/include/` + `src/plugins/src/` + `python/`
- Contains: OutOfProcessPluginHost, platform-specific sandboxing (Linux bwrap, macOS sandbox_exec, Windows AppContainer), manifest signer, pybind11 bridge module.
- Depends on: Device core, nlohmann::json (PRIVATE), standard C++20.
- Used by: Application (plugin discovery, action dispatch).

**Persistence Layer:**

- Purpose: Save/load profiles, settings, branding.
- Location: `src/app/src/` (integration with QSettings), `src/core/src/profile_io.cpp`
- Contains: ProfileIO (JSON file read/write), profile bundle export/import, trust-roots JSON parser.
- Depends on: Device core (profile schema), nlohmann::json (trust-roots only; PRIVATE to ajazz_plugins).
- Used by: ProfileController, plugin host trust-roots loader.

## Data Flow

### Primary Request Path: User Presses a Key

1. **HID Input Report** — libhidapi delivers a raw USB input report to a backend's reader thread.
1. **Backend Parse** — Protocol-specific parser (e.g., `Akp153Device::poll()` → `parseInputReport()`) decodes the key index and emits `DeviceEvent::KeyPressed`.
1. **Event Callback** — `DeviceEvent` is delivered to the registered callback (set by the Application layer).
1. **EventBus Publish** — Event is published to all subscribers (profile engine, UI model, etc.).
1. **Profile Dispatch** — ProfileEngine looks up the key in the active profile and resolves the action chain.
1. **Action Execution** — Each action is dispatched:
   - **Direct action** (e.g., "open URL") → DesktopServices call → OS side-effect.
   - **Plugin action** → ProfileController sends JSON to the OutOfProcessPluginHost via IPC → Python handler runs → result sent back.
1. **UI Update** — DeviceModel emits signals; QML ListView updates visual state.

**Example call trace:** `HidTransport::read()` → `Akp153Device::poll()` → `EventCallback()` → `EventBus::publish(KeyPressed)` → `ProfileEngine::dispatch()` → `PluginHost::dispatch(action_id, ...)` → child process Python handler → IPC response → UI toast.

### Secondary Flow: User Syncs Time (Time-Sync Service)

1. **User clicks "Sync" button in sidebar** → QML signal invokes `TimeSyncService::syncDevice(deviceId)`.
1. **Device Lookup** — Service calls `DeviceRegistry::open(deviceId)` → returns `shared_ptr<IDevice>`.
1. **Capability Query** — `dynamic_cast<IClockCapable*>(device.get())` checks if the device supports clocks.
1. **Wire Format** — `IClockCapable::setTime(now)` encodes the current UTC timestamp into the device's wire format and writes via the transport.
1. **Response** — Backend returns `TimeSyncResult::Ok`, `TimeSyncResult::NotImplemented`, or `TimeSyncResult::IoError` (`src/core/include/ajazz/core/capabilities.hpp:1176`) — never `Ok` with a lie if the device cannot do it (D-02 honesty contract).
1. **UI Feedback** — Service emits a toast or glyph update showing the result (exclamation icon on `NotImplemented`, checkmark on `Ok`); see `src/app/src/time_sync_service.cpp:252`.

**Example call trace:** `Main.qml` sync button → `TimeSyncService::syncDevice()` → `DeviceRegistry::open()` → `dynamic_cast<IClockCapable*>` → `setTime(now)` → `Akp05Device::setTime()` → wire format build → `ITransport::write()` → USB HID output report → device firmware receives → UI glyph updates.

### Tertiary Flow: Device Hot-Plug (Connect / Disconnect)

1. **HotplugMonitor polls** — Timer fires every 300 ms (debounce window per HOTPLUG-01).
1. **Enumerate** — Calls `DeviceRegistry::enumerateConnectedHidKeys()` → `hid_enumerate(0, 0)` from libhidapi.
1. **Diff** — Compares the new set to the previous snapshot; detects added/removed (vid, pid) pairs.
1. **Event Fire** — For each changed pair, emits `HotplugEvent::Connected` or `HotplugEvent::Disconnected`.
1. **Application Handler** — `Application::onHotplug()` forwards to `DeviceModel::handleHotplug()`.
1. **Model Update** — DeviceModel adds or marks device as offline; emits `dataChanged()` signal.
1. **UI Refresh** — QML ListView re-renders; offline devices show a grayed-out badge; online devices refresh.
1. **Zombie Contract (D-06)** — If a consumer holds a `shared_ptr<IDevice>` from before the disconnect, the backend instance stays alive but HID I/O fails safely (not a crash). There is no `Result::DeviceGone` type: the transport throws on the dead handle and backends either swallow-and-log (fire-and-forget setters), return an empty `std::optional` (fallible reads), or surface `TimeSyncResult::IoError` (clock ops). The shared_ptr is naturally released when the consumer drops its reference.

**Example call trace:** Hot-plug event → `HotplugMonitor::injectEvent(Disconnected)` → `Application::onHotplug()` → `DeviceModel::handleHotplug()` → `dataChanged()` signal → QML ListView re-renders → device grayed out. Later, user closes key designer → `DevicePtr` released → backend reclaimed from the flyweight cache.

## Key Abstractions

**IDevice (Core Device Interface):**

- Purpose: Represents a physical USB device; lifecycle is open (acquire HID handle) / poll (drain input reports) / close (release handle).
- Examples: `Akp153Device`, `ProprietaryKeyboard`, `AjSeriesMouse`.
- Pattern: CRTP not used; interface is virtual; backends inherit and optionally implement capability mix-ins.

**ITransport (USB I/O Abstraction):**

- Purpose: Abstract the underlying USB communication layer; default is `HidTransport` (libhidapi); tests use `MockTransport`.
- Examples: HidTransport, MockTransport (COD-026 DI seam for testing).
- Pattern: Dependency injection; IDevice holds `TransportPtr = unique_ptr<ITransport>`.

**Capability Mix-ins:**

- Purpose: Allow a backend to opt into features dynamically; UI queries at runtime via `dynamic_cast`.
- Examples: IDisplayCapable (set key image), IRgbCapable (set zone colors), IClockCapable (set time), IBatteryCapable (read charge).
- Pattern: Virtual inheritance; backend can inherit multiple (e.g., `Akp05Device : public IDevice, public IDisplayCapable, public IEncoderCapable`).

**DeviceRegistry Flyweight Cache:**

- Purpose: Ensure one backend instance per (vendorId, productId) across the whole process; multiple consumers share the same backend / one HID handle.
- Examples: Two threads calling `registry.open(deviceId)` for the same device get the same `shared_ptr<IDevice>`.
- Pattern: `weak_ptr<IDevice>` cache per (vid, pid); passive eviction when the last consumer drops its `shared_ptr` (no proactive invalidation on hot-plug).

**ProfileEngine / ActionEngine:**

- Purpose: Parse profiles (JSON with key ↔ action mapping); dispatch actions (direct or plugin-routed).
- Examples: Open URL, play sound, send macro, call plugin handler.
- Pattern: Visitor pattern over action types; profile is device-agnostic (one profile can be partially reused on multiple device families).

## Entry Points

**Application Entry:**

- Location: `src/app/src/main.cpp`
- Triggers: User runs `ajazz-control-center` binary.
- Responsibilities: Parse CLI flags, enforce single-instance lock, create Application controller, load QML, start background services.

**Device Registry Bootstrap:**

- Location: `src/app/src/application.cpp::Application::bootstrap()`
- Triggers: Application constructor → `bootstrap()`.
- Responsibilities: Call `registerAll(registry)` for streamdeck, keyboard, mouse modules; populate the registry with all known VID/PID ↔ factory mappings.

**Per-Family Registration:**

- Locations: `src/devices/{streamdeck,keyboard,mouse}/src/register.cpp`.
- Triggers: Backend module's `registerAll(DeviceRegistry&)` function called from `Application::bootstrap()`.
- Responsibilities: Register static `DeviceDescriptor` entries (VID, PID, family, model, codename, capability hints) paired with factory functions (makeAkp153, makeProprietaryKeyboard, makeAjSeries, etc.).

**Hot-Plug Polling:**

- Location: `src/core/src/hotplug_monitor.cpp` + `src/app/src/application.cpp`.
- Triggers: Timer in HotplugMonitor (300 ms debounce) or test injection via `HotplugMonitor::injectEvent()`.
- Responsibilities: Poll libhidapi, diff against previous snapshot, fire Connected/Disconnected events, forward to Application → DeviceModel.

**Plugin Host Bootstrap:**

- Location: `src/app/src/application.cpp::Application::initPluginHost()`.
- Triggers: If `AJAZZ_PYTHON_HOST` is ON and XDG AppLocalDataLocation exists.
- Responsibilities: Instantiate OutOfProcessPluginHost, scan `~/.local/share/ajazz/plugins/` for manifest.json files, verify signatures, load trusted plugins into LoadedPluginsModel.

## Architectural Constraints

- **Threading:** Main thread (Qt event loop) runs QML and all services. Device backends may run internal reader threads (e.g., HidTransport), but `IDevice::onEvent()` callbacks are invoked from the I/O thread. Application-layer signal handlers must be thread-safe or marshal to the main thread.
- **Global state:** No module-level statics except the deprecated `DeviceRegistry::instance()` singleton shim (audit finding A1 replaced it with constructor injection). Each `Application` owns its `DeviceRegistry m_deviceRegistry` as a data member.
- **Circular imports:** None documented; all depends-on relationships flow downward (UI → Services → Core → Backends). Backends do not import each other.
- **Shared ownership:** `DevicePtr = shared_ptr<IDevice>` is the standard; `unique_ptr` is NOT used for devices. This is load-bearing for HOTPLUG-01 (the zombie contract and flyweight cache).
- **COD-031 Boundary:** `nlohmann::json` is PRIVATE-linked to `ajazz_plugins` only. The public header `src/core/include/ajazz/core/device_registry.hpp` must never include `<nlohmann/json.hpp>`. Verified by grep at audit time: `grep -rn nlohmann src/core/include/ must return 0`.
- **ITransport Seam (COD-026):** Backends receive ITransport via the DI pattern (`DeviceFactory` functions receive a `TransportPtr` in the anonymous namespace). Tests inject `MockTransport` to capture wire-format exchanges without touching real USB.
- **QML_SINGLETON Dual-Instance Prevention:** All 9 QML singletons (TimeSyncService, BatteryService, SettingsService, etc.) are registered via `qmlRegisterSingletonInstance` (not the bare `QML_SINGLETON` macro), paired with `static_assert(!std::is_default_constructible_v<T>)` to convert the pattern violation to a build-time error (D-01 amendment 3).

## Anti-Patterns

### Global Device State Mutation

**What happens:** Code in one module (e.g., a device backend) modifies global mutable state visible to other modules (e.g., a different backend or the UI).
**Why it's wrong:** Makes device state machine non-deterministic; concurrent backends can step on each other's writes; tests cannot isolate.
**Do this instead:** Encapsulate state in the IDevice instance; backends are independent. Communication goes through the EventBus or explicit service calls (e.g., `DeviceModel::handleHotplug()`), never via global variables. See `src/core/include/ajazz/core/event_bus.hpp` for the event-driven pattern.

### Throwing Exceptions from Device I/O

**What happens:** A backend calls `hid_open()` and it fails, so the backend throws `std::runtime_error("device not found")` to the caller.
**Why it's wrong:** Qt signal-slot connections cannot propagate exceptions safely; the exception disappears and the app crashes unpredictably. QML has no exception handling.
**Do this instead:** Use the heterogeneous error model the codebase actually implements — `std::optional<T>` for fallible reads (e.g. `parseInputReport`, `batteryPercent`), `TimeSyncResult` for clock ops, and `void`+internal-try/catch+`AJAZZ_LOG_WARN` for fire-and-forget setters. `IDevice::open()` is the one place that throws (`std::runtime_error`) at the lifecycle edge. Application-layer code wraps the lifecycle calls in try-catch and emits a signal with the error message, which QML connects to a toast. See `src/app/src/profile_controller.cpp::loadProfile()` for the pattern.

### Lying Success UX on Unsupported Operations

**What happens:** A device backend's `setTime()` returns `TimeSyncResult::Ok` even though the firmware does not support RTC — the device cannot actually keep the time, but the UI shows a checkmark.
**Why it's wrong:** User sets time, closes the app, reopens it a week later, thinks the time is synced (it is not) — silent data loss of intent. D-02 honesty contract.
**Do this instead:** Return `TimeSyncResult::NotImplemented` (and `TimeSyncResult::IoError` on a failed write). Let the UI show an exclamation icon + tooltip "this device does not support time sync" (TIMESYNC-05). This is what `ProprietaryKeyboard::setTime()` does for unsupported backends (before ARCH-05.1 found the AK980 PRO firmware RTC).

### Ignoring the Zombie Contract in Device Backends

**What happens:** A device backend stores a raw `HID_HANDLE*` as a data member; when hot-plug removes the device, the handle becomes invalid. Later, someone holds a `shared_ptr<IDevice>` across a reconnect and tries to call a method — the backend dereferences the dead handle → SEGFAULT.
**Why it's wrong:** Breaks the flyweight cache contract; prevents safe multi-consumer device instances.
**Do this instead:** Gate every HID I/O operation on an internal `m_alive` flag or check `hid_get_info()` to detect if the USB handle is still valid. On failure, fail safely along the heterogeneous error model — empty `std::optional` for reads, `TimeSyncResult::IoError` for clock ops, swallow-and-log for fire-and-forget setters — rather than letting an exception escape into a Qt slot. See the zombie contract note in `src/core/include/ajazz/core/device.hpp` class doc.

### Circular Dependency Between Backends

**What happens:** Backend A includes a header from Backend B; Backend B includes a header from Backend A.
**Why it's wrong:** Makes it impossible to build either module in isolation; breaks the per-device-family plugin model.
**Do this instead:** All backends depend exclusively on the core layer (`src/core/include/`). Cross-backend communication happens through shared abstractions (IDevice, ITransport, event bus), never direct includes. See the architecture diagram above.

## Error Handling

**Strategy:** Layered error handling with different scopes:

1. **Transport errors** — `ITransport::read()` / `ITransport::write()` throw `std::runtime_error` on HID I/O failure. Backends decide: retry, close the handle, or return a sentinel result to the application layer.

1. **Protocol decode errors** — Wire-format parsers (e.g., `parseInputReport()`) return `std::optional<T>` and never throw on malformed data (defensive against corrupt USB reports). Return empty optional if the report is not parseable.

1. **Device lifecycle errors** — `IDevice::open()` throws `std::runtime_error` on failure (cannot acquire USB handle, permissions denied, etc.). If the device is yanked mid-call, capability methods fail safely instead of throwing into a slot: fallible reads return an empty `std::optional`, clock ops return `TimeSyncResult::IoError`, and fire-and-forget setters catch internally and `AJAZZ_LOG_WARN`. The caller marks the device offline without crashing. (There is no `Result::DeviceGone` enum — that was a documentation fiction; `TimeSyncResult` in `capabilities.hpp:1176` is the only result-enum in the model.)

1. **Application layer** — Services wrap device calls in try-catch and emit Qt signals with error messages (never throw directly from a slot). QML connects those signals to Toast components or other in-app notifications.

1. **UI layer** — QML never throws; all C++ methods exposed to QML are marked `Q_INVOKABLE` and handle errors via signal emission.

## Cross-Cutting Concerns

**Logging:** `src/core/include/ajazz/core/logger.hpp` provides a thread-safe logger with levels (DEBUG, INFO, WARN, ERROR). Backends log protocol decisions and errors; services log state changes (hot-plug events, profile loads, plugin discovery). Use `Logger::warn()` for recoverable issues, `Logger::error()` for unrecoverable ones (then emit a signal to the UI).

**Validation:** Protocol builders construct USB packets with explicit length checks and assertions. Profile JSON is validated against a schema at load time (`ProfileIO::readProfileFromDisk()` throws on schema violations). Plugin manifests are validated by the signature verifier (trust-roots parser) before load.

**Authentication:** Plugin loading is gated by manifest signature verification (SEC-003). Trust-roots JSON (`trust_roots.json` bundled with the app) lists authorized plugin publishers; manifests must be signed with a matching key. The verifier (`src/plugins/src/manifest_signer*.cpp`) is called inside the sandboxed plugin host process so the main app cannot be tricked into trusting a malicious manifest.

______________________________________________________________________

*Architecture analysis: 2026-05-22*
