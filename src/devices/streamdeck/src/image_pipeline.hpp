// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file image_pipeline.hpp
 * @brief Host-side RGBA→JPEG/PNG image-encoding pipeline for AKP-family Stream Dock backends.
 *
 * Implements ARCH-04 (Phase 09): Qt6 `QImage::scaled(SmoothTransformation)` +
 * `QImageWriter` host-side encoding, PRIVATE-linked to `ajazz_devices_streamdeck`.
 * Replaces the prior contract violation where backends required callers to pre-encode
 * JPEG/PNG bytes themselves — the @c IDisplayCapable contract says callers pass RGBA8
 * at any resolution and the backend normalises.
 *
 * Pure functions, header-only declarations. Implementation in image_pipeline.cpp uses
 * Qt6::Gui (QImage + QImageWriter + QBuffer).
 *
 * @see capabilities.hpp::IDisplayCapable
 * @see .planning/phases/09-research-captures-hygiene/ARCH-04.md
 */
#pragma once

#include "ajazz/core/capabilities.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace ajazz::streamdeck {

/// Codec used by the device's display protocol.
enum class ImageFormat : std::uint8_t {
    Jpeg, ///< AKP153 / AKP05 / AKP815 key + encoder + main LCDs.
    Png,  ///< AKP03 key LCDs (some firmware variants).
};

/**
 * @brief Geometry + orientation + codec parameters for one device-specific image surface.
 *
 * Different surfaces on the same device may require different transforms: AKP05 key LCDs
 * are 85×85 Rot0 JPEG, encoder LCDs are 100×100 Rot0 JPEG, main strip is 800×100 Rot0 JPEG.
 * AKP153 keys are 85×85 Rot90+mirror JPEG. AKP815 keys are 100×100 Rot180 JPEG.
 */
struct ImageTransform {
    std::uint16_t targetWidth{0};  ///< Output width in pixels.
    std::uint16_t targetHeight{0}; ///< Output height in pixels.
    ImageFormat format{ImageFormat::Jpeg};
    int rotationDegrees{0}; ///< 0 / 90 / 180 / 270; other values rounded to nearest.
    bool mirror{false};     ///< Horizontal mirror (applied AFTER rotation).
    int jpegQuality{85};    ///< 1..100; ignored for PNG. 85 matches vendor defaults.
};

/**
 * @brief Resize + reorient + encode an RGBA8 image to the device's expected codec/geometry.
 *
 * Uses `QImage::scaled(SmoothTransformation)` for the resize and `QImageWriter` for the
 * encode. Throws @c std::invalid_argument on size mismatch or zero-dim transform.
 *
 * @param rgbaBytes Tightly packed RGBA8 pixels; size must equal `srcWidth * srcHeight * 4`.
 * @param srcWidth  Source image width in pixels (> 0).
 * @param srcHeight Source image height in pixels (> 0).
 * @param transform Output geometry + codec settings.
 * @return Encoded JPEG or PNG bytes ready for the device's chunked-write protocol.
 *
 * @throws std::invalid_argument on dimension mismatch or zero-area target.
 * @throws std::runtime_error on QImageWriter encode failure.
 */
[[nodiscard]] std::vector<std::uint8_t> encodeForDevice(std::span<std::uint8_t const> rgbaBytes,
                                                        std::uint16_t srcWidth,
                                                        std::uint16_t srcHeight,
                                                        ImageTransform const& transform);

/**
 * @brief Synthesise a solid-color encoded image at the target geometry.
 *
 * Used by @c setKeyColor implementations to fill a key slot with a single colour without
 * requiring the caller to allocate a full RGBA8 buffer. Internally builds a 1×1 RGBA8
 * image then resizes — `QImage::scaled` cheaply fills the target with the source colour.
 *
 * @param color     Solid sRGB fill colour.
 * @param transform Output geometry + codec settings.
 * @return Encoded JPEG or PNG bytes of the solid-color image.
 *
 * @throws std::invalid_argument on zero-area target.
 * @throws std::runtime_error on QImageWriter encode failure.
 */
[[nodiscard]] std::vector<std::uint8_t> encodeSolid(core::Rgb color,
                                                    ImageTransform const& transform);

} // namespace ajazz::streamdeck
