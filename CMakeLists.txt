cmake_minimum_required(VERSION 3.28)

project(ajazz-control-center
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
option(AJAZZ_BUILD_APP          "Build the Qt desktop application"      ON)
option(AJAZZ_BUILD_TESTS        "Build unit and integration tests"      ON)
option(AJAZZ_BUILD_PYTHON_HOST  "Build embedded Python plugin host"     ON)
option(AJAZZ_ENABLE_WERROR      "Treat warnings as errors"              ON)
option(AJAZZ_ENABLE_SANITIZERS  "Enable ASan/UBSan in Debug builds"     OFF)
option(AJAZZ_ENABLE_COVERAGE    "Enable code coverage instrumentation"  OFF)

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
            GIT_TAG v2.13.6
        )
        FetchContent_MakeAvailable(pybind11)
    endif()
endif()

# hidapi — vendored via FetchContent for reproducibility
include(FetchContent)
FetchContent_Declare(
    hidapi
    GIT_REPOSITORY https://github.com/libusb/hidapi.git
    GIT_TAG        hidapi-0.14.0
)
set(HIDAPI_BUILD_HIDTEST OFF CACHE BOOL "")
set(HIDAPI_WITH_TESTS OFF CACHE BOOL "")
FetchContent_MakeAvailable(hidapi)

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
# Install / packaging
# -----------------------------------------------------------------------------
include(CPack)
set(CPACK_PACKAGE_NAME "ajazz-control-center")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_VENDOR "AJAZZ Control Center contributors")
set(CPACK_PACKAGE_CONTACT "https://github.com/Aiacos/ajazz-control-center")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
set(CPACK_RESOURCE_FILE_README  "${CMAKE_SOURCE_DIR}/README.md")

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(CPACK_GENERATOR "DEB;RPM")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Lorenzo Argentieri")
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(CPACK_GENERATOR "WIX;ZIP")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(CPACK_GENERATOR "DragNDrop;TGZ")
endif()
