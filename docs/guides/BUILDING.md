# Building from source

AJAZZ Control Center ships a single CMake build with presets for each supported OS. The build pulls hidapi and Catch2 via `FetchContent` — the only system dependencies you need are a compiler, CMake, Qt 6 and Python 3.

## Prerequisites

### Linux (Ubuntu 24.04 is the tested baseline)

```bash
sudo apt-get install -y \
    build-essential cmake ninja-build pkg-config \
    libudev-dev libusb-1.0-0-dev libhidapi-dev \
    libgl1-mesa-dev libxkbcommon-dev libxcb1-dev \
    libxcb-cursor-dev libxkbcommon-x11-dev \
    python3-dev python3-pip

# Qt 6.7+ — via your distro package or the official online installer.
```

### Windows

- Visual Studio 2022 with "Desktop development with C++"
- CMake 3.28+ (bundled with VS 2022)
- Qt 6.7+ (online installer, MSVC 2022 kit)
- Python 3.11+ (from python.org)

### macOS

```bash
brew install cmake ninja pkg-config
# Qt 6.7 via the official installer or qt6 formula (brew install qt).
```

## Configure & build

The presets live in `CMakePresets.json`:

| Host     | Configure preset   | Build preset       |
|----------|--------------------|--------------------|
| Linux    | `linux-release`    | `linux-release`    |
| Linux    | `linux-debug`      | `linux-debug`      |
| Windows  | `windows-release`  | `windows-release`  |
| macOS    | `macos-release`    | `macos-release`    |

```bash
cmake --preset linux-release
cmake --build --preset linux-release --parallel
ctest --preset linux-release
```

The resulting binary lives at `build/linux-release/src/app/ajazz-control-center`.

## Options

| Option                     | Default | Description                              |
|----------------------------|---------|------------------------------------------|
| `AJAZZ_BUILD_APP`          | `ON`    | Build the Qt desktop application         |
| `AJAZZ_BUILD_TESTS`        | `ON`    | Build Catch2 unit + integration tests    |
| `AJAZZ_BUILD_PYTHON_HOST`  | `ON`    | Embed the Python plugin host             |
| `AJAZZ_ENABLE_WERROR`      | `ON`    | Treat compiler warnings as errors        |
| `AJAZZ_ENABLE_SANITIZERS`  | `OFF`   | Enable ASan + UBSan (Debug builds only)  |
| `AJAZZ_ENABLE_COVERAGE`    | `OFF`   | Add `--coverage` flags                   |

Example:

```bash
cmake --preset linux-debug -DAJAZZ_ENABLE_SANITIZERS=ON
cmake --build --preset linux-debug
```

## Packaging (local)

```bash
cmake --build --preset linux-release
cd build/linux-release
cpack -G DEB
cpack -G RPM
```

For Flatpak:

```bash
flatpak-builder --user --install build-flatpak \
    packaging/flatpak/io.github.Aiacos.AjazzControlCenter.yml
```

On Windows: `cpack -G WIX` from the build directory.
On macOS: `cpack -G DragNDrop`.

## Running without installing

```bash
./build/linux-release/src/app/ajazz-control-center
```

On Linux the app needs the udev rules installed once to access devices without root:

```bash
sudo install -m 644 resources/linux/99-ajazz.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules && sudo udevadm trigger
```
