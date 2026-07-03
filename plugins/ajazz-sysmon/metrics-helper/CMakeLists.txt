# btop-metrics-helper — a standalone binary that reuses btop's collectors and
# emits the unified metrics JSON schema on stdout. Spawned by the ajazz-sysmon
# Stream Deck plugin. Standalone build for now (not yet wired into the app tree):
#
#   cmake -S . -B build && cmake --build build
#   ./build/btop-metrics-helper --once
#
# btop is vendored as a git submodule under third_party/btop (Apache-2.0).
# C is needed for btop's vendored intel_gpu_top sources (GPU_SUPPORT).
cmake_minimum_required(VERSION 3.20)
project(btop_metrics_helper LANGUAGES CXX C)

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
    # btop.cpp/btop_draw.cpp would define). GPU_SUPPORT is ON as of Phase 3: Nvml (dlopen
    # libnvidia-ml) / Rsmi (dlopen librocm_smi64) / amdgpu sysfs, plus btop's vendored intel_gpu_top
    # C sources (btop builds them as OBJECT lib `igt` with -w; we compile them straight into the
    # target the same way).
    set(IGT_SOURCES
        "${BTOP}/src/linux/intel_gpu_top/igt_perf.c"
        "${BTOP}/src/linux/intel_gpu_top/intel_device_info.c"
        "${BTOP}/src/linux/intel_gpu_top/intel_gpu_top.c"
        "${BTOP}/src/linux/intel_gpu_top/intel_name_lookup_shim.c"
    )
    # Third-party C code, not ours to fix: silence all warnings (matches btop's own build).
    set_source_files_properties(${IGT_SOURCES} PROPERTIES COMPILE_OPTIONS "-w")
    add_executable(
        btop-metrics-helper
        src/main.cpp
        src/shim_linux.cpp
        "${BTOP}/src/btop_shared.cpp"
        "${BTOP}/src/btop_config.cpp"
        "${BTOP}/src/btop_tools.cpp"
        "${BTOP}/src/btop_log.cpp"
        "${BTOP}/src/linux/btop_collect.cpp"
        ${IGT_SOURCES}
    )
    target_include_directories(btop-metrics-helper PRIVATE "${BTOP}/include" "${BTOP}/src")
    target_compile_definitions(btop-metrics-helper PRIVATE FMT_HEADER_ONLY GPU_SUPPORT)
    target_compile_options(btop-metrics-helper PRIVATE -O2)
    target_link_libraries(btop-metrics-helper PRIVATE ${CMAKE_DL_LIBS} pthread)
else()
    message(
        STATUS "btop-metrics-helper: only the Linux backend exists so far; skipping this target."
    )
endif()
