# Codebase Structure

**Analysis Date:** 2026-06-02

## Directory Layout

```
ajazz-control-center/
├── src/                                  # All source code
│   ├── app/                              # Qt application layer
│   │   ├── src/                          # C++ sources (Qt objects, services)
│   │   │   ├── main.cpp                  # Entry point; bootstraps Application
│   │   │   ├── application.hpp/.cpp      # Top-level controller; owns all services
│   │   │   ├── device_model.hpp/.cpp     # Qt list model for sidebar (device enumeration)
│   │   │   ├── profile_controller.hpp/.cpp  # Loads/saves profiles; bridges QML
│   │   │   ├── stream_dock_control_service.hpp/.cpp  # Render pipeline (key/encoder/strip images)
│   │   │   ├── stream_dock_input_service.hpp/.cpp    # Input poller; dispatches key/encoder/touch events
│   │   │   ├── sidecar_stream_dock_device.hpp/.cpp   # IDevice impl for AKP05/N4/AKP03/AKP153 (mirajazz proxy)
│   │   │   ├── sidecar_protocol.hpp/.cpp             # Pure JSON encode/decode for sidecar JSON/stdin/stdout protocol
│   │   │   ├── plugin_manager.hpp/.cpp               # Discovers and spawns .sdPlugin bundles
│   │   │   ├── sd_plugin_server.hpp/.cpp             # WebSocket server hosting .sdPlugin runtime
│   │   │   ├── plugin_device_bridge.hpp/.cpp         # Converges SdPluginServer ↔ StreamDockControl/Input; context registry
│   │   │   ├── plugin_debug_service.hpp/.cpp         # RPC surface for plugin introspection
│   │   │   ├── plugin_catalog_model.hpp/.cpp         # Plugin discovery + install (OpenDeck catalog)
│   │   │   ├── debug_control_server.hpp/.cpp         # Opt-in Unix socket JSON-RPC (scripts/ajazz-debug client)
│   │   │   ├── debug_control_facade.hpp/.cpp         # RPC method registry for all subsystems
│   │   │   ├── tray_controller.hpp/.cpp              # System tray icon + right-click menu
│   │   │   ├── lighting_service.hpp/.cpp             # RGB per-key lighting UI controls
│   │   │   ├── time_sync_service.hpp/.cpp            # Device RTC synchronisation (Phase 5)
│   │   │   ├── battery_service.hpp/.cpp              # Wireless device charge level monitoring
│   │   │   ├── settings_service.hpp/.cpp             # AK-series settings batch opcode 0x07 sub 0x10
│   │   │   ├── builtin_actions_service.hpp/.cpp      # Core action registry (KeyPress, RunCommand, OpenUrl, Folder, etc.)
│   │   │   ├── theme_service.hpp/.cpp                # Light/dark mode management
│   │   │   ├── branding_service.hpp/.cpp             # App name, version, copyright (from CMake defines)
│   │   │   ├── autostart_service.hpp/.cpp            # XDG autostart / launchd / Task Scheduler integration
│   │   │   ├── property_inspector_controller.hpp/.cpp  # Plugin Property Inspector (web-based config UI)
│   │   │   ├── hotplug_debouncer.hpp/.cpp            # Coalesces rapid USB enumeration events
│   │   │   ├── node_runner.hpp/.cpp                  # Spawns Node.js child processes for .sdPlugin runtime
│   │   │   ├── pi_bridge.hpp/.cpp                    # Property Inspector ↔ plugin WebSocket bridge (Phase 17)
│   │   │   ├── obs_client.hpp/.cpp                   # OBS WebSocket plugin proxy (Elgato StreamDeck OBS Plugin adapter)
│   │   │   ├── pi_url_policy.hpp/.cpp                # Content Security Policy enforcer for PI web views
│   │   │   ├── opendeck_catalog_fetcher.hpp/.cpp     # Fetches remote OpenDeck .sdPlugin catalog
│   │   │   ├── app_update_service.hpp/.cpp           # App version update checker
│   │   │   ├── firmware_update_service.hpp/.cpp      # Device firmware upgrade orchestrator
│   │   │   ├── qt_executor.hpp/.cpp                  # ActionEngine executor impl (Qt event loop sleep deferral)
│   │   │   ├── single_instance_guard.hpp/.cpp        # Enforces single running GUI instance (Unix socket + fallback)
│   │   │   └── ... (service impls)
│   │   └── qml/                          # QML UI components
│   │       ├── Main.qml                  # Root window; TabBar (DeviceList, ProfileEditor, PluginStore, etc.)
│   │       ├── DeviceView.qml            # Active device canvas; KeyCell grid + EncoderDial + TouchStrip zones
│   │       ├── ProfileEditor.qml         # Edit/delete device binding + page management
│   │       ├── PluginStore.qml           # Install/uninstall .sdPlugin; catalog browsing
│   │       ├── ActionLibraryPane.qml     # Drag-drop action picker (builtin + plugin)
│   │       ├── LoadedPluginsPage.qml     # View loaded plugin manifests
│   │       ├── Inspector.qml             # Property Inspector container (native or web-based)
│   │       ├── components/               # Reusable QML components
│   │       │   ├── KeyCell.qml           # Individual key UI (image + label + overlay states)
│   │       │   ├── EncoderDial.qml       # Rotary encoder dial (AKP05)
│   │       │   ├── TouchStripZone.qml    # Touch strip zone display
│   │       │   ├── RgbPicker.qml         # Color picker for per-key RGB
│   │       │   ├── LibraryTile.qml       # Action library drag-drop tile
│   │       │   └── ... (others)
│   │       └── ... (other pages)
│   │
│   ├── core/                             # Hardware-agnostic core library (NO Qt, NO nlohmann::json)
│   │   ├── include/ajazz/core/           # Public headers (API boundary)
│   │   │   ├── device.hpp                # IDevice + DeviceDescriptor + DeviceId + DeviceFamily
│   │   │   ├── capabilities.hpp          # Capability mix-ins (IDisplayCapable, IEncoderCapable, etc.)
│   │   │   ├── transport.hpp             # ITransport abstract interface
│   │   │   ├── hid_transport.hpp         # HidTransport impl (libhidapi wrapper)
│   │   │   ├── device_registry.hpp       # Thread-safe registry (VID/PID → factory)
│   │   │   ├── profile.hpp               # Profile / ProfilePage / Action / ActionChain data structures
│   │   │   ├── profile_io.hpp            # readProfileFromDisk / writeProfileToDisk (JSON parse/serialize)
│   │   │   ├── profile_bundle.hpp        # Bundle .ajazzprofile export/import (ZIP with JSON + assets)
│   │   │   ├── action_engine.hpp         # ActionEngine (multi-action interpreter with sleep deferral)
│   │   │   ├── executor.hpp              # Executor abstraction (blocking vs. Qt event-loop sleep)
│   │   │   ├── event_bus.hpp             # EventBus (publish-subscribe for core events)
│   │   │   ├── hotplug_monitor.hpp       # OS-specific USB hot-plug watcher
│   │   │   ├── logger.hpp                # Logging framework (Logger + Sink pattern)
│   │   │   ├── log_sinks.hpp             # FileSink, RingBufferSink, StdoutSink implementations
│   │   │   ├── input_synthesizer.hpp     # IInputSynthesizer abstraction (OS key/mouse synthesis)
│   │   │   ├── macro_recorder.hpp        # Macro recording (experimental / Phase 21)
│   │   │   ├── notification_service.hpp  # Desktop notifications (Phase 23)
│   │   │   └── ... (others)
│   │   │
│   │   └── src/                          # Implementation
│   │       ├── device_registry.cpp
│   │       ├── hid_transport.cpp
│   │       ├── profile_io.cpp
│   │       ├── action_engine.cpp
│   │       ├── hotplug_monitor.cpp       # Platform-specific: hotplug_monitor_linux.cpp, _macos.cpp, _windows.cpp
│   │       ├── logger.cpp
│   │       └── ... (others)
│   │
│   ├── devices/                          # Device-specific backends (transport + protocol)
│   │   ├── streamdeck/                   # Stream Deck family (AKP815 custom C++; AKP05/N4/AKP03/AKP153 via sidecar)
│   │   │   ├── include/ajazz/streamdeck/
│   │   │   │   └── streamdeck.hpp        # Public API: registerAll(), streamDockSidecarDescriptors(), makeAkp815()
│   │   │   └── src/
│   │   │       ├── register.cpp          # Device registration; calls registerDevice() for AKP815 + sidecar descriptors
│   │   │       ├── akp815.cpp            # Akp815Device impl (5×3 key grid, 800×480 strip)
│   │   │       ├── akp815_protocol.hpp   # AKP815 constants (opcode enum, report IDs)
│   │   │       ├── akp815_wire.hpp/.cpp  # AKP815 HID builders (image upload, brightness, etc.)
│   │   │       ├── akp_common_protocol.hpp  # Shared opcodes (common to AKP05 + AKP815 era, now mostly sidecar)
│   │   │       └── image_pipeline.hpp/.cpp  # RGBA → JPEG/PNG encoding, Rot180, scaling (shared by all AKP)
│   │   │
│   │   ├── keyboard/                     # Keyboard family (AK980 VIA-compatible, etc.)
│   │   │   ├── include/ajazz/keyboard/
│   │   │   │   └── keyboard.hpp
│   │   │   └── src/
│   │   │       ├── register.cpp
│   │   │       ├── ak980.cpp             # Ak980Keyboard impl (RGB per-key, VIA protocol, keymap config)
│   │   │       ├── ak980_protocol.hpp
│   │   │       └── ... (others)
│   │   │
│   │   └── mouse/                        # Mouse family (AJ-series 2.4G wireless, etc.)
│   │       ├── include/ajazz/mouse/
│   │       │   └── mouse.hpp
│   │       └── src/
│   │           ├── register.cpp
│   │           ├── aj_series.cpp         # AjMouseDevice impl (DPI presets, battery, wireless config)
│   │           ├── aj_protocol.hpp
│   │           └── ... (others)
│   │
│   └── plugins/                          # Plugin host abstraction (C++ ↔ Python subprocess)
│       ├── include/ajazz/plugins/
│       │   ├── i_plugin_host.hpp         # IPluginHost interface (abstract backend)
│       │   ├── out_of_process_plugin_host.hpp  # OOP impl (POSIX subprocess + JSON IPC)
│       │   ├── sandbox.hpp               # Sandbox abstraction
│       │   ├── linux_bwrap_sandbox.hpp   # bubblewrap sandboxer
│       │   ├── macos_sandbox_exec_sandbox.hpp  # sandbox-exec sandboxer
│       │   ├── windows_app_container_sandbox.hpp  # AppContainer sandboxer
│       │   └── manifest_signer.hpp       # Ed25519 manifest signature verification
│       │
│       └── src/
│           ├── out_of_process_plugin_host.cpp
│           ├── linux_bwrap_sandbox.cpp
│           ├── macos_sandbox_exec_sandbox.cpp
│           ├── windows_app_container_sandbox.cpp
│           └── ... (others)
│
├── streamdock-host/                      # Out-of-process Rust sidecar (mirajazz-based)
│   ├── Cargo.toml                        # Rust deps: mirajazz, serde_json, tokio
│   └── src/
│       └── main.rs                       # Spawned by SidecarStreamDockDevice; JSON protocol on stdin/stdout
│
├── python/                               # Python plugin runtime
│   └── ajazz_plugins/                    # OOP plugin framework
│       ├── ajazz_plugins/                # Main module
│       │   ├── __init__.py
│       │   ├── plugin.py                 # Plugin base class (user code subclasses this)
│       │   ├── host.py                   # Host-side IPC server (recv JSON, dispatch to plugin instance)
│       │   ├── action.py                 # Action descriptor
│       │   └── ... (framework)
│       ├── tests/                        # pytest test suite for plugins
│       └── ... (others)
│
├── resources/                            # Static assets
│   ├── linux/                            # Linux-specific
│   │   ├── 70-ajazz.rules                # udev rules (HID device ACL)
│   │   ├── ajazz-control-center.desktop  # .desktop entry
│   │   └── ... (others)
│   ├── macos/                            # macOS-specific
│   ├── windows/                          # Windows-specific
│   ├── app.svg                           # Icon artwork
│   └── ... (others)
│
├── docs/                                 # Documentation
│   ├── schemas/                          # JSON schema definitions
│   │   ├── profile.schema.json           # Profile format specification
│   │   ├── plugin_manifest.schema.json   # .sdPlugin manifest spec
│   │   └── ... (others)
│   ├── protocols/                        # Hardware RE docs
│   │   ├── streamdeck/                   # AKP-family RE (opcodes, wire format)
│   │   ├── keyboard/                     # VIA protocol / AK980 notes
│   │   └── ... (others)
│   ├── architecture/                     # System design docs
│   └── ... (others)
│
├── tests/                                # Test suite
│   ├── unit/                             # Unit tests (per-module)
│   │   ├── test_action_engine.cpp        # ActionEngine interpreter tests
│   │   ├── test_device_registry.cpp      # Device registry + factory tests
│   │   ├── test_profile_io.cpp           # Profile JSON parse/serialize roundtrip
│   │   ├── test_sidecar_protocol.cpp     # Sidecar JSON encode/decode
│   │   ├── test_plugin_device_bridge.cpp # Context registry, coord conversion
│   │   └── ... (many more)
│   ├── integration/                      # Multi-component tests
│   │   ├── test_e2e_device_hot_plug.cpp  # Hot-plug enumeration flow
│   │   └── ... (others)
│   └── qml/                              # QML smoke tests (offscreen rendering)
│       └── test_qml_render.cpp
│
├── scripts/                              # Utility scripts
│   ├── ajazz-debug                       # Client for debug-control channel (drives app via JSON-RPC)
│   ├── sign-plugin-manifest.py           # Ed25519 sign a plugin manifest
│   ├── akp05_color_probe.py              # Hardware probe (paint all 15 BAT surfaces on AKP05E)
│   ├── akp05_strip_probe.py              # Map touch-strip zones on AKP05E
│   ├── akp05_input_probe.py              # Verify AKP05E input (encoder/touch) reachability
│   └── ... (others)
│
├── cmake/                                # CMake modules
│   ├── Warnings.cmake                    # Cross-platform warning flags
│   ├── Sanitizers.cmake                  # ASan/UBSan configuration
│   └── StreamdockSidecar.cmake           # Build rules for Rust sidecar
│
├── .planning/                            # GSD planning documents
│   ├── PROJECT.md                        # Project overview
│   ├── STATE.md                          # Current state snapshot
│   ├── ROADMAP.md                        # Phases and milestones
│   ├── codebase/                         # Auto-generated by codebase mapper
│   │   ├── ARCHITECTURE.md               # This file: system design
│   │   ├── STRUCTURE.md                  # Directory layout + file purposes
│   │   ├── STACK.md                      # Technology stack
│   │   ├── INTEGRATIONS.md               # External services
│   │   ├── CONVENTIONS.md                # Code style rules
│   │   ├── TESTING.md                    # Test patterns
│   │   └── CONCERNS.md                   # Technical debt
│   ├── phases/                           # Phase implementations (e.g., Phase 25, 26, 27, etc.)
│   ├── milestones/                       # v1.0, v1.1, v1.3 milestone artifacts
│   └── ... (others)
│
├── CMakeLists.txt                        # Top-level CMake build configuration
├── CMakePresets.json                     # Preset configurations (linux-release, windows-2022, macos-universal)
├── pyproject.toml                        # Python build config (plugin host)
├── CLAUDE.md                             # Project memory for AI contributors (conventions, gotchas, glossary)
├── Makefile                              # Helper targets (build, test, run, fmt, lint)
├── README.md                             # User-facing overview
├── CONTRIBUTING.md                       # Developer guide
├── CHANGELOG.md                          # Release notes
└── ... (git, CI, config files)
```

## Directory Purposes

**`src/app/`**

- Purpose: Qt GUI application layer; owns all services and user-facing subsystems
- Key entry: `main.cpp` (creates QApplication, loads QML, spins event loop)
- Qt services: TrayController, LightingService, BatteryService, TimeSyncService, SettingsService, PluginManager, SdPluginServer, PluginDeviceBridge, DebugControlServer
- Sidecar proxy: SidecarStreamDockDevice, sidecar_protocol
- Plugin catalog: PluginCatalogModel (OpenDeck downloads)
- Profile persistence: ProfileController (load/save JSON)

**`src/core/`**

- Purpose: Hardware-agnostic, zero-dependency core library shared by all layers
- Exports: IDevice, ITransport, Capability mix-ins, ActionEngine, Profile, Logger, DeviceRegistry
- No external deps (except C++20 stdlib); no Qt; no nlohmann::json
- Used by: device backends, Qt app, Python plugin host

**`src/devices/`**

- Purpose: USB protocol drivers per device family
- Stream Deck: AKP815 custom C++ (5×3 keys, 800×480 strip); AKP05/N4/AKP03/AKP153 registered as sidecar factories
- Keyboard: AK980 (RGB per-key, VIA-compatible keymap)
- Mouse: AJ-series (DPI stages, wireless battery, 2.4G dongle)
- All: Implement IDevice + capability mix-ins; use HidTransport

**`streamdock-host/`**

- Purpose: Out-of-process Rust sidecar (mirajazz crate)
- Spawned by: SidecarStreamDockDevice::open()
- Drives: AKP05/N4/AKP03/AKP153 via one persistent handle (no per-interaction open/close wedge)
- Protocol: Newline-delimited JSON over stdin/stdout

**`src/plugins/`**

- Purpose: Plugin host abstraction (C++ ↔ Python subprocess)
- Interface: IPluginHost (abstract)
- Impl: OutOfProcessPluginHost (POSIX subprocess + JSON-RPC)
- Sandboxing: Pluggable per-OS (bubblewrap on Linux, sandbox-exec on macOS, AppContainer on Windows)

**`python/`**

- Purpose: Python OOP plugin runtime
- Entry: `python/ajazz_plugins/` (framework + base Plugin class)
- Startup: OutOfProcessPluginHost spawns `python3 -m ajazz_plugins.host`
- Protocol: JSON-RPC over stdin/stdout

**`src/app/qml/`**

- Purpose: Qt Quick (QML) declarative UI
- Root: Main.qml (TabBar with tabs for DeviceList, ProfileEditor, PluginStore)
- Active device: DeviceView.qml (canvas with KeyCell grid + EncoderDial + TouchStrip zones)
- Drag-drop: ActionLibraryPane.qml (action picker for bindings)
- Config: Inspector.qml (property inspector for plugin actions)
- Components: `components/` subdirectory (reusable KeyCell, EncoderDial, etc.)

**`resources/`**

- Purpose: Platform-specific static assets
- Linux: udev rules (70-ajazz.rules), .desktop entry, app icon
- macOS: Info.plist template, app icon (ICNS)
- Windows: .rc icon resource, installer metadata

**`docs/`**

- Purpose: Specifications and research
- `schemas/`: JSON schema for profile.json, plugin manifests, etc.
- `protocols/`: Hardware reverse-engineering (opcode tables, wire format, AKP-family notes)
- `architecture/`: System design documents (PLUGIN-SYSTEM.md, etc.)

**`tests/`**

- Purpose: Unit, integration, and QML smoke tests
- `unit/`: Per-module tests (action_engine, device_registry, profile_io, sidecar_protocol, etc.)
- `integration/`: Multi-component (e.g., hot-plug enumeration flow)
- `qml/`: Offscreen QML rendering smoke tests

**`.planning/`**

- Purpose: GSD project planning and codebase documentation (auto-generated)
- `codebase/`: ARCHITECTURE.md, STRUCTURE.md, STACK.md, TESTING.md, CONVENTIONS.md, CONCERNS.md
- `phases/`: Implementation phases (Phase 25, 26, 27, etc.) with tasklists
- `milestones/`: v1.0, v1.1, v1.3 retrospectives and artifacts
- `PROJECT.md`: Project overview and goals
- `ROADMAP.md`: Milestone timeline and phase descriptions

## Key File Locations

**Entry Points:**

- `src/app/src/main.cpp`: QApplication creation, QML loading, event loop
- `src/app/src/application.cpp`: Application controller, bootstrap device backends, wire services

**Configuration:**

- `CMakeLists.txt`: Build configuration (dependencies, compiler flags, Qt modules)
- `CMakePresets.json`: Build presets (linux-release, windows-2022, macos-universal)
- `pyproject.toml`: Python plugin host build
- `CLAUDE.md`: Project memory for AI contributors

**Core Logic:**

- `src/core/include/ajazz/core/device.hpp`: IDevice interface
- `src/core/include/ajazz/core/action_engine.hpp`: Multi-action interpreter
- `src/core/include/ajazz/core/profile.hpp`: Profile data structures
- `src/core/include/ajazz/core/device_registry.hpp`: Device factory registry

**Device Backends:**

- `src/devices/streamdeck/src/akp815.cpp`: AKP815 custom C++ backend
- `src/devices/streamdeck/src/register.cpp`: Backend registration + sidecar descriptor list
- `src/app/src/sidecar_stream_dock_device.hpp/.cpp`: AKP05/N4 proxy to Rust sidecar

**Plugin System:**

- `src/app/src/sd_plugin_server.hpp/.cpp`: .sdPlugin WebSocket host
- `src/app/src/plugin_manager.hpp/.cpp`: .sdPlugin discovery and spawn
- `src/app/src/plugin_device_bridge.hpp/.cpp`: Plugin action ↔ device I/O convergence
- `src/plugins/include/ajazz/plugins/i_plugin_host.hpp`: OOP plugin host interface
- `python/ajazz_plugins/plugin.py`: Python plugin base class

**Services:**

- `src/app/src/stream_dock_control_service.hpp/.cpp`: Device render (key/encoder/strip image upload)
- `src/app/src/stream_dock_input_service.hpp/.cpp`: Device input polling + event dispatch
- `src/app/src/profile_controller.hpp/.cpp`: Profile load/save
- `src/app/src/tray_controller.hpp/.cpp`: System tray integration

**Debug Control:**

- `src/app/src/debug_control_server.hpp/.cpp`: Unix socket JSON-RPC server
- `src/app/src/debug_control_facade.hpp/.cpp`: RPC method registration
- `scripts/ajazz-debug`: Client script (shell wrapper over `socat` + `jq`)

**Testing:**

- `tests/unit/test_action_engine.cpp`: ActionEngine interpreter tests
- `tests/unit/test_device_registry.cpp`: Device factory registration
- `tests/unit/test_profile_io.cpp`: Profile JSON roundtrip
- `tests/unit/test_sidecar_protocol.cpp`: Sidecar JSON encode/decode
- `CMakeLists.txt`: Test target configuration (ctest)

## Naming Conventions

**Files:**

- C++ headers: `snake_case.hpp` (e.g., `device_registry.hpp`, `action_engine.hpp`)
- C++ sources: `snake_case.cpp` (e.g., `application.cpp`)
- QML files: `PascalCase.qml` (e.g., `Main.qml`, `DeviceView.qml`, `ActionLibraryPane.qml`)
- Test files: `test_<component>.cpp` (e.g., `test_action_engine.cpp`)
- Python modules: `snake_case.py` (e.g., `plugin.py`, `host.py`)
- Shell scripts: `kebab-case` (e.g., `ajazz-debug`, `sign-plugin-manifest`)

**Directories:**

- C++ module: `src/<module>/` with `include/ajazz/<module>/` and `src/` subdirs
- QML components: `src/app/qml/components/` for reusable items
- Tests: `tests/<type>/` (unit, integration, qml)
- Device family: `src/devices/<family>/` (streamdeck, keyboard, mouse)

**C++ Classes:**

- Interface: `I<Name>` (e.g., `IDevice`, `ITransport`, `IDisplayCapable`)
- Implementation: `<Name>` or `<Name>Impl` (e.g., `Akp815Device`, `HidTransport`, `SidecarStreamDockDevice`)
- Service: `<Name>Service` (e.g., `TimeSyncService`, `LightingService`)
- Qt Object: Any `QObject` subclass kept as-is, not prefixed (e.g., `Application`, `ProfileController`)

**Qt Singletons / Models:**

- QML_SINGLETON with static factory: `create(QQmlEngine*, QJSEngine*)` + `registerInstance()`
  Example: `ProfileController`, `BrandingService`
- List models: `<Name>Model` (e.g., `DeviceModel`, `LoadedPluginsModel`, `PluginCatalogModel`)

## Where to Add New Code

**New Feature (Device Support):**

- New device backend: `src/devices/<family>/`
- Public API: `src/devices/<family>/include/ajazz/<family>/<family>.hpp` (registerAll, factory)
- Implementation: `src/devices/<family>/src/` (device class, protocol headers, wire builders)
- Descriptor registration: Call `registry.registerDevice(descriptor, factory)` from `registerAll()`
- Tests: `tests/unit/test_<family>_<component>.cpp`

**New Feature (GUI Component):**

- QML component: `src/app/qml/components/<Component>.qml` (if reusable) or `src/app/qml/<Page>.qml` (if page-level)
- C++ backend (if needed): `src/app/src/<component>.hpp/.cpp`
- Qt services should be added as member fields to `Application` in `src/app/src/application.hpp`
- Expose to QML: `Application::exposeToQml()` as a context property
- Tests: `tests/qml/` or `tests/unit/test_<component>.cpp`

**New Feature (Service):**

- Header: `src/app/src/<service>_service.hpp` (QObject, if Qt-dependent; plain class if not)
- Implementation: `src/app/src/<service>_service.cpp`
- Ownership: Add as unique_ptr member to `Application` (constructed in ctor body)
- Exposure: If QML-accessible, expose via context property in `exposeToQml()`
- Tests: `tests/unit/test_<service>.cpp`

**New Action Type:**

- Action definition: Add `Kind::<NewKind>` enum to `ajazz::core::Action` in `src/core/include/ajazz/core/profile.hpp`
- Engine callback: Add callback to `ajazz::core::ActionExecutors` struct
- Executor impl: Add executor lambda in `src/app/src/application.cpp` (or in `src/app/src/qt_executor.cpp`)
- Tests: `tests/unit/test_action_engine.cpp` (parametrized test on `Kind`)

**New Plugin Capability (Python):**

- Plugin base class: Edit `python/ajazz_plugins/plugin.py`
- Manifest schema: Update `docs/schemas/plugin_manifest.schema.json`
- Host dispatcher: Edit `python/ajazz_plugins/host.py` if new RPC methods needed
- Tests: `python/ajazz_plugins/tests/test_*.py`

**Utilities (Core Library, No Qt):**

- Header: `src/core/include/ajazz/core/<utility>.hpp`
- Implementation: `src/core/src/<utility>.cpp`
- Tests: `tests/unit/test_<utility>.cpp`
- NO external dependencies; C++20 stdlib only

## Special Directories

**`src/app/src/`** (not `src/app/include/`)

- Qt application layer does NOT split headers/source by directory; all `.hpp` files live alongside `.cpp`
- Reason: QObject MOC integration; CMake target_include_directories directly adds this dir

**`.planning/codebase/`**

- Purpose: Auto-generated codebase documentation (ARCHITECTURE.md, STRUCTURE.md, STACK.md, etc.)
- Owner: GSD codebase mapper agent (`/gsd:map-codebase`)
- Not committed manually; regenerated on request

**`streamdock-host/`** (Rust subproject)

- Cargo workspace: Independent build; produces `streamdock-host` binary
- CMake integration: `cmake/StreamdockSidecar.cmake` handles Cargo invocation
- Output: Binary copied to `build/` + installed alongside app executable on release builds

**`python/`** (Python subproject)

- Poetry / pip integration: `pyproject.toml` specifies deps
- Runtime: `OutOfProcessPluginHost` spawns `python3 -m ajazz_plugins.host` as subprocess
- Not a traditional Python package install; shipped alongside app (embedded or PATH)

**`tests/qml/`**

- Headless Qt Quick rendering (no X11/Wayland display needed)
- Used by CI on Linux (offscreen) and macOS (offscreen)
- Currently a smoke test; full QML coverage deferred to Phase 28+

______________________________________________________________________

*Structure analysis: 2026-06-02*
