add_executable(
    ajazz_unit_tests
    test_logger.cpp
    # Debug epic Phase 1: tee / ring-buffer / file LogSink building blocks.
    test_log_sinks.cpp
    # Debug epic Phase 1: pure Qt->core log mapping + level parsing (the
    # install()/Qt-handler side effects are not exercised here).
    test_debug_logging.cpp
    ${CMAKE_SOURCE_DIR}/src/app/src/debug_logging.cpp
    # Debug epic Phase 2: JSON-RPC framing + dispatch of the control channel
    # (processRequestLine; the QLocalServer transport is runtime glue).
    test_debug_control_server.cpp
    ${CMAKE_SOURCE_DIR}/src/app/src/debug_control_server.cpp
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
    # 2026-05-18 (Pass 5) — AK980 PRO TFT chunked image-upload path
    # (opcode 0x7F sub 0x03 + 28-byte chunk packets with the 0x80 marker
    # on byte 1, ITftDisplayCapable). Pins the §3 wire format from
    # docs/protocols/keyboard/ak980pro_tft_protocol.md: header + 2 315
    # chunks per 240x135 RGB565 frame, BE pixel pack, nearest-neighbour
    # resize for sub-native sources, empty-image graceful no-op. Wire
    # format rebuilt from the FUN_004231c0 decompile (2026-05-21).
    test_ak980_tft_chunked.cpp
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
    # 2026-05-18 ITouchStripDisplayCapable: byte-level tests for the public
    # AKP05 touch-strip capability mixin over the existing "DRA" rect-
    # addressable strip-update path (akp05_vendor.md sec 3 row 190). Pins
    # touchStripInfo() (800x480, 4 zones), setTouchStripImage() envelope
    # (DRA header + BE32 size + location + BE16 width/height/x/y + chunks +
    # ULEND), out-of-range location refusal (zero writes), and
    # clearTouchStrip() full-panel black-fill envelope.
    test_akp05_touch_strip.cpp
    # 2026-05-27: executable companion to akp05_input_corrections.md. GROUP A pins
    # the RE-CONFIRMED input invariants (ACK discard, key code@9 + edge@10, VER
    # on-wire layout, live-captured "V3.AKP05E.01.007" parse). GROUP B uses Catch2
    # [!shouldfail] to characterise the encoder/touch decoding DEFECTS the Ghidra
    # RE of SDActionCanvasWidget::handleKeyEvents exposed (no rotation-magnitude
    # byte; touch X is a single byte at report[10], not BE16) — they flip to red
    # (un-tag them) once parseInputReport is reworked.
    test_akp05_input_corrections.cpp
    # DEVICES-11 / ARCH-05 (Phase 14, Plan 14-01): descriptor honesty pin for
    # akp05e (0x0300:0x3004). Asserts hasClock==false on the akp05e descriptor
    # after ajazz::streamdeck::registerAll() so the phantom Sync-button
    # affordance cannot silently regress. Also guards against an over-broad
    # flip by asserting mirabox_n4 still has hasClock==true.
    test_register_akp05e_clock.cpp
    # Phase 26 Plan 26-02 (REQ-26-D): geometry-field regression test. Iterates
    # the StreamDeck registry after registerAll() and asserts that every non-deferred
    # LCD-key descriptor has keyRows > 0. AKP815 is in the kDeferredLcdSkus allow-list
    # (D-13 sentinel; 800x480 wide strip deferred). Also asserts the aux-surface
    # invariant: any descriptor with hasTouchStrip or encoderCount > 0 must declare
    # touchZoneCount > 0 OR (mainScreenWidthPx > 0 AND mainScreenHeightPx > 0).
    test_streamdeck_register_geometry.cpp
    # Phase 14 Plan 14-02 (DISPLAY-06/07/08, DOCK-01/02): MockTransport-based
    # wire assertions for StreamDockControlService. Proves the panel lights at
    # open (LIG, DISPLAY-06), key images arrive via BAT->chunks->ULEND without
    # explicit flush (DISPLAY-07, DOCK-02), profileChanged repaints all bound
    # keys (DISPLAY-08), firmwareVersionFor returns the seeded VER string (DOCK-01),
    # and the transport is opened only once across multiple assigns (Pitfall 2).
    test_stream_dock_control_service.cpp
    ${CMAKE_SOURCE_DIR}/src/app/src/stream_dock_control_service.cpp
    ${CMAKE_SOURCE_DIR}/src/app/src/profile_controller.cpp
    # Phase 15 Plan 15-01 (INPUT-03/04/05): MockTransport-fed dispatch assertions for
    # StreamDockInputService. Proves key press/release routes to the bound onPress/onRelease
    # chain (INPUT-03), encoder CW/CCW fire distinct chains after the 16 ms coalescer
    # (INPUT-04a/c), encoder press fires onPress and synthesises a paired release without any
    # wire release frame (INPUT-04b), touch tap at X=350 routes to encoder 2 onPress via the
    # provisional zone map (INPUT-05a), swipe-left/right emit pageNavRequested(-1/+1) (INPUT-05b),
    # Sleep defers via QtExecutor (non-blocking pump thread), and two pump() calls do not re-open
    # the transport (held-handle invariant).
    test_stream_dock_input_service.cpp
    ${CMAKE_SOURCE_DIR}/src/app/src/stream_dock_input_service.cpp
    # Phase 24 Plan 24-02 (DEVICES-10): per-family MockTransport byte tests proving the SAME
    # capability-generic StreamDockControlService + StreamDockInputService drive AKP03 (6 keys +
    # 3 encoders), AKP153 (15 keys), and AKP815 (15 keys). Asserts per-family image header
    # (CmdImage/CmdBat "BAT" at bytes 5-7) + ULEND terminator, descriptor-driven max-key-index
    # burst (no AKP05 10-key clamp), AKP03 all-3-encoder press/CW/CCW routing, and
    # encoderCount=0 inert path for AKP153/AKP815 (no crash). Live confirmation: Phase 25.
    # stream_dock_control_service.cpp + stream_dock_input_service.cpp are already linked above.
    test_stream_dock_family.cpp
    # Phase 16 Plan 16-01 (DISPLAY-09): MockTransport wire assertions for the
    # StreamDockControlService DISPLAY-09 extension -- Q_INVOKABLE setBrightness
    # (-> LIG) and clearAll (-> CLE). Covers brightness clamping (150->100),
    # the CLE "clear all keys" path, and the no-op contract for non-display
    # devices. Hardware-free gating proof; live witness is Phase 25.
    test_stream_dock_controls.cpp
    # Phase 16 Plan 16-02 (PROFILE-01): commit -> save -> fresh-controller load round-trip
    # equality for keys, encoders, touch KeyState, and pages; plus repaint-on-load proof
    # (BAT+ULEND per bound key via StreamDockControlService + MockTransport).
    # profile_controller.cpp is already in the binary via the 14-02 entry above.
    test_profile_persistence.cpp
    # Phase 16 Plan 16-03 (PROFILE-02): page-scoped repaintPage(pageId) assertions --
    # root paints root bindings, child paints child bindings not root (Pitfall 1 closed),
    # missing-page no-ops cleanly; plus carousel pageNavRequested(+/-1) tests and
    # single-root no-op guard (Decision 2). stream_dock_input_service.cpp is already in
    # the binary via the Phase 15 entry above.
    test_profile_pages.cpp
    # qt_executor.cpp is already in the binary (test_qt_executor block above)
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
    # 2026-05-18 key-matrix readback: byte-level tests for the AJ-series
    # mouse IMouseKeyMatrixReadable surface (opcode 0xD0
    # FEA_CMD_MOUSE_GET_KEYMATRIX, sec 3.7). Pins the request packet shape
    # (opcode at pkt[1], profile at pkt[2], BIT7 checksum at pkt[64]) plus
    # the response parser (16 x 4-byte action records; accepts both the
    # bare 64-byte body and the 65-byte envelope with leading Report ID
    # prefix; rejects too-short / oversize responses). End-to-end round-
    # trip exercises the AjSeriesMouse write -> read -> parse orchestration
    # via MockTransport::enqueueRead, including the timeout / truncated
    # response failure modes.
    test_aj_series_keymatrix_readback.cpp
    test_profile_serialization.cpp
    test_profile_io.cpp
    test_event_bus.cpp
    test_action_engine.cpp
    # Phase 21-03 (PLUGIN-12): BuiltinActionRegistry pure-core dispatch table tests (Task 1)
    # + BuiltinActionsService per-UUID handler e2e tests (Task 2). The Task-1 portion uses only
    # std headers + Catch2 (no Qt). The Task-2 portion links builtin_actions_service.cpp (which
    # includes QJsonDocument + QSettings). builtin_action_registry.cpp is linked directly as a
    # re-compiled core TU (same pattern as test_action_engine + stream_dock_control_service).
    # stream_dock_input_service.cpp is already in the binary (Phase 15 entry above).
    test_builtin_actions.cpp
    ${CMAKE_SOURCE_DIR}/src/core/src/builtin_action_registry.cpp
    ${CMAKE_SOURCE_DIR}/src/app/src/builtin_actions_service.cpp
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
    # AppUpdateService (2026-05-18, docs/architecture/APP-AUTO-UPDATE.md):
    # pins the pure-logic semver helper (isNewerThan) + the construction-
    # time FLATPAK_ID self-disable contract + the dismissCurrentUpdate
    # state transition. The real HTTP path is intentionally NOT covered
    # here -- it needs a mock QNetworkAccessManager seam (TODO).
    test_app_update_service.cpp
    ${CMAKE_SOURCE_DIR}/src/app/src/app_update_service.cpp
    # FirmwareUpdateService (docs/architecture/FIRMWARE-UPDATES.md): pins the
    # pure per-family download-URL map, the Stream-Dock-only vendor-tool
    # candidate-path list, and the "tool not installed -> use the page"
    # contract. The QProcess launch / QDesktopServices open are thin OS
    # wrappers, intentionally not exercised here.
    test_firmware_update_service.cpp
    ${CMAKE_SOURCE_DIR}/src/app/src/firmware_update_service.cpp
    # Property Inspector URL policy + PIBridge settings persistence: pure C++
    # helpers (no QtWebEngine) linked directly so the test binary does not pull
    # in the WebEngine dependency. pi_bridge.cpp uses only Qt6::Core (QSaveFile,
    # QJsonDocument, QStandardPaths) for the persistence path; the WebEngine
    # path is guarded by AJAZZ_HAVE_WEBENGINE and is NOT compiled here. AUTOMOC
    # (set below) handles the Q_OBJECT moc pass for PIBridge. Coverage in
    # test_pi_bridge.cpp (URL policy + PLUGIN-13 restart round-trip + path-
    # traversal refusal).
    test_pi_bridge.cpp
    ${CMAKE_SOURCE_DIR}/src/app/src/pi_url_policy.cpp
    ${CMAKE_SOURCE_DIR}/src/app/src/pi_bridge.cpp
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
#
# AJAZZ_TEST_PYTHON3 pins the SAME interpreter for signing and verifying: the C++ verifier resolves
# bare names only from vetted system dirs (never $PATH, CWE-426), but the interpreter that actually
# has the `cryptography` module is wherever CMake/CI provisioned it (e.g. the hostedtoolcache python
# on the macOS/Windows runners, NOT /usr/bin). Passing its absolute path makes the functional tests
# interpreter-location-independent while the dedicated "unresolvable interpreter fails closed" case
# still exercises a bare name.
find_package(Python3 COMPONENTS Interpreter REQUIRED)
set_source_files_properties(
    test_manifest_signer.cpp
    PROPERTIES
        COMPILE_DEFINITIONS
        "AJAZZ_TEST_REPO_ROOT=\"${CMAKE_SOURCE_DIR}\";AJAZZ_TEST_PYTHON3=\"${Python3_EXECUTABLE}\""
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

# Embed the bundled sdpi.css under the same QRC path the runtime binary uses
# (`qrc:/qt/qml/AjazzControlCenter/streamdock/sdpi.css`), so the qrc-presence test in
# test_pi_bridge.cpp can assert QFile::exists() on it without needing a full app binary (mirrors the
# streamdock/opendeck fallback pattern above).
set(_acc_sdpi_css ${CMAKE_SOURCE_DIR}/resources/streamdeck/sdpi.css)
set_source_files_properties(${_acc_sdpi_css} PROPERTIES QT_RESOURCE_ALIAS streamdock/sdpi.css)
qt_add_resources(
    ajazz_unit_tests "sdpi_css_test_resource" PREFIX "/qt/qml/AjazzControlCenter" FILES
    ${_acc_sdpi_css}
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
            # CorePrivate exposes QZipReader / QZipWriter. plugin_catalog_model.cpp uses QZipReader
            # to extract `.sdPlugin` archives (issue #62); the matching test builds a synthetic
            # archive via QZipWriter and round-trips it through the extractor.
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

# Phase 18 Plan 18-01 (PLUGIN-06): PluginManifest parser + gate unit tests. Links
# plugin_manifest.cpp directly (pure Qt-Core-only; no WebEngine/WebSockets dep).
# AJAZZ_TEST_REPO_ROOT lets the test locate the JSON fixtures under tests/unit/fixtures/manifests/
# regardless of CWD when ctest runs the binary.
target_sources(
    ajazz_unit_tests PRIVATE test_plugin_manifest.cpp
                             ${CMAKE_SOURCE_DIR}/src/app/src/plugin_manifest.cpp
)
set_source_files_properties(
    test_plugin_manifest.cpp PROPERTIES COMPILE_DEFINITIONS
                                        "AJAZZ_TEST_REPO_ROOT=\"${CMAKE_SOURCE_DIR}\""
)

# Phase 18 Plan 18-02 (PLUGIN-08): NodeRunner argv builder + injectable node detection. Links
# node_runner.cpp directly (pure Qt-Core-only; no process launched at unit time). All cases inject a
# fake NodeProbe — no live node is required or started.
target_sources(
    ajazz_unit_tests PRIVATE test_node_runner.cpp ${CMAKE_SOURCE_DIR}/src/app/src/node_runner.cpp
)

# Phase 18 Plan 18-03 (PLUGIN-11): Mirabox/Elgato compat shim tests. The pure-string case
# (CompatTest::miraboxSocket_aliasedToElgato) compiles unconditionally — no WebEngine dep — and
# asserts that kMiraboxShimSource contains the forwarding alias. The injection-point case
# (CompatTest mirabox shim injects at document creation) is gated on AJAZZ_HAVE_WEBENGINE and builds
# makeMiraboxShim() to assert DocumentCreation + MainWorld + matching sourceCode.
# plugin_mirabox_shim.cpp is linked here via the conditional block below.
target_sources(ajazz_unit_tests PRIVATE test_mirabox_compat.cpp)

# When WebEngine is present, link plugin_mirabox_shim.cpp (contains makeMiraboxShim() which uses
# QWebEngineScript) and add Qt6::WebEngineCore so QWebEngineScript is fully defined. The
# kMiraboxShimSource string literal in the .cpp is always compiled (not gated); only the
# QWebEngineScript builder needs WebEngine. Mirror the pi_url_policy pattern: the pure-logic source
# compiles unconditionally, the WebEngine-using parts are gated.
#
# Phase 20 Plan 20-03 (PLUGIN-09): also link property_inspector_controller.cpp + pi_cef_shim.cpp +
# pi_url_request_interceptor.cpp when the full WebEngine Quick + WebChannel Quick stack is present,
# so the [pi-bridge][inspector] cases can construct a PropertyInspectorController and assert that
# loadInspector flips hasHtmlInspector and inserts the cefQuery shim once per fresh profile. The
# test is WebEngine-gated via #if defined(AJAZZ_HAVE_WEBENGINE) in test_pi_bridge.cpp; without
# WebEngine only the pure-string + persistence tests run (mirror of the 20-01/20-02 discipline).
if(AJAZZ_HAVE_WEBENGINE)
    # Re-issue the find_package for the WebEngine components so the Qt6::WebEngineCore /
    # WebEngineQuick / WebChannelQuick imported targets are defined in this CMakeLists scope (they
    # were found in src/app/CMakeLists.txt, but CMake imported targets are not inherited across
    # directory scopes — we must re-find them here). QUIET to suppress redundant noise.
    find_package(Qt6 QUIET COMPONENTS WebEngineCore WebEngineQuick WebChannelQuick)
    target_sources(
        ajazz_unit_tests
        PRIVATE ${CMAKE_SOURCE_DIR}/src/app/src/plugin_mirabox_shim.cpp
                # 20-03: PropertyInspectorController and its WebEngine-dependent TUs.
                ${CMAKE_SOURCE_DIR}/src/app/src/property_inspector_controller.cpp
                ${CMAKE_SOURCE_DIR}/src/app/src/pi_cef_shim.cpp
                ${CMAKE_SOURCE_DIR}/src/app/src/pi_url_request_interceptor.cpp
    )
    target_link_libraries(
        ajazz_unit_tests PRIVATE Qt6::WebEngineCore Qt6::WebEngineQuick Qt6::WebChannelQuick
    )
    # plugin_manager.cpp (already linked via AJAZZ_HAVE_WEBSOCKETS) uses a private WebEngine header
    # (qquickwebenginescriptcollection_p.h) when AJAZZ_HAVE_WEBENGINE is defined. Mirror the same
    # fallback that src/app/CMakeLists.txt uses to find that private include.
    find_package(Qt6 QUIET COMPONENTS WebEngineQuickPrivate)
    if(Qt6WebEngineQuickPrivate_FOUND)
        target_link_libraries(ajazz_unit_tests PRIVATE Qt6::WebEngineQuickPrivate)
    else()
        set(_we_priv_test "${Qt6WebEngineQuick_INCLUDE_DIRS}/QtWebEngineQuick"
                          "/${Qt6WebEngineQuick_VERSION}/QtWebEngineQuick/private"
        )
        string(CONCAT _we_priv_test_path ${_we_priv_test})
        target_include_directories(ajazz_unit_tests PRIVATE "${_we_priv_test_path}")
    endif()
    target_compile_definitions(ajazz_unit_tests PRIVATE AJAZZ_HAVE_WEBENGINE=1)
else()
    # Without WebEngine, we still need plugin_mirabox_shim.cpp to compile the kMiraboxShimSource
    # constant (the pure-string test needs it). The WebEngine-guarded makeMiraboxShim() body is
    # excluded by #if defined(AJAZZ_HAVE_WEBENGINE), so no QWebEngineScript include is dragged in.
    target_sources(ajazz_unit_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/app/src/plugin_mirabox_shim.cpp)
endif()

# Phase 18 Plan 18-04 (PLUGIN-07): PluginCrashTracker — pure 3-in-30s window with injected clock.
# Pure Qt-Core-only; no QProcess, no real clock. All cases inject synthetic timestamps.
target_sources(
    ajazz_unit_tests PRIVATE test_plugin_crash_tracker.cpp
                             ${CMAKE_SOURCE_DIR}/src/app/src/plugin_crash_tracker.cpp
)

# Phase 22 Plan 22-01 (PLUGIN-14): plugin_verify_gate unit suite. Tests the three-way verdict
# (signed->SelfSigned, tampered->Refused, unsigned->Refused, signer-unavailable->Refused) and the
# verdictToTrustLevel vocabulary contract. plugin_verify_gate.cpp calls verifyManifest from the
# manifest_signer TU already compiled into this binary (WIN32 / else blocks above).
# AJAZZ_TEST_REPO_ROOT lets verifyStagedPlugin locate scripts/sign-plugin-manifest.py when the test
# builds a ManifestSignerConfig from AJAZZ_PLUGIN_VERIFIER_SCRIPT (which for the test binary points
# at the source-tree script, same as test_manifest_signer.cpp).
target_sources(
    ajazz_unit_tests PRIVATE test_plugin_verify_gate.cpp
                             ${CMAKE_SOURCE_DIR}/src/app/src/plugin_verify_gate.cpp
)

# Phase 22 Plan 22-02 (PLUGIN-14): installFromFile staging->verify->promote unit suite. Tests:
# signed+confirm->promoted, tampered->Refused (staging-before-promote invariant), self-signed
# without confirm->not promoted, non-zip/oversized->rejected, file:// URL accepted.
# plugin_catalog_model.cpp is already compiled into this binary (test_plugin_catalog_proxy_model
# entry above). plugin_verify_gate.cpp is already compiled (22-01 entry above). AJAZZ_TEST_REPO_ROOT
# lets the test locate scripts/sign-plugin-manifest.py for keygen/sign.
target_sources(ajazz_unit_tests PRIVATE test_plugin_install_from_file.cpp)
set_source_files_properties(
    test_plugin_install_from_file.cpp PROPERTIES COMPILE_DEFINITIONS
                                                 "AJAZZ_TEST_REPO_ROOT=\"${CMAKE_SOURCE_DIR}\""
)

# Phase 22 Plan 22-02 (PLUGIN-14): no-phone-home / offline-default assertion suite. Tests:
# construction with online=OFF makes no live HTTP request; opt-in enabled exercises live branch;
# installFromFile triggers no online fetch; onlineCatalogEnabled defaults false. Uses
# ACC_STREAMDOCK_CATALOG_URL=disabled env seam + QStandardPaths::setTestModeEnabled.
# plugin_catalog_model.cpp already compiled (test_plugin_catalog_proxy_model entry above).
target_sources(ajazz_unit_tests PRIVATE test_catalog_offline.cpp)
set(_pvg_defs
    "AJAZZ_TEST_REPO_ROOT=\"${CMAKE_SOURCE_DIR}\""
    "AJAZZ_PLUGIN_VERIFIER_SCRIPT=\"${CMAKE_SOURCE_DIR}/scripts/sign-plugin-manifest.py\""
    "AJAZZ_PLUGIN_TRUST_ROOTS=\"${CMAKE_SOURCE_DIR}/resources/trusted_publishers.json\""
)
set_source_files_properties(
    test_plugin_verify_gate.cpp PROPERTIES COMPILE_DEFINITIONS "${_pvg_defs}"
)
set(_pvg_gate_defs
    "AJAZZ_PLUGIN_VERIFIER_SCRIPT=\"${CMAKE_SOURCE_DIR}/scripts/sign-plugin-manifest.py\""
    "AJAZZ_PLUGIN_TRUST_ROOTS=\"${CMAKE_SOURCE_DIR}/resources/trusted_publishers.json\""
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/src/app/src/plugin_verify_gate.cpp PROPERTIES COMPILE_DEFINITIONS
                                                                      "${_pvg_gate_defs}"
)

# Phase 18 Plan 18-04 (PLUGIN-07/08): PluginManager discovery + spawn + lifecycle tests. Needs
# Qt6::CorePrivate (QZipWriter for fixture archives), Qt6::WebSockets for SdPluginServer, and
# Qt6::Test for QSignalSpy. All process spawn is mocked — no real node or process is started.
if(AJAZZ_HAVE_WEBSOCKETS)
    target_sources(
        ajazz_unit_tests PRIVATE test_plugin_lifecycle.cpp
                                 ${CMAKE_SOURCE_DIR}/src/app/src/plugin_manager.cpp
    )
    target_compile_definitions(ajazz_unit_tests PRIVATE AJAZZ_HAVE_PLUGIN_MANAGER=1)
endif()

# Phase 21 Plan 21-01 (PLUGIN-12): IInputSynthesizer fake-backend assertions. Hardware-free
# (AJAZZ_FEATURE_INPUT_SYNTH OFF = default) — no /dev/uinput, no OS permissions needed.
# test_input_synth.cpp defines a FakeSynth recording subclass and exercises
# makeDefaultInputSynthesizer() from ajazz::core (already linked above). No direct stub re-link
# needed — makeDefaultInputSynthesizer() is already provided by ajazz::core (mirror of
# test_action_engine.cpp pattern: include header + link core).
target_sources(ajazz_unit_tests PRIVATE test_input_synth.cpp)

# Phase 19 Plans 19-01/19-02 (PLUGIN-10): PluginDeviceBridge pure-helper, ContextRegistry, and e2e
# loopback tests. Gated on AJAZZ_HAVE_WEBSOCKETS — the bridge header composes SdPluginServer (Phase
# 17) and drives StreamDockControlService (Phase 14) via assignKeyImage. Links
# plugin_device_bridge.cpp + stream_dock_control_service.cpp directly (both already in the
# AJAZZ_HAVE_WEBSOCKETS block since stream_dock_control_service.cpp is unconditional above, but the
# e2e test needs AJAZZ_HAVE_WEBSOCKETS for QWebSocket + SdPluginServer). profile_controller.cpp is
# already linked above (Phase 14 entry). COMPILE_DEFINITION AJAZZ_HAVE_WEBSOCKETS=1 is already set
# by the sd_plugin_server block.
if(AJAZZ_HAVE_WEBSOCKETS)
    target_sources(
        ajazz_unit_tests PRIVATE test_plugin_device_bridge.cpp
                                 ${CMAKE_SOURCE_DIR}/src/app/src/plugin_device_bridge.cpp
    )
    target_compile_definitions(ajazz_unit_tests PRIVATE AJAZZ_HAVE_WEBSOCKETS=1)
endif()

# Phase 21 Plan 21-02 (PLUGIN-12): ObsClient obs-websocket v5 auth handshake tests. Hardware-free:
# loopback QWebSocketServer mock OBS server (bound to 127.0.0.1). Mirrors the test_sd_plugin_server
# block: links obs_client.cpp + Qt6::WebSockets directly (both already available when
# AJAZZ_HAVE_WEBSOCKETS). Gated identically so the suite builds on minimal Qt installs without the
# WebSockets module.
if(AJAZZ_HAVE_WEBSOCKETS)
    target_sources(
        ajazz_unit_tests PRIVATE test_obs_client.cpp ${CMAKE_SOURCE_DIR}/src/app/src/obs_client.cpp
    )
    # AJAZZ_HAVE_WEBSOCKETS compile definition + Qt6::WebSockets already set by the sd_plugin_server
    # block above — no duplication needed here.
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
