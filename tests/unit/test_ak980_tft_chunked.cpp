// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_ak980_tft_chunked.cpp
 * @brief End-to-end byte-level test of the AK980 PRO TFT chunked image
 *        upload path (opcode 0x7F sub 0x03 + 28-byte chunk packets with
 *        the 0x80 marker on the chunk-index byte, ITftDisplayCapable).
 *
 * Pins the wire format defined in
 * `docs/protocols/keyboard/ak980pro_tft_protocol.md` §3:
 *
 *   - Header packet (opcode 0x7F sub 0x03, total chunk count uint32-LE at
 *     bytes 4..7, LCD-select + 1 at byte 3).
 *   - Per-chunk packet (24-bit chunk index split across bytes 1/2/3 with
 *     the 0x80 marker on byte 2; 28-byte RGB565 payload at bytes 4..31).
 *
 * Coverage:
 *   - `buildTftChunkedHeader`/`buildTftChunkedPayload` byte layout.
 *   - 240x135 full-frame upload chunk count (2 315 + 1 header = 2 316 writes).
 *   - 64x64 source upscaled to 240x135 panel still produces the canonical
 *     2 315 chunk slice (resize happens host-side, wire shape unchanged).
 *   - Empty image: graceful no-op (returns false, no packets emitted).
 *
 * Tags: `[ak980][tft][chunked]` so the prompt's CTest filter selects them.
 */
#include "ajazz/core/capabilities.hpp"
#include "ajazz/core/device.hpp"
#include "ajazz/keyboard/keyboard.hpp"
#include "fixtures/mock_transport.hpp"
#include "proprietary_protocol.hpp"
#include "qt_app_fixture.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using namespace ajazz;
using namespace ajazz::keyboard::proprietary;

namespace {

core::DeviceDescriptor makeDescriptor() {
    core::DeviceDescriptor d{};
    d.vendorId = 0x0c45;
    d.productId = 0x8009;
    d.family = core::DeviceFamily::Keyboard;
    d.model = "AK980 PRO (test)";
    d.codename = "ak980pro";
    return d;
}

core::DeviceId makeId() {
    core::DeviceId id{};
    id.vendorId = 0x0c45;
    id.productId = 0x8009;
    id.serial = "TEST";
    return id;
}

/// Build a synthetic RGBA8 gradient image of arbitrary size. Caller chooses
/// dimensions; the test then feeds the resulting bytes to uploadTftImage.
std::vector<std::uint8_t> gradientRgba(std::uint16_t width, std::uint16_t height) {
    std::vector<std::uint8_t> rgba;
    rgba.resize(static_cast<std::size_t>(width) * height * 4u);
    for (std::uint16_t y = 0; y < height; ++y) {
        for (std::uint16_t x = 0; x < width; ++x) {
            std::size_t const idx = (static_cast<std::size_t>(y) * width + x) * 4u;
            rgba[idx + 0] = static_cast<std::uint8_t>(x % 256u);
            rgba[idx + 1] = static_cast<std::uint8_t>(y % 256u);
            rgba[idx + 2] = static_cast<std::uint8_t>((x + y) % 256u);
            rgba[idx + 3] = 0xff;
        }
    }
    return rgba;
}

} // namespace

TEST_CASE("ak980 TFT chunk packet has 0x80 marker and 28-byte RGB565 payload",
          "[ak980][tft][chunked]") {
    // Synthetic 28-byte payload: each chunk byte equals its index 0..27.
    std::array<std::uint8_t, kTftChunkPayload> payload{};
    for (std::size_t i = 0; i < payload.size(); ++i) {
        payload[i] = static_cast<std::uint8_t>(i + 1u);
    }

    SECTION("chunk index 0 sets only the marker bit") {
        auto const pkt =
            buildTftChunkedPayload(0u, std::span<std::uint8_t const, kTftChunkPayload>{payload});
        REQUIRE(pkt.size() == ReportSize);
        REQUIRE(pkt[0] == 0x00);       // TFT path uses ReportId 0x00, NOT 0x04.
        REQUIRE(pkt[1] == 0x00);       // chunk index low 8 bits = 0.
        REQUIRE(pkt[2] == 0x80);       // 0x80 marker bit set; high 7 chunk bits = 0.
        REQUIRE((pkt[2] & 0x80) != 0); // marker bit must be set on every chunk.
        REQUIRE(pkt[3] == 0x00);       // chunk index middle 8 bits = 0.
        for (std::size_t i = 0; i < kTftChunkPayload; ++i) {
            REQUIRE(pkt[4 + i] == payload[i]);
        }
        for (std::size_t i = 4 + kTftChunkPayload; i < ReportSize; ++i) {
            REQUIRE(pkt[i] == 0x00); // pad bytes stay zero.
        }
    }

    SECTION("chunk index 2314 (last of 240x135 frame) splits across bytes 1+3") {
        auto const pkt =
            buildTftChunkedPayload(2314u, std::span<std::uint8_t const, kTftChunkPayload>{payload});
        REQUIRE(pkt[1] == 0x0a); // 2314 & 0xFF.
        REQUIRE(pkt[2] == 0x80); // marker only; high 7 bits of 2314 = 0.
        REQUIRE(pkt[3] == 0x09); // (2314 >> 8) & 0xFF.
    }
}

TEST_CASE("ak980 TFT chunked HEADER carries opcode 0x7F sub 0x03 + total chunks",
          "[ak980][tft][chunked]") {
    auto const pkt = buildTftChunkedHeader(/*lcdSelect=*/0,
                                           /*totalChunks=*/2315u);
    REQUIRE(pkt.size() == ReportSize);
    REQUIRE(pkt[0] == 0x00);              // TFT-path ReportId.
    REQUIRE(pkt[1] == CmdScreenHeader);   // 0x7F.
    REQUIRE(pkt[2] == CmdScreenSubBegin); // 0x03 — the discriminator the
                                          // chunk packets distinguish via
                                          // their 0x80 marker on byte 2.
    REQUIRE(pkt[3] == 0x01);              // lcdSelect + 1.
    REQUIRE(pkt[4] == 0x0b);              // 2315 = 0x90b LE -> 0x0b, 0x09.
    REQUIRE(pkt[5] == 0x09);
    REQUIRE(pkt[6] == 0x00);
    REQUIRE(pkt[7] == 0x00);
}

TEST_CASE("ak980 TFT full-frame upload emits header + 2315 chunks", "[ak980][tft][chunked]") {
    tests::qtGuiApp();
    auto transport = std::make_unique<tests::MockTransport>();
    auto* observer = transport.get();
    transport->open();

    auto device = keyboard::makeProprietaryKeyboardWithTransport(
        makeDescriptor(), makeId(), std::move(transport));
    REQUIRE(device != nullptr);

    auto* tft = dynamic_cast<core::ITftDisplayCapable*>(device.get());
    REQUIRE(tft != nullptr);

    auto const info = tft->tftPanelInfo();
    REQUIRE(info.widthPx == 240);
    REQUIRE(info.heightPx == 135);

    // Native-resolution source so resize is a no-op.
    auto const rgba = gradientRgba(240, 135);
    REQUIRE(tft->uploadTftImage(rgba, 240, 135));

    auto const& writes = observer->writes();
    // 240x135 RGB565 = 64 800 bytes / 28 bytes-per-chunk = 2 315 chunks
    // (ceil); + 1 header packet = 2 316 wire packets.
    REQUIRE(writes.size() == 1u + 2315u);
    REQUIRE(observer->writeFeatureCount() == writes.size());

    // First packet must be the chunked header (opcode 0x7F sub 0x03).
    auto const& header = writes.front();
    REQUIRE(header.size() == ReportSize);
    REQUIRE(header[1] == CmdScreenHeader);
    REQUIRE(header[2] == CmdScreenSubBegin);

    // Subsequent packets are chunks 0..2314 — each carries the 0x80 marker
    // on byte 2 and a monotonically increasing chunk index decoded from
    // bytes 1/2/3 per §3.3.
    for (std::size_t i = 1; i < writes.size(); ++i) {
        auto const& pkt = writes.at(i);
        REQUIRE(pkt.size() == ReportSize);
        REQUIRE((pkt[2] & 0x80u) != 0);
        std::uint32_t const decoded = static_cast<std::uint32_t>(pkt[1]) |
                                      (static_cast<std::uint32_t>(pkt[3]) << 8u) |
                                      (static_cast<std::uint32_t>(pkt[2] & 0x7fu) << 16u);
        REQUIRE(decoded == static_cast<std::uint32_t>(i - 1u));
    }
}

TEST_CASE("ak980 TFT upload resizes a smaller source to the panel resolution",
          "[ak980][tft][chunked]") {
    tests::qtGuiApp();
    auto transport = std::make_unique<tests::MockTransport>();
    auto* observer = transport.get();
    transport->open();

    auto device = keyboard::makeProprietaryKeyboardWithTransport(
        makeDescriptor(), makeId(), std::move(transport));
    auto* tft = dynamic_cast<core::ITftDisplayCapable*>(device.get());
    REQUIRE(tft != nullptr);

    // 64x64 source — well below the panel — drives the host-side resize path
    // (nearest-neighbour scale to 240x135 inside encodeRgb565).
    auto const rgba = gradientRgba(64, 64);
    REQUIRE(tft->uploadTftImage(rgba, 64, 64));

    // The wire envelope is identical regardless of source size: 1 header + 2 315
    // chunks (the host pipeline always sizes the RGB565 stream to the panel
    // resolution before slicing).
    REQUIRE(observer->writes().size() == 1u + 2315u);
}

TEST_CASE("ak980 TFT upload returns false for empty image without emitting packets",
          "[ak980][tft][chunked]") {
    tests::qtGuiApp();
    auto transport = std::make_unique<tests::MockTransport>();
    auto* observer = transport.get();
    transport->open();

    auto device = keyboard::makeProprietaryKeyboardWithTransport(
        makeDescriptor(), makeId(), std::move(transport));
    auto* tft = dynamic_cast<core::ITftDisplayCapable*>(device.get());
    REQUIRE(tft != nullptr);

    // Empty span + zero dims: graceful no-op (return false, no writes).
    std::vector<std::uint8_t> empty{};
    REQUIRE_FALSE(tft->uploadTftImage(empty, 0, 0));
    REQUIRE(observer->writes().empty());

    // Mismatched size (claims 4x4 but supplies 0 bytes): same graceful no-op.
    REQUIRE_FALSE(tft->uploadTftImage(empty, 4, 4));
    REQUIRE(observer->writes().empty());
}

TEST_CASE("ak980 TFT encodeRgb565 produces 64 800 big-endian bytes for native input",
          "[ak980][tft][chunked]") {
    tests::qtGuiApp();
    // Build a raw RGBA8 buffer directly so the on-the-wire byte order is
    // unambiguous (no QImage::fill / QRgb endianness gotchas). Every pixel
    // is #FF8000FF (pure orange) which packs to RGB565 = R=31, G=32, B=0
    // -> 0xFC00; on the wire we expect BE bytes 0xFC, 0x00 per §6.
    std::vector<std::uint8_t> rgba;
    rgba.resize(240u * 135u * 4u);
    for (std::size_t i = 0; i < rgba.size(); i += 4) {
        rgba[i + 0] = 0xff; // R
        rgba[i + 1] = 0x80; // G
        rgba[i + 2] = 0x00; // B
        rgba[i + 3] = 0xff; // A
    }
    auto const encoded = encodeRgb565(rgba, 240, 135);
    REQUIRE(encoded.size() == kTftFrameBytes);
    REQUIRE(encoded.size() == 240u * 135u * 2u);
    // First pixel: 0xFC, 0x00 (BE high byte first per §6).
    REQUIRE(encoded.at(0) == 0xfc);
    REQUIRE(encoded.at(1) == 0x00);
    // Last pixel of the frame echoes the same colour (uniform fill).
    REQUIRE(encoded.at(kTftFrameBytes - 2u) == 0xfc);
    REQUIRE(encoded.at(kTftFrameBytes - 1u) == 0x00);
}
