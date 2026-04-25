qt_add_executable(
    ajazz-control-center
    src/main.cpp
    src/application.cpp
    src/device_model.cpp
    src/profile_controller.cpp
    src/branding_service.cpp
    src/tray_controller.cpp
    src/theme_service.cpp
)

# Icon lives outside this directory, so give Qt a stable relative resource alias to satisfy
# qt_add_qml_module's "no absolute paths" rule.
set(ACC_APP_ICON ${CMAKE_SOURCE_DIR}/resources/icons/app.svg)
set_source_files_properties(${ACC_APP_ICON} PROPERTIES QT_RESOURCE_ALIAS icons/app.svg)

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
    RESOURCES ${ACC_APP_ICON} ${ACC_BRAND_APP} ${ACC_BRAND_TRAY} ${ACC_BRAND_THEME}
              ${ACC_BRAND_THEME_LIGHT}
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

# Qt's QML cache loader (auto-generated by qt_add_qml_module) triggers a false-positive
# -Wnull-dereference on GCC 13 because it inlines a QGlobalStatic that GCC can't prove non-null.
# Relax warning-as-error for generated sources we don't own.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set(_ajazz_qmlcache_dir ${CMAKE_CURRENT_BINARY_DIR}/.rcc/qmlcache)
    set_source_files_properties(
        ${_ajazz_qmlcache_dir}/ajazz-control-center_qmlcache_loader.cpp
        PROPERTIES COMPILE_OPTIONS "-Wno-error=null-dereference;-Wno-null-dereference" GENERATED
                                                                                       TRUE
    )
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
