# Technology Stack

**Analysis Date:** 2026-06-02

## Languages

**Primary:**

- C++ 20 - Qt6/QML-based desktop application, core device drivers, and plugin system (`src/`, CMakeLists.txt:14)
- QML 6 - User interface layer (`src/app/qml/`)
- Rust 2021 edition - Out-of-process sidecar for Stream Deck device families (`streamdock-host/`)
- Python 3.11+ - Plugin SDK and out-of-process plugin host runtime (`python/`, `pyproject.toml`:9)

**Secondary:**

- C - HID device access via hidapi library (vendored)
- JavaScript/TypeScript - Vendor plugin Property Inspector HTML pages (embedded in QML via WebEngine)

## Runtime

**Environment:**

- Qt 6.7+ (required, `CMakeLists.txt`:59)
- Rust toolchain (1.0+) for sidecar binary compilation
- Python 3.11+ for plugin host (runtime spawned via `execvp`, no embedded interpreter)
- CMake 3.28+ (build-time, `CMakeLists.txt`:1)
- Ninja build generator (primary, `CMakePresets.json`:9)

**Package Manager:**

- CMake/vcpkg for C++/Rust deps - `vcpkg.json` pins nlohmann-json 3.12.0
- Cargo for Rust sidecar - `streamdock-host/Cargo.toml` (mirajazz as git dependency)
- setuptools + pip for Python plugin SDK - `pyproject.toml`
- Lockfile: CMake FetchContent for Qt dependencies; vcpkg baseline managed in `vcpkg.json`; Cargo.lock generated

## Frameworks

**Core:**

- Qt 6.7+ - Cross-platform GUI framework with QML, widgets, networking, WebSockets, WebEngine
  - Core: event loop, JSON, file I/O, settings storage
  - Gui: image manipulation, QZipReader/Writer for `.sdPlugin` archive extraction
  - Network: `QNetworkAccessManager` for HTTP(S) catalog/update fetches
  - Quick/QuickControls2: QML scene graph and Material Design controls
  - WebSockets: Elgato Stream Deck v6 plugin server (loopback-only)
  - WebEngineQuick + WebChannelQuick: Property Inspector HTML rendering (optional, fallback to native)
  - Test: Qt unit test framework (part of the test suite)
  - Widgets: System tray integration, fallback dialogs
  - Svg: Application icon and UI graphics

**Plugin System:**

- Python Plugin SDK (`ajazz_plugins` package) - base `Plugin` class, `@action` decorator, `ActionContext`
- Elgato Stream Deck v6 WebSocket protocol - `SdPluginServer` implements the wire spec
- JSON-RPC debug control channel (Unix domain socket) - out-of-process `scripts/ajazz-debug` client

**Device Backends:**

- **Stream Deck (AKP03/AKP05/AKP153):** Rust mirajazz sidecar (`streamdock-host/`) over JSON/stdio
- **Stream Deck (AKP815):** Custom C++ backend (`src/devices/streamdeck/src/akp815*`)
- **Keyboards (AK-series):** Custom C++ protocol handlers (`src/devices/keyboard/`)
- **Mice (AJ-series):** Custom C++ protocol handlers (`src/devices/mouse/`)

**Testing:**

- Catch2 (vendored) - C++ unit test framework (~399 test cases in `tests/unit/`)
- pytest - Python plugin SDK tests (`python/ajazz_plugins/tests/`)
- libFuzzer (optional, Clang-only) - security fuzzing for trust-root loading (`tests/fuzz/`)
- Qt Test Framework - QML smoke tests (`tests/qml/`)

**Build/Dev:**

- CMake 3.28+ with Ninja generator
- Qt6 toolchain (6.7+) with optional MaintenanceTool for private headers
- Cargo (Rust) for sidecar binary build
- pre-commit hooks (`.pre-commit-config.yaml`) - formatting, linting, security (gitleaks, typos)
- ruff (Python linting/formatting), mypy (type checking), black (formatter)
- clang-format / clang-tidy - C++ code style enforcement
- doxygen - API documentation generation (`Doxyfile`)

## Key Dependencies

**Critical:**

- **hidapi 0.14.0** (vendored via FetchContent or system, `CMakeLists.txt`:187–203) - Cross-platform HID device enumeration and I/O. Statically linked. `hidapi::hidraw` backend on Linux (kernel `hidraw*` API), WinUSB on Windows, Darwin native on macOS.
- **nlohmann/json 3.12.0** (vendored, PRIVATE-linked to `ajazz_plugins` only per COD-031, `CMakeLists.txt`:206–218) - JSON serialization for plugin manifest validation. Not exposed in public headers.
- **mirajazz (Rust crate, git dependency)** (`streamdock-host/Cargo.toml`:23) - Pure Rust implementation of the Stream Deck AKP05/N4 protocol. Replaces old in-tree C++ wire code; provides async device enumeration, persistent handles, and command dispatch.

**Infrastructure:**

- **tokio 1.x** (async runtime in sidecar, `streamdock-host/Cargo.toml`:24) - Async/await executor for concurrent device handling
- **serde_json 1.x** (JSON encoding in sidecar, `streamdock-host/Cargo.toml`:26) - Newline-delimited JSON wire protocol serialization
- **image 0.25** (sidecar, `streamdock-host/Cargo.toml`:28) - Image decode/encode for Stream Deck button rendering
- **base64 0.22** (sidecar, `streamdock-host/Cargo.toml`:29) - Base64 codec for image payloads over JSON
- **futures-lite 2.6** (sidecar, `streamdock-host/Cargo.toml`:25) - Lightweight futures combinators

**Optional (Graceful Fallback):**

- **Qt6::WebEngineQuick + Qt6::WebChannelQuick** - HTML Property Inspector pages (auto-disabled if not found, `src/app/CMakeLists.txt`:13–34)
- **Qt6::WebSockets** - Elgato plugin server (auto-disabled if missing, `CMakeLists.txt`:140–149)

## Configuration

**Environment:**

- `AJAZZ_DEBUG_CONTROL` - Enable opt-in JSON-RPC debug channel (empty env var string activates)
- `FLATPAK_ID` - Auto-detected at runtime; disables auto-update check under Flatpak (Flathub manages updates)
- `ACC_STREAMDOCK_CATALOG_URL` - Override upstream plugin catalog URL (default: AJAZZ-hosted)
- `XDG_CACHE_HOME` - Cache directory for downloaded plugin catalogs (falls back to `~/.cache`)
- `XDG_RUNTIME_DIR` - Unix socket directory for debug control channel (falls back to `/tmp`)
- `PYTHONPATH` - Injected per-plugin-spawn for isolated plugin SDK discovery

**Build:**

- `CMakePresets.json` - Six preset targets (linux-debug/release, windows-debug/release, macos-debug/release) + three abstract aliases (dev, release, coverage)
- `CMakeLists.txt` global options:
  - `AJAZZ_BUILD_APP` (ON) - Build the Qt desktop application
  - `AJAZZ_BUILD_TESTS` (ON) - Build and enable ctest suite
  - `AJAZZ_BUILD_PYTHON_HOST` (ON) - Build out-of-process plugin host subsystem
  - `AJAZZ_ENABLE_WERROR` (ON) - Treat warnings as errors
  - `AJAZZ_ENABLE_SANITIZERS` (OFF) - ASan/UBSan in Debug builds
  - `AJAZZ_ENABLE_TSAN` (OFF) - ThreadSanitizer (mutually exclusive with ASan)
  - `AJAZZ_ENABLE_COVERAGE` (OFF) - Code coverage instrumentation (Linux only)
  - `AJAZZ_BUILD_FUZZ_TESTS` (OFF) - libFuzzer harnesses (Clang-only)
  - `AJAZZ_FEATURE_INPUT_SYNTH` (OFF) - Native OS input synthesis backends (Linux uinput, Windows SendInput, macOS CGEvent)
  - `AJAZZ_USE_SYSTEM_DEPS` (OFF) - Resolve hidapi + nlohmann_json via system find_package instead of FetchContent (required for Flatpak)
  - `AJAZZ_INSTALL_UDEV_RULES` (ON) - Install Linux udev rule to `/usr/lib/udev/rules.d` (OFF for Flatpak)
  - `AJAZZ_BUILD_PROPERTY_INSPECTOR` (ON) - Embed Property Inspector HTML pages

**Compilation Flags:**

- C++ standard: C++20 (strict, no extensions)
- Out-of-source builds enforced
- Compile commands exported for tooling integration
- Per-platform compiler strictness:
  - Linux GCC/Clang: default warning set
  - Apple Clang (macOS): `-Werror`, catches `-Wunused-const-variable` on inline constexpr
  - MSVC (Windows): `/W4 /WX`, C4996 deprecation warnings treated as errors; prefer `_s` variants

## Platform Requirements

**Development:**

- Qt6 6.7+ SDK (developer headers + private headers for `QZipReader`)
- CMake 3.28+
- Ninja build tool
- C++20 compatible compiler (GCC 11+, Clang 14+, MSVC 193+)
- Rust 1.x toolchain (for sidecar builds)
- Python 3.11+ (runtime, not compile-time)
- hidapi development headers (or rely on FetchContent vendor)
- Linux: `libudev-dev`, PkgConfig for udev rules + hotplug monitoring
- Windows: MSVC toolchain with WinAPI headers
- macOS: Xcode Command Line Tools

**Production:**

- **Linux:** Qt6 runtime libraries, hidapi-hidraw (or statically linked), Python 3.11+ plugin host, Rust sidecar binary (streamdock-host), udev + systemd for device ACLs
- **Windows:** Qt6 runtime, MSVC runtime, Python 3.11+ plugin host, Rust sidecar binary
- **macOS:** Qt6 runtime, Python 3.11+ plugin host, Rust sidecar binary
- **Cross-platform:** HID device access (Linux hidraw kernel module, Windows libusb-win32/WinUSB, macOS native IOKit)

**Deployment:**

- Linux: .deb (Debian/Ubuntu), .rpm (Fedora/RHEL), .flatpak (universal)
- Windows: .msi (WiX Toolset), .zip (portable)
- macOS: .dmg (DragNDrop), Universal binary (Apple Silicon + Intel)
- GitHub Releases: automatic update detection via API

______________________________________________________________________

*Stack analysis: 2026-06-02*
