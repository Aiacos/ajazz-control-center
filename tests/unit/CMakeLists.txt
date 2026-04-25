add_executable(
    ajazz_unit_tests
    test_logger.cpp
    test_device_registry.cpp
    test_akp153_protocol.cpp
    test_akp03_protocol.cpp
    test_akp05_protocol.cpp
    test_proprietary_keyboard_protocol.cpp
    test_profile_serialization.cpp
    test_profile_io.cpp
    test_event_bus.cpp
    test_action_engine.cpp
)

target_link_libraries(
    ajazz_unit_tests PRIVATE ajazz::core ajazz::streamdeck ajazz::keyboard ajazz::mouse
                             ajazz::warnings Catch2::Catch2WithMain
)

# Protocol-header tests need access to the backends' private headers.
target_include_directories(
    ajazz_unit_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/devices/streamdeck/src
                             ${CMAKE_SOURCE_DIR}/src/devices/keyboard/src
)

catch_discover_tests(ajazz_unit_tests)
