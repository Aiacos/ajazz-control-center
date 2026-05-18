// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file aj_series_tft_pipeline.hpp
 * @brief Host-side renderer for the AJ-series mouse TFT LCD basetta
 *        (clock + active DPI widget).
 *
 * The AJ-series wireless mice (2.4G 8K, AJ159 APEX, AJ199 family, ...) ship
 * with a small TFT LCD on the dock / base. The vendor app renders the
 * current time + active DPI value as a bitmap and uploads it via opcode
 * 0x25 `FEA_CMD_SETTFTLCDDATA` chunked at 54 bytes RGB565 per packet
 * (`aj_series_opcode_table.md` §3.12, vendor doc line 326).
 *
 * Architecture mirrors `src/devices/streamdeck/src/image_pipeline.hpp`:
 *  - Pure functions, no I/O, no Qt event loop required.
 *  - QImage in, RGB565 byte stream out.
 *  - Caller drives the chunked HID upload using the protocol-layer builder
 *    @ref ajazz::mouse::aj_series::buildSetTftLcdData (this header has no
 *    knowledge of the transport).
 *
 * Closes the documented v1.x gap recorded in
 * `docs/protocols/mouse/aj_series_vendor.md` line 393:
 *   `Screen / LCD | absent | gap (defer to a future v1.x)`.
 */
#pragma once

#include <QImage>
#include <QSize>
#include <QString>

#include <chrono>
#include <cstdint>
#include <vector>

namespace ajazz::mouse {

/**
 * @brief Render a clock + DPI face for the basetta TFT LCD.
 *
 * Composes a simple HH:MM clock readout above the current DPI value (e.g.
 * "16:34" / "1600 DPI"), centred on a 16-bit-friendly canvas. The rendered
 * QImage is in @c QImage::Format_RGB16 ready for direct memory-copy into
 * the chunked transmit buffer.
 *
 * @param panelSize   Pixel resolution of the target TFT. Common values
 *                    are 128x128 (small) or 240x240 (AJ159 APEX). The
 *                    AJAZZ catalogue does not publish exact panel
 *                    dimensions; callers should pass the value verified
 *                    against their hardware (USB capture).
 * @param now         Local time to render. Caller passes
 *                    `std::chrono::system_clock::now()` for live update;
 *                    tests can pass a fixed time_point for determinism.
 * @param activeDpi   Active DPI stage value (e.g. 1600). Pass 0 to omit
 *                    the DPI line (mouse without DPI introspection).
 *
 * @return RGB565 QImage of exactly @p panelSize, ready to feed
 *         @ref encodeRgb565Chunks().
 */
[[nodiscard]] QImage renderClockDpiFace(QSize panelSize,
                                        std::chrono::system_clock::time_point now,
                                        std::uint32_t activeDpi);

/**
 * @brief Pack a Format_RGB16 QImage into the chunked envelope payload
 *        bytes expected by @ref buildSetTftLcdData.
 *
 * Each chunk is `<= aj_series::kTftChunkPayloadBytes` (54) bytes. Pixel
 * order is row-major (top-left first), matching how the vendor's
 * `_oledUpgrade` loop walks the framebuffer.
 *
 * RGB565 byte order on the wire is **little-endian uint16 per pixel**:
 * the low byte (containing the 5 blue bits + low 3 of green) comes first,
 * followed by the high byte (5 red bits + high 3 of green). This matches
 * QImage::Format_RGB16's in-memory layout on every platform Qt 6 supports
 * (little-endian native), so we simply linearly copy the pixel buffer
 * into chunk slices without any per-pixel byte-swap.
 *
 * @param frame  Source QImage; must be QImage::Format_RGB16. Caller
 *               converts via @c QImage::convertedTo(Format_RGB16) before
 *               passing.
 * @return Vector of byte vectors; outer index = chunk index, inner
 *         length is <= 54 bytes (the last chunk may be shorter).
 */
[[nodiscard]] std::vector<std::vector<std::uint8_t>> encodeRgb565Chunks(QImage const& frame);

} // namespace ajazz::mouse
