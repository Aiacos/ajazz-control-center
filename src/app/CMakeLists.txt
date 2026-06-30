# zlib (ZLIB::ZLIB) for the self-contained plugin-archive extractor (sdplugin_extractor.cpp) is
# resolved centrally in the root CMakeLists.txt: find_package under AJAZZ_USE_SYSTEM_DEPS (Flatpak /
# distro), vendored via FetchContent otherwise. A find_package(ZLIB) here would fail on the Windows
# CI runner, which has no findable system zlib.

# ----------------------------------------------------------------------------
# Optional: Qt WebEngine for the Property Inspector HTML panel
# ----------------------------------------------------------------------------
# When ON and Qt6::WebEngineQuick is found, the Property Inspector pane embeds plugin-authored HTML
# PI pages (Stream Deck SDK-compatible). When OFF or the Qt module is missing — common on minimal Qt
# installs and headless CI — the build falls back silently to the schema-driven native inspector.
# The stub controller compiles either way so QML bindings to `propertyInspectorController` never
# break.
option(AJAZZ_BUILD_PROPERTY_INSPECTOR "Embed plugin Property Inspector HTML pages via Qt WebEngine"
       ON
)
if(AJAZZ_BUILD_PROPERTY_INSPECTOR)
    # WebChannelQuick is the QML-friendly JS↔C++ bridge for `$SD` (M3) — it provides
    # `QQmlWebChannel`, the only type `WebEngineView.webChannel` accepts. WebEngineQuick is the
    # rendering surface (M2). Both must be present for the HTML PI path to activate. Either missing
    # → graceful fallback to the native renderer.
    find_package(Qt6 QUIET COMPONENTS WebEngineQuick WebChannelQuick)
    if(Qt6WebEngineQuick_FOUND AND Qt6WebChannelQuick_FOUND)
        set(AJAZZ_HAVE_WEBENGINE TRUE)
        message(
            STATUS "Qt6::WebEngineQuick + Qt6::WebChannelQuick found — Property Inspector enabled"
        )
    else()
        set(AJAZZ_HAVE_WEBENGINE FALSE)
        message(
            WARNING
                "AJAZZ_BUILD_PROPERTY_INSPECTOR=ON but Qt WebEngineQuick or "
                "WebChannelQuick was not found. Falling back to the native inspector placeholder."
        )
    endif()
else()
    set(AJAZZ_HAVE_WEBENGINE FALSE)
endif()

# ----------------------------------------------------------------------------
# Optional: embed the OpenDeck Svelte SPA (the `webui` UI mode)
# ----------------------------------------------------------------------------
# OFF by default: the native QML UI (ui.mode = qml) is the default and needs no Node.js. When ON
# (and Qt WebEngine is present), scripts/build-webui.sh runs `vite build` on the OpenDeck submodule
# (src/app/webui/opendeck) + injects the Tauri-compat shim, and the static SPA is bundled into the
# :/opendeck Qt resource. WebUiHost.qml then loads it. See docs/opendeck-ui/.
option(AJAZZ_BUILD_WEBUI
       "Embed the OpenDeck web UI (webui mode, the default); needs Node.js, else falls back to qml."
       ON
)

qt_add_executable(
    ajazz-control-center
    src/main.cpp
    src/application.cpp
    # OpenDeck UI integration: command/event bridge mapping our backend singletons to the OpenDeck
    # IPC contract (webui seam). See docs/opendeck-ui/. opendeck_shaping.cpp is the pure
    # (unit-testable) our-types -> OpenDeck-JSON layer; opendeck_bridge.cpp is the QObject that
    # drives it from the singletons.
    src/opendeck_shaping.cpp
    src/opendeck_bridge.cpp
    # Debug epic Phase 1: unifies core AJAZZ_LOG_* and Qt qDebug/qCDebug into one tee (stderr + file
    # + ring) and installs the qInstallMessageHandler bridge. Backs the out-of-process debug log
    # stream.
    src/debug_logging.cpp
    # Debug epic Phase 2: opt-in JSON-RPC control channel over a Unix domain socket (off unless
    # AJAZZ_DEBUG_CONTROL is set) + the facade that maps log/state/device/profile/plugin/action
    # methods onto it. Lets the out-of-process scripts/ajazz-debug client drive a running instance.
    src/debug_control_server.cpp
    src/debug_control_facade.cpp
    # Debug epic Phase 3: QML introspection + driving + screenshot methods
    # (qml.tree/get/set/invoke/click + screenshot) so the channel can see and drive the UI. Needs
    # the QQmlApplicationEngine + Qt6::Quick (QQuickWindow).
    src/debug_control_qml.cpp
    src/device_model.cpp
    src/plugin_catalog_model.cpp
    # Shared on-disk plugin settings store (per-context + global), used by the plugin WebSocket
    # settings path and the Property Inspector bridge.
    src/plugin_settings_store.cpp
    # Extracts downloaded `.sdPlugin` archives in place so the plugin host scans an expanded
    # directory tree (issue #62). Used by the install path and a first-launch sweep.
    src/sdplugin_extractor.cpp
    # Phase 18 Plan 18-01 (PLUGIN-06): standalone manifest parser for the Elgato v6 schema + AJAZZ
    # extensions. Pure Qt-Core-only; no nlohmann (COD-031). Used by discovery (18-04) and any future
    # catalog enrichment. manifestRunnableHere() gates OS + Software.MinimumVersion.
    src/plugin_manifest.cpp
    # Phase 18 Plan 18-02 (PLUGIN-08): Node.js spawn argv builder + injectable node >= 20 resolver.
    # Pure Qt-Core-only; no process launched at unit time (injectable NodeProbe for tests). The
    # Wave-3 PluginManager calls QProcess::start(nodeExe, buildNodeArgv(...)); absence/old-version
    # surfaces as a disabled plugin. No bundled node (anti-feature, CONTEXT.md).
    src/node_runner.cpp
    # Phase 18 Plan 18-04 (PLUGIN-07): PluginCrashTracker — pure 3-in-30s crash window. Clock is
    # injected via an explicit nowMs parameter so tests feed synthetic timestamps. No QProcess, no
    # real clock inside. COD-031: Qt-Core only.
    src/plugin_crash_tracker.cpp
    # Phase 18 Plan 18-04 (PLUGIN-07/08): PluginManager — discovery + extraction + spawn dispatch +
    # crash lifecycle + exitApp shutdown. Composes manifest parser, extractor, node_runner, crash
    # tracker, mirabox shim, and SdPluginServer::sendEvent (Phase 17 seam). Gated on
    # AJAZZ_HAVE_WEBSOCKETS (SdPluginServer dep); the crash tracker compiles regardless.
    src/plugin_manager.cpp
    src/loaded_plugins_model.cpp
    # Developer debug console: logs plugin/device protocol traffic + injects simulated device input
    # and plugin->host actions (PluginDebug QML singleton).
    src/plugin_debug_service.cpp
    src/time_sync_service.cpp
    # 2026-05-18: AK980 PRO 20-mode firmware lighting picker (opcode 0x13). Mirrors TimeSyncService
    # DI shape; dynamic_cast to IFirmwareLightingCapable so any future keyboard that ships a fixed
    # effect catalogue gets a QML picker for free.
    src/lighting_service.cpp
    # 2026-05-18 issue #57: AK980 PRO settings batch QML bridge (opcode 0x07 sub 0x10 — fn-layer /
    # sleep timer / response time). Same DeviceLookup pattern as LightingService; dynamic_cast to
    # ISettingsCapable inside the service so non-capable devices return the vendor-default tuple
    # instead of crashing.
    src/settings_service.cpp
    src/plugin_catalog_proxy_model.cpp
    src/streamdock_catalog_fetcher.cpp
    src/opendeck_catalog_fetcher.cpp
    src/mirabox_github_catalog_fetcher.cpp
    src/mirabox_github_installer.cpp
    src/profile_controller.cpp
    src/branding_service.cpp
    src/tray_controller.cpp
    src/theme_service.cpp
    src/autostart_service.cpp
    src/single_instance_guard.cpp
    src/app_icon.cpp
    src/property_inspector_controller.cpp
    src/pi_bridge.cpp
    src/pi_url_policy.cpp
    src/qt_executor.cpp
    src/hotplug_debouncer.cpp
    # P3.16: Phase C QML UX — battery query service singleton (mirrors TimeSyncService design).
    # Wires the IBatteryCapable mix-in from ARCH-05.1 / commit eea50a5 to a 15-s polling QTimer +
    # QML-bindable signals (batteryQueried / batteryUnavailable).
    src/battery_service.cpp
    # 2026-05-18 / docs/architecture/APP-AUTO-UPDATE.md: in-app GitHub-Releases-driven update
    # checker (notify-only). Mirrors LightingService / SettingsService DI shape; self-disables under
    # Flatpak and surfaces a Material banner from Main.qml when a newer tag is observed.
    src/app_update_service.cpp
    # Vendor firmware-update deep-link (FirmwareUpdate QML singleton). Read-only / delegate-only per
    # docs/architecture/FIRMWARE-UPDATES.md: detects + launches the vendor firmware tool, or opens
    # the per-family download page. No in-app flashing.
    src/firmware_update_service.cpp
    # Phase 14 Plan 14-02: app-layer Stream Dock panel control (DISPLAY-06/07/08, DOCK-01/02). Holds
    # the active device open for the session (single HID handle), issues brightness LIG at open,
    # pushes key images via a coalesced QTimer drain (BAT->chunks->ULEND, no manual flush), repaints
    # all keys on profileChanged, and surfaces the cached firmware VER string. The reuse surface
    # that Phases 15 (input), 16 (controls), and 19 (plugin bridge) build on.
    src/stream_dock_control_service.cpp
    # Phase 15 Plan 15-01: app-layer input dispatch service (INPUT-03/04/05). First instantiation of
    # core ActionEngine in the app — QTimer pump -> DeviceEvent -> Profile binding lookup ->
    # ActionEngine::run(). Includes 16 ms rotation coalescer, encoder press->synthetic-release
    # pairing, provisional touch-strip zone map, and pageNavRequested signal for Phase 16.
    src/stream_dock_input_service.cpp
    # Phase 21-03 (PLUGIN-12): BuiltinActionsService — populates the BuiltinActionRegistry with the
    # ~24 com.hotspot.streamdock.* built-in UUID handlers and provides the plugin-executor
    # short-circuit (handles -> dispatch; non-builtin UUIDs forward to Phase-19 path). Per-UUID
    # handlers parse settingsJson app-tier (QJsonDocument, COD-031). LunBo per-key cursor
    # (T-21-lunbo). Opt-in capture gate (T-21-hook OFF by default). OBS auth default-on
    # (T-21-obsauth, inherited from ObsClient). Not QML-exposed.
    src/builtin_actions_service.cpp
    # experiment/mirajazz Slice 3a: pure encode/decode for the streamdock-host Rust sidecar wire
    # protocol (newline-delimited JSON over stdio). Qt-Core only, no QProcess/QObject (no nlohmann
    # -> COD-031). The QProcess-backed SidecarStreamDockDevice (Slice 3b) composes this.
    src/sidecar_protocol.cpp
    # experiment/mirajazz Slice 3b: core::IDevice + IDisplayCapable + IEncoderCapable backed by the
    # out-of-process streamdock-host (mirajazz) sidecar via QProcess. Holds one persistent handle
    # (the hardware-confirmed fix for the open/close wedge). Not a QObject (functor connects), so no
    # AUTOMOC entry needed.
    src/sidecar_stream_dock_device.cpp
    # Phase 30 Plan 30-03 (HOST-01): UnifiedPluginHost aggregator — the single IPluginHost2
    # implementation that routes dispatch by UUID across all four runtimes (.sdPlugin WS path via
    # PluginManager + Python OOP path via OutOfProcessPluginHost). Qt->STL conversion lives at the
    # aggregator boundary. COD-031: app-layer, no nlohmann in public surface.
    src/unified_plugin_host.cpp
    # Phase 34 (APROF-01): per-OS IActiveWindowWatcher backends. Added to the app target source list
    # UNCONDITIONALLY (mirroring the input_synthesizer split in src/core/CMakeLists.txt) so the
    # build list is STABLE across platforms; each TU is self-empty unless its platform #if (and
    # AJAZZ_FEATURE_ACTIVE_WINDOW) is satisfied. They live app-tier (not in ajazz_core) because they
    # pull Qt6:: WaylandClient / libX11 / AppKit, which must not cross the COD-031 core boundary.
    # The real backend bodies LAND IN PLAN 03 — these are currently self-#if-guarded placeholder TUs
    # that keep the build green now and give the source list a stable shape. The .mm Obj-C++ TU is
    # added in the Darwin branch below (CMake needs OBJCXX enabled for it).
    src/active_window_watcher_wayland.cpp
    src/active_window_watcher_x11.cpp
    src/active_window_watcher_win.cpp
    # Phase 34 Plan 03 (APROF-01): runtime backend-selection factory. Picks the Wayland vs X11 vs
    # Win vs macOS backend by session type (XDG_SESSION_TYPE / QGuiApplication::platformName()) at
    # RUNTIME, falling back to the core recording stub when no native backend binds. App-tier (it
    # references the per-OS make* symbols + Qt platformName); the core interface stays Qt-free.
    src/active_window_watcher_factory.cpp
)

# P3.16: Elgato Stream Deck v6-compatible WebSocket plugin server (loopback-only). Conditional on
# Qt6::WebSockets availability — when the module is missing the app still builds; plugin host
# functionality is disabled at runtime.
if(AJAZZ_HAVE_WEBSOCKETS)
    target_sources(ajazz-control-center PRIVATE src/sd_plugin_server.cpp)
    # Phase 34 Plan 34-04 (APROF-04 / EVENT-03): host-level outbound plugin-event fan-out helpers
    # (applicationDidLaunch/Terminate, systemDidWakeUp). Registered- only delivery (V4) +
    # length-bounded payload (V5); factored as free functions so the delivery contract is
    # unit-testable against a live SdPluginServer.
    target_sources(ajazz-control-center PRIVATE src/app_event_dispatch.cpp)
    # Phase 19 Plan 19-01 (PLUGIN-10): PluginDeviceBridge — bridge QObject shell + ContextRegistry +
    # pure helpers (decodeDataUriImage, coordsForKeyIndex, keyIndexForCoords, ownerForActionUuid).
    # Gated on WebSockets because it will compose SdPluginServer (Phase 17) in 19-02/19-03. Pure
    # in-tree C++/Qt6; no new external packages (Package Legitimacy Gate N/A per 19-RESEARCH).
    target_sources(ajazz-control-center PRIVATE src/plugin_device_bridge.cpp)
    # Built-in dial layout renderer ($X1/$A0/$A1/$B1/$B2/$C1) — pure QImage composition consumed by
    # the bridge's setFeedback/setFeedbackLayout path.
    target_sources(ajazz-control-center PRIVATE src/encoder_layout_renderer.cpp)
    # Phase 21 Plan 21-02 (PLUGIN-12): obs-websocket v5 client for the obsstudio built-in action.
    # Auth default-on (LOCKED anti-feature T-21-obsauth): refuses Identify when OBS demands auth but
    # no password is configured. App-tier QJsonDocument (COD-031: no nlohmann). 21-03 wires the
    # obsstudio UUID to ObsClient; not QML-exposed here.
    target_sources(ajazz-control-center PRIVATE src/obs_client.cpp)
    target_link_libraries(ajazz-control-center PRIVATE Qt6::WebSockets)
    target_compile_definitions(ajazz-control-center PRIVATE AJAZZ_HAVE_WEBSOCKETS=1)
endif()

# ----------------------------------------------------------------------------
# Phase 34 (APROF-01): per-app-profile foreground-window detection.
#
# The Wayland backend binds zwlr_foreign_toplevel_management_unstable_v1 via Qt6::WaylandClient +
# qtwaylandscanner-generated bases. The X11 backend reads _NET_ACTIVE_WINDOW / _NET_WM_PID /
# WM_CLASS via libX11. macOS uses AppKit (NSWorkspace, an Obj-C++ .mm TU); Windows uses
# GetForegroundWindow (user32). The watcher interface + stub are pure-core (COD-031); only these
# per-OS backends pull Qt/X11/AppKit, so they are app-tier.
#
# Qt6::WaylandClient (and qtwaylandscanner) only exists on Linux — the qtwayland module is not built
# on macOS/Windows, so the find_package + codegen MUST be Linux-gated or Windows/macOS CI breaks at
# configure. The wayland backend TU is already #if __linux__-gated, and the watcher factory picks
# wayland-vs-x11 at runtime on Linux only. PRIVATE_CODE keeps the generated QtWayland::zwlr_* bases
# out of installed headers. Qt6::WaylandClient is provided by the qtwayland module (CI adds it; the
# org.kde Flatpak runtime ships it).
# ----------------------------------------------------------------------------
# Phase 34 Plan 03 (APROF-01): activate the per-OS watcher backends. The feature gate is defined on
# the APP target only (the backends pull Qt6::WaylandClient / libX11 / AppKit / user32, which must
# not cross the COD-031 ajazz_core boundary). ajazz_core's stub TU is compiled WITHOUT this macro,
# so makeDefaultActiveWindowWatcher() in core stays self-contained (returns the recording stub) and
# core unit tests link standalone; the app composes the real backend via
# app::makeActiveWindowWatcher() which selects wayland-vs-x11 at RUNTIME (XDG_SESSION_TYPE /
# QGuiApplication::platformName()).
target_compile_definitions(ajazz-control-center PRIVATE AJAZZ_FEATURE_ACTIVE_WINDOW=1)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(Qt6 REQUIRED COMPONENTS WaylandClient)
    qt_generate_wayland_protocol_client_sources(
        ajazz-control-center PRIVATE_CODE FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/wayland/wlr-foreign-toplevel-management-unstable-v1.xml
    )
    target_link_libraries(ajazz-control-center PRIVATE Qt6::WaylandClient)

    # qtwaylandscanner-generated glue trips several project-wide -Werror warnings (old-style-cast to
    # wl_proxy*/void*, sign-conversion, etc. — cmake/Warnings.cmake). This is GENERATED code we do
    # not own and cannot fix at source, so disable warnings for just that TU with -w (the trailing
    # flag wins over the earlier -Wall/-Werror from ajazz::warnings). The generated path is
    # deterministic for PRIVATE_CODE: <build>/qwayland-<basename>.cpp. The rest of the app stays
    # under strict -Werror.
    set_source_files_properties(
        ${CMAKE_CURRENT_BINARY_DIR}/qwayland-wlr-foreign-toplevel-management-unstable-v1.cpp
        PROPERTIES COMPILE_OPTIONS "-w"
    )

    # libX11 (Xlib EWMH property reads) for the X11 backend on the same Linux build.
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(X11 REQUIRED IMPORTED_TARGET x11)
    target_link_libraries(ajazz-control-center PRIVATE PkgConfig::X11)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    enable_language(OBJCXX)
    target_sources(ajazz-control-center PRIVATE src/active_window_watcher_mac.mm)
    target_link_libraries(ajazz-control-center PRIVATE "-framework AppKit")
elseif(WIN32)
    # GetForegroundWindow / GetWindowThreadProcessId / QueryFullProcessImageNameW live in user32;
    # MSVC links it by default, explicit for Clang-cl / MinGW.
    target_link_libraries(ajazz-control-center PRIVATE user32)
endif()

# Icon lives outside this directory, so give Qt a stable relative resource alias to satisfy
# qt_add_qml_module's "no absolute paths" rule.
set(ACC_APP_ICON ${CMAKE_SOURCE_DIR}/resources/icons/app.svg)
set_source_files_properties(${ACC_APP_ICON} PROPERTIES QT_RESOURCE_ALIAS icons/app.svg)

# Per-platform desktop / file-explorer icons. The Qt SVG resource above renders the runtime UI icon;
# these files are picked up by the OS shell to label .app bundles, .exe windows and the Linux
# desktop entry.
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(ACC_BUNDLE_ICON ${CMAKE_SOURCE_DIR}/resources/icons/app.icns)
    if(EXISTS "${ACC_BUNDLE_ICON}")
        set_source_files_properties(${ACC_BUNDLE_ICON} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
        target_sources(ajazz-control-center PRIVATE ${ACC_BUNDLE_ICON})
        set_target_properties(
            ajazz-control-center
            PROPERTIES MACOSX_BUNDLE TRUE
                       MACOSX_BUNDLE_ICON_FILE app.icns
                       MACOSX_BUNDLE_BUNDLE_NAME "AJAZZ Control Center"
                       MACOSX_BUNDLE_GUI_IDENTIFIER io.github.Aiacos.AjazzControlCenter
                       MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
                       MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
        )
    endif()
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(ACC_WIN_ICON ${CMAKE_SOURCE_DIR}/resources/icons/app.ico)
    if(EXISTS "${ACC_WIN_ICON}")
        # Generate a tiny .rc file so the produced .exe carries the icon (visible in Explorer,
        # taskbar, and the WiX-installed Start Menu shortcut).
        set(ACC_WIN_RC ${CMAKE_CURRENT_BINARY_DIR}/ajazz-control-center.rc)
        file(WRITE ${ACC_WIN_RC} "// Auto-generated. Do not edit.\n"
                                 "IDI_ICON1 ICON DISCARDABLE \"${ACC_WIN_ICON}\"\n"
        )
        target_sources(ajazz-control-center PRIVATE ${ACC_WIN_RC})
        set_target_properties(ajazz-control-center PROPERTIES WIN32_EXECUTABLE TRUE)
    endif()
endif()

# ----------------------------------------------------------------------------
# Branding asset selection: pick AJAZZ_BRAND_DIR/{app.svg,tray.svg,theme.json} when the cache var is
# set, otherwise fall back to resources/branding which ships the upstream identity. Each file is
# bound to a stable QRC alias so BrandingService can load it via qrc:/branding/<name>.
# ----------------------------------------------------------------------------
set(_ajazz_brand_default ${CMAKE_SOURCE_DIR}/resources/branding)
if(AJAZZ_BRAND_DIR AND EXISTS "${AJAZZ_BRAND_DIR}")
    set(_ajazz_brand_dir "${AJAZZ_BRAND_DIR}")
else()
    set(_ajazz_brand_dir "${_ajazz_brand_default}")
endif()
set(ACC_BRAND_APP ${_ajazz_brand_dir}/app.svg)
set(ACC_BRAND_TRAY ${_ajazz_brand_dir}/tray.svg)
set(ACC_BRAND_THEME ${_ajazz_brand_dir}/theme.json)
set_source_files_properties(${ACC_BRAND_APP} PROPERTIES QT_RESOURCE_ALIAS branding/app.svg)
set_source_files_properties(${ACC_BRAND_TRAY} PROPERTIES QT_RESOURCE_ALIAS branding/tray.svg)
set_source_files_properties(${ACC_BRAND_THEME} PROPERTIES QT_RESOURCE_ALIAS branding/theme.json)

# Optional banner / wordmark logo for the application header. The default repo-vendored AJAZZ
# wordmark is at resources/branding/ajazz-logo.png; integrators can override via AJAZZ_BRAND_DIR by
# dropping a same-named file there.
set(ACC_BRAND_LOGO ${_ajazz_brand_dir}/ajazz-logo.png)
if(NOT EXISTS "${ACC_BRAND_LOGO}")
    set(ACC_BRAND_LOGO ${_ajazz_brand_default}/ajazz-logo.png)
endif()
set_source_files_properties(${ACC_BRAND_LOGO} PROPERTIES QT_RESOURCE_ALIAS branding/ajazz-logo.png)

# Theme.qml is a `pragma Singleton` — it must be marked with the QT_QML_SINGLETON_TYPE source file
# property *before* qt_add_qml_module so Qt's QML compiler registers it as a singleton in the
# AjazzControlCenter module instead of as a regular component.
set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

# DragRelay.qml is likewise a `pragma Singleton` (Phase 29 — internal-drag relay shared by every
# editor drag source); mark it so qmlcachegen registers it as a module-level singleton rather than a
# regular component.
set_source_files_properties(qml/DragRelay.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

# Optional light theme override — packaged so BrandingService can load it from
# qrc:/qt/qml/AjazzControlCenter/branding/theme-light.json when the user picks Settings → Appearance
# → Light.
set(ACC_BRAND_THEME_LIGHT ${CMAKE_SOURCE_DIR}/resources/branding/theme-light.json)
set_source_files_properties(
    ${ACC_BRAND_THEME_LIGHT} PROPERTIES QT_RESOURCE_ALIAS branding/theme-light.json
)

# Embed CHANGELOG.md and PRIVACY.md so the in-app viewer (#37) can render them without hitting the
# network. Aliases live at the QML module root so BrandingService::changelogText() / privacyText()
# find them via QFile.
set(ACC_DOC_CHANGELOG ${CMAKE_SOURCE_DIR}/CHANGELOG.md)
set(ACC_DOC_PRIVACY ${CMAKE_SOURCE_DIR}/docs/PRIVACY.md)
set_source_files_properties(${ACC_DOC_CHANGELOG} PROPERTIES QT_RESOURCE_ALIAS CHANGELOG.md)
set_source_files_properties(${ACC_DOC_PRIVACY} PROPERTIES QT_RESOURCE_ALIAS PRIVACY.md)

# Bundled Elgato sdpi.css for Property Inspector pages (PLUGIN-09 / Phase 20 Plan 20-02). PI HTML
# pages reference `static/css/sdpi.css` or `sdpi.css` (Elgato convention); the
# PIUrlRequestInterceptor redirects those requests to this bundled qrc resource so each plugin need
# not ship its own copy. Redirect target is a fixed qrc: constant (T-20-CSS-REDIR). qrc path:
# qrc:/qt/qml/AjazzControlCenter/streamdock/sdpi.css
set(ACC_SDPI_CSS ${CMAKE_SOURCE_DIR}/resources/streamdeck/sdpi.css)
set_source_files_properties(${ACC_SDPI_CSS} PROPERTIES QT_RESOURCE_ALIAS streamdock/sdpi.css)

# Bundled offline fixture for the AJAZZ Streamdock catalogue tab. Loaded by
# StreamdockCatalogFetcher::loadBundledFallback() when neither the upstream catalogue nor the
# on-disk mirror is available (first launch on an air-gapped host, CI smoke runs, etc.). Stable QRC
# alias keeps the C++ side decoupled from CMake source paths.
set(ACC_STREAMDOCK_FALLBACK ${CMAKE_SOURCE_DIR}/resources/streamdock-fallback.json)
set_source_files_properties(
    ${ACC_STREAMDOCK_FALLBACK} PROPERTIES QT_RESOURCE_ALIAS streamdock-fallback.json
)

# Bundled offline fixture for the OpenDeck catalogue tab. Same pattern as the Streamdock one above
# but for OpenDeckCatalogFetcher::loadBundledFallback() — keeps the OpenDeck tab non-empty when
# `https://plugins.amankhanna.me/catalogue.json` is unreachable and no cache exists yet.
set(ACC_OPENDECK_FALLBACK ${CMAKE_SOURCE_DIR}/resources/opendeck-fallback.json)
set_source_files_properties(
    ${ACC_OPENDECK_FALLBACK} PROPERTIES QT_RESOURCE_ALIAS opendeck-fallback.json
)

# Clean-room original UI icon set (see resources/icons/README.md). Theme-tintable SVGs: each line
# glyph uses stroke="currentColor", so a single file renders white on the dark theme and near-black
# on the light theme — no per-theme duplicates. Three groups: glyphs/ (feature icons), actions/
# (key-assignment actions), devices/ (per-family illustrations). Aliased under icons/ so QML loads
# them via qrc:/qt/qml/AjazzControlCenter/icons/<group>/<name>.svg. CONFIGURE_DEPENDS re-globs when
# an SVG is added/removed so contributors just drop a file in. (The per-family DeviceRow icon and
# the DeviceImage SVG fallback both load from icons/devices/ here.)
set(ACC_UI_ICONS "")
file(GLOB _acc_ui_glyphs CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/resources/icons/glyphs/*.svg)
file(GLOB _acc_ui_actions CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/resources/icons/actions/*.svg)
file(GLOB _acc_ui_devices CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/resources/devices/*.svg)
foreach(icon_file IN LISTS _acc_ui_glyphs)
    get_filename_component(icon_name "${icon_file}" NAME)
    set_source_files_properties(
        "${icon_file}" PROPERTIES QT_RESOURCE_ALIAS icons/glyphs/${icon_name}
    )
endforeach()
foreach(icon_file IN LISTS _acc_ui_actions)
    get_filename_component(icon_name "${icon_file}" NAME)
    set_source_files_properties(
        "${icon_file}" PROPERTIES QT_RESOURCE_ALIAS icons/actions/${icon_name}
    )
endforeach()
foreach(icon_file IN LISTS _acc_ui_devices)
    get_filename_component(icon_name "${icon_file}" NAME)
    set_source_files_properties(
        "${icon_file}" PROPERTIES QT_RESOURCE_ALIAS icons/devices/${icon_name}
    )
endforeach()
list(APPEND ACC_UI_ICONS ${_acc_ui_glyphs} ${_acc_ui_actions} ${_acc_ui_devices})

# Bundled icon font. The QML references font.family "Material Symbols Outlined" in several places
# (action icons, encoder/touch-zone glyphs, trash button); that font is NOT guaranteed to be
# installed on the host (it is absent on stock Fedora/Wayland), so without bundling it the icon
# ligatures render as their literal text ("tune", "delete", ...). We bundle a static, glyph-subset
# build of Google's Material Symbols Outlined (Apache-2.0), loaded at startup via a FontLoader in
# Main.qml. QRC alias: fonts/<name> -> qrc:/qt/qml/AjazzControlCenter/fonts/<name>.
set(ACC_UI_FONTS "")
file(GLOB ACC_UI_FONTS CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/resources/fonts/*.ttf)
foreach(font_path IN LISTS ACC_UI_FONTS)
    get_filename_component(font_name "${font_path}" NAME)
    set_source_files_properties("${font_path}" PROPERTIES QT_RESOURCE_ALIAS fonts/${font_name})
endforeach()

# Per-model product photos (resources/devices/products/*.{png,jpg}). Downsized (~256px) vendor
# product shots, fetched once and bundled so the device editor header shows a real picture offline —
# no runtime CDN dependency. Aliased under icons/devices/products/ →
# qrc:/qt/qml/AjazzControlCenter/icons/devices/products/<name>. DeviceImage.qml resolves a codename
# to one of these; missing models fall back to the per-family SVG (loaded from icons/devices/
# above).
set(ACC_DEVICE_PRODUCTS "")
file(GLOB ACC_DEVICE_PRODUCTS CONFIGURE_DEPENDS
     ${CMAKE_SOURCE_DIR}/resources/devices/products/*.png
     ${CMAKE_SOURCE_DIR}/resources/devices/products/*.jpg
)
foreach(product_path IN LISTS ACC_DEVICE_PRODUCTS)
    get_filename_component(product_name "${product_path}" NAME)
    set_source_files_properties(
        "${product_path}" PROPERTIES QT_RESOURCE_ALIAS icons/devices/products/${product_name}
    )
endforeach()

# Per-SKU device layout JSONs (resources/device-layouts/<codename>.json). These small JSON files
# carry cell-position coordinates for the photo-background overlay in DeviceView.qml (D-06, Phase
# 26). The JSONs are authored in Plan 26-06; this glob tolerates an empty directory so the module
# compiles before any layout JSON exists. CONFIGURE_DEPENDS re-globs when a JSON is added/removed so
# cmake reconfigures automatically. QRC alias: device-layouts/<name>.json ->
# qrc:/qt/qml/AjazzControlCenter/device-layouts/<name>.json
set(ACC_DEVICE_LAYOUTS "")
if(EXISTS "${CMAKE_SOURCE_DIR}/resources/device-layouts")
    file(GLOB ACC_DEVICE_LAYOUTS CONFIGURE_DEPENDS
         ${CMAKE_SOURCE_DIR}/resources/device-layouts/*.json
    )
    foreach(layout_path IN LISTS ACC_DEVICE_LAYOUTS)
        get_filename_component(layout_name "${layout_path}" NAME)
        set_source_files_properties(
            "${layout_path}" PROPERTIES QT_RESOURCE_ALIAS device-layouts/${layout_name}
        )
    endforeach()
endif()

# Cross-module QML imports. WebUiHost.qml / OpenDeckPane.qml import QtWebEngine + QtWebChannel (the
# embedded OpenDeck SPA host + bridge); qmllint only resolves those types when the modules are
# listed as `import`s in our generated qmldir. The list is empty on minimal builds (so the WebEngine
# modules don't need to exist on the target system) and populated on the full WebEngine build.
set(_acc_qml_imports "")
if(AJAZZ_HAVE_WEBENGINE)
    list(APPEND _acc_qml_imports QtWebEngine QtWebChannel)
endif()

# Qt 6.8 introduced policy QTP0004: modules with QML files in a subdirectory (we have
# qml/components/) need a separate generated qmldir in each subdirectory, so the components' own
# implicit imports resolve to the module root instead of to `components/`. Without the policy set
# explicitly, Qt 6.8+ issues a `CMake Warning (dev) at ... QTP0004 is not set` at configure time,
# which violates our zero-warnings invariant. The policy only exists from Qt 6.8 onward; on 6.7 (our
# baseline + the current CI matrix) there is no warning and no policy command to call, so the
# `qt_policy` call is gated on a version check. `qt_standard_project_setup(REQUIRES 6.8)` would also
# work, but we keep the REQUIRES floor at 6.7 until CI moves.
if(Qt6_VERSION VERSION_GREATER_EQUAL 6.8)
    qt_policy(SET QTP0004 NEW)
endif()

# The QML module's C++ singleton SOURCES, the QML_FILES, and the RESOURCES are captured into
# variables so the QML smoke-test target (tests/qml) can build a parallel copy of the exact same
# module with zero list divergence. The lists are relative to this directory; the test target
# consumes them with a ${CMAKE_CURRENT_SOURCE_DIR} of src/app via the exported source dir below.
set(ACC_QML_MODULE_SOURCES
    src/branding_service.cpp
    src/branding_service.hpp
    src/theme_service.cpp
    src/theme_service.hpp
    src/autostart_service.cpp
    src/autostart_service.hpp
    src/tray_controller.cpp
    src/tray_controller.hpp
    src/device_model.cpp
    src/device_model.hpp
    src/profile_controller.cpp
    src/profile_controller.hpp
    src/plugin_manifest.cpp
    src/plugin_manifest.hpp
    src/node_runner.cpp
    src/node_runner.hpp
    # Phase 18 Plan 18-03 (PLUGIN-11): Mirabox/Elgato compat shim. The header is always included
    # (kMiraboxShimSource is inline constexpr, non-WebEngine builds can unit-test it); the .cpp is
    # in the WebEngine-gated block below (makeMiraboxShim uses QWebEngineScript).
    src/plugin_mirabox_shim.hpp
    # Phase 20 Plan 20-02 (PLUGIN-09): cefQuery polyfill header. kCefQueryShimSource is always
    # reachable (inline constexpr std::string_view, no WebEngine dep); the .cpp is WebEngine-gated.
    src/pi_cef_shim.hpp
    src/plugin_catalog_model.cpp
    src/plugin_catalog_model.hpp
    src/loaded_plugins_model.cpp
    src/loaded_plugins_model.hpp
    src/time_sync_service.cpp
    src/time_sync_service.hpp
    src/lighting_service.cpp
    src/lighting_service.hpp
    src/settings_service.cpp
    src/settings_service.hpp
    src/battery_service.cpp
    src/battery_service.hpp
    src/app_update_service.cpp
    src/app_update_service.hpp
    src/property_inspector_controller.cpp
    src/property_inspector_controller.hpp
    src/qt_executor.cpp
    src/qt_executor.hpp
    src/stream_dock_control_service.cpp
    src/stream_dock_control_service.hpp
    src/stream_dock_input_service.cpp
    src/stream_dock_input_service.hpp
    # Phase 19 Plan 19-01 (PLUGIN-10): PluginDeviceBridge shell + ContextRegistry + pure helpers.
    # Listed here so AUTOMOC runs on the Q_OBJECT header (moc generation). The .cpp is in the
    # AJAZZ_HAVE_WEBSOCKETS-gated target_sources block above; the header is listed here so moc finds
    # the Q_OBJECT declaration. Mirror of the branding_service / stream_dock_control_service
    # pattern. Only the .hpp is listed here (for AUTOMOC); the .cpp is compiled SOLELY in the
    # AJAZZ_HAVE_WEBSOCKETS-gated target_sources block above. The header class is itself #if
    # defined(AJAZZ_HAVE_WEBSOCKETS)-gated, so AUTOMOC is a no-op on minimal (no-WebSockets) builds
    # and ajazz-control-center.exe links without the module (was: LNK1120 on SdPluginServer).
    src/plugin_device_bridge.hpp
    # Phase 21 Plan 21-02 (PLUGIN-12): ObsClient Q_OBJECT header listed here so AUTOMOC finds the
    # moc declaration. The .cpp is in the AJAZZ_HAVE_WEBSOCKETS-gated block above. The whole class
    # is gated by #if defined(AJAZZ_HAVE_WEBSOCKETS) so moc is safe on minimal builds.
    src/obs_client.hpp
    # Phase 21-03 (PLUGIN-12): BuiltinActionsService Q_OBJECT header + source. Not QML-exposed;
    # listed here so AUTOMOC runs the moc pass for the Q_OBJECT declaration. The .cpp is in the
    # unconditional target_sources block above (no WebSockets gate — the ObsClient code inside is
    # already gated by #ifdef AJAZZ_HAVE_WEBSOCKETS in the .cpp).
    src/builtin_actions_service.cpp
    src/builtin_actions_service.hpp
    # Phase 22 Plan 22-01 (PLUGIN-14): Ed25519 verify gate — fail-closed trust choke point for all
    # install paths. Listed unconditionally (not inside the AJAZZ_BUILD_PYTHON_HOST guard) because
    # the gate must be present regardless of whether the full Python host is built: a reduced build
    # returns Refused rather than silently trusting an unverified package (D-05 fail-closed
    # contract).
    src/plugin_verify_gate.cpp
    src/plugin_verify_gate.hpp
)
set(ACC_QML_MODULE_QML_FILES
    qml/Main.qml
    qml/Theme.qml
    qml/DragRelay.qml
    qml/DeviceList.qml
    qml/ProfileEditor.qml
    qml/RgbPicker.qml
    qml/SettingsRow.qml
    qml/ProfileBar.qml
    qml/MousePanel.qml
    qml/PluginStore.qml
    qml/DebugConsole.qml
    qml/LoadedPluginsPage.qml
    qml/SettingsPage.qml
    qml/components/AppHeader.qml
    qml/components/BatteryIndicator.qml
    qml/components/Card.qml
    qml/components/DeviceImage.qml
    qml/components/DeviceRow.qml
    qml/components/EmptyState.qml
    qml/components/FirmwarePanel.qml
    qml/components/Notification.qml
    qml/components/PageHeader.qml
    qml/components/PrimaryButton.qml
    qml/components/Ripple.qml
    qml/components/SecondaryButton.qml
    qml/components/Toast.qml
    qml/components/UpdateBanner.qml
)
set(ACC_QML_MODULE_RESOURCES
    ${ACC_APP_ICON}
    ${ACC_BRAND_APP}
    ${ACC_BRAND_TRAY}
    ${ACC_BRAND_THEME}
    ${ACC_BRAND_THEME_LIGHT}
    ${ACC_BRAND_LOGO}
    ${ACC_DOC_CHANGELOG}
    ${ACC_DOC_PRIVACY}
    ${ACC_STREAMDOCK_FALLBACK}
    ${ACC_OPENDECK_FALLBACK}
    ${ACC_UI_ICONS}
    ${ACC_UI_FONTS}
    ${ACC_DEVICE_PRODUCTS}
    ${ACC_DEVICE_LAYOUTS}
    ${ACC_SDPI_CSS}
)

# Export the module lists + this source dir so tests/qml can rebuild an identical AjazzControlCenter
# module in a standalone test executable without duplicating (and drifting from) the file lists
# above.
set(ACC_QML_MODULE_SOURCES "${ACC_QML_MODULE_SOURCES}" PARENT_SCOPE)
set(ACC_QML_MODULE_QML_FILES "${ACC_QML_MODULE_QML_FILES}" PARENT_SCOPE)
set(ACC_QML_MODULE_RESOURCES "${ACC_QML_MODULE_RESOURCES}" PARENT_SCOPE)
set(ACC_APP_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}" PARENT_SCOPE)
set(ACC_QML_IMPORTS "${_acc_qml_imports}" PARENT_SCOPE)

qt_add_qml_module(
    ajazz-control-center
    URI AjazzControlCenter
    VERSION 1.0
            IMPORTS
            ${_acc_qml_imports}
            # C++ types declared with QML_NAMED_ELEMENT + QML_SINGLETON are picked up via the
            # SOURCES list. Migrating from setContextProperty to QML singletons so qmllint can
            # statically resolve them — see TODO.md → Quality bar.
    SOURCES ${ACC_QML_MODULE_SOURCES}
    QML_FILES ${ACC_QML_MODULE_QML_FILES}
    RESOURCES ${ACC_QML_MODULE_RESOURCES}
)

target_include_directories(ajazz-control-center PRIVATE src)

# `loaded_plugins_model.hpp` includes `ajazz/plugins/i_plugin_host.hpp` for the `PluginInfo`
# definition (model rows reference it directly). The include is always needed — even when
# `AJAZZ_BUILD_PYTHON_HOST=OFF` skips linking the library — because the header is header-only
# declarations and the model still has to compile. Adding the include path unconditionally keeps
# CodeQL (which configures with the plugin host disabled) and any future reduced-build target green.
target_include_directories(ajazz-control-center PRIVATE ${CMAKE_SOURCE_DIR}/src/plugins/include)

target_link_libraries(
    ajazz-control-center
    PRIVATE ajazz::core
            ajazz::devices
            ajazz::warnings
            ajazz::sanitizers
            Qt6::Core
            # CorePrivate exposes QZipWriter, still used by the unit tests to build fixture
            # archives. (The runtime extractor no longer uses QZipReader — see
            # sdplugin_extractor.cpp: it parses the central directory + zlib raw-inflate directly to
            # handle data-descriptor archives QZipReader mis-reads.) Stable since Qt 4.7.
            Qt6::CorePrivate
            # zlib: raw DEFLATE inflate for the self-contained `.sdPlugin` / `.streamDeckPlugin`
            # extractor (sdplugin_extractor.cpp).
            ZLIB::ZLIB
            Qt6::Gui
            Qt6::Network
            Qt6::Widgets
            Qt6::Quick
            Qt6::QuickControls2
            Qt6::Svg
)

if(AJAZZ_HAVE_WEBENGINE)
    # WebEngineCore provides QWebEnginePage/QWebEngineProfile used by the HTML plugin run-path
    # (headless page-load + connectElgatoStreamDeckSocket).
    find_package(Qt6 QUIET COMPONENTS WebEngineCore)
    target_link_libraries(
        ajazz-control-center PRIVATE Qt6::WebEngineQuick Qt6::WebChannelQuick Qt6::WebEngineCore
    )
    # Qt6::WebEngineQuickPrivate exposes the private include path required by plugin_manager.cpp to
    # call profile->userScripts()->insert(...). QQuickWebEngineScriptCollection is only
    # forward-declared in the public qquickwebengineprofile.h; its complete definition lives in the
    # private header qquickwebenginescriptcollection_p.h. The symbols already live in
    # Qt6::WebEngineQuick (already linked above) — WebEngineQuickPrivate only adds the include path.
    find_package(Qt6 QUIET COMPONENTS WebEngineQuickPrivate)
    if(Qt6WebEngineQuickPrivate_FOUND)
        target_link_libraries(ajazz-control-center PRIVATE Qt6::WebEngineQuickPrivate)
    else()
        # Fallback: splice the private include dir directly for older/slimmer Qt installs.
        set(_we_priv "${Qt6WebEngineQuick_INCLUDE_DIRS}/QtWebEngineQuick"
                     "/${Qt6WebEngineQuick_VERSION}/QtWebEngineQuick/private"
        )
        string(CONCAT _we_priv_path ${_we_priv})
        target_include_directories(ajazz-control-center PRIVATE "${_we_priv_path}")
    endif()
    target_compile_definitions(ajazz-control-center PRIVATE AJAZZ_HAVE_WEBENGINE=1)
    # OpenDeckPane.qml is the embeddable OpenDeck SPA host (WebEngineView + QWebChannel) that
    # ProfileEditor mounts as the streamdeck editor area. It imports QtWebEngine/QtWebChannel which
    # only exist when the WebEngineQuick module is present, so we attach it via
    # qt_target_qml_sources only on the WebEngine path (adding it to the QML module list
    # unconditionally would fail QML compilation on minimal Qt installs).
    qt_target_qml_sources(ajazz-control-center QML_FILES qml/OpenDeckPane.qml)
    # The URL request interceptor pulls QWebEngineCore types (and AUTOMOC has to find a Q_OBJECT
    # inside it), so it only compiles when WebEngine is present.
    target_sources(ajazz-control-center PRIVATE src/pi_url_request_interceptor.cpp)
    # OpenDeck webui: custom URL-scheme handler that serves the bundled SPA over opendeck://app/
    # (QWebEngineUrlSchemeHandler -> :/opendeck). WebEngine-gated.
    target_sources(ajazz-control-center PRIVATE src/opendeck_scheme_handler.cpp)
    # Phase 18 Plan 18-03 (PLUGIN-11): Mirabox/Elgato compat shim. kMiraboxShimSource is declared
    # inline constexpr in the header (always reachable); makeMiraboxShim() body uses
    # QWebEngineScript, so the .cpp is gated here. The Wave-4 PluginManager (18-04) calls
    # profile->userScripts()->insert(makeMiraboxShim()) before loading HTML plugin index.html.
    target_sources(ajazz-control-center PRIVATE src/plugin_mirabox_shim.cpp)
    # Phase 20 Plan 20-02 (PLUGIN-09): cefQuery polyfill. kCefQueryShimSource is declared inline
    # constexpr in the header (always reachable without WebEngine; unit-testable as a pure string
    # per 18-03 discipline); makeCefQueryShim() body uses QWebEngineScript, so the .cpp is gated
    # here. PropertyInspectorController::loadInspector() calls
    # profile->scripts()->insert(makeCefQueryShim()) before loading the PI HTML.
    target_sources(ajazz-control-center PRIVATE src/pi_cef_shim.cpp)
endif()

# ----------------------------------------------------------------------------
# OpenDeck webui SPA bundle (-DAJAZZ_BUILD_WEBUI=ON)
# ----------------------------------------------------------------------------
# Builds the vendored OpenDeck SPA at configure time (scripts/build-webui.sh) and bundles the static
# output under the :/opendeck Qt resource prefix, then defines AJAZZ_HAVE_WEBUI so main.cpp knows
# the bundle is present. ON by default (webui is the default UI mode), but degrades GRACEFULLY: a
# missing Qt WebEngine or a missing Node/npm toolchain is a WARNING + skip — not a hard error — and
# the app then falls back to the native qml UI at runtime (main.cpp). A genuine build failure (npm
# present but build-webui.sh errors) stays FATAL so it surfaces.
if(AJAZZ_BUILD_WEBUI)
    if(NOT AJAZZ_HAVE_WEBENGINE)
        message(
            WARNING "AJAZZ_BUILD_WEBUI=ON but Qt WebEngine is not available "
                    "(needs AJAZZ_BUILD_PROPERTY_INSPECTOR=ON + WebEngineQuick/WebChannelQuick). "
                    "Skipping the OpenDeck SPA bundle; the app falls back to the native qml UI."
        )
    else()
        find_program(AJAZZ_NPM_EXECUTABLE npm)
        if(NOT AJAZZ_NPM_EXECUTABLE)
            message(
                WARNING
                    "AJAZZ_BUILD_WEBUI=ON but npm/Node.js (>= 20) was not found. "
                    "Skipping the OpenDeck SPA bundle; the app falls back to the native qml UI. "
                    "Install Node.js to embed the OpenDeck web UI."
            )
        else()
            set(_webui_out "${CMAKE_CURRENT_SOURCE_DIR}/webui/opendeck/build")
            message(
                STATUS "AJAZZ_BUILD_WEBUI=ON — building OpenDeck SPA (scripts/build-webui.sh) ..."
            )
            execute_process(
                COMMAND bash "${CMAKE_SOURCE_DIR}/scripts/build-webui.sh"
                WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
                RESULT_VARIABLE _webui_rc
                OUTPUT_VARIABLE _webui_log
                ERROR_VARIABLE _webui_log
            )
            if(NOT _webui_rc EQUAL 0)
                message(FATAL_ERROR "build-webui.sh failed (rc=${_webui_rc}):\n${_webui_log}")
            endif()
            if(NOT EXISTS "${_webui_out}/index.html")
                message(FATAL_ERROR "build-webui.sh produced no ${_webui_out}/index.html")
            endif()
            # Bundle the whole SPA tree under :/opendeck (BASE strips the build dir so index.html ->
            # :/opendeck/index.html and _app/* keep their relative paths).
            file(GLOB_RECURSE _webui_files "${_webui_out}/*")
            qt_add_resources(
                ajazz-control-center
                "opendeck_spa"
                PREFIX
                "/opendeck"
                BASE
                "${_webui_out}"
                FILES
                ${_webui_files}
            )
            target_compile_definitions(ajazz-control-center PRIVATE AJAZZ_HAVE_WEBUI=1)
            message(STATUS "AJAZZ_BUILD_WEBUI: bundled OpenDeck SPA into :/opendeck")
        endif()
    endif()
endif()

# ----------------------------------------------------------------------------
# Branding: forward CMake cache vars to the C++ source via -D defines. All four are optional;
# BrandingService falls back to the upstream identity when a define is missing.
# ----------------------------------------------------------------------------
set(AJAZZ_PRODUCT_NAME "AJAZZ Control Center" CACHE STRING
                                                    "Product display name baked into the binary"
)
set(AJAZZ_VENDOR_NAME "Aiacos" CACHE STRING "Vendor / publisher name")
set(AJAZZ_APP_ID "io.github.Aiacos.AjazzControlCenter" CACHE STRING "Reverse-DNS application id")
set(AJAZZ_BRAND_DIR "" CACHE PATH "Optional brand asset directory (icons + theme.json)")

# Resource paths follow qt_add_qml_module's convention: qrc:/qt/qml/<URI-with-slashes>/<alias> We
# hard-code these so BrandingService can load files without knowing CMake.
set(_ajazz_qrc_root "qrc:/qt/qml/AjazzControlCenter")

target_compile_definitions(
    ajazz-control-center
    PRIVATE AJAZZ_PRODUCT_NAME="${AJAZZ_PRODUCT_NAME}"
            AJAZZ_VENDOR_NAME="${AJAZZ_VENDOR_NAME}"
            AJAZZ_APP_ID="${AJAZZ_APP_ID}"
            AJAZZ_APP_ICON_QRC="${_ajazz_qrc_root}/branding/app.svg"
            AJAZZ_TRAY_ICON_QRC="${_ajazz_qrc_root}/branding/tray.svg"
            AJAZZ_THEME_QRC=":/qt/qml/AjazzControlCenter/branding/theme.json"
)

# Phase 22 Plan 22-01 (PLUGIN-14 / COD-031): ajazz::plugins is linked unconditionally so the Ed25519
# verify gate (plugin_verify_gate.{hpp,cpp}) is always available. The signer subprocess-calls python
# at runtime; it does NOT need the python *host* to be built. The two verifier-specific defs
# (AJAZZ_PLUGIN_VERIFIER_SCRIPT and AJAZZ_PLUGIN_TRUST_ROOTS) move out of the
# AJAZZ_BUILD_PYTHON_HOST guard so the gate is never silently bypassed on a
# -DAJAZZ_BUILD_PYTHON_HOST=OFF build — it returns Refused instead (D-05 fail-closed). The host-only
# defs (AJAZZ_PYTHON_HOST, AJAZZ_PLUGIN_HOST_SCRIPT, AJAZZ_PLUGIN_PYTHONPATH) stay inside the guard
# because they are only consumed by Application::initPluginHost().
target_link_libraries(ajazz-control-center PRIVATE ajazz::plugins)
target_compile_definitions(
    ajazz-control-center
    PRIVATE AJAZZ_PLUGIN_VERIFIER_SCRIPT="${CMAKE_SOURCE_DIR}/scripts/sign-plugin-manifest.py"
            AJAZZ_PLUGIN_TRUST_ROOTS="${CMAKE_SOURCE_DIR}/resources/trusted_publishers.json"
)

if(AJAZZ_BUILD_PYTHON_HOST)
    # Resource paths consumed by Application::initPluginHost() at runtime. Today they point at the
    # in-tree dev layout; install support lands when the resources gain `install(FILES …)` rules.
    # The compile definitions let `Application` resolve the four artefacts (child script, sdk
    # PYTHONPATH, verifier script, trust roots) without QStandardPaths gymnastics for the dev case.
    target_compile_definitions(
        ajazz-control-center
        PRIVATE AJAZZ_PYTHON_HOST=1
                AJAZZ_PLUGIN_HOST_SCRIPT="${CMAKE_SOURCE_DIR}/python/ajazz_plugins/_host_child.py"
                AJAZZ_PLUGIN_PYTHONPATH="${CMAKE_SOURCE_DIR}/python"
    )
endif()

# Qt's QML cache code is auto-generated by qt_add_qml_module / qmlcachegen and triggers
# false-positive warnings on GCC 13 (-Wnull-dereference on the loader, -Wconversion on
# qsizetype->int reductions inside Main_qml.cpp etc.). We don't own those sources, so relax
# warning-as-error for the entire qmlcache directory.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    set(_ajazz_qmlcache_root ${CMAKE_CURRENT_BINARY_DIR}/.rcc/qmlcache)
    # qmlcachegen for Qt 6.5+ emits per-target subdirectory: <root>/<target>_qml/<stem>_qml.cpp.
    set(_ajazz_qmlcache_target_dir ${_ajazz_qmlcache_root}/ajazz-control-center_qml)
    set(_ajazz_qmlcache_relax_flags "")
    list(APPEND _ajazz_qmlcache_relax_flags -Wno-error=null-dereference)
    list(APPEND _ajazz_qmlcache_relax_flags -Wno-null-dereference)
    list(APPEND _ajazz_qmlcache_relax_flags -Wno-error=conversion)
    list(APPEND _ajazz_qmlcache_relax_flags -Wno-conversion)
    list(APPEND _ajazz_qmlcache_relax_flags -Wno-error=sign-conversion)
    list(APPEND _ajazz_qmlcache_relax_flags -Wno-sign-conversion)
    # -Wshorten-64-to-32 is a Clang/AppleClang-only diagnostic; GCC errors out at driver level if it
    # sees the flag, so guard it.
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang")
        list(APPEND _ajazz_qmlcache_relax_flags -Wno-error=shorten-64-to-32)
        list(APPEND _ajazz_qmlcache_relax_flags -Wno-shorten-64-to-32)
    endif()
    # Loader is emitted at the qmlcache root.
    set_source_files_properties(
        ${_ajazz_qmlcache_root}/ajazz-control-center_qmlcache_loader.cpp
        PROPERTIES COMPILE_OPTIONS "${_ajazz_qmlcache_relax_flags}" GENERATED TRUE
    )
    # Per-QML compiled sources: enumerate from QML files and map *.qml -> *_qml.cpp. Both the legacy
    # flat layout and the per-target subdir layout are covered.
    file(GLOB_RECURSE _ajazz_qml_files RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}/qml
         ${CMAKE_CURRENT_SOURCE_DIR}/qml/*.qml
    )
    set(_ajazz_qmlcache_generated_sources "")
    foreach(qml_file IN LISTS _ajazz_qml_files)
        get_filename_component(_qml_stem "${qml_file}" NAME_WE)
        get_filename_component(_qml_subdir "${qml_file}" DIRECTORY)
        if(_qml_subdir)
            set(_qml_subdir "${_qml_subdir}/")
        endif()
        # Flat layout (Qt < 6.5)
        list(APPEND _ajazz_qmlcache_generated_sources
             "${_ajazz_qmlcache_root}/${_qml_stem}_qml.cpp"
        )
        # Per-target subdir layout, preserving the QML source subdirectory
        list(APPEND _ajazz_qmlcache_generated_sources
             "${_ajazz_qmlcache_target_dir}/${_qml_subdir}${_qml_stem}_qml.cpp"
        )
    endforeach()
    if(_ajazz_qmlcache_generated_sources)
        set_source_files_properties(
            ${_ajazz_qmlcache_generated_sources}
            PROPERTIES COMPILE_OPTIONS "${_ajazz_qmlcache_relax_flags}" GENERATED TRUE
        )
    endif()
endif()

install(TARGETS ajazz-control-center RUNTIME DESTINATION bin BUNDLE DESTINATION .)

# experiment/mirajazz Slice F: build the out-of-process mirajazz Rust sidecar (streamdock-host) via
# cargo and stage/install it beside the app binary, so SidecarStreamDockDevice's
# applicationDirPath() lookup resolves it in both the dev layout and every package generator. Gated
# by AJAZZ_BUILD_SIDECAR.
ajazz_add_streamdock_sidecar(ajazz-control-center)

# Windows: bundle the Qt runtime into the install tree so the CPack WIX MSI and ZIP (which harvest
# the whole install tree) are self-contained. Without this the package ships ONLY
# ajazz-control-center.exe and the installed app fails to start with "The code execution cannot
# proceed because Qt6Widgets.dll was not found" (Qt6Widgets is pulled in by QApplication in main.cpp
# + QSystemTrayIcon in tray_controller.cpp). qt_generate_deploy_qml_app_script runs windeployqt (Qt
# libs + platform/style/imageformat/tls plugins + the QML module imports) at `cmake --install` time,
# populating bin/ before CPack harvests it. NOTE: validated by the windows-2022 MSI job — cannot be
# exercised on Linux.
if(WIN32)
    qt_generate_deploy_qml_app_script(
        TARGET ajazz-control-center OUTPUT_SCRIPT _ajazz_deploy_script
        NO_UNSUPPORTED_PLATFORM_ERROR NO_TRANSLATIONS
    )
    install(SCRIPT "${_ajazz_deploy_script}")
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    install(FILES ${CMAKE_SOURCE_DIR}/resources/linux/ajazz-control-center.desktop
            DESTINATION share/applications
    )
    install(FILES ${CMAKE_SOURCE_DIR}/resources/linux/70-ajazz.rules DESTINATION lib/udev/rules.d)

    # ----------------------------------------------------------------------
    # hicolor icon theme entries.
    #
    # We ship the brand-aligned SVG (resources/branding/app.svg, the same artwork embedded in the
    # README) as the scalable theme icon, AND a full ladder of pre-rasterised PNG fallbacks so file
    # managers, tray implementations and X11 / Wayland status bars that ignore SVGs (or rasterise
    # them poorly at very small sizes) still pick up the brand mark. The scalable SVG plus the eight
    # raster sizes mirror what makeAppIcon() pre-renders at runtime so QIcon::fromTheme("ajazz-
    # control-center") returns crisp pixmaps regardless of the size Qt asks for.
    # ----------------------------------------------------------------------
    install(FILES ${CMAKE_SOURCE_DIR}/resources/branding/app.svg
            DESTINATION share/icons/hicolor/scalable/apps RENAME ajazz-control-center.svg
    )
    foreach(
        sz
        16
        22
        24
        32
        48
        64
        128
        256
        512
    )
        install(FILES ${CMAKE_SOURCE_DIR}/resources/icons/hicolor/app-${sz}.png
                DESTINATION share/icons/hicolor/${sz}x${sz}/apps RENAME ajazz-control-center.png
        )
    endforeach()
endif()
