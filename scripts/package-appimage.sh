#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Package a distro-agnostic AppImage from the linux-release build tree.
#
# Why an AppImage on top of the .deb/.rpm/Flatpak channel: the native packages
# pin Qt >= 6.8 (see the CPACK_DEBIAN_PACKAGE_DEPENDS block in CMakeLists.txt)
# and therefore refuse to install on older distros — Ubuntu 24.04 LTS ships Qt
# 6.4.2 and none of the qml6-module-qtquick-* names the app needs exist there
# (only Ubuntu 25.04 "plucky" / Qt 6.8 adds them). The AppImage bundles the Qt
# 6.8 runtime + QML imports the binary was built against, so the same nightly
# runs on any glibc x86-64 distro regardless of its distro Qt. See
# docs/guides/BUILDING.md → "Packaging an AppImage" section for the full
# rationale and support matrix.
#
# The payload reuses the CMake install() rules via `cmake --install` into an
# AppDir-style prefix, so the desktop entry, hicolor icon ladder, udev rule,
# bundled com.ajazz.sysmon2 plugin AND the streamdock-host sidecar (beside the
# binary, where SidecarStreamDockDevice resolves it via applicationDirPath())
# are all carried for free. linuxdeploy + its Qt plugin then pull the Qt
# libs / platform themes / QML module imports the app links.
#
# Usage:
#   ./scripts/package-appimage.sh                 # uses build/linux-release
#   ./scripts/package-appimage.sh -b build/linux-release -a ~/Qt/6.8.3/gcc_64
#
# Requirements:
#   * the CMake build directory produced by the linux-release preset
#   * the Qt prefix the build was configured against (auto-detected from the
#     binary's build via the Qt6_DIR cache entry, or passed with -a). For
#     aqtinstall layouts this is <prefix>/6.8.3/gcc_64.
#   * curl + `file` + network on first run (downloads linuxdeploy + plugin)
#
# Output: build/<profile>/ajazz-control-center-<version>-x86_64.AppImage
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
profile="linux-release"
build_dir="$repo_root/build/$profile"
qt_prefix=""

usage() {
    cat <<'EOF'
Usage: package-appimage.sh [-b build-dir] [-a qt-prefix]

  -b build-dir   CMake build dir containing the built binary (default: build/linux-release)
  -a qt-prefix   Qt installation root used for the build (default: auto-detect from build cache)
  -h             show this help
EOF
}

while getopts "b:a:h" opt; do
    case "$opt" in
    b) build_dir="$OPTARG" ;;
    a) qt_prefix="$OPTARG" ;;
    h)
        usage
        exit 0
        ;;
    *)
        usage
        exit 1
        ;;
    esac
done

if [[ ! -x "$build_dir/ajazz-control-center" ]] && [[ ! -x "$build_dir/src/app/ajazz-control-center" ]]; then
    echo "package-appimage: no ajazz-control-center binary in $build_dir" >&2
    echo "  Build it first: cmake --preset $profile && cmake --build --preset $profile" >&2
    exit 1
fi

# ---------------------------------------------------------------------------
# Qt prefix resolution
# ---------------------------------------------------------------------------
if [[ -z "$qt_prefix" ]]; then
    # Qt6_DIR cache entry looks like <prefix>/lib/cmake/Qt6 — climb up to the prefix.
    qt6_dir=$(grep -o '^Qt6_DIR:PATH=.*' "$build_dir/CMakeCache.txt" 2>/dev/null | cut -d= -f2-)
    if [[ -n "$qt6_dir" ]] && [[ -d "$qt6_dir" ]]; then
        qt_prefix=$(cd "$qt6_dir/../../.." && pwd)
    fi
fi
if [[ -z "$qt_prefix" ]] || [[ ! -d "$qt_prefix" ]]; then
    echo "package-appimage: could not locate the Qt prefix the build used" >&2
    echo "  Pass -a <qt-prefix> (e.g. ~/Qt/6.8.3/gcc_64 for aqtinstall layouts)." >&2
    exit 1
fi

version=$(grep -o 'set(CPACK_PACKAGE_VERSION "[0-9.]*")' "$build_dir/CPackConfig.cmake" | grep -o '[0-9.]*')
if [[ -z "$version" ]]; then
    echo "package-appimage: could not read CPACK_PACKAGE_VERSION from $build_dir/CPackConfig.cmake" >&2
    exit 1
fi
tools_dir="$build_dir/appimage-tools"
staging_dir="$build_dir/appimage-staging"
appdir="$staging_dir/AppDir"

# ---------------------------------------------------------------------------
# 1. Stage the install tree under an AppDir-style prefix.
# ---------------------------------------------------------------------------
# linuxdeploy wants the payload laid out as <AppDir>/usr/... ; re-run the
# install() rules per COMPONENT into that prefix so desktop/icon/udev/plugins/
# sidecar come along exactly as the .deb/.rpm payloads carry them. The udev
# rule's OWN install rule targets the ABSOLUTE system path
# /usr/lib/udev/rules.d (host resource, skipped by Flatpak), which must not be
# re-run against an AppDir — install every component EXCEPT udev, then drop the
# rule into usr/lib/udev/rules.d/ so the AppImage still SHIPS it (users on
# distros without the native package extract it next to the binary).
rm -rf "$staging_dir"
mkdir -p "$appdir"
for component in Unspecified desktop icon metainfo; do
    cmake --install "$build_dir" --prefix "$appdir/usr" --component "$component" >/dev/null
done
mkdir -p "$appdir/usr/lib/udev/rules.d"
cp "$repo_root/resources/linux/70-ajazz.rules" "$appdir/usr/lib/udev/rules.d/"
if [[ ! -x "$appdir/usr/bin/ajazz-control-center" ]]; then
    echo "package-appimage: cmake --install produced no usr/bin/ajazz-control-center" >&2
    exit 1
fi

# ---------------------------------------------------------------------------
# 2. Acquire linuxdeploy + the Qt plugin (cached after first run).
# ---------------------------------------------------------------------------
fetch() { # curl preferred (CI), wget fallback (minimal local installs)
    if command -v curl >/dev/null 2>&1; then
        curl -L --fail -o "$2" "$1"
    else
        wget -q -O "$2" "$1"
    fi
}
mkdir -p "$tools_dir"
linuxdeploy="$tools_dir/linuxdeploy-x86_64.AppImage"
qtplugin="$tools_dir/linuxdeploy-plugin-qt-x86_64.AppImage"

if [[ ! -x "$linuxdeploy" ]]; then
    fetch \
        https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage \
        "$linuxdeploy"
    chmod +x "$linuxdeploy"
fi
if [[ ! -x "$qtplugin" ]]; then
    fetch \
        https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage \
        "$qtplugin"
    chmod +x "$qtplugin"
fi

# linuxdeploy's AppImages are self-executing. On FUSE-less runners (containers,
# minimal CI images) direct execution fails; fall back to --appimage-extract.
run_ld() {
    local image="$1"
    shift
    if "$image" --version >/dev/null 2>&1; then
        "$image" "$@"
    else
        local dir="${image%.AppImage}.dir"
        mkdir -p "$dir"
        "$image" --appimage-extract >/dev/null
        "$dir/squashfs-root/AppRun" "$@"
    fi
}

# ---------------------------------------------------------------------------
# 3. Deploy Qt runtime + QML imports + produce the AppImage.
# ---------------------------------------------------------------------------
export PATH="$qt_prefix/bin:$PATH"
export QML_SOURCES_PATHS="$repo_root/src/app/qml"
# linuxdeploy's base dependency pass resolves the app's linked Qt libs via the
# loader; the aqtinstall Qml dirs layout has them under $prefix/lib but not in
# the system loader path, so expose them so the ELF scan succeeds. The qt
# plugin then relocates everything into the AppDir.
export LD_LIBRARY_PATH="$qt_prefix/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

# The Qt plugin discovers the Qt install via qmake on PATH; --plugin qt runs it
# after linuxdeploy has copied the app binary and its direct deps.
cd "$staging_dir"
run_ld "$linuxdeploy" \
    --appdir "$appdir" \
    --plugin qt \
    --output appimage

# linuxdeploy names the output "<Name>-<arch>.AppImage" from the desktop entry
# (Name=AJAZZ Control Center -> "AJAZZ Control Center-x86_64.AppImage").
find "$staging_dir" -maxdepth 1 -name '*.AppImage' -print0 | while IFS= read -r -d '' img; do
    target="$build_dir/ajazz-control-center-$version-x86_64.AppImage"
    mv -f "$img" "$target"
    echo "==> AppImage: $target"
done
