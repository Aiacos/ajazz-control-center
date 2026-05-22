# Technology Stack

**Analysis Date:** 2026-05-22

## Languages

**Primary:**

- C++20 - Core application, device drivers, plugin host infrastructure (`src/core/`, `src/devices/`, `src/plugins/`, `src/app/`)
- C - Conditionally linked for platform-specific sandbox implementations (bwrap on Linux, sandbox-exec on macOS, App Container on Windows)
- QML - Qt 6 declarative UI for the desktop application (`src/app/qml/`)

**Secondary:**

- Python 3.11+ - Out-of-process plugin runtime; spawned via `execvp("python3", ...)` at runtime with no compile-time dependency (`python/ajazz_plugins/`)

## Runtime

**Environment:**

- Qt 6.7+ - Cross-platform application framework (GUI, networking, event loop)
- CMake 3.28+ - Build configuration and dependency orchestration
- Ninja - Build system (configured in `CMakePresets.json`)

**Package Manager:**

- vcpkg - Vendored dependencies (nlohmann-json 3.12.0) via `vcpkg.json`
- CMake FetchContent - Vendored builds for reproducibility
  - hidapi 0.14.0 (from `libusb/hidapi.git`, tag `hidapi-0.14.0`)
  - nlohmann-json 3.12.0 (from `nlohmann/json.git`, tag `v3.12.0`)
  - Catch2 3.7.1 (from `catchorg/Catch2.git`, tag `v3.7.1`) - testing only
  - pybind11 2.13.6 (Flatpak manifest only; not used in current OOP host architecture)
- Lockfile: `vcpkg.lock.yaml` present

## Frameworks

**Core:**

- Qt 6.7 (minimum) - Desktop UI, networking, QML engine, event loop
  - Qt6::Core - Event loop, strings, containers, logging
  - Qt6::Gui - Text rendering, image handling
  - Qt6::Network - QNetworkAccessManager for plugin catalog fetching
  - Qt6::Quick - QML runtime
  - Qt6::QuickControls2 - Material Design 3 UI components (optional `Qt6::QuickControls2Material`)
  - Qt6::Test - Qt testing framework (used by unit tests)
  - Qt6::Widgets - Legacy fallback components
  - Qt6::Svg - SVG rendering for icons
  - Qt6::CorePrivate - Private QZipReader/QZipWriter for `.sdPlugin` archive extraction (`src/app/src/sdplugin_extractor.cpp`)
  - Qt6::WebSockets - Elgato Stream Deck v6-compatible plugin server (`src/app/src/sd_plugin_server.cpp`); optional but enabled by default
  - Qt6::WebEngineQuick + Qt6::WebChannelQuick - HTML Property Inspector embedding (optional, graceful fallback to native inspector)

**Testing:**

- Catch2 3.7.1 - C++ unit and integration test framework
  - ~286 test cases across `tests/unit/` and `tests/integration/`
  - Run command: `ctest --preset linux-release` (see `CMakePresets.json`)
- pytest 8.0+ - Python plugin SDK tests (`python/ajazz_plugins/tests/`)
- libFuzzer (Clang-only, opt-in via `-DAJAZZ_BUILD_FUZZ_TESTS=ON`) - Security harnesses for plugin-host trust primitives

**Build/Dev:**

- CMake - Configuration, dependency management, cross-platform build generation
- pkg-config - Hardware library detection on Linux (optional; fallback to CMake)
- clang-tidy - Static analysis (CI via `.github/workflows/lint.yml`)
- sanitizers - ASan/UBSan (Linux Debug preset), ThreadSanitizer (optional), code coverage (optional)
- flatpak-builder - Containerized builds for Flatpak distribution

## Key Dependencies

**Critical:**

- hidapi 0.14.0 - HID device enumeration and I/O

  - Backend: `hidapi_hidraw` (Linux, kernel-native `/dev/hidraw*`) exclusively
  - libusb backend explicitly disabled (both in CMakeLists.txt and Flatpak manifest)
  - Used by: `src/core/src/hid_transport.cpp`, device hotplug monitoring

- nlohmann-json 3.12.0 - JSON parsing and serialization

  - **Scoping rule (COD-031):** PRIVATE-linked to `ajazz_plugins` only
  - **NOT** in `ajazz_core` public headers or `ajazz_app`
  - Used by: `src/plugins/src/manifest_signer_common.cpp` (plugin trust root loading)
  - Qt's QJsonDocument/QJsonObject used in `ajazz_core` and `ajazz_app` instead

**Infrastructure:**

- pybind11 2.13.6 - (Flatpak manifest only; not used in current code path)
  - Deprecated in favour of out-of-process plugin host (`src/plugins/src/out_of_process_plugin_host.cpp`)

## Configuration

**Environment:**

- CMake cache variables (set via `CMakePresets.json` or `-D` flags):
  - `CMAKE_BUILD_TYPE`: Debug, RelWithDebInfo
  - `CMAKE_CXX_STANDARD`: 20 (enforced)
  - `CMAKE_EXPORT_COMPILE_COMMANDS`: ON (for editor integration)
  - `AJAZZ_BUILD_APP`: ON (desktop application)
  - `AJAZZ_BUILD_TESTS`: ON (unit/integration tests)
  - `AJAZZ_BUILD_PYTHON_HOST`: ON (out-of-process plugin host infrastructure)
  - `AJAZZ_BUILD_PROPERTY_INSPECTOR`: ON (HTML Property Inspector via WebEngine)
  - `AJAZZ_BUILD_FUZZ_TESTS`: OFF (libFuzzer harnesses, opt-in)
  - `AJAZZ_ENABLE_WERROR`: ON (treat warnings as errors)
  - `AJAZZ_ENABLE_SANITIZERS`: ON (Debug builds only)
  - `AJAZZ_ENABLE_TSAN`: OFF (mutually exclusive with ASan)
  - `AJAZZ_ENABLE_COVERAGE`: OFF (code coverage instrumentation)
  - `AJAZZ_BRAND_DIR`: Optional override for app branding assets

**Build:**

- `CMakeLists.txt` - Root configuration (`cmake_minimum_required(3.28)`)
- `CMakePresets.json` - Build presets (linux-debug, linux-release, windows-*, macos-*, dev, release, coverage)
- `src/core/CMakeLists.txt` - Core library (hidapi, Qt6::Core linked PRIVATELY)
- `src/devices/CMakeLists.txt` - Device family subdirectories (keyboard, mouse, streamdeck)
- `src/plugins/CMakeLists.txt` - Plugin host infrastructure (nlohmann-json linked PRIVATELY)
- `src/app/CMakeLists.txt` - Desktop application (Qt6::Quick, WebSockets, WebEngine)
- `tests/CMakeLists.txt` - Test suite (Catch2 fetched, pytest for Python)
- `cmake/Warnings.cmake` - Compiler warning configuration per platform
- `cmake/Sanitizers.cmake` - ASan/UBSan/TSAN configuration
- `vcpkg.json` - Manifest for vcpkg dependency management
- `pyproject.toml` - Python package configuration (setuptools backend, ruff linting, mypy typing, pytest)

## Platform Requirements

**Development:**

- GCC 11+ or Clang 14+ (Linux)
- Apple Clang 14+ (macOS, enforces `-Werror` on `-Wunused-const-variable`)
- MSVC 2022+ (Windows, enforces `/W4 /WX` with C4996 deprecation warnings)
- Python 3.11+ (for plugin runtime; not a compile-time requirement)
- CMake 3.28+ with Ninja
- Qt 6.7+ SDK (MaintenanceTool or aqtinstall; CI uses aqtinstall which omits `Qt6CorePrivate` CMake config, so CMakeLists synthesizes a fallback)

**Production:**

- Linux (primary) - x86_64, tested on Fedora 44+
  - Flatpak 1.14+ (optional containerized distribution)
  - udev rules installed (`resources/linux/70-ajazz.rules`)
  - Systemd 258+ (note: known ACL regression with synthetic re-enumeration; see CLAUDE.md)
- macOS 12+ (Intel/Apple Silicon universal binaries)
- Windows 10 (build 1909+) / Windows 11

**Deployment:**

- Linux: `.deb` (Debian), `.rpm` (Fedora), `.flatpak` (containerized)
- macOS: Universal `.dmg` (Intel + Apple Silicon)
- Windows: MSI installer, portable ZIP

______________________________________________________________________

*Stack analysis: 2026-05-22*
