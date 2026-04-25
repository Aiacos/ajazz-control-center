qt_add_executable(
    ajazz-control-center
    src/main.cpp
    src/application.cpp
    src/device_model.cpp
    src/plugin_catalog_model.cpp
    src/streamdock_catalog_fetcher.cpp
    src/profile_controller.cpp
    src/branding_service.cpp
    src/tray_controller.cpp
    src/theme_service.cpp
    src/autostart_service.cpp
    src/single_instance_guard.cpp
)

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

# Bundled offline fixture for the AJAZZ Streamdock catalogue tab. Loaded by
# StreamdockCatalogFetcher::loadBundledFallback() when neither the upstream catalogue nor the
# on-disk mirror is available (first launch on an air-gapped host, CI smoke runs, etc.). Stable QRC
# alias keeps the C++ side decoupled from CMake source paths.
set(ACC_STREAMDOCK_FALLBACK ${CMAKE_SOURCE_DIR}/resources/streamdock-fallback.json)
set_source_files_properties(
    ${ACC_STREAMDOCK_FALLBACK} PROPERTIES QT_RESOURCE_ALIAS streamdock-fallback.json
)

qt_add_qml_module(
    ajazz-control-center
    URI AjazzControlCenter
    VERSION 1.0
    QML_FILES qml/Main.qml
              qml/Theme.qml
              qml/DeviceList.qml
              qml/ProfileEditor.qml
              qml/RgbPicker.qml
              qml/KeyDesigner.qml
              qml/EncoderPanel.qml
              qml/MousePanel.qml
              qml/Inspector.qml
              qml/PluginStore.qml
              qml/PropertyInspector.qml
              qml/Settings.qml
              qml/components/AppHeader.qml
              qml/components/Card.qml
              qml/components/DeviceRow.qml
              qml/components/EmptyState.qml
              qml/components/EncoderCard.qml
              qml/components/KeyCell.qml
              qml/components/PageHeader.qml
              qml/components/PrimaryButton.qml
              qml/components/SecondaryButton.qml
              qml/components/Toast.qml
    RESOURCES ${ACC_APP_ICON}
              ${ACC_BRAND_APP}
              ${ACC_BRAND_TRAY}
              ${ACC_BRAND_THEME}
              ${ACC_BRAND_THEME_LIGHT}
              ${ACC_BRAND_LOGO}
              ${ACC_DOC_CHANGELOG}
              ${ACC_DOC_PRIVACY}
              ${ACC_STREAMDOCK_FALLBACK}
)

target_include_directories(ajazz-control-center PRIVATE src)

target_link_libraries(
    ajazz-control-center
    PRIVATE ajazz::core
            ajazz::devices
            ajazz::warnings
            ajazz::sanitizers
            Qt6::Core
            Qt6::Gui
            Qt6::Network
            Qt6::Widgets
            Qt6::Quick
            Qt6::QuickControls2
            Qt6::Svg
)

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

if(AJAZZ_BUILD_PYTHON_HOST)
    target_link_libraries(ajazz-control-center PRIVATE ajazz::plugins)
    target_compile_definitions(ajazz-control-center PRIVATE AJAZZ_PYTHON_HOST=1)
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

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    install(FILES ${CMAKE_SOURCE_DIR}/resources/linux/ajazz-control-center.desktop
            DESTINATION share/applications
    )
    install(FILES ${CMAKE_SOURCE_DIR}/resources/linux/99-ajazz.rules DESTINATION lib/udev/rules.d)
    install(FILES ${CMAKE_SOURCE_DIR}/resources/icons/app.svg
            DESTINATION share/icons/hicolor/scalable/apps RENAME ajazz-control-center.svg
    )
endif()
