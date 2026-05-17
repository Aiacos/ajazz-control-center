add_library(
    ajazz_mouse STATIC
    src/aj_series.cpp
    # P3.12: vendor-correct wire-format primitives per aj_series_opcode_table.md.
    # Additive in this commit (aj_series.cpp not yet migrated). The migration
    # to the new protocol is the follow-up commit P3.12.2.
    src/aj_series_protocol.cpp
    src/register.cpp
)
add_library(ajazz::mouse ALIAS ajazz_mouse)

target_include_directories(
    ajazz_mouse PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(ajazz_mouse PUBLIC ajazz::core PRIVATE ajazz::warnings ajazz::sanitizers)
