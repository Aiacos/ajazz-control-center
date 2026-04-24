add_library(ajazz_keyboard STATIC
    src/via_keyboard.cpp
    src/proprietary_keyboard.cpp
    src/register.cpp
)
add_library(ajazz::keyboard ALIAS ajazz_keyboard)

target_include_directories(ajazz_keyboard
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(ajazz_keyboard
    PUBLIC  ajazz::core
    PRIVATE ajazz::warnings ajazz::sanitizers
)
