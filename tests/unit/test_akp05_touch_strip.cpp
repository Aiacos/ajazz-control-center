// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_akp05_touch_strip.cpp
 * @brief Byte-level tests for the newly-exposed ITouchStripDisplayCapable
 *        surface on Akp05Device.
 *
 * Pins the public capability mix-in over the existing AKP05 "DRA" rect-
 * addressable touch-strip upload path (akp05_vendor.md sec 3 row 190):
 *
 *   - touchStripInfo() reports the 800x480 panel + 4-zone split.
 *   - setTouchStripImage(<image>, location, x, y, rectW, rectH) emits the
 *     DRA header packet (BE32 JPEG size at bytes 8..11, location at byte 12,
 *     BE16 width/height/x/y at bytes 13..20), N chunked JPEG packets, and
 *     the ULEND commit-after-image-burst sentinel.
 *   - Out-of-range location returns false WITHOUT emitting any wire packet.
 *   - clearTouchStrip() emits a DRA header + chunks + ULEND with a black
 *     full-panel image (exact JPEG bytes are encoder-dependent and not
 *     pinned; only the envelope shape is asserted).
 *
 * Tests use MockTransport + makeAkp05WithTransport (the COD-026 DI seam) so
 * no real HID hardware is required. qtGuiApp() bootstraps QGuiApplication
 * because the host-side image pipeline drives QImage::scaled + QImageWriter
 * for the JPEG encode.
 */
#include "ajazz/core/capabilities.hpp"
#include "ajazz/core/device.hpp"
#include "ajazz/streamdeck/streamdeck.hpp"
#include "akp05_protocol.hpp"
#include "fixtures/mock_transport.hpp"
#include "qt_app_fixture.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using namespace ajazz;

namespace {

core::DeviceDescriptor makeAkp05Descriptor() {
    core::DeviceDescriptor d{};
    d.vendorId = streamdeck::akp05::VendorIdMiraboxN4;
    d.productId = streamdeck::akp05::ProductIdMiraboxN4;
    d.family = core::DeviceFamily::StreamDeck;
    d.model = "AJAZZ AKP05 / Mirabox N4 (touch-strip test)";
    d.codename = "AKP05V25";
    return d;
}

core::DeviceId makeAkp05Id() {
    core::DeviceId id{};
    id.vendorId = streamdeck::akp05::VendorIdMiraboxN4;
    id.productId = streamdeck::akp05::ProductIdMiraboxN4;
    id.serial = "TEST-TOUCH-STRIP";
    return id;
}

struct Fixture {
    core::DevicePtr device;
    tests::MockTransport* transport;
};

Fixture buildFixture() {
    auto owned = std::make_unique<tests::MockTransport>();
    auto* observer = owned.get();
    owned->open();
    auto dev = streamdeck::makeAkp05WithTransport(makeAkp05Descriptor(), makeAkp05Id(),
                                                  std::move(owned));
    return Fixture{std::move(dev), observer};
}

/// Build a synthetic RGBA8 gradient image of the requested dimensions.
std::vector<std::uint8_t> gradientRgba(std::uint16_t width, std::uint16_t height) {
    std::vector<std::uint8_t> out;
    out.resize(static_cast<std::size_t>(width) * height * 4u);
    for (std::uint16_t y = 0; y < height; ++y) {
        for (std::uint16_t x = 0; x < width; ++x) {
            std::size_t const idx = (static_cast<std::size_t>(y) * width + x) * 4u;
            out[idx + 0] = static_cast<std::uint8_t>(x % 256u);
            out[idx + 1] = static_cast<std::uint8_t>(y % 256u);
            out[idx + 2] = static_cast<std::uint8_t>((x + y) % 256u);
            out[idx + 3] = 0xff;
        }
    }
    return out;
}

} // namespace

TEST_CASE("akp05 touchStripInfo reports 800x480 panel and 4 zones",
          "[akp05][touch-strip]") {
    tests::qtGuiApp();
    auto fx = buildFixture();
    auto* strip = dynamic_cast<core::ITouchStripDisplayCapable*>(fx.device.get());
    REQUIRE(strip != nullptr);

    auto const info = strip->touchStripInfo();
    CHECK(info.widthPx == 800);
    CHECK(info.heightPx == 480);
    CHECK(info.zoneCount == 4);
}

TEST_CASE("akp05 setTouchStripImage emits DRA header + chunks + ULEND",
          "[akp05][touch-strip]") {
    tests::qtGuiApp();
    auto fx = buildFixture();
    auto* strip = dynamic_cast<core::ITouchStripDisplayCapable*>(fx.device.get());
    REQUIRE(strip != nullptr);

    auto const rgba = gradientRgba(64, 64);
    // Target zone 0 at offset (100, 200) inside a 200x100 rect — host-side
    // resize scales the 64x64 source up to fill that rect before JPEG encode.
    REQUIRE(strip->setTouchStripImage(rgba, 64, 64,
                                      /*location=*/0,
                                      /*x=*/100,
                                      /*y=*/200,
                                      /*rectWidth=*/200,
                                      /*rectHeight=*/100));

    auto const& writes = fx.transport->writes();
    // Sequence: DRA header + N chunked JPEG packets + ULEND commit sentinel.
    REQUIRE(writes.size() >= 3);

    // ---- Header packet --------------------------------------------------
    auto const& header = writes.front();
    REQUIRE(header.size() == streamdeck::akp05::PacketSize);
    // CRT prefix.
    CHECK(header[0] == 0x43); // C
    CHECK(header[1] == 0x52); // R
    CHECK(header[2] == 0x54); // T
    // DRA command at 5..7.
    CHECK(header[5] == 0x44); // D
    CHECK(header[6] == 0x52); // R
    CHECK(header[7] == 0x41); // A
    // BE32 JPEG size at bytes 8..11 (non-zero — exact bytes depend on encoder).
    std::uint32_t const jpegSize =
        (static_cast<std::uint32_t>(header[8]) << 24) |
        (static_cast<std::uint32_t>(header[9]) << 16) |
        (static_cast<std::uint32_t>(header[10]) << 8) |
        static_cast<std::uint32_t>(header[11]);
    CHECK(jpegSize > 0);
    // Location at byte 12.
    CHECK(header[12] == 0x00);
    // BE16 rect width at 13..14 (200 = 0x00C8).
    CHECK(header[13] == 0x00);
    CHECK(header[14] == 0xc8);
    // BE16 rect height at 15..16 (100 = 0x0064).
    CHECK(header[15] == 0x00);
    CHECK(header[16] == 0x64);
    // BE16 x at 17..18 (100 = 0x0064).
    CHECK(header[17] == 0x00);
    CHECK(header[18] == 0x64);
    // BE16 y at 19..20 (200 = 0x00C8).
    CHECK(header[19] == 0x00);
    CHECK(header[20] == 0xc8);

    // ---- Chunk packets --------------------------------------------------
    // Every packet between header and ULEND tail must be 512 bytes (zero-
    // padded by the device backend's chunk loop).
    for (std::size_t i = 1; i + 1 < writes.size(); ++i) {
        CAPTURE(i);
        CHECK(writes[i].size() == streamdeck::akp05::PacketSize);
    }

    // ---- ULEND commit sentinel -----------------------------------------
    auto const& tail = writes.back();
    REQUIRE(tail.size() == streamdeck::akp05::PacketSize);
    CHECK(tail[0] == 0x43); // C
    CHECK(tail[1] == 0x52); // R
    CHECK(tail[2] == 0x54); // T
    CHECK(tail[5] == 0x55); // U
    CHECK(tail[6] == 0x4c); // L
    CHECK(tail[7] == 0x45); // E
    CHECK(tail[8] == 0x4e); // N
    CHECK(tail[9] == 0x44); // D
}

TEST_CASE("akp05 setTouchStripImage rejects out-of-range location with no writes",
          "[akp05][touch-strip]") {
    tests::qtGuiApp();
    auto fx = buildFixture();
    auto* strip = dynamic_cast<core::ITouchStripDisplayCapable*>(fx.device.get());
    REQUIRE(strip != nullptr);

    auto const rgba = gradientRgba(64, 64);
    // location=99 is well outside the AKP05 4-zone range; the capability
    // surface must refuse cleanly and emit zero wire packets so a buggy
    // caller cannot poke an unrelated firmware surface.
    REQUIRE_FALSE(strip->setTouchStripImage(rgba, 64, 64,
                                            /*location=*/99,
                                            /*x=*/0,
                                            /*y=*/0,
                                            /*rectWidth=*/200,
                                            /*rectHeight=*/100));
    CHECK(fx.transport->writes().empty());
}

TEST_CASE("akp05 clearTouchStrip emits a DRA header + chunks + ULEND",
          "[akp05][touch-strip]") {
    tests::qtGuiApp();
    auto fx = buildFixture();
    auto* strip = dynamic_cast<core::ITouchStripDisplayCapable*>(fx.device.get());
    REQUIRE(strip != nullptr);

    REQUIRE(strip->clearTouchStrip());

    auto const& writes = fx.transport->writes();
    // Header + at least one chunk + ULEND tail.
    REQUIRE(writes.size() >= 3);

    // First packet MUST be a DRA header (don't pin the black-JPEG bytes —
    // those depend on the JPEG encoder; just verify the envelope shape).
    auto const& header = writes.front();
    REQUIRE(header.size() == streamdeck::akp05::PacketSize);
    CHECK(header[5] == 0x44); // D
    CHECK(header[6] == 0x52); // R
    CHECK(header[7] == 0x41); // A
    // Full-panel rect: width=800 (0x0320), height=480 (0x01E0), x=0, y=0.
    CHECK(header[13] == 0x03);
    CHECK(header[14] == 0x20);
    CHECK(header[15] == 0x01);
    CHECK(header[16] == 0xe0);
    CHECK(header[17] == 0x00);
    CHECK(header[18] == 0x00);
    CHECK(header[19] == 0x00);
    CHECK(header[20] == 0x00);

    // Last packet MUST be the ULEND commit sentinel.
    auto const& tail = writes.back();
    REQUIRE(tail.size() == streamdeck::akp05::PacketSize);
    CHECK(tail[5] == 0x55); // U
    CHECK(tail[6] == 0x4c); // L
    CHECK(tail[7] == 0x45); // E
    CHECK(tail[8] == 0x4e); // N
    CHECK(tail[9] == 0x44); // D
}
