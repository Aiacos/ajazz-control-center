# Building from Source

The authoritative build guide lives at
[`docs/guides/BUILDING.md`](https://github.com/Aiacos/ajazz-control-center/blob/main/docs/guides/BUILDING.md).
This page is a condensed walkthrough.

## Prerequisites

| Dependency    | Minimum                        | Notes                                                      |
| ------------- | ------------------------------ | ---------------------------------------------------------- |
| CMake         | 3.28                           | Presets rely on 3.28 features                              |
| Ninja         | 1.11                           | Default generator                                          |
| C++ compiler  | GCC 13 / Clang 17 / MSVC 19.39 | C++20                                                      |
| Qt            | 6.7                            | `Core`, `Gui`, `Qml`, `Quick`, `QuickControls2`, `Widgets` |
| Python        | 3.11                           | headers + dev libs; embedded via pybind11                  |
| libusb/hidapi | bundled via FetchContent       | no system install needed                                   |

### Distro-specific packages

**Fedora / RHEL / openSUSE:**

```bash
sudo dnf install -y cmake ninja-build gcc-c++ \
    qt6-qtbase-devel qt6-qtdeclarative-devel qt6-qtquickcontrols2-devel \
    python3-devel systemd-devel libudev-devel
```

**Debian / Ubuntu (24.04+):**

```bash
sudo apt install -y cmake ninja-build g++ \
    qt6-base-dev qt6-declarative-dev qt6-quickcontrols2-dev \
    python3-dev libudev-dev
```

**macOS (Homebrew):**

```bash
brew install cmake ninja qt@6 python@3.11
export CMAKE_PREFIX_PATH="$(brew --prefix qt@6)"
```

**Windows:** install Qt 6.7 via the
[online installer](https://www.qt.io/download-qt-installer) and Visual
Studio 2022 with the *Desktop development with C++* workload. Use the
*Developer PowerShell for VS 2022*.

## One-command bootstrap

```bash
git clone https://github.com/Aiacos/ajazz-control-center.git
cd ajazz-control-center
make bootstrap   # installs every dep, udev rule, and builds
make run         # launches the app
```

`make bootstrap` detects your distro and installs every prerequisite
through the native package manager. On Linux it also installs the udev
rule so the app can talk to devices **without `plugdev` membership and
without a logout**.

## Manual configure & build

If you prefer invoking CMake directly:

```bash
cmake --preset dev            # debug
cmake --preset release        # optimized
cmake --build --preset dev
ctest --preset dev
```

Presets are defined in
[`CMakePresets.json`](https://github.com/Aiacos/ajazz-control-center/blob/main/CMakePresets.json).

## Running the app

```bash
./build/dev/src/app/ajazz-control-center     # or `make run`
```

If you skipped `make bootstrap`, install the udev rule once (nothing
else, no user group, no logout):

```bash
make udev
```

## Packaging

See [`docs/guides/BUILDING.md`](https://github.com/Aiacos/ajazz-control-center/blob/main/docs/guides/BUILDING.md)
for `.deb`, `.rpm`, Flatpak, `.msi` and `.dmg` recipes, or just push a
tag and GitHub Actions will build all of them.

## IDE setup

- **VS Code:** the `CMake Tools` extension reads `CMakePresets.json`
  automatically. The `.clang-format` and `.clang-tidy` files in the repo
  are picked up by the Clang extension.
- **CLion / Qt Creator:** open the project via `CMakeLists.txt`; presets
  are detected natively.
