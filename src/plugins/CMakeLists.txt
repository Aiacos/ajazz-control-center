set(_ajazz_plugins_sources src/plugin_host.cpp src/python_bindings.cpp)
if(NOT WIN32)
    list(APPEND _ajazz_plugins_sources src/out_of_process_plugin_host.cpp
         src/linux_bwrap_sandbox.cpp
    )
endif()
add_library(ajazz_plugins STATIC ${_ajazz_plugins_sources})
add_library(ajazz::plugins ALIAS ajazz_plugins)

target_include_directories(
    ajazz_plugins PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(
    ajazz_plugins PUBLIC ajazz::core PRIVATE ajazz::warnings ajazz::sanitizers pybind11::embed
                                             Python3::Python
)

set_target_properties(ajazz_plugins PROPERTIES POSITION_INDEPENDENT_CODE ON)
