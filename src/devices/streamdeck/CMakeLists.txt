add_library(ajazz_streamdeck STATIC
    src/akp153.cpp
    src/akp03.cpp
    src/akp05.cpp
    src/register.cpp
)
add_library(ajazz::streamdeck ALIAS ajazz_streamdeck)

target_include_directories(ajazz_streamdeck
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(ajazz_streamdeck
    PUBLIC  ajazz::core
    PRIVATE ajazz::warnings ajazz::sanitizers Qt6::Core
)
