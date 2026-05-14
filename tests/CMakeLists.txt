include(FetchContent)
FetchContent_Declare(Catch2 GIT_REPOSITORY https://github.com/catchorg/Catch2.git GIT_TAG v3.7.1)
FetchContent_MakeAvailable(Catch2)

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)

add_subdirectory(unit)
add_subdirectory(integration)

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
