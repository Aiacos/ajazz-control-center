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
| Python        | 3.11 (runtime only)            | system `python3` invoked by the OOP plugin host at runtime |
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

The official release artifacts are built by GitHub Actions when you
push a `vX.Y.Z` tag — see [Release Process](Release-Process.md). You
can also produce the same artifacts locally for QA preview, behind a
corporate firewall, or to test changes without waiting for CI.

The Makefile target `make package` invokes the `release` preset and
runs every CPack generator configured for the current OS. If you only
want one format, drive `cpack` directly after the release build:

### Fedora / RHEL / openSUSE — `.rpm`

```bash
cmake --preset linux-release
cmake --build --preset linux-release --parallel "$(nproc)"
( cd build/linux-release && cpack -G RPM )
# Output: build/linux-release/ajazz-control-center-<version>-Linux.rpm
```

Inspect the package before installing:

```bash
RPM=build/linux-release/ajazz-control-center-*-Linux.rpm
rpm -qpi $RPM        # name, version, license, summary
rpm -qpl $RPM        # list of installed files
rpm -qpR $RPM        # required dependencies
```

Install **via the CLI**, not gnome-software / dnfdragora:

```bash
sudo dnf install ./build/linux-release/ajazz-control-center-*-Linux.rpm
```

> **"Failed to obtain authentication" error?** That comes from PackageKit
> (the GUI software-center back-end). Locally-built unsigned RPMs go
> through PolicyKit, which requires an interactive polkit agent — not
> always available over SSH or in tiling WMs without one running.
> Install with plain `sudo dnf install` instead, which uses your shell
> credentials and bypasses PackageKit entirely.

### Debian / Ubuntu — `.deb`

```bash
cmake --preset linux-release
cmake --build --preset linux-release --parallel "$(nproc)"
( cd build/linux-release && cpack -G DEB )
sudo apt install ./build/linux-release/ajazz-control-center-*-Linux.deb
```

### Linux — Flatpak bundle

The Flatpak manifest at
`packaging/flatpak/io.github.Aiacos.AjazzControlCenter.yml` builds
independently of CMake/CPack and is the path used by the Flathub
distribution. Local build:

```bash
flatpak install --user flathub org.kde.Sdk//6.7 org.kde.Platform//6.7
flatpak-builder --user --install --force-clean build-flatpak \
    packaging/flatpak/io.github.Aiacos.AjazzControlCenter.yml
flatpak run io.github.Aiacos.AjazzControlCenter
```

### Windows — `.msi`

From the *Developer PowerShell for VS 2022*, with Qt 6.7+ on
`CMAKE_PREFIX_PATH`:

```powershell
cmake --preset windows-release
cmake --build --preset windows-release
cd build/windows-release
cpack -G WIX            # produces ajazz-control-center-<version>-win64.msi
cpack -G ZIP            # also a portable zip
```

The MSI installer is unsigned; SmartScreen will warn the first time it
runs. Code signing tracking lives in the
[Release Process](Release-Process.md#signing--notarization) page.

### macOS — universal `.dmg`

```bash
cmake -S . -B build/macos-release -G Ninja \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
cmake --build build/macos-release
( cd build/macos-release && cpack -G DragNDrop )
# Output: build/macos-release/AJAZZ Control Center-<version>-Darwin.dmg
```

The `CMAKE_OSX_ARCHITECTURES="arm64;x86_64"` flag produces a universal
binary that runs natively on both Apple Silicon and Intel Macs. Like
Windows, the DMG is unsigned by default; Gatekeeper will warn until the
project obtains an Apple Developer ID.

### Quick reference

| OS              | Generator    | Output extension | Install                                    |
| --------------- | ------------ | ---------------- | ------------------------------------------ |
| Fedora / RHEL   | `RPM`        | `.rpm`           | `sudo dnf install ./pkg.rpm`               |
| Debian / Ubuntu | `DEB`        | `.deb`           | `sudo apt install ./pkg.deb`               |
| Linux (any)     | flatpak-builder | `.flatpak`    | `flatpak install --user pkg.flatpak`       |
| Windows         | `WIX`        | `.msi`           | Double-click, or `msiexec /i pkg.msi`      |
| macOS           | `DragNDrop`  | `.dmg`           | Open, drag the `.app` into `/Applications` |

For the CI pipeline that builds all of these from a single tag push,
see the [Release Process](Release-Process.md) page.

## IDE setup

- **VS Code:** the `CMake Tools` extension reads `CMakePresets.json`
  automatically. The `.clang-format` and `.clang-tidy` files in the repo
  are picked up by the Clang extension.
- **CLion / Qt Creator:** open the project via `CMakeLists.txt`; presets
  are detected natively.
