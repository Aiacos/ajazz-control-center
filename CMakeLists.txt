cmake_minimum_required(VERSION 3.28)

project(
    ajazz-control-center
    VERSION 0.1.0
    DESCRIPTION "Cross-platform control center for AJAZZ devices"
    HOMEPAGE_URL "https://github.com/Aiacos/ajazz-control-center"
    LANGUAGES CXX C
)

# -----------------------------------------------------------------------------
# Global settings
# -----------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Out-of-source builds only
if("${CMAKE_SOURCE_DIR}" STREQUAL "${CMAKE_BINARY_DIR}")
    message(FATAL_ERROR "In-source builds are not allowed. Use a build directory.")
endif()

# -----------------------------------------------------------------------------
# Options
# -----------------------------------------------------------------------------
option(AJAZZ_BUILD_APP "Build the Qt desktop application" ON)
option(AJAZZ_BUILD_TESTS "Build unit and integration tests" ON)
# AJAZZ_BUILD_PYTHON_HOST historically gated an embedded pybind11 interpreter. Slice 3e of audit
# finding A4 retired that backend; the option now simply gates the (lightweight)
# OutOfProcessPluginHost subsystem so existing CI matrices (`-DAJAZZ_BUILD_PYTHON_HOST=OFF`) keep
# working without changes. Building the OOP host has no Python compile-time dep — `python3` is
# invoked at runtime via `execvp`.
option(AJAZZ_BUILD_PYTHON_HOST "Build the out-of-process plugin host subsystem" ON)
option(AJAZZ_ENABLE_WERROR "Treat warnings as errors" ON)
option(AJAZZ_ENABLE_SANITIZERS "Enable ASan/UBSan in Debug builds" OFF)
option(AJAZZ_ENABLE_TSAN "Enable ThreadSanitizer (mutually exclusive with ASan)" OFF)
option(AJAZZ_ENABLE_COVERAGE "Enable code coverage instrumentation" OFF)
# Phase 7 / Plan 07-03: opt-in libFuzzer harnesses for plugin-host security primitives (currently
# loadTrustRoots). Default OFF so the regular CI matrix is unaffected. Clang-only (libFuzzer is a
# Clang sanitizer runtime); the tests/CMakeLists.txt gate errors out at configure time if
# AJAZZ_BUILD_FUZZ_TESTS=ON is paired with GCC/MSVC, surfacing the constraint up front.
option(AJAZZ_BUILD_FUZZ_TESTS "Build libFuzzer harnesses (Clang only, opt-in)" OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
include(Warnings)
include(Sanitizers)

# -----------------------------------------------------------------------------
# Dependencies
# -----------------------------------------------------------------------------
find_package(
    Qt6 6.7
    COMPONENTS Core
               Gui
               Network
               Quick
               QuickControls2
               Test
               Widgets
               Svg
    REQUIRED
)
# CorePrivate exposes QZipReader / QZipWriter (`<private/qzipreader_p.h>`), the only Qt zip API
# short of bundling QuaZip. PluginCatalogModel uses QZipReader to extract `.sdPlugin` archives in
# place at install time (issue #62 / `src/app/src/sdplugin_extractor.cpp`); the matching unit test
# uses QZipWriter to build a synthetic archive for the round-trip.
#
# Qt private modules ship their headers inside the regular `qtbase` archive (under
# `include/QtCore/<ver>/QtCore/private/`) but the Qt6CorePrivate CMake config files are NOT included
# in the binary archives distributed via Qt's online installer / aqtinstall — only MaintenanceTool
# and source builds materialise them on disk. So `find_package(... CorePrivate)` REQUIRED silently
# works on developer boxes (which use MaintenanceTool) and silently fails on every CI runner (which
# uses `jurplel/install-qt-action` → `aqtinstall`).
#
# Workaround: probe for the official CorePrivate config; if it's absent, synthesize an interface
# target that wires the private include directory the same way Qt's own Qt6CorePrivateTargets.cmake
# does. Private headers are header-only / inlined into libQt6Core, so no extra link target is
# required.
find_package(Qt6 QUIET COMPONENTS CorePrivate)
if(NOT TARGET Qt6::CorePrivate)
    get_target_property(_qt6_core_includes Qt6::Core INTERFACE_INCLUDE_DIRECTORIES)
    if(NOT _qt6_core_includes)
        message(FATAL_ERROR "Qt6::Core has no INTERFACE_INCLUDE_DIRECTORIES — cannot "
                            "synthesize Qt6::CorePrivate fallback. Check Qt install."
        )
    endif()
    # Qt6_VERSION is set by find_package(Qt6) above. The private header subtree is laid out as
    # `<include>/QtCore/<ver>` and `<include>/QtCore/<ver>/QtCore`. The first directory contains the
    # version-stamped headers used internally; the second is what `#include
    # <private/qzipreader_p.h>` actually resolves through.
    set(_qt6_core_include_root "")
    foreach(inc IN LISTS _qt6_core_includes)
        if(EXISTS "${inc}/QtCore/${Qt6_VERSION}/QtCore/private/qzipreader_p.h")
            set(_qt6_core_include_root "${inc}")
            break()
        endif()
    endforeach()
    if(NOT _qt6_core_include_root)
        message(
            FATAL_ERROR
                "Could not locate Qt6 private headers (qzipreader_p.h) under "
                "any of Qt6::Core's interface include dirs: "
                "${_qt6_core_includes}. Install qt.qt6.<ver>.qtbase via "
                "MaintenanceTool or rebuild Qt from source to ship private "
                "headers."
        )
    endif()
    # Build the include-paths list in two halves so cmake-lint's 100-char line cap isn't tripped by
    # the long Qt path. Both halves are needed: the parent dir gives `<private/...>` resolution and
    # the inner QtCore/ dir gives the version-stamped headers.
    set(_qt6_priv_include_a "${_qt6_core_include_root}/QtCore/${Qt6_VERSION}")
    set(_qt6_priv_include_b "${_qt6_priv_include_a}/QtCore")
    add_library(Qt6::CorePrivate INTERFACE IMPORTED)
    set_target_properties(
        Qt6::CorePrivate
        PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${_qt6_priv_include_a};${_qt6_priv_include_b}"
                   INTERFACE_LINK_LIBRARIES "Qt6::Core"
    )
    unset(_qt6_priv_include_a)
    unset(_qt6_priv_include_b)
    message(STATUS "Qt6::CorePrivate CMake config missing (typical for aqtinstall "
                   "binary distributions) — synthesised interface target from "
                   "Qt6::Core include dirs (${_qt6_core_include_root})"
    )
    unset(_qt6_core_include_root)
    unset(_qt6_core_includes)
endif()
# P3.16 — Qt6::WebSockets is OPTIONAL. When present, SdPluginServer (Elgato Stream Deck
# v6-compatible WebSocket plugin host) is built into the app + tests. When absent (common on minimal
# Qt installs that ship only the default modules), the build still succeeds and plugin host
# functionality is disabled at runtime — mirrors the AJAZZ_BUILD_PROPERTY_INSPECTOR fallback pattern
# in src/app/CMakeLists.txt.
find_package(Qt6 QUIET COMPONENTS WebSockets)
if(Qt6WebSockets_FOUND)
    set(AJAZZ_HAVE_WEBSOCKETS TRUE)
    message(STATUS "Qt6::WebSockets found — SdPluginServer (Elgato plugin host) enabled")
else()
    set(AJAZZ_HAVE_WEBSOCKETS FALSE)
    message(STATUS "Qt6::WebSockets NOT found — SdPluginServer disabled "
                   "(install qt.qt6.<ver>.qtwebsockets via MaintenanceTool to enable)"
    )
endif()
qt_standard_project_setup(REQUIRES 6.7)

# hidapi — vendored via FetchContent for reproducibility
include(FetchContent)
FetchContent_Declare(
    hidapi
    GIT_REPOSITORY https://github.com/libusb/hidapi.git
    GIT_TAG 73d292a8f4d18f7fb532b9829b9a26ca8d864419 # hidapi-0.14.0
    GIT_SHALLOW OFF
)
set(HIDAPI_BUILD_HIDTEST OFF CACHE BOOL "")
set(HIDAPI_WITH_TESTS OFF CACHE BOOL "")
# hidapi-0.14.0 declares cmake_minimum_required(VERSION 3.1.3) at the root and 3.6.3 under libusb/.
# CMake 4.0 dropped compatibility for policies introduced before 3.5, and CMake 4.x emits
# `Compatibility with CMake < 3.10 will be removed` deprecation warnings when the vendored project
# targets a floor below 3.10. Force a 3.10 policy floor for the subproject only so the fetched
# CMakeLists are accepted without patching *and* without triggering a deprecation warning (violates
# our zero-warnings invariant). Revisit when hidapi upstream bumps its own minimum.
set(CMAKE_POLICY_VERSION_MINIMUM 3.10 CACHE STRING "" FORCE)
FetchContent_MakeAvailable(hidapi)
unset(CMAKE_POLICY_VERSION_MINIMUM CACHE)

# nlohmann::json — vendored via FetchContent for reproducibility. PRIVATE-linked to `ajazz_plugins`
# only (see `src/plugins/CMakeLists.txt`); MUST NOT appear in `ajazz_core` or any installed public
# header (COD-031 invariant per Phase 3 / ARCH-01). Version pinned to v3.12.0 in lockstep with
# `vcpkg.json`.
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.12.0
    GIT_SHALLOW TRUE
)
set(JSON_BuildTests OFF CACHE BOOL "Disable nlohmann tests" FORCE)
set(JSON_Install OFF CACHE BOOL "Disable nlohmann install rules" FORCE)
FetchContent_MakeAvailable(nlohmann_json)

# -----------------------------------------------------------------------------
# Subprojects
# -----------------------------------------------------------------------------
add_subdirectory(src/core)
add_subdirectory(src/devices)

if(AJAZZ_BUILD_PYTHON_HOST)
    add_subdirectory(src/plugins)
endif()

if(AJAZZ_BUILD_APP)
    add_subdirectory(src/app)
endif()

if(AJAZZ_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# -----------------------------------------------------------------------------
# Install (shared resources: udev, desktop entry, icon, AppStream)
# -----------------------------------------------------------------------------
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    install(FILES resources/linux/99-ajazz.rules DESTINATION /usr/lib/udev/rules.d COMPONENT udev)
    install(FILES resources/linux/ajazz-control-center.desktop DESTINATION share/applications
            COMPONENT desktop
    )
    install(FILES resources/linux/io.github.Aiacos.AjazzControlCenter.appdata.xml
            DESTINATION share/metainfo COMPONENT metainfo
    )
    install(
        FILES resources/icons/app.svg
        DESTINATION share/icons/hicolor/scalable/apps
        RENAME io.github.Aiacos.AjazzControlCenter.svg
        COMPONENT icon
    )
    # Multi-size PNG fallbacks for environments where the SVG renderer is not available (older GTK
    # launchers, KDE icon thumbnails, GNOME tray).
    foreach(
        icon_size
        16
        24
        32
        48
        64
        128
        256
        512
    )
        install(
            FILES resources/icons/hicolor/app-${icon_size}.png
            DESTINATION share/icons/hicolor/${icon_size}x${icon_size}/apps
            RENAME io.github.Aiacos.AjazzControlCenter.png
            COMPONENT icon
        )
    endforeach()
endif()

# -----------------------------------------------------------------------------
# Packaging
# -----------------------------------------------------------------------------
set(CPACK_PACKAGE_NAME "ajazz-control-center")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_VENDOR "AJAZZ Control Center contributors")
set(CPACK_PACKAGE_CONTACT "https://github.com/Aiacos/ajazz-control-center")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
# Multi-line long description used by `rpm -qpi`, dpkg, AppStream tools, etc. CPack falls back to a
# placeholder ("DESCRIPTION ===") when this is unset.
set(CPACK_PACKAGE_DESCRIPTION
    "AJAZZ Control Center is a cross-platform desktop application for AJAZZ\n"
    "and Mirabox HID devices: stream-deck-style key panels (AKP153 / AKP153E\n"
    "/ AKP03 / AKP05), VIA-compatible keyboards (AK-series), and AJ-line\n"
    "mice. It provides a Qt-based GUI for binding keys, encoders and mouse\n"
    "buttons to multi-step action chains (key macros, URLs, plugin actions),\n"
    "managing folder pages, importing / exporting profile bundles, and a\n"
    "system-tray quick-switcher. The udev rule shipped with this package\n"
    "grants user-level device access via TAG+=\"uaccess\" — no group\n"
    "membership and no logout required."
)
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
set(CPACK_RESOURCE_FILE_README "${CMAKE_SOURCE_DIR}/README.md")
set(CPACK_STRIP_FILES TRUE)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(CPACK_GENERATOR "DEB;RPM")
    set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")

    # .deb
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Lorenzo Argentieri <uni.lorenzo.a@gmail.com>")
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
    set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "${PROJECT_HOMEPAGE_URL}")
    set(_pkg "${CMAKE_SOURCE_DIR}/packaging/linux")
    set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA "${_pkg}/postinst.sh;${_pkg}/postrm.sh")
    unset(_pkg)

    # .rpm
    set(CPACK_RPM_PACKAGE_LICENSE "GPL-3.0-or-later")
    set(CPACK_RPM_PACKAGE_GROUP "Applications/System")
    set(CPACK_RPM_PACKAGE_URL "${PROJECT_HOMEPAGE_URL}")
    set(CPACK_RPM_PACKAGE_AUTOREQ ON)
    # CPack's RPM generator ignores CPACK_PACKAGE_DESCRIPTION; the per-generator variable is
    # required to populate the spec %description block. Build the prose with string(APPEND) so each
    # source line stays under cmake-lint's 100-column ceiling without splitting the runtime value.
    set(CPACK_RPM_PACKAGE_DESCRIPTION "")
    string(
        APPEND
        CPACK_RPM_PACKAGE_DESCRIPTION
        "AJAZZ Control Center is a cross-platform desktop application for AJAZZ and Mirabox HID "
        "devices: stream-deck-style key panels (AKP153 / AKP153E / AKP03 / AKP05), "
        "VIA-compatible and Microdia-chipset keyboards (AK820 Pro, AK980 PRO, AK series), "
        "and AJ-line mice. It provides a Qt-based GUI for binding keys, encoders and mouse "
        "buttons to multi-step action chains (key macros, URLs, plugin actions), managing "
        "folder pages, importing / exporting profile bundles, and a system-tray "
        "quick-switcher. The udev rule shipped with this package grants user-level device "
        "access via TAG+=uaccess — no group membership and no logout required."
    )
    # Mirror it for DEB so dpkg-deb / apt show the same long description.
    set(CPACK_DEBIAN_PACKAGE_DESCRIPTION "${CPACK_RPM_PACKAGE_DESCRIPTION}")
    set(CPACK_RPM_POST_INSTALL_SCRIPT_FILE "${CMAKE_SOURCE_DIR}/packaging/linux/postinst.sh")
    set(CPACK_RPM_POST_UNINSTALL_SCRIPT_FILE "${CMAKE_SOURCE_DIR}/packaging/linux/postrm.sh")
    # Don't let RPM claim /usr/lib/udev/rules.d — distros already own it. Also drop hidapi's
    # pkg-config files: hidapi is FetchContent'd and statically linked into ajazz_core, so its dev
    # metadata leaks into the binary package by accident. Excluding from the RPM filelist removes
    # them from the cpio archive entirely.
    set(CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION
        "/usr/lib/udev"
        "/usr/lib/udev/rules.d"
        "/usr/share/applications"
        "/usr/share/metainfo"
        "/usr/share/icons"
        "/usr/share/icons/hicolor"
        "/usr/share/icons/hicolor/scalable"
        "/usr/share/icons/hicolor/scalable/apps"
        "/usr/lib64/pkgconfig"
        "/usr/lib64/pkgconfig/hidapi-hidraw.pc"
        "/usr/lib64/pkgconfig/hidapi-libusb.pc"
    )
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(CPACK_GENERATOR "WIX;ZIP")
    set(CPACK_WIX_UPGRADE_GUID "9b7e8f2c-5d4a-4e6f-8b1c-3a9e7d2f4c8b")
    set(CPACK_WIX_PRODUCT_ICON "${CMAKE_SOURCE_DIR}/resources/icons/app.ico")
    # WiX rejects license files without a .txt or .rtf extension. Stage a copy of ./LICENSE under
    # the build dir as LICENSE.txt and point CPack at it.
    set(_ajazz_wix_license "${CMAKE_BINARY_DIR}/LICENSE.txt")
    configure_file("${CMAKE_SOURCE_DIR}/LICENSE" "${_ajazz_wix_license}" COPYONLY)
    set(CPACK_RESOURCE_FILE_LICENSE "${_ajazz_wix_license}")
    set(CPACK_WIX_LICENSE_RTF "")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(CPACK_GENERATOR "DragNDrop;TGZ")
    set(CPACK_DMG_VOLUME_NAME "AJAZZ Control Center")
endif()

include(CPack)
