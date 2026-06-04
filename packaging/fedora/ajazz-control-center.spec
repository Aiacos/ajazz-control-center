# AJAZZ Control Center — Fedora RPM spec
#
# Builds from the upstream GitHub release tarball (the `vX.Y.Z` tag archive).
# Targets current Fedora (>= 40) and EPEL where Qt 6.7+ is available.
#
# Conventions followed (see packaging/fedora/SUBMITTING.md for doc URLs):
#   - %cmake / %cmake_build / %cmake_install macros
#     https://docs.fedoraproject.org/en-US/packaging-guidelines/CMake/
#   - SPDX license identifier "GPL-3.0-or-later"
#     https://docs.fedoraproject.org/en-US/legal/allowed-licenses/
#   - AppStream metainfo + .desktop validated in %check
#     https://docs.fedoraproject.org/en-US/packaging-guidelines/AppData/
#     https://docs.fedoraproject.org/en-US/packaging-guidelines/#_desktop_files

Name:           ajazz-control-center
Version:        0.1.1
Release:        1%{?dist}
Summary:        Cross-platform control center for AJAZZ devices

# Entire codebase is GPL-3.0-or-later (see LICENSE).
License:        GPL-3.0-or-later
URL:            https://github.com/Aiacos/ajazz-control-center
Source0:        %{url}/archive/refs/tags/v%{version}/%{name}-%{version}.tar.gz

# Qt 6 GUI app + hidapi: not noarch.
BuildRequires:  cmake >= 3.28
BuildRequires:  ninja-build
BuildRequires:  gcc-c++
# Qt 6 (>= 6.7) modules used by the app and tests.
BuildRequires:  cmake(Qt6Core)
BuildRequires:  cmake(Qt6Gui)
BuildRequires:  cmake(Qt6Network)
BuildRequires:  cmake(Qt6Quick)
BuildRequires:  cmake(Qt6QuickControls2)
BuildRequires:  cmake(Qt6Widgets)
BuildRequires:  cmake(Qt6Svg)
BuildRequires:  cmake(Qt6WebSockets)
# qt6-qtbase-devel pulls qmake/Core/Gui/Network/Widgets + the private headers
# (QZipReader, used by sdplugin_extractor.cpp). qt6-qtdeclarative-devel provides
# Quick / QuickControls2. Listed explicitly alongside the cmake() deps so a
# bare `dnf builddep` on older metadata still resolves.
BuildRequires:  qt6-qtbase-devel
BuildRequires:  qt6-qtdeclarative-devel
BuildRequires:  qt6-qtsvg-devel
BuildRequires:  qt6-qtwebsockets-devel
# HID transport (hidraw backend).
BuildRequires:  hidapi-devel
# nlohmann::json is PRIVATE-linked to the plugin library (COD-031 boundary).
# Use the system copy instead of the vendored FetchContent clone (no network in
# the Koji/Copr build root) via -DAJAZZ_USE_SYSTEM_DEPS=ON.
BuildRequires:  json-devel
# Out-of-process plugin host invokes python3 at runtime via execvp; no
# compile-time Python linkage, but the dev metadata is harmless and matches the
# project's documented dependency surface.
BuildRequires:  python3-devel
BuildRequires:  pybind11-devel
# %check validators.
BuildRequires:  desktop-file-utils
BuildRequires:  libappstream-glib

# Runtime: python3 for the plugin host; udev for the shipped hidraw rule.
Requires:       python3
Requires:       hidapi
# udev rule is installed to %{_udevrulesdir}; trigger a reload on (un)install.
Requires(post): systemd-udev
Requires(postun): systemd-udev

%description
AJAZZ Control Center is a cross-platform desktop application for AJAZZ and
Mirabox HID devices: stream-deck-style key panels (AKP153 / AKP153E / AKP03 /
AKP05), VIA-compatible and Microdia-chipset keyboards (AK820 Pro, AK980 PRO,
AK series), and AJ-line mice.

It provides a Qt 6 / QML GUI for binding keys, encoders and mouse buttons to
multi-step action chains (key macros, URLs, plugin actions), managing folder
pages, importing and exporting profile bundles, and a system-tray
quick-switcher. An out-of-process Python plugin host runs third-party actions.

The udev rule shipped with this package grants user-level device access via
TAG+="uaccess" — no group membership and no logout required.

%prep
%autosetup -n %{name}-%{version}

%build
# Notes:
#   * Fedora's %cmake defaults to the Ninja generator (CMake-ninja-default
#     change), hence BuildRequires: ninja-build and no explicit -G.
#   * AJAZZ_USE_SYSTEM_DEPS=ON: resolve hidapi + nlohmann_json via find_package
#     instead of FetchContent (the build root has no network).
#   * AJAZZ_ENABLE_WERROR=OFF: do not turn upstream/Qt header warnings into hard
#     errors during a distro build (Qt point releases periodically add new
#     warnings; -Werror would make the package FTBFS on a Qt rebuild).
#   * Tests OFF for the shipped build (AJAZZ_BUILD_TESTS=OFF); enable them only
#     in %check below via a dedicated configure if desired.
%cmake \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DAJAZZ_USE_SYSTEM_DEPS=ON \
    -DAJAZZ_BUILD_APP=ON \
    -DAJAZZ_BUILD_PYTHON_HOST=ON \
    -DAJAZZ_BUILD_TESTS=OFF \
    -DAJAZZ_ENABLE_WERROR=OFF \
    -DAJAZZ_INSTALL_UDEV_RULES=ON
%cmake_build

%install
%cmake_install

# The project installs the udev rule to an absolute /usr/lib/udev/rules.d via
# the top-level CMakeLists, and again (relative) from src/app — both land in
# the same directory. Normalise to Fedora's macro path in case the prefix
# layout shifts on a future release.
mkdir -p %{buildroot}%{_udevrulesdir}
if [ -f %{buildroot}/usr/lib/udev/rules.d/70-ajazz.rules ] && \
   [ "%{_udevrulesdir}" != "/usr/lib/udev/rules.d" ]; then
    mv %{buildroot}/usr/lib/udev/rules.d/70-ajazz.rules %{buildroot}%{_udevrulesdir}/
fi

%check
desktop-file-validate %{buildroot}%{_datadir}/applications/%{name}.desktop
appstream-util validate-relax --nonet \
    %{buildroot}%{_metainfodir}/io.github.Aiacos.AjazzControlCenter.appdata.xml

%files
%license LICENSE
%doc README.md CHANGELOG.md
%{_bindir}/%{name}
%{_datadir}/applications/%{name}.desktop
%{_metainfodir}/io.github.Aiacos.AjazzControlCenter.appdata.xml
# Upstream installs icons under two names depending on the CMakeLists path that
# runs (the reverse-DNS AppStream ID and the short binary name). Glob both so
# the file list stays correct regardless of upstream icon-naming changes.
%{_datadir}/icons/hicolor/scalable/apps/%{name}.svg
%{_datadir}/icons/hicolor/*/apps/%{name}.png
%{_datadir}/icons/hicolor/scalable/apps/io.github.Aiacos.AjazzControlCenter.svg
%{_datadir}/icons/hicolor/*/apps/io.github.Aiacos.AjazzControlCenter.png
# udev rule (host resource granting hidraw access via uaccess).
%{_udevrulesdir}/70-ajazz.rules

%changelog
* Sat May 23 2026 Lorenzo Argentieri <uni.lorenzo.a@gmail.com> - 0.1.0-1
- Initial Fedora package (upstream v0.1.0).
