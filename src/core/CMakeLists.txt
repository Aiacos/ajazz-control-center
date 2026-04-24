add_library(ajazz_core STATIC
    src/device_registry.cpp
    src/hid_transport.cpp
    src/event_bus.cpp
    src/profile.cpp
    src/logger.cpp
)

add_library(ajazz::core ALIAS ajazz_core)

target_include_directories(ajazz_core
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(ajazz_core
    PUBLIC
        Qt6::Core
    PRIVATE
        ajazz::warnings
        ajazz::sanitizers
        hidapi::hidapi
)

set_target_properties(ajazz_core PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)
