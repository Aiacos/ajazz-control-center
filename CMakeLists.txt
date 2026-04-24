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
            pybind11 GIT_REPOSITORY https://github.com/pybind/pybind11.git GIT_TAG v2.13.6
        )
        FetchContent_MakeAvailable(pybind11)
    endif()
endif()

# hidapi — vendored via FetchContent for reproducibility
include(FetchContent)
FetchContent_Declare(
    hidapi GIT_REPOSITORY https://github.com/libusb/hidapi.git GIT_TAG hidapi-0.14.0
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
    set(CPACK_RPM_POST_INSTALL_SCRIPT_FILE "${CMAKE_SOURCE_DIR}/packaging/linux/postinst.sh")
    set(CPACK_RPM_POST_UNINSTALL_SCRIPT_FILE "${CMAKE_SOURCE_DIR}/packaging/linux/postrm.sh")
    # Don't let RPM claim /usr/lib/udev/rules.d — distros already own it.
    set(CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION
        "/usr/lib/udev"
        "/usr/lib/udev/rules.d"
        "/usr/share/applications"
        "/usr/share/metainfo"
        "/usr/share/icons"
        "/usr/share/icons/hicolor"
        "/usr/share/icons/hicolor/scalable"
        "/usr/share/icons/hicolor/scalable/apps"
    )
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(CPACK_GENERATOR "WIX;ZIP")
    set(CPACK_WIX_UPGRADE_GUID "9b7e8f2c-5d4a-4e6f-8b1c-3a9e7d2f4c8b")
    set(CPACK_WIX_PRODUCT_ICON "${CMAKE_SOURCE_DIR}/resources/icons/app.ico")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(CPACK_GENERATOR "DragNDrop;TGZ")
    set(CPACK_DMG_VOLUME_NAME "AJAZZ Control Center")
endif()

include(CPack)
