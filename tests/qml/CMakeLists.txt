# ----------------------------------------------------------------------------
# QML load-time smoke test (health report 2026-05-22, finding #3).
#
# Builds a standalone executable that recreates the production
# `AjazzControlCenter` QML module — reusing the exact SOURCES / QML_FILES /
# RESOURCES lists exported from src/app/CMakeLists.txt so the two cannot drift —
# then drives Application::exposeToQml() (the production singleton-registration
# path) and loads every shippable component through a real QQmlEngine.
#
# Headless: ctest sets QT_QPA_PLATFORM=offscreen for this target (and the test
# also pins it in-code) so it needs no display/GPU on CI.
#
# WebEngine is intentionally NOT enabled here: the harness exercises the
# minimal-Qt fallback module (no PIWebView.qml, no QtWebEngine/QtWebChannel
# dependency), which keeps the headless build self-contained. The HTML Property
# Inspector path is covered separately by test_pi_bridge.cpp.
#
# Phase 26 Plan 26-05 (REQ-26-B) adds two sets of tests to this same target:
#   test_device_view_tests.cpp    -- Catch2 C++ driver for geometry + drag-drop
#   test_device_view_geometry.qml -- QtTest QML spec for 3 SKU geometry checks
#   test_device_view_drag_drop.qml -- QtTest QML spec for commit-method wiring
# The .qml files are added to the module's QML_FILES so they are accessible as
# AjazzControlCenter/test_device_view_geometry and .../test_device_view_drag_drop.
# ----------------------------------------------------------------------------

if(NOT DEFINED ACC_QML_MODULE_SOURCES OR NOT DEFINED ACC_APP_SOURCE_DIR)
    message(FATAL_ERROR "tests/qml requires the app QML module lists exported from src/app "
                        "(ACC_QML_MODULE_SOURCES / ACC_APP_SOURCE_DIR). Was src/app configured?"
    )
endif()

# qt_add_qml_module's SOURCES / QML_FILES / RESOURCES are relative to the current source directory,
# but the exported lists are relative to src/app. Rebase them to absolute paths so this target can
# sit in tests/qml.
set(_qmlt_sources "")
foreach(rel_src IN LISTS ACC_QML_MODULE_SOURCES)
    list(APPEND _qmlt_sources "${ACC_APP_SOURCE_DIR}/${rel_src}")
endforeach()
set(_qmlt_qml_files "")
foreach(rel_qml IN LISTS ACC_QML_MODULE_QML_FILES)
    set(_abs "${ACC_APP_SOURCE_DIR}/${rel_qml}")
    list(APPEND _qmlt_qml_files "${_abs}")
    # The QML files are referenced by absolute path (they live in src/app, not here), so Qt needs an
    # explicit relative resource alias for each. Strip the leading `qml/` so the in-module path
    # matches the app target's layout (qrc:/qt/qml/AjazzControlCenter/<Name>.qml and
    # .../components/<Name>.qml).
    string(REGEX REPLACE "^qml/" "" _alias "${rel_qml}")
    set_source_files_properties("${_abs}" PROPERTIES QT_RESOURCE_ALIAS "${_alias}")
endforeach()

# The Theme.qml + DragRelay.qml singletons need the same source-file property the app sets, or
# qmlcachegen registers them as plain components.
set_source_files_properties(
    "${ACC_APP_SOURCE_DIR}/qml/Theme.qml" PROPERTIES QT_QML_SINGLETON_TYPE TRUE
)
set_source_files_properties(
    "${ACC_APP_SOURCE_DIR}/qml/DragRelay.qml" PROPERTIES QT_QML_SINGLETON_TYPE TRUE
)

qt_add_executable(
    ajazz_qml_tests
    test_qml_smoke.cpp
    # Phase 26 Plan 26-05 (REQ-26-B): DeviceView geometry + drag-drop Catch2 tests.
    test_device_view_tests.cpp
    # Application controller + its non-module deps. These are recompiled here (rather than linked
    # from the app executable, which is not a library) so the test owns the full singleton-wiring
    # path.
    ${ACC_APP_SOURCE_DIR}/src/application.cpp
    ${ACC_APP_SOURCE_DIR}/src/hotplug_debouncer.cpp
    ${ACC_APP_SOURCE_DIR}/src/streamdock_catalog_fetcher.cpp
    ${ACC_APP_SOURCE_DIR}/src/opendeck_catalog_fetcher.cpp
    ${ACC_APP_SOURCE_DIR}/src/sdplugin_extractor.cpp
    # FirmwareUpdateService is an Application member but is NOT in the QML module SOURCES list (it
    # has no QML_NAMED_ELEMENT export path via the module — it's registered manually). app_icon
    # backs the TrayController constructed in Application's ctor.
    ${ACC_APP_SOURCE_DIR}/src/firmware_update_service.cpp
    ${ACC_APP_SOURCE_DIR}/src/app_icon.cpp
    # PluginCatalogProxy is a QML_NAMED_ELEMENT type used by PluginStore.qml. In the app target it's
    # a plain target source (not a qt_add_qml_module SOURCE), and qmltyperegistration picks it up
    # because it lives in the same target. We must do the same here or PluginStore.qml fails with
    # "PluginCatalogProxy is not a type".
    ${ACC_APP_SOURCE_DIR}/src/plugin_catalog_proxy_model.cpp
    # Debug-control epic + plugin host: Application::run()/initPluginHost() reference these
    # unconditionally-compiled app sources (they are NOT in ACC_QML_MODULE_SOURCES — they have no
    # QML_NAMED_ELEMENT export). Recompile them here so application.cpp links; otherwise the smoke
    # test fails with undefined references to DebugLogging::*, DebugControlServer::*,
    # registerDebugControlMethods/registerQmlControlMethods, PluginDebugService::*,
    # PluginManager::*. The WebSockets-gated symbols they touch (SdPluginServer /
    # PluginDeviceBridge) are added in the AJAZZ_HAVE_WEBSOCKETS block below, mirroring the app
    # target.
    ${ACC_APP_SOURCE_DIR}/src/debug_logging.cpp
    ${ACC_APP_SOURCE_DIR}/src/debug_control_server.cpp
    ${ACC_APP_SOURCE_DIR}/src/debug_control_facade.cpp
    ${ACC_APP_SOURCE_DIR}/src/debug_control_qml.cpp
    ${ACC_APP_SOURCE_DIR}/src/plugin_debug_service.cpp
    ${ACC_APP_SOURCE_DIR}/src/plugin_manager.cpp
    ${ACC_APP_SOURCE_DIR}/src/plugin_crash_tracker.cpp
)

set_target_properties(ajazz_qml_tests PROPERTIES AUTOMOC ON)

# QTP0004 (Qt 6.8+): per-subdirectory generated qmldir for qml/components/. Mirror the gate in
# src/app/CMakeLists.txt to keep the configure step warning-free under our zero-warnings invariant.
if(Qt6_VERSION VERSION_GREATER_EQUAL 6.8)
    qt_policy(SET QTP0004 NEW)
endif()

# NOTE: the app target's RESOURCES (branding SVG/JSON, UI icons, product photos) are deliberately
# NOT bundled here. Their per-file QT_RESOURCE_ALIAS properties are set in src/app's directory scope
# and don't carry into this one, and a load-time smoke test doesn't need them: a missing icon/image
# is a runtime image-load warning (filtered out by the test's defect-warning classifier), not a QML
# compile/binding error. BrandingService falls back to its embedded defaults when
# branding/theme.json is absent. This keeps the test module's resource wiring trivial without
# weakening the load/binding/import/type checks.
qt_add_qml_module(
    ajazz_qml_tests
    URI AjazzControlCenter
    VERSION 1.0 IMPORTS ${ACC_QML_IMPORTS}
    SOURCES ${_qmlt_sources}
    QML_FILES ${_qmlt_qml_files}
              # Phase 26 Plan 26-05 (REQ-26-B): DeviceView test QML files added to the module so
              # they are loadable as AjazzControlCenter/test_device_view_geometry etc.
              test_device_view_geometry.qml
              test_device_view_drag_drop.qml
)

# Branding compile defines consumed by branding_service.cpp / property_inspector_controller.cpp.
# Mirror the app target's values so the resource paths resolve at runtime.
set(_qmlt_qrc_root "qrc:/qt/qml/AjazzControlCenter")
target_compile_definitions(
    ajazz_qml_tests
    PRIVATE AJAZZ_PRODUCT_NAME="AJAZZ Control Center"
            AJAZZ_VENDOR_NAME="Aiacos"
            AJAZZ_APP_ID="io.github.Aiacos.AjazzControlCenter"
            AJAZZ_APP_ICON_QRC="${_qmlt_qrc_root}/branding/app.svg"
            AJAZZ_TRAY_ICON_QRC="${_qmlt_qrc_root}/branding/tray.svg"
            AJAZZ_THEME_QRC=":/qt/qml/AjazzControlCenter/branding/theme.json"
            # Phase 22 Plan 22-01 (PLUGIN-14): verify gate needs the verifier/trust-root paths (same
            # defs the app target carries) so plugin_verify_gate.cpp compiles correctly in this
            # harness. Without them the gate falls back to fail-closed (safe), but the QML smoke
            # test would never confirm a real verify path; pass them for completeness.
            AJAZZ_PLUGIN_VERIFIER_SCRIPT="${CMAKE_SOURCE_DIR}/scripts/sign-plugin-manifest.py"
            AJAZZ_PLUGIN_TRUST_ROOTS="${CMAKE_SOURCE_DIR}/resources/trusted_publishers.json"
)

target_include_directories(
    ajazz_qml_tests
    PRIVATE ${ACC_APP_SOURCE_DIR}/src
            # loaded_plugins_model.hpp pulls in PluginInfo from the host header.
            ${CMAKE_SOURCE_DIR}/src/plugins/include
)

target_link_libraries(
    ajazz_qml_tests
    PRIVATE ajazz::core
            ajazz::devices
            # Phase 22 Plan 22-01 (PLUGIN-14): plugin_verify_gate.cpp (in ACC_QML_MODULE_SOURCES)
            # calls ajazz::plugins::verifyManifest, so the full plugins library must be linked here
            # — same reason the app target links it unconditionally after the un-gate.
            ajazz::plugins
            ajazz::warnings
            Catch2::Catch2WithMain
            Qt6::Core
            Qt6::CorePrivate # QZipReader in sdplugin_extractor / plugin_catalog_model
            Qt6::Gui
            Qt6::Network
            Qt6::Qml
            Qt6::Quick
            Qt6::QuickControls2
            Qt6::Svg
            # TrayController (an Application member) uses QSystemTrayIcon / QMenu.
            Qt6::Widgets
)

# Phase 19 (PLUGIN-10): PluginDeviceBridge + SdPluginServer are included via ACC_QML_MODULE_SOURCES
# and reference Qt6::WebSockets symbols. When WebSockets is available (same condition as the app
# target) we must add sd_plugin_server.cpp and link Qt6::WebSockets here to resolve those symbols;
# without this the QML smoke test fails to link even though the e2e behaviour is disabled at runtime
# in the headless harness.
if(AJAZZ_HAVE_WEBSOCKETS)
    target_sources(ajazz_qml_tests PRIVATE "${ACC_APP_SOURCE_DIR}/src/sd_plugin_server.cpp")
    # Phase 21 Plan 21-02 (PLUGIN-12): obs_client.hpp is included via ACC_QML_MODULE_SOURCES (for
    # AUTOMOC) so its Q_OBJECT symbols must be resolved here — mirror the sd_plugin_server pattern
    # above.
    target_sources(ajazz_qml_tests PRIVATE "${ACC_APP_SOURCE_DIR}/src/obs_client.cpp")
    # Phase 19 Plan 19-01 (PLUGIN-10): PluginDeviceBridge .cpp was missing from this target, causing
    # undefined-reference link errors for all PluginDeviceBridge::on* virtual methods. The .hpp is
    # already in ACC_QML_MODULE_SOURCES for AUTOMOC; the .cpp must match (pre-existing latent issue
    # fixed in Phase 26 Plan 26-05).
    target_sources(ajazz_qml_tests PRIVATE "${ACC_APP_SOURCE_DIR}/src/plugin_device_bridge.cpp")
    target_link_libraries(ajazz_qml_tests PRIVATE Qt6::WebSockets)
    target_compile_definitions(ajazz_qml_tests PRIVATE AJAZZ_HAVE_WEBSOCKETS=1)
endif()

# qmlcachegen emits generated C++ that trips GCC/Clang -Werror (null-deref on the loader,
# qsizetype->int conversions). We don't own those sources — relax warnings-as-error for the
# generated qmlcache TUs, mirroring src/app.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    set(_qmlt_cache_root ${CMAKE_CURRENT_BINARY_DIR}/.rcc/qmlcache)
    set(_qmlt_cache_target_dir ${_qmlt_cache_root}/ajazz_qml_tests_qml)
    set(_qmlt_relax_flags "")
    list(APPEND _qmlt_relax_flags -Wno-error=null-dereference -Wno-null-dereference)
    list(APPEND _qmlt_relax_flags -Wno-error=conversion -Wno-conversion)
    list(APPEND _qmlt_relax_flags -Wno-error=sign-conversion -Wno-sign-conversion)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang")
        list(APPEND _qmlt_relax_flags -Wno-error=shorten-64-to-32 -Wno-shorten-64-to-32)
    endif()
    set_source_files_properties(
        ${_qmlt_cache_root}/ajazz_qml_tests_qmlcache_loader.cpp
        PROPERTIES COMPILE_OPTIONS "${_qmlt_relax_flags}" GENERATED TRUE
    )
    set(_qmlt_cache_generated "")
    foreach(qml_file IN LISTS ACC_QML_MODULE_QML_FILES)
        get_filename_component(_stem "${qml_file}" NAME_WE)
        get_filename_component(_subdir "${qml_file}" DIRECTORY)
        if(_subdir)
            # ACC_QML_MODULE_QML_FILES entries look like qml/components/Foo.qml; the qmlcache layout
            # drops the leading qml/ and keeps the rest.
            string(REGEX REPLACE "^qml/" "" _subdir "${_subdir}")
            if(_subdir)
                set(_subdir "${_subdir}/")
            endif()
        endif()
        list(APPEND _qmlt_cache_generated "${_qmlt_cache_root}/${_stem}_qml.cpp")
        list(APPEND _qmlt_cache_generated "${_qmlt_cache_target_dir}/${_subdir}${_stem}_qml.cpp")
    endforeach()
    if(_qmlt_cache_generated)
        set_source_files_properties(
            ${_qmlt_cache_generated} PROPERTIES COMPILE_OPTIONS "${_qmlt_relax_flags}" GENERATED
                                                                                       TRUE
        )
    endif()
endif()

# LABELS "qml" lets the coverage CI job exclude these via `ctest -LE qml`. They instantiate QML
# through a real QQmlEngine, which SIGSEGVs under gcov coverage instrumentation (gcov x Qt-QML
# interaction) while passing in every normal build. They add no logic coverage, so the coverage job
# skips them and the release build matrix keeps them as the real smoke gate.
catch_discover_tests(
    ajazz_qml_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "qml"
)
