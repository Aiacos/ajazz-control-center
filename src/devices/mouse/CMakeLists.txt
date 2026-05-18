find_package(Qt6 REQUIRED COMPONENTS Gui)

add_library(
    ajazz_mouse STATIC
    src/aj_series.cpp
    # P3.12: vendor-correct wire-format primitives per aj_series_opcode_table.md.
    src/aj_series_protocol.cpp
    # 2026-05-18: host-side renderer for the TFT LCD basetta (clock + DPI
    # widget). Pure QImage in / RGB565 chunks out — opcode 0x25 chunked
    # via aj_series::buildSetTftLcdData. Closes the v1.x gap recorded in
    # docs/protocols/mouse/aj_series_vendor.md line 393.
    src/aj_series_tft_pipeline.cpp
    src/register.cpp
)
add_library(ajazz::mouse ALIAS ajazz_mouse)

target_include_directories(
    ajazz_mouse PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Qt6::Gui is PRIVATE: ajazz_mouse's public surface (mouse.hpp factories) is Qt-free; only the .cpp
# implementation needs QImage / QPainter for the TFT clock+DPI face renderer.
target_link_libraries(
    ajazz_mouse PUBLIC ajazz::core PRIVATE ajazz::warnings ajazz::sanitizers Qt6::Gui
)
