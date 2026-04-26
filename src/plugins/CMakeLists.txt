# Slice 3e (audit finding A4): the in-process pybind11-embedded plugin host
# was retired in favour of the OutOfProcessPluginHost. The library is now
# Qt-free and pybind11-free; the OOP child runs `python3` via execvp at
# runtime, so there is no compile-time Python dep here.
#
# Slice 3d adds the Windows OOP backend (`_spawnvp` + `_pipe` over the CRT)
# alongside the existing POSIX implementation. Each platform gets its own TU
# under a `#ifdef _WIN32` / `#ifndef _WIN32` guard; the public header is
# platform-agnostic.
set(_ajazz_plugins_sources)
if(WIN32)
    list(APPEND _ajazz_plugins_sources src/out_of_process_plugin_host_win32.cpp)
else()
    list(APPEND _ajazz_plugins_sources src/out_of_process_plugin_host.cpp
         src/linux_bwrap_sandbox.cpp src/macos_sandbox_exec_sandbox.cpp
    )
endif()

add_library(ajazz_plugins STATIC ${_ajazz_plugins_sources})
add_library(ajazz::plugins ALIAS ajazz_plugins)

target_include_directories(
    ajazz_plugins PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(ajazz_plugins PUBLIC ajazz::core PRIVATE ajazz::warnings ajazz::sanitizers)

set_target_properties(ajazz_plugins PROPERTIES POSITION_INDEPENDENT_CODE ON)
