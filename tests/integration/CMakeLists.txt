add_executable(ajazz_integration_tests test_capture_replay.cpp)

# CR-01 (Phase 06, Plan 06-02): Win32-only integration test that spawns a real Python child via
# CreateProcessW with a Win32EnvBlock and asserts (a) the child sees the override PYTHONPATH AND (b)
# the parent's _wgetenv(PYTHONPATH) is unchanged after the spawn (the actual CR-01 regression test).
# N=2 concurrent spawns close the cross-instance race the v1.0 audit flagged.
if(WIN32)
    target_sources(
        ajazz_integration_tests PRIVATE test_oop_plugin_host_win32_env.cpp
                                        ${CMAKE_SOURCE_DIR}/src/plugins/src/win32_env_block.cpp
    )
    target_include_directories(ajazz_integration_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/plugins/src)
endif()

target_link_libraries(
    ajazz_integration_tests PRIVATE ajazz::core ajazz::streamdeck ajazz::warnings
                                    Catch2::Catch2WithMain
)

target_include_directories(
    ajazz_integration_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/devices/streamdeck/src
                                    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Inject an absolute path to the on-disk fixtures so tests can be launched from any working
# directory (CTest, IDE, manual run). The CR-01 regression test (Plan 06-02) uses this to locate
# `fixtures/print_pythonpath.py`.
target_compile_definitions(
    ajazz_integration_tests PRIVATE AJAZZ_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
)

catch_discover_tests(ajazz_integration_tests)
