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
option(AJAZZ_BUILD_PYTHON_HOST "Build embedded Python plugin host" ON)
option(AJAZZ_ENABLE_WERROR "Treat warnings as errors" ON)
option(AJAZZ_ENABLE_SANITIZERS "Enable ASan/UBSan in Debug builds" OFF)
option(AJAZZ_ENABLE_TSAN "Enable ThreadSanitizer (mutually exclusive with ASan)" OFF)
option(AJAZZ_ENABLE_COVERAGE "Enable code coverage instrumentation" OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
include(Warnings)
include(Sanitizers)

# -----------------------------------------------------------------------------
# Dependencies
# -----------------------------------------------------------------------------
find_package(Qt6 6.7 COMPONENTS Core Quick QuickControls2 Widgets Svg REQUIRED)
qt_standard_project_setup(REQUIRES 6.7)

if(AJAZZ_BUILD_PYTHON_HOST)
    find_package(Python3 3.11 COMPONENTS Interpreter Development REQUIRED)
    find_package(pybind11 2.12 QUIET)
    if(NOT pybind11_FOUND)
        include(FetchContent)
        FetchContent_Declare(
            pybind11
            GIT_REPOSITORY https://github.com/pybind/pybind11.git
            GIT_TAG a2e59f0e7065404b44dfe92a28aca47ba1378dc4 # v2.13.6
            GIT_SHALLOW OFF
        )
        FetchContent_MakeAvailable(pybind11)
    endif()
endif()

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
# hidapi-0.14.0 declares cmake_minimum_required(VERSION 3.1.3); newer CMake (>= 4.0) removed compat
# with < 3.5. Force a modern policy floor for the subproject only so the fetched CMakeLists is
# accepted without patching.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "" FORCE)
FetchContent_MakeAvailable(hidapi)
unset(CMAKE_POLICY_VERSION_MINIMUM CACHE)

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
