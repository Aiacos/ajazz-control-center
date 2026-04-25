add_library(
    ajazz_core STATIC src/device_registry.cpp src/hid_transport.cpp src/event_bus.cpp
                      src/profile.cpp src/logger.cpp src/hotplug_monitor.cpp
)

add_library(ajazz::core ALIAS ajazz_core)

target_include_directories(
    ajazz_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(
    ajazz_core PUBLIC Qt6::Core PRIVATE ajazz::warnings ajazz::sanitizers hidapi::hidapi
)

# ----------------------------------------------------------------------------
# HotplugMonitor needs OS-specific libraries for USB-arrival notifications.
# ----------------------------------------------------------------------------
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(UDEV REQUIRED IMPORTED_TARGET libudev)
    target_link_libraries(ajazz_core PRIVATE PkgConfig::UDEV)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    target_link_libraries(ajazz_core PRIVATE "-framework CoreFoundation" "-framework IOKit")
elseif(WIN32)
    # RegisterDeviceNotificationW lives in user32.lib, which MSVC links by default; explicit
    # just-in-case for Clang-cl / MinGW.
    target_link_libraries(ajazz_core PRIVATE user32)
endif()

set_target_properties(ajazz_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
