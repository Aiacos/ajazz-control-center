// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_factory_reset_and_logo.cpp
 * @brief Byte-level tests for two newly-exposed capability surfaces:
 *
 *   1. IFactoryResettable on AjSeriesMouse (AJ-series opcode 0x02 /
 *      FEA_CMD_SET_RESERT per docs/protocols/mouse/aj_series_opcode_table.md
 *      sec 3.2). Pins the no-payload packet shape and BIT7 checksum.
 *
 *   2. IBootLogoCapable on Akp05Device (AKP05/Mirabox N4 "LOG" opcode per
 *      docs/protocols/streamdeck/akp05_vendor.md sec 2 row 188). Pins the
 *      header packet shape (CRT LOG + BE16 size at bytes 10..11) and the
 *      512-byte JPEG chunking discipline that follows.
 *
 * Both tests drive the COD-026 DI seam via MockTransport - no real HID
 * hardware required. Test tags `[factory-reset]` and `[akp05][boot-logo]`
 * gate the suite for targeted runs per the harness contract.
 */
#include "ajazz/core/capabilities.hpp"
#include "ajazz/core/device.hpp"
#include "ajazz/mouse/mouse.hpp"
#include "ajazz/streamdeck/streamdeck.hpp"
#include "akp05_protocol.hpp"
#include "fixtures/mock_transport.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <numeric>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using namespace ajazz;

// ===========================================================================
// Feature 1 - AJ-series factory reset (opcode 0x02)
// ===========================================================================

namespace {

core::DeviceDescriptor makeMouseDescriptor() {
    core::DeviceDescriptor d{};
    d.vendorId = 0x3151;
    d.productId = 0x5008;
    d.family = core::DeviceFamily::Mouse;
    d.model = "AJAZZ AJ159 APEX (test)";
    d.codename = "aj159_apex";
    d.dpiStageCount = 8;
    return d;
}

core::DeviceId makeMouseId() {
    core::DeviceId id{};
    id.vendorId = 0x3151;
    id.productId = 0x5008;
    id.serial = "TEST-RESET";
    return id;
}

/// Independent BIT7 checksum verifier: sum(pkt[1..63]) & 0x7F per
/// aj_series_opcode_table.md sec 5.
[[nodiscard]] std::uint8_t expectedBit7Checksum(std::vector<std::uint8_t> const& pkt) {
    auto const sum = std::accumulate(pkt.begin() + 1, pkt.end() - 1, std::uint32_t{0});
    return static_cast<std::uint8_t>(sum & 0x7fu);
}

struct MouseFixture {
    core::DevicePtr device;
    tests::MockTransport* transport;
};

MouseFixture buildMouseFixture() {
    auto owned = std::make_unique<tests::MockTransport>();
    auto* observer = owned.get();
    owned->open();
    auto dev =
        mouse::makeAjSeriesWithTransport(makeMouseDescriptor(), makeMouseId(), std::move(owned));
    return MouseFixture{std::move(dev), observer};
}

} // namespace

TEST_CASE("AJ-series factoryReset emits opcode 0x02 with no payload",
          "[factory-reset][aj_series][wire]") {
    auto fx = buildMouseFixture();
    auto* reset = dynamic_cast<core::IFactoryResettable*>(fx.device.get());
    REQUIRE(reset != nullptr);

    REQUIRE(reset->factoryReset());

    auto const& writes = fx.transport->writes();
    REQUIRE(writes.size() == 1);
    auto const& pkt = writes.back();

    // Standard AJ-series envelope: 1-byte HID Report ID + 64-byte body.
    REQUIRE(pkt.size() == 65);

    // pkt[0] = HID Report ID (0x05 per aj_series_opcode_table.md sec 1).
    CHECK(pkt[0] == 0x05);

    // pkt[1] = opcode 0x02 (FEA_CMD_SET_RESERT per sec 3.2).
    CHECK(pkt[1] == 0x02);

    // No-payload command: every byte from pkt[2] through pkt[63] must be 0.
    // (pkt[63] is the last payload slot; pkt[64] is the BIT7 checksum.)
    for (std::size_t i = 2; i < 64; ++i) {
        CAPTURE(i);
        CHECK(pkt[i] == 0x00);
    }

    // BIT7 checksum at pkt[64]: sum(pkt[1..63]) & 0x7F. Since the only
    // non-zero payload byte is the opcode (0x02), the checksum is
    // 0x02 & 0x7F = 0x02. Top bit MUST be clear (BIT7 mask, not BIT8).
    CHECK(pkt[64] == 0x02);
    CHECK((pkt[64] & 0x80u) == 0u);
    CHECK(pkt[64] == expectedBit7Checksum(pkt));
}

TEST_CASE("AJ-series factoryReset returns true on successful transport write",
          "[factory-reset][aj_series]") {
    // Sanity guard: a clean fixture without any throwing transport should
    // always return true. The negative-path coverage lives implicitly in
    // the WARN-log branch which is exercised by the integration suite.
    auto fx = buildMouseFixture();
    auto* reset = dynamic_cast<core::IFactoryResettable*>(fx.device.get());
    REQUIRE(reset != nullptr);
    CHECK(reset->factoryReset() == true);
    CHECK(fx.transport->writes().size() == 1);
}

// ===========================================================================
// Feature 2 - AKP05 boot-logo upload (LOG opcode)
// ===========================================================================

namespace {

core::DeviceDescriptor makeAkp05Descriptor() {
    core::DeviceDescriptor d{};
    d.vendorId = streamdeck::akp05::VendorIdMiraboxN4;
    d.productId = streamdeck::akp05::ProductIdMiraboxN4;
    d.family = core::DeviceFamily::StreamDeck;
    d.model = "AJAZZ AKP05 / Mirabox N4 (test)";
    d.codename = "AKP05V25";
    return d;
}

core::DeviceId makeAkp05Id() {
    core::DeviceId id{};
    id.vendorId = streamdeck::akp05::VendorIdMiraboxN4;
    id.productId = streamdeck::akp05::ProductIdMiraboxN4;
    id.serial = "TEST-LOGO";
    return id;
}

struct Akp05Fixture {
    core::DevicePtr device;
    tests::MockTransport* transport;
};

Akp05Fixture buildAkp05Fixture() {
    auto owned = std::make_unique<tests::MockTransport>();
    auto* observer = owned.get();
    owned->open();
    auto dev =
        streamdeck::makeAkp05WithTransport(makeAkp05Descriptor(), makeAkp05Id(), std::move(owned));
    return Akp05Fixture{std::move(dev), observer};
}

/// Build a flat RGBA8 buffer of the requested size, filled with a solid colour.
std::vector<std::uint8_t> makeSolidRgba(std::uint16_t w, std::uint16_t h, core::Rgb c) {
    std::vector<std::uint8_t> out(static_cast<std::size_t>(w) * h * 4u);
    for (std::size_t i = 0; i < out.size(); i += 4) {
        out[i + 0] = c.r;
        out[i + 1] = c.g;
        out[i + 2] = c.b;
        out[i + 3] = 0xff;
    }
    return out;
}

} // namespace

TEST_CASE("akp05 buildLogoSizeHeader encodes CRT LOG with BE16 size at 10..11",
          "[akp05][boot-logo][protocol]") {
    auto const pkt = streamdeck::akp05::buildLogoSizeHeader(0xabcd);
    REQUIRE(pkt.size() == streamdeck::akp05::PacketSize);

    // CRT prefix.
    CHECK(pkt[0] == 0x43); // C
    CHECK(pkt[1] == 0x52); // R
    CHECK(pkt[2] == 0x54); // T
    CHECK(pkt[3] == 0x00);
    CHECK(pkt[4] == 0x00);

    // LOG command at 5..7 per akp05_vendor.md sec 2 row 188.
    CHECK(pkt[5] == 0x4c); // L
    CHECK(pkt[6] == 0x4f); // O
    CHECK(pkt[7] == 0x47); // G

    // Bytes 8..9 reserved zero (mirrors key/main image header padding).
    CHECK(pkt[8] == 0x00);
    CHECK(pkt[9] == 0x00);

    // BE16 JPEG size at bytes 10..11 (matches buildKeyImageHeader shape).
    CHECK(pkt[10] == 0xab);
    CHECK(pkt[11] == 0xcd);

    // All other bytes zero.
    for (std::size_t i = 12; i < streamdeck::akp05::PacketSize; ++i) {
        CAPTURE(i);
        CHECK(pkt[i] == 0x00);
    }
}

TEST_CASE("akp05 setBootLogo emits CRT LOG header + 512-byte chunks + ULEND",
          "[akp05][boot-logo][wire]") {
    auto fx = buildAkp05Fixture();
    auto* logo = dynamic_cast<core::IBootLogoCapable*>(fx.device.get());
    REQUIRE(logo != nullptr);

    // Push a solid RGBA8 buffer at the main-strip dimensions; the backend
    // host-resizes + JPEG-encodes through the standard image pipeline.
    auto const rgba = makeSolidRgba(streamdeck::akp05::MainDisplayWidthPx,
                                    streamdeck::akp05::MainDisplayHeightPx,
                                    core::Rgb{0x10, 0x20, 0x30});
    logo->setBootLogo(
        rgba, streamdeck::akp05::MainDisplayWidthPx, streamdeck::akp05::MainDisplayHeightPx);

    auto const& writes = fx.transport->writes();

    // Sequence: header + N chunks + ULEND commit sentinel.
    REQUIRE(writes.size() >= 2);

    auto const& header = writes.front();
    REQUIRE(header.size() == streamdeck::akp05::PacketSize);

    // First packet MUST start with "CRT LOG".
    CHECK(header[0] == 0x43); // C
    CHECK(header[1] == 0x52); // R
    CHECK(header[2] == 0x54); // T
    CHECK(header[5] == 0x4c); // L
    CHECK(header[6] == 0x4f); // O
    CHECK(header[7] == 0x47); // G

    // Header MUST carry a non-zero JPEG size at bytes 10..11 (BE16). The
    // exact size depends on the JPEG encoder output but it has to be > 0.
    std::uint16_t const jpegSize =
        static_cast<std::uint16_t>((static_cast<std::uint16_t>(header[10]) << 8) | header[11]);
    CHECK(jpegSize > 0);

    // Every chunk between the header and the ULEND tail must be 512 bytes
    // (zero-padded by the device backend's chunk loop).
    REQUIRE(writes.size() >= 3); // header + >= 1 chunk + ULEND
    for (std::size_t i = 1; i + 1 < writes.size(); ++i) {
        CAPTURE(i);
        CHECK(writes[i].size() == streamdeck::akp05::PacketSize);
    }

    // The ULEND commit sentinel ends the sequence per akp05_vendor.md
    // sec 3 row 193 + roadmap sec 11.3. Bytes 5..9 = "ULEND".
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

TEST_CASE("akp05 buildLogoSizeHeader is distinct from key/encoder/main headers",
          "[akp05][boot-logo][protocol]") {
    // Pitfall guard: ensure CmdLogo ("LOG") does not collide with any of the
    // existing 3-byte image opcodes (BAT / ENC / MAI) at the command-word
    // slot (bytes 5..7).
    auto const logo = streamdeck::akp05::buildLogoSizeHeader(0x100);
    auto const key = streamdeck::akp05::buildKeyImageHeader(1, 0x100);
    auto const enc = streamdeck::akp05::buildEncoderImageHeader(0, 0x100);
    auto const main = streamdeck::akp05::buildMainImageHeader(0x100);

    // Command words must differ at bytes 5..7.
    CHECK(logo[5] != key[5]);
    CHECK(logo[5] != enc[5]);
    CHECK(logo[5] != main[5]);

    // Size field encoding must match the key/main header convention so the
    // chunked-upload pipeline stays generic across all image surfaces.
    CHECK(logo[10] == key[10]);
    CHECK(logo[11] == key[11]);
}
