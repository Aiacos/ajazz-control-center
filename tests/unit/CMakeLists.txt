add_executable(
    ajazz_unit_tests
    test_logger.cpp
    test_device_registry.cpp
    test_akp153_protocol.cpp
    test_akp03_protocol.cpp
    test_akp05_protocol.cpp
    # ARCH-04 (Phase 09): host-side RGBA→JPEG/PNG pipeline. Locks JPEG/PNG magic bytes,
    # rotation/mirror transforms (AKP153 Rot90+mirror, AKP815 Rot180), and validation
    # contracts (zero-area, dim mismatch).
    test_image_pipeline.cpp
    test_proprietary_keyboard_protocol.cpp
    # Audit follow-up (2026-05-18): end-to-end byte-level coverage of the
    # AK980 PRO firmware-RTC sync envelope (ARCH-05.1). Pins the orchestration
    # in ProprietaryKeyboard::setTime() that the per-packet builder tests
    # alone could not catch (writeFeature vs write, packet order, tm_wday
    # propagation, byte-8 marker, 0xAA/0x55 tail).
    test_ak980_clock_sync_e2e.cpp
    # 2026-05-18 audit-3: byte-level test for the 20-mode firmware
    # RGB picker (IFirmwareLightingCapable, opcode 0x13 envelope).
    # Pins the 4-packet START/MODE_BEGIN/DATA/SAVE sequence and the
    # 20-mode catalogue the QML LightingService surfaces.
    test_ak980_firmware_lighting.cpp
    # Issue #57 / P3.x — AK980 PRO settings batch envelope
    # (opcode 0x07 sub 0x10, ISettingsCapable). Pins the 4-packet
    # START/SETTINGS/SAVE/FINISH sequence + byte map + host cache.
    test_ak980_settings_batch.cpp
    # CAPTURE-04 (Phase 09 / Plan 09-04): MockTransport smoke test.
    # Wires the new header-only `tests/unit/fixtures/mock_transport.hpp`
    # into AjSeriesMouse via the public `makeAjSeriesWithTransport` factory
    # and asserts the exact 64-byte feature-report envelope produced by
    # setActiveDpiStage(0). Locks the wire-format mock seam in place
    # before Phase 10/11/12 byte-level tests depend on it.
    test_aj_series_mock_transport.cpp
    # P3.12 — vendor-correct wire-format primitives for AJ-series mouse.
    # Byte-precise regression guards (BIT7 checksum, _RateToNum lookup,
    # action at bytes 8..11 not 4..7, 8th-DPI-stage B/checksum collision,
    # no-standalone-battery-opcode invariant).
    test_aj_series_wire_format.cpp
    # 2026-05-18 OLED basetta: end-to-end byte-level test for the
    # AJ-series mouse TFT LCD clock+DPI upload (opcode 0x25 chunked
    # RGB565). Wires MockTransport through makeAjSeriesWithTransport,
    # invokes IClockCapable::setTime(), and asserts every chunk's
    # envelope. Pins the OLED basetta code path against regression.
    test_aj_series_tft_clock.cpp
    # 2026-05-18 settings surface: byte-level tests for the two new
    # AJ-series mouse capabilities — IPollingRateCapable (opcode 0x04,
    # _RateToNum lookup, 0x80-MSB high-rate encoding) and
    # IProfileSelectCapable (opcode 0x05, 8 onboard slots persistent
    # across power-cycle) per aj_series_opcode_table.md sec 3.3 + 3.4.
    test_aj_series_settings.cpp
    # 2026-05-18 omnibus settings: byte-level tests for the AJ-series
    # mouse IMouseSettingsCapable surface (opcode 0x53
    # FEA_CMD_MOUSE_SET_OPTIONPARAM0). Pins every documented byte slot
    # in aj_series_opcode_table.md sec 3.9 (LOD at 52, sensitivity at
    # 50/51, sleep timers at 40..47, battery LED RGB at 54..59, charging
    # switch at 60, 5 named flag bits across bytes 12..13) plus clamping
    # + cache normalisation contract.
    test_aj_series_omnibus_settings.cpp
    # 2026-05-18 capability-mixin pair: AJ-series IFactoryResettable
    # (opcode 0x02 / FEA_CMD_SET_RESERT per aj_series_opcode_table.md sec
    # 3.2 - no-payload BIT7-checksummed packet) and AKP05 IBootLogoCapable
    # (CRT LOG opcode per akp05_vendor.md sec 2 row 188 - BE16 size header
    # plus 512-byte JPEG chunks plus ULEND commit sentinel). Both pin the
    # exact wire-byte layout end-to-end via MockTransport.
    test_factory_reset_and_logo.cpp
    # 2026-05-18 DPI table + Fn-layer: byte-level tests for the two new
    # AJ-series mouse capability surfaces - IDpiTableCapable (opcode 0x54,
    # FEA_CMD_MOUSE_SET_OPTIONPARAM1, per-profile 8-stage DPI table) and
    # IMouseFnRemappable (opcode 0x51, FEA_CMD_MOUSE_SET_FNMATRIX, Fn-layer
    # key rebind). Pins every documented byte slot in aj_series_opcode_table.md
    # sec 3.8 (Fn-matrix shape: layer/button at vendor bytes 1/2, action big-
    # endian at 8..11) and sec 3.10 (DPI table: profile/active/count at vendor
    # bytes 1/2/3, 8 x uint16-LE DPI at 8..23, 8 x {R,G,B} colours at 40..63,
    # 8th-stage B-channel collision with checksum slot). Includes clamp +
    # cache normalisation contract.
    test_aj_series_dpi_fn.cpp
    # 2026-05-18 macro upload: byte-level tests for the AJ-series mouse
    # IMouseMacroCapable surface (opcode 0x16 FEA_CMD_SET_MACRO_SIMPLE).
    # Pins the sec 3.11 chunked-upload envelope (slot/chunkIdx/lastNonZeroPos/
    # isFinal header bytes, 56-byte payload window) plus the event-stream
    # encoder (KeyDown bit7-set, KeyUp bit7-clear, 1-byte delay <=127,
    # uint16-LE delay > 127). Empty-events and out-of-range slot clamp
    # behaviours are covered too so destructive caller bugs cannot poke
    # an unrelated firmware slot.
    test_aj_series_mouse_macros.cpp
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
    # PluginCatalogProxyModel: pins the filter contract for the Plugin
    # Store GridView (HIGH-1/HIGH-2 fix from quick task 260513-u0b). The
    # proxy references PluginCatalogModel::Roles only — no networking,
    # no Qt event loop spin — so the source file links cleanly into
    # this binary alongside the existing fetcher tests.
    test_plugin_catalog_proxy_model.cpp
    ${CMAKE_SOURCE_DIR}/src/app/src/plugin_catalog_proxy_model.cpp
    ${CMAKE_SOURCE_DIR}/src/app/src/plugin_catalog_model.cpp
    # Issue #62: .sdPlugin archive extractor with round-trip via QZipWriter.
    test_sdplugin_extractor.cpp
    ${CMAKE_SOURCE_DIR}/src/app/src/sdplugin_extractor.cpp
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
    # TimeSyncService (Phase 5 / Plan 05-04): the QML TimeSyncService
    # singleton's contract — Ok / NotImplemented / IoError / null-cast
    # signal emission, autoSync persistence, onDeviceArrived gating.
    # The cpp source is linked directly so the test binary doesn't need
    # to pull the full app target (Q_OBJECT runs through AUTOMOC here).
    test_time_sync_service.cpp
    ${CMAKE_SOURCE_DIR}/src/app/src/time_sync_service.cpp
    # BatteryService (2026-05-18 P3.d / QML wire-up): pins lastKnownPercent
    # cache semantics (positive reading stored, unavailable clears, unknown
    # codenames return -1). The wider signal-emission surface is covered
    # implicitly through the live-device path on AK980 PRO; this test
    # isolates the small helper added to seed the QML BatteryIndicator's
    # initial state.
    test_battery_service.cpp
    ${CMAKE_SOURCE_DIR}/src/app/src/battery_service.cpp
    # SettingsService (2026-05-18 issue #57 / QML wire-up): pins the
    # clampResponseLevel helper that QML uses to preview the persisted
    # response-time level without a HID round-trip. The wider service
    # surface (DeviceLookup resolution, signal emission) is covered
    # through the live AK980 PRO path + the existing
    # test_ak980_settings_batch.cpp byte-level coverage; this test
    # isolates the small helper added in the QML wire-up commit.
    test_settings_service.cpp
    ${CMAKE_SOURCE_DIR}/src/app/src/settings_service.cpp
    # Property Inspector URL policy: pure C++ helpers (no QtWebEngine), so we
    # link the source directly into this test binary without dragging the
    # WebEngine dependency in. Coverage in test_pi_bridge.cpp.
    test_pi_bridge.cpp
    ${CMAKE_SOURCE_DIR}/src/app/src/pi_url_policy.cpp
    # Phase 7 / Plan 07-02 / TRUST-03: loadTrustRoots corpus — positive cases
    # (BOM, escape sequences, top-level array, boundary entry counts 1/2/100/1024),
    # negative cases (>1 MB byte cap, 1025-entry overcount, truncated JSON, malformed
    # UTF-8, wrong shape), and pinned NUL-byte handling. Exercises the nlohmann impl
    # from Plan 07-01; the common.cpp source is already in the per-platform
    # target_sources block below so the symbol resolves at link time.
    test_load_trust_roots.cpp
    # Phase 4 multi-device hot-plug integration harness (Plan 04-05, HOTPLUG-06).
    # Drives the ARCH-02 mock seams (HotplugMonitor::injectEvent + MockHidEnumerator)
    # to mechanically prove SC1 (no UAF), SC2 (per-row dataChanged), SC4 (300ms
    # coalescing). The HotplugDebouncer + DeviceModel TUs are linked directly
    # rather than via the app target so the harness doesn't drag in the QML
    # singleton machinery — both classes are Q_OBJECTTs so AUTOMOC handles them.
    # hotplug_monitor.cpp is recompiled here with AJAZZ_TESTING (target-wide define)
    # so the injectEvent shim symbol resolves at link time — libajazz_core.a was
    # built WITHOUT AJAZZ_TESTING so that production ABI stays clean (Plan 04-02
    # invariant: production library has zero injectEvent symbols).
    test_hotplug_harness.cpp
    ${CMAKE_SOURCE_DIR}/src/app/src/hotplug_debouncer.cpp
    ${CMAKE_SOURCE_DIR}/src/app/src/device_model.cpp
    ${CMAKE_SOURCE_DIR}/src/core/src/hotplug_monitor.cpp
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
    # SEC-003 follow-up #51: Ed25519 manifest verifier — Win32 backend uses `_wspawnvp`. The test
    # exercises the same canonical-form contract on Windows so any drift between the two backends
    # surfaces immediately. CR-01 (Phase 06, Plan 06-02): black-box tests for Win32EnvBlock — five
    # TEST_CASEs covering the four Pitfall 5 sub-traps (\0\0 terminator, sort order, =-prefix
    # preservation, snapshot-free) + D-04 case-insensitive override semantics. The implementation TU
    # is linked in directly so the tests don't pull the full ajazz_plugins lib. Phase 7 (D-01):
    # `manifest_signer_common.cpp` carries the shared nlohmann-based `loadTrustRoots` impl and is
    # compiled into the test binary so the symbol resolves at link time without pulling the full
    # ajazz_plugins lib. Phase 04 (Plan 04-06): Win32-only WM_DEVICECHANGE → HotplugEvent parser
    # smoke test. Drives the AJAZZ_TESTING-gated `parseDevicePathW` helper + `injectEvent`
    # round-trip with canonical AJAZZ Stream Dock device-path test vectors. Mitigates the
    # Cross-Cutting pitfall (Linux-CI-blind Windows breakage) for the hot-plug subsystem.
    target_sources(
        ajazz_unit_tests
        PRIVATE test_windows_app_container_sandbox.cpp
                test_manifest_signer.cpp
                test_win32_env_block.cpp
                test_hotplug_win32_smoke.cpp
                ${CMAKE_SOURCE_DIR}/src/plugins/src/out_of_process_plugin_host_win32.cpp
                ${CMAKE_SOURCE_DIR}/src/plugins/src/windows_app_container_sandbox.cpp
                ${CMAKE_SOURCE_DIR}/src/plugins/src/manifest_signer_win32.cpp
                ${CMAKE_SOURCE_DIR}/src/plugins/src/manifest_signer_common.cpp
                ${CMAKE_SOURCE_DIR}/src/plugins/src/win32_env_block.cpp
                ${CMAKE_SOURCE_DIR}/src/plugins/src/sandbox.cpp
    )
else()
    # SEC-003 follow-up #51: Ed25519 manifest verifier — POSIX backend uses `fork`+`execvp`. Phase 7
    # (D-01): `manifest_signer_common.cpp` carries the shared nlohmann-based `loadTrustRoots` impl
    # and is compiled into the test binary so the symbol resolves at link time.
    target_sources(
        ajazz_unit_tests
        PRIVATE test_linux_bwrap_sandbox.cpp
                test_macos_sandbox_exec_sandbox.cpp
                test_manifest_signer.cpp
                ${CMAKE_SOURCE_DIR}/src/plugins/src/out_of_process_plugin_host.cpp
                ${CMAKE_SOURCE_DIR}/src/plugins/src/linux_bwrap_sandbox.cpp
                ${CMAKE_SOURCE_DIR}/src/plugins/src/macos_sandbox_exec_sandbox.cpp
                ${CMAKE_SOURCE_DIR}/src/plugins/src/manifest_signer.cpp
                ${CMAKE_SOURCE_DIR}/src/plugins/src/manifest_signer_common.cpp
                ${CMAKE_SOURCE_DIR}/src/plugins/src/sandbox.cpp
    )
endif()

# AJAZZ_TEST_REPO_ROOT lets the verifier test locate scripts/sign-plugin-manifest.py +
# resources/trusted_publishers.json regardless of CWD when ctest runs the binary. Applies on every
# platform — the test now compiles on POSIX + Windows.
set_source_files_properties(
    test_manifest_signer.cpp PROPERTIES COMPILE_DEFINITIONS
                                        "AJAZZ_TEST_REPO_ROOT=\"${CMAKE_SOURCE_DIR}\""
)

# AJAZZ_TESTING flips on the ARCH-02 mock seams: HotplugMonitor::injectEvent (Plan 04-02) and any
# other test-only hooks behind the same guard. Production ajazz_core builds never define this, so
# the symbols disappear from the library ABI entirely.
target_compile_definitions(ajazz_unit_tests PRIVATE AJAZZ_TESTING)

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
            # Phase 7 / ARCH-01: nlohmann_json is consumed by the shared
            # `manifest_signer_common.cpp` that this binary compiles directly. Same PRIVATE link
            # discipline as `ajazz_plugins` itself — nlohmann never appears in any public header
            # used by the rest of the test binary (COD-031 invariant).
            nlohmann_json::nlohmann_json
            Qt6::Core
            # CorePrivate exposes QZipReader / QZipWriter. plugin_catalog_model.cpp uses
            # QZipReader to extract `.sdPlugin` archives (issue #62); the matching test
            # builds a synthetic archive via QZipWriter and round-trips it through the
            # extractor.
            Qt6::CorePrivate
            Qt6::Gui # QColor for BrandingService (no rendering required)
            Qt6::Network
            Qt6::Qml # QML_NAMED_ELEMENT in plugin_catalog_model.hpp pulls in QtQmlIntegration
            Qt6::Test # QSignalSpy
)

# P3.16 — SdPluginServer tests gated on Qt6::WebSockets availability (same fallback discipline as
# the app target).
if(AJAZZ_HAVE_WEBSOCKETS)
    target_sources(
        ajazz_unit_tests PRIVATE test_sd_plugin_server.cpp
                                 ${CMAKE_SOURCE_DIR}/src/app/src/sd_plugin_server.cpp
    )
    target_link_libraries(ajazz_unit_tests PRIVATE Qt6::WebSockets)
    target_compile_definitions(ajazz_unit_tests PRIVATE AJAZZ_HAVE_WEBSOCKETS=1)
endif()

# Protocol-header tests need access to the backends' private headers. The streamdock fetcher TU we
# pull from the app target also needs its sibling `plugin_catalog_model.hpp` and the fetcher header
# on the include path — hence the additional `src/app/src` entry.
target_include_directories(
    ajazz_unit_tests
    PRIVATE ${CMAKE_SOURCE_DIR}/src/devices/streamdeck/src
            ${CMAKE_SOURCE_DIR}/src/devices/keyboard/src
            ${CMAKE_SOURCE_DIR}/src/devices/mouse/src
            ${CMAKE_SOURCE_DIR}/src/app/src
            # CR-01 (Phase 06, Plan 06-02): test_win32_env_block.cpp needs the host-private
            # `win32_env_block.hpp` (it lives under src/plugins/src/, not include/).
            ${CMAKE_SOURCE_DIR}/src/plugins/src
)

catch_discover_tests(ajazz_unit_tests)
