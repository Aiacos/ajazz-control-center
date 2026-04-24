add_library(ajazz_mouse STATIC
    src/aj_series.cpp
    src/register.cpp
)
add_library(ajazz::mouse ALIAS ajazz_mouse)

target_include_directories(ajazz_mouse
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(ajazz_mouse
    PUBLIC  ajazz::core
    PRIVATE ajazz::warnings ajazz::sanitizers
)
