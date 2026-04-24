qt_add_executable(
    ajazz-control-center src/main.cpp src/application.cpp src/device_model.cpp
                         src/profile_controller.cpp
)

# Icon lives outside this directory, so give Qt a stable relative resource alias to satisfy
# qt_add_qml_module's "no absolute paths" rule.
set(ACC_APP_ICON ${CMAKE_SOURCE_DIR}/resources/icons/app.svg)
set_source_files_properties(${ACC_APP_ICON} PROPERTIES QT_RESOURCE_ALIAS icons/app.svg)

qt_add_qml_module(
    ajazz-control-center
    URI AjazzControlCenter
    VERSION 1.0
    QML_FILES qml/Main.qml
              qml/DeviceList.qml
              qml/ProfileEditor.qml
              qml/RgbPicker.qml
              qml/KeyDesigner.qml
              qml/EncoderPanel.qml
              qml/MousePanel.qml
    RESOURCES ${ACC_APP_ICON}
)

target_include_directories(ajazz-control-center PRIVATE src)

target_link_libraries(
    ajazz-control-center
    PRIVATE ajazz::core
            ajazz::devices
            ajazz::warnings
            ajazz::sanitizers
            Qt6::Core
            Qt6::Quick
            Qt6::QuickControls2
            Qt6::Svg
)

if(AJAZZ_BUILD_PYTHON_HOST)
    target_link_libraries(ajazz-control-center PRIVATE ajazz::plugins)
    target_compile_definitions(ajazz-control-center PRIVATE AJAZZ_PYTHON_HOST=1)
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
