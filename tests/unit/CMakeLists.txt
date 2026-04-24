add_executable(
    ajazz_unit_tests test_logger.cpp test_device_registry.cpp test_akp153_protocol.cpp
                     test_profile_serialization.cpp test_event_bus.cpp
)

target_link_libraries(
    ajazz_unit_tests PRIVATE ajazz::core ajazz::streamdeck ajazz::keyboard ajazz::mouse
                             ajazz::warnings Catch2::Catch2WithMain
)

# The AKP153 protocol test needs access to the private protocol header.
target_include_directories(ajazz_unit_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/devices/streamdeck/src)

catch_discover_tests(ajazz_unit_tests)
