# Slice 3e (audit finding A4): the in-process pybind11-embedded plugin host
# was retired in favour of the OutOfProcessPluginHost. The library is now
# Qt-free and pybind11-free; the OOP child runs `python3` via execvp at
# runtime, so there is no compile-time Python dep here.
set(_ajazz_plugins_sources)
if(NOT WIN32)
    list(APPEND _ajazz_plugins_sources src/out_of_process_plugin_host.cpp
         src/linux_bwrap_sandbox.cpp src/macos_sandbox_exec_sandbox.cpp
    )
endif()

# Windows still has no OOP backend (slice 3d covers the `_spawnvp` port). Provide a stub TU so the
# static library always has at least one object — `add_library(... STATIC <empty>)` is a
# configure-time error on Ninja.
if(NOT _ajazz_plugins_sources)
    file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/_plugins_stub.cpp
         "// Auto-generated; placeholder until slice 3d lands the Windows OOP host.\n"
    )
    list(APPEND _ajazz_plugins_sources ${CMAKE_CURRENT_BINARY_DIR}/_plugins_stub.cpp)
endif()

add_library(ajazz_plugins STATIC ${_ajazz_plugins_sources})
add_library(ajazz::plugins ALIAS ajazz_plugins)

target_include_directories(
    ajazz_plugins PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(ajazz_plugins PUBLIC ajazz::core PRIVATE ajazz::warnings ajazz::sanitizers)

set_target_properties(ajazz_plugins PROPERTIES POSITION_INDEPENDENT_CODE ON)
