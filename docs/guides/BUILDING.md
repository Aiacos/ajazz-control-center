# Building AJAZZ Control Center

> TL;DR — `make bootstrap && make run`. The rest of this document is
> only needed when something goes wrong or you want to understand what
> those commands are actually doing.

## The friendly path (recommended)

```bash
git clone https://github.com/Aiacos/ajazz-control-center.git
cd ajazz-control-center

make bootstrap   # installs every system dep, udev rules, and builds
make run         # launches the freshly-built app
```

`make bootstrap` calls [`scripts/bootstrap-dev.sh`](../../scripts/bootstrap-dev.sh),
which:

1. Detects your distro (Fedora / RHEL / openSUSE / Debian / Ubuntu /
   Arch / macOS).
2. Installs every build dependency through the native package manager.
3. Installs the udev rule (Linux only) so device access works
   immediately — no group membership, no logout, no replug.
4. Configures CMake with the `dev` preset and builds.

Common follow-up targets:

| Command           | What it does                                             |
|-------------------|----------------------------------------------------------|
| `make`            | Incremental debug build (alias for `make build`).        |
| `make run`        | Build + launch the app.                                  |
| `make test`       | Build + run the full test suite.                         |
| `make release`    | Optimized build (no sanitizers).                         |
| `make package`    | Produce `.deb`/`.rpm` (Linux), `.dmg` (macOS), `.msi` (Windows). |
| `make install`    | Install into `/usr/local` (Linux / macOS).               |
| `make uninstall`  | Remove what `make install` placed.                       |
| `make format`     | Run `clang-format` across the tree.                      |
| `make lint`       | Run `clang-tidy` across the tree.                        |
| `make doctor`     | Diagnose your environment (toolchain, Qt, devices).      |
| `make help`       | Show the full list.                                      |

## Manual CMake (if you prefer)

The Makefile is just a thin wrapper. The underlying build is plain CMake:

```bash
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

Presets live in [`CMakePresets.json`](../../CMakePresets.json). The
available ones are:

| Preset       | Purpose                                        |
|--------------|------------------------------------------------|
| `dev`        | Debug build with sanitizers, compile commands. |
| `release`    | Optimized build, LTO, strip.                   |
| `coverage`   | Debug + `--coverage` instrumentation for CI.   |
| `clang`      | Same as `dev` but forced to Clang/libc++.      |

## Prerequisites (for curious developers)

| Dependency    | Minimum | Typical provider                              |
|---------------|---------|-----------------------------------------------|
| CMake         | 3.28    | `dnf install cmake` / `brew install cmake`    |
| Ninja         | 1.11    | `dnf install ninja-build`                     |
| C++ compiler  | GCC 13 / Clang 17 / MSVC 19.39 | distro / Xcode / VS 2022        |
| Qt            | 6.7     | `qt6-*-devel` on Linux, `brew install qt@6`   |
| Python        | 3.11    | system                                        |
| libusb/hidapi | bundled | fetched automatically                          |

If you want to install these yourself instead of running
`make bootstrap`, the per-distro commands are in the bootstrap script —
it is designed to be readable as documentation.

## Platform notes

### Linux

- On Wayland the app runs out of the box. On X11 you may want
  `QT_QPA_PLATFORM=xcb`.
- The udev rule uses `TAG+="uaccess"`. This grants the currently
  logged-in user device access through systemd-logind ACLs. No
  `plugdev` group membership is required, and you do **not** need to
  log out and back in — newly-plugged devices are re-ACL'd
  automatically.
- Installing via the `.deb` / `.rpm` packages takes care of the udev
  rule automatically via post-install scripts
  ([`packaging/linux/postinst.sh`](../../packaging/linux/postinst.sh)).

### macOS

- Build produces a universal binary (`arm64;x86_64`) when Xcode
  supports both.
- On first launch grant **Input Monitoring** and **Accessibility**
  permission in *System Settings → Privacy & Security* so the app can
  emit synthetic keystrokes for macros.

### Windows

- Use the *Developer PowerShell for VS 2022* so the MSVC compiler is on
  `PATH`.
- Qt must be installed via the Qt online installer; point CMake at it
  with `-DCMAKE_PREFIX_PATH="C:\Qt\6.7.3\msvc2022_64"`.
- WIX Toolset 3.14+ is required for `make package` to produce an MSI.

## Packaging locally

```bash
make package
```

produces:

- on Linux: `build/release/ajazz-control-center-*.deb` and `*.rpm`
- on macOS: `build/release/ajazz-control-center-*.dmg`
- on Windows: `build/release/ajazz-control-center-*.msi`

For Flatpak, use the manifest directly:

```bash
flatpak-builder --user --install --force-clean build-flatpak \
    packaging/flatpak/io.github.Aiacos.AjazzControlCenter.yml
```

## Troubleshooting the build

- `make doctor` prints a health check of your toolchain.
- `make clean` deletes every build directory.
- CI uses exactly the same commands as the Makefile, so if it passes on
  CI but not locally, comparing the workflow logs and `make doctor`
  usually points to the problem in seconds.
