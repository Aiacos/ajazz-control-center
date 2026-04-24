add_subdirectory(streamdeck)
add_subdirectory(keyboard)
add_subdirectory(mouse)

add_library(ajazz_devices INTERFACE)
add_library(ajazz::devices ALIAS ajazz_devices)
target_link_libraries(ajazz_devices INTERFACE ajazz::streamdeck ajazz::keyboard ajazz::mouse)
