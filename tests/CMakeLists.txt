include(FetchContent)
FetchContent_Declare(Catch2 GIT_REPOSITORY https://github.com/catchorg/Catch2.git GIT_TAG v3.7.1)
FetchContent_MakeAvailable(Catch2)

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)

add_subdirectory(unit)
add_subdirectory(integration)

# QML load-time smoke harness (health report 2026-05-22, finding #3). Rebuilds the
# AjazzControlCenter QML module in a standalone target and load-tests every component through a real
# QQmlEngine. Runs headless (QT_QPA_PLATFORM=offscreen).
#
# Gated on AJAZZ_BUILD_APP: the harness reuses the QML module SOURCES/QML_FILES/RESOURCES exported
# by src/app (ACC_QML_MODULE_SOURCES / ACC_APP_SOURCE_DIR), so it cannot configure when the app is
# disabled. The sanitizer CI job builds with -DAJAZZ_BUILD_APP=OFF and only targets the unit +
# integration binaries, so skip the QML smoke test there instead of hard-failing configure.
if(AJAZZ_BUILD_APP)
    add_subdirectory(qml)
else()
    message(STATUS "tests/qml: skipped (AJAZZ_BUILD_APP=OFF -- no app QML module to smoke-test)")
endif()

# Phase 7 / Plan 07-03: opt-in libFuzzer harnesses. Gated on AJAZZ_BUILD_FUZZ_TESTS (default OFF in
# the root CMakeLists). The Clang-only requirement is checked here at configure time — better to
# surface the constraint up front than produce a confusing link error later.
if(AJAZZ_BUILD_FUZZ_TESTS)
    if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        message(FATAL_ERROR "AJAZZ_BUILD_FUZZ_TESTS=ON requires Clang (libFuzzer is a Clang-only "
                            "sanitizer runtime). Current compiler: ${CMAKE_CXX_COMPILER_ID}."
        )
    endif()
    add_subdirectory(fuzz)
endif()
