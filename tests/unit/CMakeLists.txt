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
    # QtExecutor: pins the A2 audit-finding contract — non-blocking
    # scheduling, parent-thread dispatch, null-safe, drop-on-destroy.
    # Q_OBJECT in the header so AUTOMOC has to scan it.
    test_qt_executor.cpp
    ${CMAKE_SOURCE_DIR}/src/app/src/qt_executor.cpp
    # BrandingService: the QML `branding` context property's contract.
    # Default palette + theme-file override semantics; isolated from
    # the developer's real QSettings via test mode in qt_app_fixture.
    test_branding_service.cpp
    ${CMAKE_SOURCE_DIR}/src/app/src/branding_service.cpp
    # ThemeService: the QML `themeService` context property's contract.
    # Mode parsing, persistence, modeChanged signal semantics. Layered
    # on top of BrandingService (already in this list).
    test_theme_service.cpp
    ${CMAKE_SOURCE_DIR}/src/app/src/theme_service.cpp
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
        PRIVATE test_windows_app_container_sandbox.cpp
                ${CMAKE_SOURCE_DIR}/src/plugins/src/out_of_process_plugin_host_win32.cpp
                ${CMAKE_SOURCE_DIR}/src/plugins/src/windows_app_container_sandbox.cpp
                ${CMAKE_SOURCE_DIR}/src/plugins/src/sandbox.cpp
    )
else()
    # SEC-003 follow-up #51: Ed25519 manifest verifier. POSIX-only in this slice; the Win32 backend
    # lands as a follow-up. The test exec's the publisher CLI via fork+execvp so the canonical-form
    # contract is exercised cross-language end-to-end.
    target_sources(
        ajazz_unit_tests
        PRIVATE test_linux_bwrap_sandbox.cpp
                test_macos_sandbox_exec_sandbox.cpp
                test_manifest_signer.cpp
                ${CMAKE_SOURCE_DIR}/src/plugins/src/out_of_process_plugin_host.cpp
                ${CMAKE_SOURCE_DIR}/src/plugins/src/linux_bwrap_sandbox.cpp
                ${CMAKE_SOURCE_DIR}/src/plugins/src/macos_sandbox_exec_sandbox.cpp
                ${CMAKE_SOURCE_DIR}/src/plugins/src/manifest_signer.cpp
                ${CMAKE_SOURCE_DIR}/src/plugins/src/sandbox.cpp
    )
    # AJAZZ_TEST_REPO_ROOT lets the verifier test locate scripts/sign-plugin-manifest.py +
    # resources/trusted_publishers.json regardless of CWD when ctest runs the binary.
    set_source_files_properties(
        test_manifest_signer.cpp PROPERTIES COMPILE_DEFINITIONS
                                            "AJAZZ_TEST_REPO_ROOT=\"${CMAKE_SOURCE_DIR}\""
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
            Qt6::Gui # QColor for BrandingService (no rendering required)
            Qt6::Network
            Qt6::Qml # QML_NAMED_ELEMENT in plugin_catalog_model.hpp pulls in QtQmlIntegration
            Qt6::Test # QSignalSpy
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
