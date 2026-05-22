# Codebase Structure

**Analysis Date:** 2026-05-22

## Directory Layout

```
ajazz-control-center/
├── .github/
│   └── workflows/               # GitHub Actions CI/release pipelines
│
├── .planning/
│   ├── phases/                  # Per-phase GSD plans (Phase 1–13)
│   ├── milestones/              # Archived milestone artifacts (v1.0, v1.1)
│   ├── research/                # Technical research notes and findings
│   ├── PROJECT.md               # Master project document
│   ├── STATE.md                 # Current milestone state & blockers
│   └── codebase/                # Codebase analysis (ARCHITECTURE.md, STRUCTURE.md)
│
├── cmake/                       # CMake module utilities (Warnings.cmake, Sanitizers.cmake)
│
├── docs/
│   ├── architecture/            # ARCHITECTURE.md (high-level overview, links to subsystems)
│   │   ├── THREADING.md
│   │   ├── HOTPLUG.md
│   │   ├── PLUGIN-SYSTEM.md
│   │   ├── PROTOCOLS.md
│   │   └── BRANDING.md
│   ├── protocols/               # Per-device USB wire-format reverse-engineering docs
│   │   ├── CAPTURING.md         # Wireshark + usbmon capture runbook
│   │   ├── PROFILE_SCHEMA.md    # Profile JSON schema (action types, device fields)
│   │   ├── REVERSE_ENGINEERING.md
│   │   ├── streamdeck/
│   │   │   ├── akp153.md        # 15-key JPEG protocol
│   │   │   ├── akp03.md         # 6-key + 3-encoder protocol
│   │   │   ├── akp05.md         # 10-key + 4-encoder + touch strip
│   │   │   ├── akp815.md        # 15-key + 800×480 strip
│   │   │   ├── akp_device_matrix.md  # Full vendor codec table (~96 SKUs)
│   │   │   └── _research-sources.md  # Tag definitions (ajazz-sdk, opendeck-*)
│   │   ├── keyboard/
│   │   │   ├── proprietary.md   # AK980 PRO (0x0c45:0x8009) opcodes
│   │   │   └── via_protocol.md  # QMK VIA standard
│   │   └── mouse/
│   │       ├── aj_series.md     # AJ139/159/179/199 opcode table
│   │       └── aj_series_device_matrix.md  # SKU-to-wire-format mapping
│
├── python/
│   └── ajazz_plugins/           # Python plugin SDK (pytest tests, pybind11 module)
│       ├── tests/               # pytest unit tests for plugin host / trust-roots
│       ├── ajazz/               # Runtime `ajazz` module exposed to plugins
│       └── examples/            # Example plugins (template)
│
├── resources/
│   ├── device-db/               # Device catalogue JSON (keyboards.json, mice.json) — future
│   ├── linux/
│   │   ├── 70-ajazz.rules       # udev rules; must sort before 73-seat-late.rules
│   │   └── ajazz-control-center.desktop
│   ├── windows/                 # .rc (app icon + version info)
│   └── macos/                   # .icns bundle icon
│
├── scripts/
│   └── hex-to-cpparray.py       # Convert Wireshark hex dumps to C++ test fixtures
│
├── packaging/                   # Installer / package metadata (Flatpak, APT, etc.)
│
├── src/
│   ├── core/                    # Core library (public: abstract interfaces)
│   │   ├── include/ajazz/core/
│   │   │   ├── device.hpp               # IDevice interface; DeviceId; DeviceEvent
│   │   │   ├── device_registry.hpp      # DeviceRegistry (VID/PID ↔ factory; flyweight cache)
│   │   │   ├── transport.hpp            # ITransport interface (HID I/O seam)
│   │   │   ├── capabilities.hpp         # 68 KB — mix-in capability interfaces (IDisplayCapable, IRgbCapable, etc.)
│   │   │   ├── hid_transport.hpp        # HidTransport (libhidapi wrapper)
│   │   │   ├── hotplug_monitor.hpp      # Hot-plug polling + event injection seam
│   │   │   ├── profile.hpp              # Profile schema classes (Profile, Action, Key, ...)
│   │   │   ├── profile_io.hpp           # readProfileFromDisk() / writeProfileToDisk()
│   │   │   ├── profile_bundle.hpp       # exportProfileBundle() / importProfileBundle()
│   │   │   ├── event_bus.hpp            # Event pub-sub (publish/subscribe pattern)
│   │   │   ├── action_engine.hpp        # ActionEngine (dispatch actions from profiles)
│   │   │   ├── executor.hpp             # Executor interface (Qt event loop executor)
│   │   │   ├── logger.hpp               # Thread-safe logging (DEBUG/INFO/WARN/ERROR)
│   │   │   ├── macro_recorder.hpp       # Macro recording API (scaffolded)
│   │   │   └── notification_service.hpp # OS notification API (non-modal toasts)
│   │   └── src/
│   │       ├── device_registry.cpp      # Registry implementation
│   │       ├── hid_transport.cpp        # libhidapi wrapper; HID report I/O
│   │       ├── hotplug_monitor.cpp      # Timer-driven enumeration loop
│   │       ├── profile_io.cpp           # JSON parser/builder for profiles
│   │       ├── event_bus.cpp
│   │       ├── action_engine.cpp
│   │       ├── executor.cpp
│   │       ├── logger.cpp
│   │       └── notification_service.cpp
│
│   ├── devices/                 # Per-family device backends (pluggable modules)
│   │   ├── CMakeLists.txt
│   │   ├── streamdeck/
│   │   │   ├── include/ajazz/streamdeck/
│   │   │   │   ├── streamdeck.hpp       # Public factory functions (makeAkp153, makeAkp03, makeAkp05, makeAkp815)
│   │   │   │   ├── akp153_device.hpp
│   │   │   │   ├── akp03_device.hpp
│   │   │   │   ├── akp05_device.hpp
│   │   │   │   └── akp815_device.hpp
│   │   │   └── src/
│   │   │       ├── register.cpp         # Backend bootstrap; registerAll(DeviceRegistry&)
│   │   │       ├── akp153_device.cpp
│   │   │       ├── akp153_protocol.hpp  # Wire-format builders (AKP153 JPEG encoding, 85×85, …)
│   │   │       ├── akp03_device.cpp
│   │   │       ├── akp03_protocol.hpp   # AKP03 protocol (6 keys + 3 encoders, PNG)
│   │   │       ├── akp05_device.cpp
│   │   │       ├── akp05_protocol.hpp   # AKP05 protocol (10 keys + 4 encoders + touch)
│   │   │       ├── akp815_device.cpp
│   │   │       ├── akp815_protocol.hpp  # AKP815 protocol (15 keys, 100×100, 800×480 strip)
│   │   │       └── image_pipeline.{hpp,cpp}  # Qt6 QImage JPEG encoder (ARCH-04, Phase 10)
│   │   │
│   │   ├── keyboard/
│   │   │   ├── include/ajazz/keyboard/
│   │   │   │   ├── keyboard.hpp         # Public factories (makeViaKeyboard, makeProprietaryKeyboard)
│   │   │   │   ├── via_keyboard.hpp
│   │   │   │   └── proprietary_keyboard.hpp
│   │   │   └── src/
│   │   │       ├── register.cpp         # VIA (AK820 Pro @ 0x3151:0x4021) + Proprietary (AK980 PRO @ 0x0c45:0x8009)
│   │   │       ├── via_keyboard.cpp     # QMK VIA protocol
│   │   │       ├── proprietary_keyboard.cpp  # Microdia AK980 PRO (RTC @ 0x28, RGB @ 0x13, battery @ 0x20, …)
│   │   │       └── proprietary_protocol.hpp  # Opcode builders for AK980 PRO
│   │   │
│   │   └── mouse/
│   │       ├── include/ajazz/mouse/
│   │       │   ├── mouse.hpp            # Public factory (makeAjSeries)
│   │       │   └── aj_series_mouse.hpp
│   │       └── src/
│   │           ├── register.cpp         # AJ139/159/179/199 families (wired + 2.4GHz dongle)
│   │           ├── aj_series_mouse.cpp  # Wire-format parsers & builders
│   │           ├── aj_series_protocol.hpp  # Opcode table (DPI, RGB, TFT clock, battery, macros, …)
│   │           └── aj_series.hpp        # Protocol constants and helpers
│
│   ├── plugins/                 # Out-of-process plugin host (C++ side)
│   │   ├── include/ajazz/plugins/
│   │   │   └── plugin_host.hpp          # IPluginHost interface
│   │   └── src/
│   │       ├── out_of_process_plugin_host.cpp       # Main IPC broker (Qt / libusb event loop)
│   │       ├── out_of_process_plugin_host_win32.cpp # Win32 AppContainer sandbox launch
│   │       ├── sandbox.cpp              # Platform abstraction (launch child + IPC)
│   │       ├── linux_bwrap_sandbox.cpp  # Bubblewrap sandbox (Flatpak integration)
│   │       ├── macos_sandbox_exec_sandbox.cpp       # macOS sandbox_exec system call
│   │       ├── windows_app_container_sandbox.cpp    # Windows AppContainer API
│   │       ├── win32_env_block.{hpp,cpp}       # UTF-16 environment block builder (CR-01 fix)
│   │       ├── win32_python_resolve.hpp        # Find python3.exe on Windows PATH
│   │       ├── manifest_signer*.cpp            # Trust-roots parser + signature verification (SEC-003)
│   │       ├── wire_protocol.hpp               # JSON IPC message format (action dispatch, results)
│   │       └── process_attributes_impl_win32.hpp  # Win32 process attributes (CREATE_UNICODE_ENVIRONMENT)
│
│   └── app/                     # Qt 6 application layer
│       ├── qml/                 # QML components (Material Design 3 UI)
│       │   ├── Main.qml
│       │   ├── components/      # Component library (DeviceList, KeyDesigner, RgbPicker, ProfileEditor, …)
│       │   └── icons/           # Material Design Icons SVG embedded
│       │
│       └── src/                 # Application controller & services (C++)
│           ├── main.cpp         # Entry point; Qt app setup; single-instance lock; QML load
│           ├── application.{hpp,cpp}        # Top-level controller (bootstrap, backend registration)
│           ├── device_model.{hpp,cpp}       # QML list model of devices (hot-plug sync)
│           ├── profile_controller.{hpp,cpp} # Load/save profiles; dispatch actions
│           ├── property_inspector_controller.{hpp,cpp}  # Plugin HTML property inspector (Qt WebEngine)
│           ├── pi_bridge.{hpp,cpp}         # WebChannel bridge (property inspector ↔ C++)
│           ├── time_sync_service.{hpp,cpp} # Phase 5 — per-device Sync button + auto-sync toggle (QML_SINGLETON)
│           ├── lighting_service.{hpp,cpp}  # Phase 8+ — AK980 PRO 20-mode RGB picker (IFirmwareLightingCapable)
│           ├── settings_service.{hpp,cpp}  # Phase 3.6+ — AK980 PRO settings batch (ISettingsCapable)
│           ├── battery_service.{hpp,cpp}   # Battery poller + low-battery toast (IBatteryCapable)
│           ├── theme_service.{hpp,cpp}     # Light/dark/system theme toggle (QML_SINGLETON)
│           ├── branding_service.{hpp,cpp}  # Product strings + vendor name
│           ├── firmware_update_service.{hpp,cpp}  # Firmware flash workflow
│           ├── app_update_service.{hpp,cpp}      # Check-for-updates + download
│           ├── autostart_service.{hpp,cpp}       # Launch-at-login toggle (#35)
│           ├── tray_controller.{hpp,cpp}   # System tray icon + context menu
│           ├── hotplug_debouncer.{hpp,cpp} # 300 ms debounce for hot-plug events (HOTPLUG-01)
│           ├── device_model_roles.hpp      # Qt::ItemDataRole enums (Name, Family, Online, …)
│           ├── device_maturity_map.generated.hpp  # Auto-generated device maturity lookup
│           ├── loaded_plugins_model.{hpp,cpp}    # QML list model of loaded plugins
│           ├── plugin_catalog_model.{hpp,cpp}    # QML mock plugin store
│           ├── sdplugin_extractor.{hpp,cpp}      # Extract .sdPlugin archives at install time (issue #62)
│           ├── opendeck_catalog_fetcher.{hpp,cpp}# Download OpenDeck device catalogue
│           ├── streamdock_catalog_fetcher.{hpp,cpp}  # Download Stream Deck device catalogue
│           ├── single_instance_guard.{hpp,cpp}   # Socket-based single-instance lock
│           ├── app_icon.{hpp,cpp}                # Multi-size app icon renderer from SVG
│           └── qt_executor.{hpp,cpp}            # Qt event loop executor (implements Executor interface)
│
├── tests/
│   ├── CMakeLists.txt
│   ├── unit/                    # Unit tests (~180 test cases, 286 TEST_CASE invocations as of 2026-05-18)
│   │   ├── CMakeLists.txt       # Catch2 test runner configuration
│   │   ├── fixtures/            # Test data and mock helpers
│   │   │   └── mock_transport.hpp    # MockTransport test double (COD-026 DI seam)
│   │   ├── test_device_registry.cpp          # Registry enumeration + factory dispatch
│   │   ├── test_event_bus.cpp                # Pub-sub event routing
│   │   ├── test_action_engine.cpp            # Action dispatch (direct + plugin-routed)
│   │   ├── test_profile_io.cpp               # Profile JSON load/save/validate
│   │   ├── test_time_sync_service.cpp        # TimeSyncService (setTime + IClockCapable integration)
│   │   ├── test_battery_service.cpp          # Battery polling + low-charge toast
│   │   ├── test_out_of_process_plugin_host.cpp  # Plugin spawning + manifest sig verification (SEC-003)
│   │   ├── test_*_protocol.cpp               # Per-device wire-format encoding/decoding
│   │   │   ├── test_akp153_protocol.cpp
│   │   │   ├── test_akp03_protocol.cpp
│   │   │   ├── test_akp05_protocol.cpp
│   │   │   ├── test_ak980_*.cpp              # AK980 PRO (RTC, RGB, settings, battery)
│   │   │   └── test_aj_series_*.cpp          # AJ-series mouse (DPI, RGB, macros, TFT clock)
│   │   ├── test_*_sandbox.cpp                # Platform-specific sandboxing (Linux bwrap, macOS, Win32)
│   │   ├── test_pi_bridge.cpp                # WebChannel property inspector IPC
│   │   ├── test_theme_service.cpp            # Theme persistence + Material attached props
│   │   ├── test_branding_service.cpp
│   │   ├── test_win32_env_block.cpp          # UTF-16 environment block ordering (CR-01 fix)
│   │   ├── test_app_update_service.cpp
│   │   ├── qt_app_fixture.hpp                # Qt QApplication + QQmlApplicationEngine setup
│   │   └── mock_hid_enumerator.hpp           # Mock HID device enumerator (HOTPLUG-06 test seam)
│   │
│   ├── integration/              # Integration tests (cross-layer workflows)
│   │   ├── CMakeLists.txt
│   │   ├── fixtures/             # Multi-device test harness (live HidTransport or MockTransport)
│   │   └── test_multi_device_hotplug.cpp    # Hot-plug symphony test (HOTPLUG-06)
│   │
│   └── fuzz/                    # libFuzzer harnesses (opt-in, Clang-only)
│       ├── CMakeLists.txt
│       ├── fuzz_trust_roots_parser.cpp  # Fuzz trust-roots JSON (SEC-003 input validation)
│       └── fuzz_profile_parser.cpp      # Fuzz profile JSON (input validation)
│
├── CMakeLists.txt              # Root CMake; project-wide settings, Qt 6 discovery
├── build/dev/                  # Out-of-source build directory (dev preset)
│   ├── build.log               # CMake configure output
│   └── ctest_results.xml       # CTest results (ctest --preset linux-release)
│
├── .gitignore
├── CLAUDE.md                   # Project conventions (Qt gotchas, direct-to-main workflow, hard rules)
├── CHANGELOG.md                # Release notes (updated at each milestone)
├── README.md                   # Auto-generated device tables + quick-start guide
├── LICENSE                     # GPL-3.0-or-later
└── .pre-commit-config.yaml     # Pre-commit hooks (ruff, clang-format, gitleaks, …)
```

## Directory Purposes

**`.planning/`:**

- Purpose: GSD (Generate Structured Designs) phases, milestone artifacts, technical research.
- Contains: Phase 1–13 plans, v1.0/v1.1 archived milestones, PROJECT.md (master requirements), STATE.md (current progress), research findings.
- Key files: `.planning/PROJECT.md` (updated 2026-05-15), `.planning/STATE.md` (current position), `.planning/phases/` (per-phase plan artifacts).

**`cmake/`:**

- Purpose: CMake module utilities shared across the build.
- Contains: Warnings.cmake (`-Wall -Wextra -Wpedantic`), Sanitizers.cmake (ASan/UBSan/TSAN options).

**`docs/`:**

- Purpose: User documentation and reverse-engineering reference.
- Key files:
  - `docs/architecture/ARCHITECTURE.md` — High-level overview with links to subsystem docs.
  - `docs/architecture/THREADING.md`, `HOTPLUG.md`, `PLUGIN-SYSTEM.md`, `PROTOCOLS.md` — Deep dives on specific subsystems.
  - `docs/protocols/CAPTURING.md` — Wireshark + usbmon runbook for USB protocol capture.
  - `docs/protocols/streamdeck/akp{153,03,05,815}.md` — Per-model wire formats.
  - `docs/protocols/keyboard/proprietary.md` — AK980 PRO opcodes.
  - `docs/protocols/mouse/aj_series*.md` — AJ-series wire formats.

**`python/ajazz_plugins/`:**

- Purpose: Python plugin SDK and tests (pytest).
- Contains: `ajazz` runtime module (pybind11), example plugins, trust-roots parser test suite.

**`resources/`:**

- Purpose: Data files, icons, udev rules.
- Key files:
  - `resources/linux/70-ajazz.rules` — udev rules (MUST sort before `73-seat-late.rules`).
  - `resources/device-db/` — Future device catalogue JSON (not yet integrated).

**`scripts/`:**

- Purpose: Dev-time utility scripts.
- Key: `hex-to-cpparray.py` — Convert Wireshark hex dumps to C++ test fixture headers.

**`src/core/`:**

- Purpose: Core library (public abstract interfaces, no device-specific code).
- Public headers: `include/ajazz/core/*.hpp` — IDevice, ITransport, capabilities, registry, profiles, logger.
- Implementation: `src/*.cpp` — HID transport, registry, hot-plug monitor, profile I/O, event bus.

**`src/devices/{streamdeck,keyboard,mouse}/`:**

- Purpose: Per-family device backend modules.
- Structure:
  - `include/ajazz/{streamdeck,keyboard,mouse}/` — Public factory functions (`makeAkp153`, `makeViaKeyboard`, `makeAjSeries`).
  - `src/register.cpp` — Bootstrap function (`registerAll(DeviceRegistry&)`); VID/PID ↔ factory mapping.
  - `src/*_device.cpp` — Concrete device class implementations.
  - `src/*_protocol.hpp` — Wire-format constants, builders, and decoders.

**`src/plugins/`:**

- Purpose: Out-of-process plugin host (C++ side).
- Contains: IPC broker, sandboxing (platform-specific), trust-roots parser, manifest signer.

**`src/app/`:**

- Purpose: Qt 6 application layer (UI and services).
- `qml/` — QML components (Material Design 3 UI).
- `src/` — Application controller, services (time sync, battery, settings, lighting, etc.), models (device list, loaded plugins).

**`tests/unit/`:**

- Purpose: Unit tests for the core library and all backends.
- Contains: ~180 test cases covering registry, event bus, action engine, all wire-format protocols, plugin host, platform sandboxing.
- Naming: `test_<subsystem>.cpp` (e.g., `test_akp153_protocol.cpp`, `test_time_sync_service.cpp`).

**`tests/integration/`:**

- Purpose: Cross-layer integration tests (multi-device hot-plug, etc.).
- Key: `test_multi_device_hotplug.cpp` — Harness to test device addition/removal without real USB.

**`tests/fuzz/`:**

- Purpose: libFuzzer harnesses (opt-in, Clang-only).
- Key: `fuzz_trust_roots_parser.cpp` (SEC-003 input validation), `fuzz_profile_parser.cpp` (profile JSON validation).

## Key File Locations

**Entry Points:**

- `src/app/src/main.cpp`: Qt application entry point; parses CLI flags; single-instance gate; loads QML; starts background services.
- `src/app/src/application.cpp` (method `bootstrap()`): Calls `registerAll()` on all device families; populates registry.

**Configuration:**

- `CMakeLists.txt`: Root CMake; Qt 6 discovery, options (AJAZZ_BUILD_APP, AJAZZ_BUILD_TESTS, AJAZZ_BUILD_PYTHON_HOST).
- `.pre-commit-config.yaml`: Hook plugins (ruff for Python, clang-format for C++, gitleaks for secrets).
- `CLAUDE.md`: Project memory (Qt gotchas, workflow conventions, hard rules).

**Core Logic:**

- `src/core/include/ajazz/core/capabilities.hpp`: 68 KB capability interface definitions (68 KB — load-bearing for the dynamic_cast discovery pattern).
- `src/core/include/ajazz/core/device_registry.hpp`: Registry interface and flyweight cache contract.
- `src/devices/{family}/src/register.cpp`: Per-family bootstrap; VID/PID registration.

**Testing:**

- `tests/unit/test_device_registry.cpp`: Registry enumeration test.
- `tests/unit/test_*_protocol.cpp`: Wire-format encode/decode tests for each backend.
- `tests/unit/mock_hid_enumerator.hpp`: Mock for DeviceRegistry::HidEnumerator (HOTPLUG-06 test seam).
- `tests/unit/fixtures/mock_transport.hpp`: MockTransport test double (COD-026 DI seam).

**Reverse-Engineering Docs:**

- `docs/protocols/streamdeck/akp{153,03,05,815}.md`: Per-model USB protocol.
- `docs/protocols/keyboard/proprietary.md`: AK980 PRO opcode table.
- `docs/protocols/mouse/aj_series_opcode_table.md`: AJ-series opcode reference.
- `docs/protocols/CAPTURING.md`: Wireshark + usbmon capture runbook (Phase 9 prerequisite).

## Naming Conventions

**Files:**

- **Backend implementations:** `<model>_device.cpp` (e.g., `akp153_device.cpp`, `proprietary_keyboard.cpp`, `aj_series_mouse.cpp`).
- **Protocol headers:** `<model>_protocol.hpp` (e.g., `akp05_protocol.hpp`); contains opcode enums, packet builders, wire-format constants.
- **Tests:** `test_<subsystem>.cpp` (e.g., `test_akp03_protocol.cpp`, `test_time_sync_service.cpp`); unit tests are in `tests/unit/`, integration in `tests/integration/`.
- **Service implementations:** `<service_name>_service.{hpp,cpp}` (e.g., `time_sync_service.hpp`, `battery_service.cpp`).

**Directories:**

- **Device backends:** `src/devices/{streamdeck,keyboard,mouse}/` — lowercase, plural when family is diverse (keyboard covers VIA + proprietary; mouse covers AJ-series variants).
- **Include structure:** `include/ajazz/{core,streamdeck,keyboard,mouse,plugins}/` — double-nested under namespace + project (allows coexistence with other `ajazz::*` libraries if ever extracted).

**Codenames** (used throughout codebase):

- **Stream Deck:** `akp153`, `akp03`, `akp05`, `akp815`, `mirabox_n3`, `mirabox_n4` (lowercase, underscore-separated).
- **Keyboard:** `ak820pro`, `ak980pro` (lowercase, underscore-separated if multi-word).
- **Mouse:** `aj_series_wired_primary`, `aj159_apex_wired`, `ajazz_24g_8k` (consistently lowercase + underscore).

## Where to Add New Code

**New Device Backend:**

1. Create `src/devices/<family>/` if it does not exist (e.g., `src/devices/gamepad/`).
1. Add a header file: `src/devices/<family>/include/ajazz/<family>/<family>.hpp` — declare `DeviceDescriptor` and factory functions (`make<Model>`).
1. Add a device class file: `src/devices/<family>/src/<model>_device.cpp` — inherit `IDevice` and optionally capability mix-ins (IDisplayCapable, IRgbCapable, etc.).
1. Add a protocol file: `src/devices/<family>/src/<model>_protocol.hpp` — opcode enums, wire-format builders, constants (widthPx, heightPx, etc.).
1. Add a registration file: `src/devices/<family>/src/register.cpp` — `void registerAll(core::DeviceRegistry& registry)` function; register each VID/PID pair with its factory.
1. Update `src/app/src/application.cpp::Application::bootstrap()` — call the new family's `registerAll(m_deviceRegistry)`.
1. Add CMakeLists.txt in `src/devices/<family>/` — define the library target; link against `ajazz_core`.
1. Add unit tests: `tests/unit/test_<model>_protocol.cpp` — test wire-format encode/decode with mock transport.

**New Service (QML Singleton):**

1. Create `src/app/src/<service_name>_service.{hpp,cpp}` — inherit from `QObject`, add `Q_OBJECT` macro.
1. Add slots for actions (e.g., `void syncDevice(QString deviceId)`).
1. Emit signals for results (e.g., `void syncCompleted(bool success, QString message)`).
1. Register in `src/app/src/application.cpp::Application::Application()` — construct the service and call `qmlRegisterSingletonInstance<ServiceName>(...)`.
1. Add a build-break assertion: `static_assert(!std::is_default_constructible_v<ServiceName>, ...)` (prevents the QML_SINGLETON dual-instance bug; see CLAUDE.md).
1. Add unit tests: `tests/unit/test_<service_name>.cpp` — mock DeviceRegistry, inject mock devices, verify signals.

**New QML Component:**

1. Create `src/app/qml/components/<ComponentName>.qml`.
1. Use Material Design 3 components from QtQuick.Controls, apply Material attached properties (Material.primary, Material.backgroundColor, etc.).
1. Connect to Application context properties (DeviceModel, ProfileController, etc.) or services (TimeSyncService, etc.).
1. Add a corresponding `.qmltype` descriptor if the component is exposed as a custom type (not needed for one-off components in Main.qml or other QML files).

**New Test:**

1. Unit test → `tests/unit/test_<subsystem>.cpp` — use Catch2 `TEST_CASE` and `REQUIRE` macros.
1. Integration test → `tests/unit/test_<subsystem>.cpp` or `tests/integration/test_<scenario>.cpp` — use the multi-device fixture if testing hot-plug or multi-backend interaction.
1. Fuzz test → `tests/fuzz/fuzz_<subsystem>.cpp` (opt-in; Clang only) — link against libFuzzer runtime.

**New Protocol Doc:**

1. Create `docs/protocols/{streamdeck,keyboard,mouse}/<model>.md`.
1. Include sections: USB descriptors, opcode reference, packet layout (hex tables), known limitations, reverse-engineering source citations (`[ajazz-sdk]`, `[opendeck-*]`, etc.).
1. Link from the family overview (e.g., `docs/protocols/streamdeck/akp03.md` is referenced in `docs/protocols/streamdeck/akp_device_matrix.md`).

## Special Directories

**`build/dev/`:**

- Purpose: Out-of-source CMake build directory.
- Generated by: `cmake -B build/dev -DCMAKE_BUILD_TYPE=Release` (or `Debug` for dev).
- Key files: `ctest_results.xml` (CTest output), `compile_commands.json` (for IDE integration).
- Committed: No — `.gitignore` excludes the entire `build/` tree.

**`.planning/phases/`:**

- Purpose: GSD phase artifacts (one subdirectory per phase; e.g., `09-research-captures-hygiene/`).
- Contains: Phase plan (markdown), per-plan artifacts (ADRs, decision docs), commit hashes for traceability.
- Pattern: Phase naming follows GSD convention: `<NN>-<slug>/` where NN is the phase number and slug is a kebab-case descriptor.

**`.planning/milestones/`:**

- Purpose: Archived milestone documents (sealed after completion).
- Contains: v1.0 and v1.1 retrospectives, phase summaries, completion date.

**`tests/unit/fixtures/`:**

- Purpose: Mock helpers and test data.
- Key: `mock_transport.hpp` (MockTransport for USB I/O testing), `qt_app_fixture.hpp` (Qt event loop setup for each test), `mock_hid_enumerator.hpp` (synthetic USB device enumeration).

______________________________________________________________________

*Structure analysis: 2026-05-22*
