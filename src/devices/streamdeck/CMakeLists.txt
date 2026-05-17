add_library(
    ajazz_streamdeck STATIC
    src/akp153.cpp
    src/akp03.cpp
    src/akp05.cpp
    src/akp815.cpp
    # ARCH-04 (Phase 09): host-side RGBA→JPEG/PNG pipeline via Qt6 QImage + QImageWriter.
    # PRIVATE-linked (Qt6::Gui pulled into the same PRIVATE scope below) — the public
    # ajazz::core interface stays Qt-free per COD-031 sibling boundary.
    src/image_pipeline.cpp
    src/register.cpp
)
add_library(ajazz::streamdeck ALIAS ajazz_streamdeck)

target_include_directories(
    ajazz_streamdeck PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(
    ajazz_streamdeck
    PUBLIC ajazz::core
    PRIVATE ajazz::warnings
            ajazz::sanitizers
            Qt6::Core
            # Qt6::Gui carries QImage / QImageWriter / QBuffer used by image_pipeline.cpp.
            # PRIVATE so consumers of ajazz::streamdeck don't inherit a Qt::Gui dependency
            # they don't need (matches the PRIVATE discipline for Qt6::Core above).
            Qt6::Gui
)
