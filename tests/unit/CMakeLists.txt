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
    # Property Inspector URL policy: pure C++ helpers (no QtWebEngine), so we
    # link the source directly into this test binary without dragging the
    # WebEngine dependency in. Coverage in test_pi_bridge.cpp.
    test_pi_bridge.cpp
    ${CMAKE_SOURCE_DIR}/src/app/src/pi_url_policy.cpp
)

# Set the AUTOMOC flag for the fetcher TU we pull in from the app target — StreamdockCatalogFetcher
# is a Q_OBJECT so it needs moc to run.
set_target_properties(ajazz_unit_tests PROPERTIES AUTOMOC ON)

# Embed the bundled offline fixture under the same QRC path the runtime binary uses
# (`qrc:/qt/qml/AjazzControlCenter/streamdock-fallback.json`), so loadBundledFallback() finds it
# during the cache / fallback tests.
set(_acc_fallback ${CMAKE_SOURCE_DIR}/resources/streamdock-fallback.json)
set_source_files_properties(${_acc_fallback} PROPERTIES QT_RESOURCE_ALIAS streamdock-fallback.json)
qt_add_resources(
    ajazz_unit_tests "streamdock_fallback_test_resource" PREFIX "/qt/qml/AjazzControlCenter" FILES
    ${_acc_fallback}
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
