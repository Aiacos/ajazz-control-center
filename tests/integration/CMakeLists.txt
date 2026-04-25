add_executable(ajazz_integration_tests test_capture_replay.cpp)

target_link_libraries(
    ajazz_integration_tests PRIVATE ajazz::core ajazz::streamdeck ajazz::warnings
                                    Catch2::Catch2WithMain
)

target_include_directories(
    ajazz_integration_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/devices/streamdeck/src
                                    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Inject an absolute path to the on-disk fixtures so tests can be launched from any working
# directory (CTest, IDE, manual run).
target_compile_definitions(
    ajazz_integration_tests PRIVATE AJAZZ_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
)

catch_discover_tests(ajazz_integration_tests)
