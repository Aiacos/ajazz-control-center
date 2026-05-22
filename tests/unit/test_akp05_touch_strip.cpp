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
#include "ajazz/core/transport.hpp"
#include "ajazz/streamdeck/streamdeck.hpp"
#include "akp05_protocol.hpp"
#include "fixtures/mock_transport.hpp"
#include "qt_app_fixture.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
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
    auto dev =
        streamdeck::makeAkp05WithTransport(makeAkp05Descriptor(), makeAkp05Id(), std::move(owned));
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

/// Minimal ITransport whose write() always throws, simulating a device
/// physically yanked mid-burst (makeHidTransport's write() throws on a
/// failed HID write). MockTransport is `final`, so this is a separate stub.
class ThrowingTransport final : public core::ITransport {
public:
    void open() override {}
    void close() override {}
    [[nodiscard]] bool isOpen() const noexcept override { return true; }
    std::size_t write(std::span<std::uint8_t const>) override {
        throw std::runtime_error("simulated device-yank: HID write failed");
    }
    std::size_t read(std::span<std::uint8_t>, std::chrono::milliseconds) override { return 0; }
    std::size_t writeFeature(std::span<std::uint8_t const>) override {
        throw std::runtime_error("simulated device-yank: HID writeFeature failed");
    }
    std::size_t readFeature(std::span<std::uint8_t>) override { return 0; }
    [[nodiscard]] core::TransportStats stats() const noexcept override { return {}; }
};

} // namespace

TEST_CASE("akp05 touchStripInfo reports 800x480 panel and 4 zones", "[akp05][touch-strip]") {
    tests::qtGuiApp();
    auto fx = buildFixture();
    auto* strip = dynamic_cast<core::ITouchStripDisplayCapable*>(fx.device.get());
    REQUIRE(strip != nullptr);

    auto const info = strip->touchStripInfo();
    CHECK(info.widthPx == 800);
    CHECK(info.heightPx == 480);
    CHECK(info.zoneCount == 4);
}

TEST_CASE("akp05 setTouchStripImage emits DRA header + chunks + ULEND", "[akp05][touch-strip]") {
    tests::qtGuiApp();
    auto fx = buildFixture();
    auto* strip = dynamic_cast<core::ITouchStripDisplayCapable*>(fx.device.get());
    REQUIRE(strip != nullptr);

    auto const rgba = gradientRgba(64, 64);
    // Target zone 0 at offset (100, 200) inside a 200x100 rect — host-side
    // resize scales the 64x64 source up to fill that rect before JPEG encode.
    REQUIRE(strip->setTouchStripImage(rgba,
                                      64,
                                      64,
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
    std::uint32_t const jpegSize = (static_cast<std::uint32_t>(header[8]) << 24) |
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
    REQUIRE_FALSE(strip->setTouchStripImage(rgba,
                                            64,
                                            64,
                                            /*location=*/99,
                                            /*x=*/0,
                                            /*y=*/0,
                                            /*rectWidth=*/200,
                                            /*rectHeight=*/100));
    CHECK(fx.transport->writes().empty());
}

TEST_CASE("akp05 clearTouchStrip emits a DRA header + chunks + ULEND", "[akp05][touch-strip]") {
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

// VER-probe at open: the host sends CRT VER first thing after opening, and the
// AKP05E answers via a HID GET_REPORT (readFeature), not on the interrupt-IN
// endpoint (akp05_init_sequence.md §3.2; confirmed on real hardware 2026-05-20).
TEST_CASE("akp05 open() probes and caches the firmware version", "[akp05][open][vendor-re]") {
    auto owned = std::make_unique<tests::MockTransport>();
    auto* observer = owned.get();
    // The real AKP05E returns the version via a HID GET_FEATURE_REPORT on report
    // id 0x01 with NO preceding write (mirajazz read_firmware_version_from_raw_device,
    // github.com/4ndv/mirajazz). Response: a leading report-id byte followed by
    // the ASCII version string, zero-padded.
    std::vector<std::uint8_t> verResp{0x00};
    for (char const c : std::string{"V3.AKP05E.01.007"}) {
        verResp.push_back(static_cast<std::uint8_t>(c));
    }
    verResp.resize(20, 0x00); // the probe reads into a 20-byte buffer
    observer->enqueueReadFeature(verResp);

    auto dev =
        streamdeck::makeAkp05WithTransport(makeAkp05Descriptor(), makeAkp05Id(), std::move(owned));

    // Transport starts closed, so open() runs the probe.
    REQUIRE(dev->firmwareVersion() == "unknown");
    dev->open();
    CHECK(dev->firmwareVersion() == "V3.AKP05E.01.007");

    // The version is pulled purely via GET_FEATURE_REPORT — no CRT VER OUT write
    // is emitted by the probe (it was the wrong method and never elicited a reply
    // on real hardware). On Windows hidapi this GET_FEATURE_REPORT returns nothing
    // (mirajazz #10), but the mock supplies it, so the parse path is exercised here.
}

TEST_CASE("akp05 rejects out-of-range key/encoder indices without writing the wire",
          "[akp05][bounds]") {
    // WR-02: an out-of-range index must be refused before any header is built,
    // never shipped verbatim to firmware. The 0xff broadcast clear stays valid.
    tests::qtGuiApp();
    auto fx = buildFixture();
    auto* disp = dynamic_cast<core::IDisplayCapable*>(fx.device.get());
    auto* enc = dynamic_cast<core::IEncoderCapable*>(fx.device.get());
    REQUIRE(disp != nullptr);
    REQUIRE(enc != nullptr);

    auto const rgba = gradientRgba(16, 16);

    // Out-of-range indices: nothing reaches the transport.
    disp->setKeyImage(200, rgba, 16, 16);   // > KeyCount (10)
    disp->setKeyImage(0, rgba, 16, 16);     // 0 invalid (keys are 1-based)
    enc->setEncoderImage(50, rgba, 16, 16); // >= EncoderCount (4)
    disp->clearKey(99);                     // not 0xff, out of range
    REQUIRE(fx.transport->writes().empty());

    // A valid key index DOES write, and the 0xff "clear all" broadcast survives.
    disp->setKeyImage(1, rgba, 16, 16);
    REQUIRE_FALSE(fx.transport->writes().empty());
    auto const before = fx.transport->writes().size();
    disp->clearKey(0xff);
    REQUIRE(fx.transport->writes().size() > before);
}

TEST_CASE("akp05 touch-strip writes return false (not throw) on a device-yank",
          "[akp05][touch-strip]") {
    // Regression for Phase 10 CR-01: a transport write that throws mid-burst
    // (physical yank) must surface as a false return per the
    // ITouchStripDisplayCapable contract, never escape the override.
    tests::qtGuiApp();
    auto dev = streamdeck::makeAkp05WithTransport(
        makeAkp05Descriptor(), makeAkp05Id(), std::make_unique<ThrowingTransport>());
    auto* strip = dynamic_cast<core::ITouchStripDisplayCapable*>(dev.get());
    REQUIRE(strip != nullptr);

    auto const rgba = gradientRgba(64, 64);
    bool result = true;
    REQUIRE_NOTHROW(result = strip->setTouchStripImage(rgba,
                                                       64,
                                                       64,
                                                       /*location=*/0,
                                                       /*x=*/0,
                                                       /*y=*/0,
                                                       /*rectWidth=*/200,
                                                       /*rectHeight=*/100));
    CHECK_FALSE(result);

    result = true;
    REQUIRE_NOTHROW(result = strip->clearTouchStrip());
    CHECK_FALSE(result);
}
