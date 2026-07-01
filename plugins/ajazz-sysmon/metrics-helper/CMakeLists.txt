# btop-metrics-helper — a standalone binary that reuses btop's collectors and
# emits the unified metrics JSON schema on stdout. Spawned by the ajazz-sysmon
# Stream Deck plugin. Standalone build for now (not yet wired into the app tree):
#
#   cmake -S . -B build && cmake --build build
#   ./build/btop-metrics-helper --once
#
# btop is vendored as a git submodule under third_party/btop (Apache-2.0).
cmake_minimum_required(VERSION 3.20)
project(btop_metrics_helper LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(BTOP "${CMAKE_CURRENT_SOURCE_DIR}/third_party/btop")
if(NOT EXISTS "${BTOP}/src/btop_shared.cpp")
    message(
        FATAL_ERROR
            "btop submodule missing at ${BTOP}. Run: git submodule update --init --recursive"
    )
endif()

if(UNIX AND NOT APPLE)
    # Minimal set of btop collector TUs + our shim (shim provides the TUI/box globals
    # btop.cpp/btop_draw.cpp would define). GPU_SUPPORT is intentionally OFF for Phase 0
    # (CPU/mem/net/temp only); it is added in a later phase.
    add_executable(
        btop-metrics-helper
        src/main.cpp
        src/shim_linux.cpp
        "${BTOP}/src/btop_shared.cpp"
        "${BTOP}/src/btop_config.cpp"
        "${BTOP}/src/btop_tools.cpp"
        "${BTOP}/src/btop_log.cpp"
        "${BTOP}/src/linux/btop_collect.cpp"
    )
    target_include_directories(btop-metrics-helper PRIVATE "${BTOP}/include" "${BTOP}/src")
    target_compile_definitions(btop-metrics-helper PRIVATE FMT_HEADER_ONLY)
    target_compile_options(btop-metrics-helper PRIVATE -O2)
    target_link_libraries(btop-metrics-helper PRIVATE ${CMAKE_DL_LIBS} pthread)
else()
    message(
        STATUS "btop-metrics-helper: only the Linux backend exists so far; skipping this target."
    )
endif()
