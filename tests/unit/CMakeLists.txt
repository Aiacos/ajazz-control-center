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
    # Streamdock catalogue fetcher: pure helpers + cache + fallback. The
    # fetcher source is compiled directly into the test binary so we don't
    # need to factor it out of the app target.
    test_streamdock_catalog_fetcher.cpp
    ${CMAKE_SOURCE_DIR}/src/app/src/streamdock_catalog_fetcher.cpp
    # OpenDeck catalogue fetcher: parallel coverage to the Streamdock suite.
    # The OpenDeck wire shape is a flat JSON array (not the Streamdock
    # `data.list` envelope), so the parser tests exercise a genuinely
    # different path.
    test_opendeck_catalog_fetcher.cpp
    ${CMAKE_SOURCE_DIR}/src/app/src/opendeck_catalog_fetcher.cpp
    # Property Inspector URL policy: pure C++ helpers (no QtWebEngine), so we
    # link the source directly into this test binary without dragging the
    # WebEngine dependency in. Coverage in test_pi_bridge.cpp.
    test_pi_bridge.cpp
    ${CMAKE_SOURCE_DIR}/src/app/src/pi_url_policy.cpp
)
# Out-of-process plugin host (audit finding A4). The class itself is Qt-free and pybind11-free, so
# we link the platform-appropriate source directly into this test binary instead of pulling the
# whole `ajazz_plugins` library. Slice 3d (this cycle) extended the test to all platforms — the host
# header is platform-agnostic and the win32 backend exposes the same `IPluginHost` surface, so
# `test_out_of_process_plugin_host.cpp` exercises the same handshake / load_all / dispatch /
# crash-isolation contract on Linux + macOS + Windows. Sandbox tests stay POSIX-gated because they
# import platform-specific sandbox headers (bwrap on Linux, sandbox-exec on macOS).
target_sources(ajazz_unit_tests PRIVATE test_out_of_process_plugin_host.cpp)
target_include_directories(ajazz_unit_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/plugins/include)

if(WIN32)
    target_sources(
        ajazz_unit_tests
        PRIVATE ${CMAKE_SOURCE_DIR}/src/plugins/src/out_of_process_plugin_host_win32.cpp
    )
else()
    target_sources(
        ajazz_unit_tests
        PRIVATE test_linux_bwrap_sandbox.cpp
                test_macos_sandbox_exec_sandbox.cpp
                ${CMAKE_SOURCE_DIR}/src/plugins/src/out_of_process_plugin_host.cpp
                ${CMAKE_SOURCE_DIR}/src/plugins/src/linux_bwrap_sandbox.cpp
                ${CMAKE_SOURCE_DIR}/src/plugins/src/macos_sandbox_exec_sandbox.cpp
    )
endif()

# Set the AUTOMOC flag for the fetcher TU we pull in from the app target — StreamdockCatalogFetcher
# is a Q_OBJECT so it needs moc to run.
set_target_properties(ajazz_unit_tests PROPERTIES AUTOMOC ON)

# Embed the bundled offline fixtures under the same QRC paths the runtime binary uses
# (`qrc:/qt/qml/AjazzControlCenter/streamdock-fallback.json` and `opendeck-fallback.json`), so
# loadBundledFallback() finds them during the cache / fallback tests.
set(_acc_fallback ${CMAKE_SOURCE_DIR}/resources/streamdock-fallback.json)
set_source_files_properties(${_acc_fallback} PROPERTIES QT_RESOURCE_ALIAS streamdock-fallback.json)
qt_add_resources(
    ajazz_unit_tests "streamdock_fallback_test_resource" PREFIX "/qt/qml/AjazzControlCenter" FILES
    ${_acc_fallback}
)
set(_acc_opendeck_fallback ${CMAKE_SOURCE_DIR}/resources/opendeck-fallback.json)
set_source_files_properties(
    ${_acc_opendeck_fallback} PROPERTIES QT_RESOURCE_ALIAS opendeck-fallback.json
)
qt_add_resources(
    ajazz_unit_tests "opendeck_fallback_test_resource" PREFIX "/qt/qml/AjazzControlCenter" FILES
    ${_acc_opendeck_fallback}
)

target_link_libraries(
    ajazz_unit_tests
    PRIVATE ajazz::core
            ajazz::streamdeck
            ajazz::keyboard
            ajazz::mouse
            ajazz::warnings
            Catch2::Catch2WithMain
            Qt6::Core
            Qt6::Network
)

# Protocol-header tests need access to the backends' private headers. The streamdock fetcher TU we
# pull from the app target also needs its sibling `plugin_catalog_model.hpp` and the fetcher header
# on the include path — hence the additional `src/app/src` entry.
target_include_directories(
    ajazz_unit_tests
    PRIVATE ${CMAKE_SOURCE_DIR}/src/devices/streamdeck/src
            ${CMAKE_SOURCE_DIR}/src/devices/keyboard/src ${CMAKE_SOURCE_DIR}/src/app/src
)

catch_discover_tests(ajazz_unit_tests)
