// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_image_pipeline.cpp
 * @brief Unit tests for the AKP-family host-side image pipeline (ARCH-04 / Phase 09).
 *
 * Locks the IDisplayCapable contract that backends OWN the resize + encode pipeline:
 * callers pass any-resolution RGBA8, the pipeline normalises to the device's native
 * geometry and emits JPEG (AKP05/AKP153/AKP815) or PNG (some AKP03 variants). The tests
 * exercise the codec magic-byte invariants, dimension validation, and the rotation/mirror
 * transforms used by the AKP153 (Rot90+mirror) and AKP815 (Rot180) backends.
 */
#include "image_pipeline.hpp"

#include "ajazz/core/capabilities.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

using namespace ajazz::streamdeck;
using ajazz::core::Rgb;

namespace {

/// Build a deterministic RGBA8 test pattern of the requested size.
[[nodiscard]] std::vector<std::uint8_t> makeRgbaPattern(std::uint16_t w, std::uint16_t h) {
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(w) * h * 4u);
    for (std::size_t i = 0; i + 3 < bytes.size(); i += 4) {
        bytes[i + 0] = static_cast<std::uint8_t>(i & 0xff);         // R varies with offset
        bytes[i + 1] = static_cast<std::uint8_t>((i >> 8) & 0xff);  // G
        bytes[i + 2] = static_cast<std::uint8_t>((i >> 16) & 0xff); // B
        bytes[i + 3] = 0xff;                                        // A
    }
    return bytes;
}

constexpr ImageTransform kAkp05Key{
    .targetWidth = 85,
    .targetHeight = 85,
    .format = ImageFormat::Jpeg,
};

constexpr ImageTransform kAkp153Key{
    .targetWidth = 85,
    .targetHeight = 85,
    .format = ImageFormat::Jpeg,
    .rotationDegrees = 90,
    .mirror = true,
};

constexpr ImageTransform kAkp815Key{
    .targetWidth = 100,
    .targetHeight = 100,
    .format = ImageFormat::Jpeg,
    .rotationDegrees = 180,
};

constexpr ImageTransform kPngVariant{
    .targetWidth = 64,
    .targetHeight = 64,
    .format = ImageFormat::Png,
};

} // namespace

TEST_CASE("encodeForDevice produces JPEG SOI bytes for AKP05 key transform",
          "[image_pipeline][akp05]") {
    auto const rgba = makeRgbaPattern(200, 200);
    auto const out = encodeForDevice(rgba, 200, 200, kAkp05Key);
    REQUIRE(out.size() >= 4);
    // JPEG SOI = 0xFF 0xD8, EOI = 0xFF 0xD9
    REQUIRE(out[0] == 0xff);
    REQUIRE(out[1] == 0xd8);
    REQUIRE(out[out.size() - 2] == 0xff);
    REQUIRE(out[out.size() - 1] == 0xd9);
}

TEST_CASE("encodeForDevice handles AKP153 Rot90+mirror transform", "[image_pipeline][akp153]") {
    auto const rgba = makeRgbaPattern(150, 100);
    auto const out = encodeForDevice(rgba, 150, 100, kAkp153Key);
    REQUIRE(out.size() >= 4);
    REQUIRE(out[0] == 0xff); // SOI
    REQUIRE(out[1] == 0xd8);
}

TEST_CASE("encodeForDevice handles AKP815 Rot180 transform", "[image_pipeline][akp815]") {
    auto const rgba = makeRgbaPattern(120, 120);
    auto const out = encodeForDevice(rgba, 120, 120, kAkp815Key);
    REQUIRE(out.size() >= 4);
    REQUIRE(out[0] == 0xff);
    REQUIRE(out[1] == 0xd8);
}

TEST_CASE("encodeForDevice produces PNG signature for PNG format variant",
          "[image_pipeline][akp03]") {
    auto const rgba = makeRgbaPattern(100, 100);
    auto const out = encodeForDevice(rgba, 100, 100, kPngVariant);
    REQUIRE(out.size() >= 8);
    // PNG signature: 89 50 4E 47 0D 0A 1A 0A
    REQUIRE(out[0] == 0x89);
    REQUIRE(out[1] == 0x50);
    REQUIRE(out[2] == 0x4e);
    REQUIRE(out[3] == 0x47);
    REQUIRE(out[4] == 0x0d);
    REQUIRE(out[5] == 0x0a);
    REQUIRE(out[6] == 0x1a);
    REQUIRE(out[7] == 0x0a);
}

TEST_CASE("encodeSolid red color produces a small valid JPEG", "[image_pipeline][solid]") {
    auto const out = encodeSolid(Rgb{255, 0, 0}, kAkp05Key);
    REQUIRE(out.size() >= 4);
    REQUIRE(out[0] == 0xff);
    REQUIRE(out[1] == 0xd8);
    // Solid colors compress extremely well — 85x85 RGB solid should fit in well under 2 KB.
    REQUIRE(out.size() < 2048);
}

TEST_CASE("encodeForDevice rejects RGBA buffer size mismatch", "[image_pipeline][validation]") {
    std::vector<std::uint8_t> too_small(10 * 10 * 4 - 7);
    REQUIRE_THROWS_AS(encodeForDevice(too_small, 10, 10, kAkp05Key), std::invalid_argument);
}

TEST_CASE("encodeForDevice rejects zero-area target", "[image_pipeline][validation]") {
    auto const rgba = makeRgbaPattern(50, 50);
    ImageTransform bad = kAkp05Key;
    bad.targetWidth = 0;
    REQUIRE_THROWS_AS(encodeForDevice(rgba, 50, 50, bad), std::invalid_argument);
}

TEST_CASE("encodeForDevice rejects zero-area source", "[image_pipeline][validation]") {
    std::vector<std::uint8_t> empty;
    REQUIRE_THROWS_AS(encodeForDevice(empty, 0, 50, kAkp05Key), std::invalid_argument);
    REQUIRE_THROWS_AS(encodeForDevice(empty, 50, 0, kAkp05Key), std::invalid_argument);
}

TEST_CASE("encodeSolid rejects zero-area target", "[image_pipeline][validation]") {
    ImageTransform bad = kAkp05Key;
    bad.targetHeight = 0;
    REQUIRE_THROWS_AS(encodeSolid(Rgb{0, 0, 0}, bad), std::invalid_argument);
}

TEST_CASE("encodeForDevice produces a usable main-strip 800x100 JPEG",
          "[image_pipeline][akp05][main-strip]") {
    // Host-rendered clock-widget workflow uses the main-strip transform: caller
    // renders an arbitrary RGBA8 image and the backend resizes + encodes.
    constexpr ImageTransform main{
        .targetWidth = 800,
        .targetHeight = 100,
        .format = ImageFormat::Jpeg,
    };
    auto const rgba = makeRgbaPattern(640, 80);
    auto const out = encodeForDevice(rgba, 640, 80, main);
    REQUIRE(out.size() >= 4);
    REQUIRE(out[0] == 0xff);
    REQUIRE(out[1] == 0xd8);
}
