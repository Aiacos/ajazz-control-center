# Slice 3e (audit finding A4): the in-process pybind11-embedded plugin host
# was retired in favour of the OutOfProcessPluginHost. The library is now
# Qt-free and pybind11-free; the OOP child runs `python3` via execvp at
# runtime, so there is no compile-time Python dep here.
#
# Slice 3d adds the Windows OOP backend (`_spawnvp` + `_pipe` over the CRT)
# alongside the existing POSIX implementation. Each platform gets its own TU
# under a `#ifdef _WIN32` / `#ifndef _WIN32` guard; the public header is
# platform-agnostic.
# Phase 7 / ARCH-01: shared nlohmann-based loadTrustRoots impl lives in
# manifest_signer_common.cpp and is compiled on every platform. The
# platform-specific verifier TU (`manifest_signer.cpp` / `manifest_signer_win32.cpp`)
# now includes `manifest_signer_common.hpp` and routes through the shared symbol —
# drift between the two TUs is structurally impossible (D-01).
set(_ajazz_plugins_sources src/sandbox.cpp src/manifest_signer_common.cpp)
if(WIN32)
    list(
        APPEND
        _ajazz_plugins_sources
        src/out_of_process_plugin_host_win32.cpp
        src/windows_app_container_sandbox.cpp
        # SEC-003 follow-up #51: Ed25519 manifest verifier (Win32). Mirrors `manifest_signer.cpp`
        # but uses `_wspawnvp` instead of `fork`+`execvp`.
        src/manifest_signer_win32.cpp
        # CR-01 fix (Phase 06): per-spawn UTF-16 env block for CreateProcessW. Replaces the previous
        # parent-env-mutating `_putenv_s` pattern that leaked PYTHONPATH across host instances and
        # into sibling subprocesses.
        src/win32_env_block.cpp
    )
else()
    list(
        APPEND
        _ajazz_plugins_sources
        src/out_of_process_plugin_host.cpp
        src/linux_bwrap_sandbox.cpp
        src/macos_sandbox_exec_sandbox.cpp
        # SEC-003 follow-up #51: Ed25519 manifest verifier (POSIX).
        src/manifest_signer.cpp
    )
endif()

add_library(ajazz_plugins STATIC ${_ajazz_plugins_sources})
add_library(ajazz::plugins ALIAS ajazz_plugins)

target_include_directories(
    ajazz_plugins PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# nlohmann_json is PRIVATE — it MUST NOT appear in `ajazz_core` or in any installed public header
# (COD-031 invariant per Phase 3 / ARCH-01). The shared loadTrustRoots impl in
# `manifest_signer_common.cpp` is the only consumer.
target_link_libraries(
    ajazz_plugins PUBLIC ajazz::core PRIVATE ajazz::warnings ajazz::sanitizers
                                             nlohmann_json::nlohmann_json
)

set_target_properties(ajazz_plugins PROPERTIES POSITION_INDEPENDENT_CODE ON)
