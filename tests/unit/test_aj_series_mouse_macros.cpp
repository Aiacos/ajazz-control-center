// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_aj_series_mouse_macros.cpp
 * @brief Byte-level tests for the AJ-series mouse macro upload capability
 *        (IMouseMacroCapable, opcode 0x16 / FEA_CMD_SET_MACRO_SIMPLE).
 *
 * Wires MockTransport through the existing makeAjSeriesWithTransport DI
 * seam (CAPTURE-04), drives uploadMacro(), and asserts the exact byte
 * layout per docs/protocols/mouse/aj_series_opcode_table.md sec 3.11.
 *
 * Critical coverage:
 *   - Header packet shape: pkt[1]=0x16, pkt[2]=slot, pkt[3]=chunkIdx,
 *     pkt[4]=lastNonZeroPos, pkt[5]=isFinal, payload at pkt[9..].
 *   - Event encoding: KeyDown sets bit 7 of HID usage byte; KeyUp clears
 *     it; short Delay (<=127) emits 1 byte with bit 7 clear; long Delay
 *     (>127) emits uint16-LE.
 *   - Empty events sequence still emits one header chunk (firmware sees
 *     a "macro cleared" intent) with isFinal=true.
 *   - Multi-chunk: a macro big enough to span 2-3 chunks. chunkIdx is
 *     monotonically increasing 0..N-1, isFinal is set ONLY on the last.
 *     lastNonZeroPos is identical on every chunk.
 *   - Out-of-range slot clamps to macroSlotCount-1 (= 19).
 *   - Round-trip: encodeMouseMacro of [KeyDown(0x04), Delay(100), KeyUp(0x04)]
 *     produces the documented byte stream.
 *   - BIT7 checksum independently recomputed and validated on every emitted
 *     packet (no test calls into stampBit7Checksum to verify it).
 */
#include "aj_series_protocol.hpp"
#include "ajazz/core/capabilities.hpp"
#include "ajazz/core/device.hpp"
#include "ajazz/mouse/mouse.hpp"
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
using namespace ajazz::mouse::aj_series;
using core::MouseMacroEvent;

namespace {

core::DeviceDescriptor makeDescriptor() {
    core::DeviceDescriptor d{};
    d.vendorId = 0x3151;
    d.productId = 0x5008;
    d.family = core::DeviceFamily::Mouse;
    d.model = "AJAZZ AJ159 APEX (test)";
    d.codename = "aj159_apex";
    d.dpiStageCount = 8;
    return d;
}

core::DeviceId makeId() {
    core::DeviceId id{};
    id.vendorId = 0x3151;
    id.productId = 0x5008;
    id.serial = "TEST-MACRO";
    return id;
}

/// Independent BIT7 checksum verifier - sums pkt[1..63] & 0x7F. Mirrors
/// aj_series_protocol.cpp's stampBit7Checksum so the test does not call
/// through to the impl being verified.
[[nodiscard]] std::uint8_t expectedBit7Checksum(std::vector<std::uint8_t> const& pkt) {
    auto const sum = std::accumulate(pkt.begin() + 1, pkt.end() - 1, std::uint32_t{0});
    return static_cast<std::uint8_t>(sum & 0x7fu);
}

[[nodiscard]] std::uint8_t expectedBit7Checksum(std::array<std::uint8_t, kReportSize> const& pkt) {
    auto const sum = std::accumulate(pkt.begin() + 1, pkt.end() - 1, std::uint32_t{0});
    return static_cast<std::uint8_t>(sum & 0x7fu);
}

struct Fixture {
    core::DevicePtr device;
    tests::MockTransport* transport;
};

Fixture buildFixture() {
    auto owned = std::make_unique<tests::MockTransport>();
    auto* observer = owned.get();
    owned->open();
    auto dev = mouse::makeAjSeriesWithTransport(makeDescriptor(), makeId(), std::move(owned));
    return Fixture{std::move(dev), observer};
}

/// KeyDown event helper.
MouseMacroEvent kd(std::uint16_t hid) {
    return MouseMacroEvent{MouseMacroEvent::Kind::KeyDown, hid};
}

/// KeyUp event helper.
MouseMacroEvent ku(std::uint16_t hid) {
    return MouseMacroEvent{MouseMacroEvent::Kind::KeyUp, hid};
}

/// Delay event helper.
MouseMacroEvent dl(std::uint16_t ms) {
    return MouseMacroEvent{MouseMacroEvent::Kind::Delay, ms};
}

} // namespace

// ===========================================================================
// IMouseMacroCapable - opcode 0x16 FEA_CMD_SET_MACRO_SIMPLE, vendor sec 3.11
// ===========================================================================

TEST_CASE("AJ-series exposes IMouseMacroCapable with 20 macro slots per vendor",
          "[aj_series][macro]") {
    auto fx = buildFixture();
    auto* mac = dynamic_cast<core::IMouseMacroCapable*>(fx.device.get());
    REQUIRE(mac != nullptr);
    // Per sec 3.11 line 488 "byte 1 : macro slot idx (0..19)".
    CHECK(mac->macroSlotCount() == 20);
}

// ---------------------------------------------------------------------------
// encodeMouseMacro byte-stream encoder (sec 3.11 lines 504..513)
// ---------------------------------------------------------------------------

TEST_CASE("AJ-series encodeMouseMacro emits repeatCount header bytes 0..1",
          "[aj_series][macro][encode]") {
    // Empty event list: encoder still emits 2-byte uint16-LE repeatCount.
    std::vector<MouseMacroEvent> const events{};
    auto const bytes = encodeMouseMacro(events);
    REQUIRE(bytes.size() == 2);
    // repeatCount = 1 (we hard-code 1 since the capability does not expose loops).
    CHECK(bytes[0] == 0x01);
    CHECK(bytes[1] == 0x00);
}

TEST_CASE("AJ-series encodeMouseMacro round-trips KeyDown 0x04 Delay 100 KeyUp 0x04",
          "[aj_series][macro][encode]") {
    // The documented example sequence from the prompt. Per sec 3.11:
    //   KeyDown(0x04 = HID 'A') -> byte = (0x04 | 0x80) = 0x84
    //   Delay(100)              -> 100 <= 127 so 1 byte, bit 7 clear = 0x64
    //   KeyUp(0x04)             -> byte = (0x04 & 0x7F) = 0x04
    std::vector<MouseMacroEvent> const events{
        kd(0x04),
        dl(100),
        ku(0x04),
    };
    auto const bytes = encodeMouseMacro(events);
    REQUIRE(bytes.size() == 5);
    CHECK(bytes[0] == 0x01); // repeatCount LO
    CHECK(bytes[1] == 0x00); // repeatCount HI
    CHECK(bytes[2] == 0x84); // KeyDown(0x04) | 0x80
    CHECK(bytes[3] == 0x64); // Delay(100)
    CHECK(bytes[4] == 0x04); // KeyUp(0x04)
}

TEST_CASE("AJ-series encodeMouseMacro emits 1-byte delay for ms <= 127",
          "[aj_series][macro][encode]") {
    // Boundary check at 127 (max single-byte delay) and 0 (minimum).
    std::vector<MouseMacroEvent> const events{dl(0), dl(1), dl(127)};
    auto const bytes = encodeMouseMacro(events);
    REQUIRE(bytes.size() == 5); // 2 header + 3 delay bytes
    CHECK(bytes[2] == 0x00);
    CHECK(bytes[3] == 0x01);
    CHECK(bytes[4] == 0x7F);
}

TEST_CASE("AJ-series encodeMouseMacro emits uint16-LE for delay > 127",
          "[aj_series][macro][encode]") {
    // Per sec 3.11 line 509 "if delay > 127: uint16-LE for longer delays".
    // 128 (0x0080), 1000 (0x03E8), 65535 (0xFFFF) - boundary + typical + max.
    std::vector<MouseMacroEvent> const events{dl(128), dl(1000), dl(65535)};
    auto const bytes = encodeMouseMacro(events);
    REQUIRE(bytes.size() == 2 + 3 * 2);
    // 128 = 0x0080 LE -> 0x80, 0x00
    CHECK(bytes[2] == 0x80);
    CHECK(bytes[3] == 0x00);
    // 1000 = 0x03E8 LE -> 0xE8, 0x03
    CHECK(bytes[4] == 0xE8);
    CHECK(bytes[5] == 0x03);
    // 65535 = 0xFFFF -> 0xFF, 0xFF
    CHECK(bytes[6] == 0xFF);
    CHECK(bytes[7] == 0xFF);
}

TEST_CASE("AJ-series encodeMouseMacro pins KeyDown bit7-set and KeyUp bit7-clear",
          "[aj_series][macro][encode]") {
    // Sec 3.11 line 510: "byte = HID usage; bit7 = down flag".
    // Test boundary HID usages 0x00, 0x01, 0x7F to confirm the bit-7 split.
    std::vector<MouseMacroEvent> const events{
        kd(0x00),
        ku(0x00),
        kd(0x01),
        ku(0x01),
        kd(0x7F),
        ku(0x7F),
    };
    auto const bytes = encodeMouseMacro(events);
    REQUIRE(bytes.size() == 2 + 6);
    CHECK(bytes[2] == 0x80); // KeyDown(0x00) -> 0x00 | 0x80 = 0x80
    CHECK(bytes[3] == 0x00); // KeyUp(0x00)   -> 0x00 & 0x7F = 0x00
    CHECK(bytes[4] == 0x81); // KeyDown(0x01) -> 0x81
    CHECK(bytes[5] == 0x01); // KeyUp(0x01)   -> 0x01
    CHECK(bytes[6] == 0xFF); // KeyDown(0x7F) -> 0xFF
    CHECK(bytes[7] == 0x7F); // KeyUp(0x7F)   -> 0x7F
}

// ---------------------------------------------------------------------------
// buildMacroHeader / buildMacroChunk packet shape (sec 3.11 lines 487..496)
// ---------------------------------------------------------------------------

TEST_CASE("AJ-series buildMacroHeader pins opcode 0x16 + slot + chunkIdx 0",
          "[aj_series][macro][wire]") {
    std::array<std::uint8_t, 4> const payload{0xDE, 0xAD, 0xBE, 0xEF};
    auto const pkt = buildMacroHeader(/*slot=*/7,
                                      /*lastNonZeroPos=*/3,
                                      /*isFinal=*/true,
                                      std::span<std::uint8_t const>(payload));
    REQUIRE(pkt[0] == kReportId);
    REQUIRE(pkt[1] == static_cast<std::uint8_t>(FeaCmd::SetMacroSimple)); // 0x16
    CHECK(pkt[2] == 7);                                                   // slot
    CHECK(pkt[3] == 0); // chunkIdx (header = chunk 0)
    CHECK(pkt[4] == 3); // lastNonZeroPos
    CHECK(pkt[5] == 1); // isFinal
    // Reserved bytes 6..8 (vendor bytes 5..7) stay zero.
    CHECK(pkt[6] == 0);
    CHECK(pkt[7] == 0);
    CHECK(pkt[8] == 0);
    // Payload at pkt[9..12].
    CHECK(pkt[9] == 0xDE);
    CHECK(pkt[10] == 0xAD);
    CHECK(pkt[11] == 0xBE);
    CHECK(pkt[12] == 0xEF);
    CHECK(pkt[kReportSize - 1] == expectedBit7Checksum(pkt));
}

TEST_CASE("AJ-series buildMacroChunk pins monotonic chunkIdx + isFinal",
          "[aj_series][macro][wire]") {
    std::array<std::uint8_t, 4> const payload{0x01, 0x02, 0x03, 0x04};
    auto const pkt = buildMacroChunk(/*slot=*/3,
                                     /*chunkIdx=*/4,
                                     /*lastNonZeroPos=*/200,
                                     /*isFinal=*/false,
                                     std::span<std::uint8_t const>(payload));
    REQUIRE(pkt[1] == static_cast<std::uint8_t>(FeaCmd::SetMacroSimple)); // 0x16
    CHECK(pkt[2] == 3);                                                   // slot
    CHECK(pkt[3] == 4);                                                   // chunkIdx
    CHECK(pkt[4] == 200); // lastNonZeroPos (same as header)
    CHECK(pkt[5] == 0);   // isFinal=false
}

TEST_CASE("AJ-series buildMacroHeader clamps slot >= 20 to 19", "[aj_series][macro][clamp]") {
    auto const pkt = buildMacroHeader(/*slot=*/99, 0, true, {});
    CHECK(pkt[2] == 19); // clamped from 99 to macroSlotCount-1
}

// ---------------------------------------------------------------------------
// AjSeriesMouse::uploadMacro end-to-end via MockTransport
// ---------------------------------------------------------------------------

TEST_CASE("AJ-series uploadMacro empty events still emits one header chunk",
          "[aj_series][macro][wire]") {
    // Design choice: empty events still emits a single header chunk with
    // isFinal=true so the firmware sees a "macro cleared" intent rather
    // than silently no-op'ing. uploadMacro() returns true on success.
    auto fx = buildFixture();
    auto* mac = dynamic_cast<core::IMouseMacroCapable*>(fx.device.get());
    REQUIRE(mac != nullptr);

    REQUIRE(mac->uploadMacro(/*slot=*/0, {}));
    auto const& writes = fx.transport->writes();
    REQUIRE(writes.size() == 1);
    auto const& pkt = writes.back();
    REQUIRE(pkt.size() == kReportSize);

    CHECK(pkt[1] == static_cast<std::uint8_t>(FeaCmd::SetMacroSimple)); // 0x16
    CHECK(pkt[2] == 0);                                                 // slot
    CHECK(pkt[3] == 0);                                                 // chunkIdx
    CHECK(pkt[5] == 1);                                                 // isFinal
    CHECK(pkt[kReportSize - 1] == expectedBit7Checksum(pkt));
}

TEST_CASE("AJ-series uploadMacro single-chunk small macro emits 1 packet",
          "[aj_series][macro][wire]") {
    // 3-event macro encodes to 2 (header) + 3 (events) = 5 bytes - fits in
    // one 56-byte chunk so we expect exactly 1 wire packet with isFinal=1.
    auto fx = buildFixture();
    auto* mac = dynamic_cast<core::IMouseMacroCapable*>(fx.device.get());
    REQUIRE(mac != nullptr);

    std::vector<MouseMacroEvent> const events{kd(0x04), dl(100), ku(0x04)};
    REQUIRE(mac->uploadMacro(/*slot=*/2, events));
    auto const& writes = fx.transport->writes();
    REQUIRE(writes.size() == 1);
    auto const& pkt = writes.back();

    CHECK(pkt[1] == static_cast<std::uint8_t>(FeaCmd::SetMacroSimple)); // 0x16
    CHECK(pkt[2] == 2);                                                 // slot
    CHECK(pkt[3] == 0);                                                 // chunkIdx
    CHECK(pkt[5] == 1);                                                 // isFinal
    // Encoded payload at pkt[9..13] (matches the encode round-trip test).
    CHECK(pkt[9] == 0x01);  // repeatCount LO
    CHECK(pkt[10] == 0x00); // repeatCount HI
    CHECK(pkt[11] == 0x84); // KeyDown(0x04) | 0x80
    CHECK(pkt[12] == 0x64); // Delay(100)
    CHECK(pkt[13] == 0x04); // KeyUp(0x04)
    // lastNonZeroPos = position of last non-zero byte (0-based) in payload.
    // Bytes: [0x01, 0x00, 0x84, 0x64, 0x04] - last non-zero at index 4.
    CHECK(pkt[4] == 4);
    CHECK(pkt[kReportSize - 1] == expectedBit7Checksum(pkt));
}

TEST_CASE("AJ-series uploadMacro multi-chunk macro spans 2 chunks with monotonic idx",
          "[aj_series][macro][wire]") {
    // Build a macro that encodes to > 56 bytes so it spans 2 chunks. 60
    // KeyDown events @ 1 byte each + 2 header bytes = 62 bytes > 56 chunk.
    auto fx = buildFixture();
    auto* mac = dynamic_cast<core::IMouseMacroCapable*>(fx.device.get());
    REQUIRE(mac != nullptr);

    std::vector<MouseMacroEvent> events;
    for (int i = 0; i < 60; ++i) {
        events.push_back(kd(static_cast<std::uint16_t>(0x04 + (i % 10))));
    }
    REQUIRE(mac->uploadMacro(/*slot=*/5, events));
    auto const& writes = fx.transport->writes();
    REQUIRE(writes.size() == 2); // 62 bytes / 56 = 2 chunks

    // Header chunk: chunkIdx 0, isFinal 0.
    auto const& hdr = writes[0];
    CHECK(hdr[1] == static_cast<std::uint8_t>(FeaCmd::SetMacroSimple));
    CHECK(hdr[2] == 5); // slot
    CHECK(hdr[3] == 0); // chunkIdx
    CHECK(hdr[5] == 0); // isFinal=false (first of two)
    // Continuation chunk: chunkIdx 1, isFinal 1.
    auto const& cont = writes[1];
    CHECK(cont[1] == static_cast<std::uint8_t>(FeaCmd::SetMacroSimple));
    CHECK(cont[2] == 5); // slot
    CHECK(cont[3] == 1); // chunkIdx
    CHECK(cont[5] == 1); // isFinal=true (last)
    // lastNonZeroPos identical on both chunks (per sec 3.11 wire format
    // - firmware reassembles by chunkIdx but sees the same end marker).
    CHECK(hdr[4] == cont[4]);
    CHECK(hdr[kReportSize - 1] == expectedBit7Checksum(hdr));
    CHECK(cont[kReportSize - 1] == expectedBit7Checksum(cont));
}

TEST_CASE("AJ-series uploadMacro large macro spans 3 chunks with monotonic idx",
          "[aj_series][macro][wire]") {
    // Build a macro that encodes to > 112 bytes so it spans 3 chunks.
    // 120 KeyDown events + 2 header bytes = 122 bytes > 56 + 56 = 112.
    auto fx = buildFixture();
    auto* mac = dynamic_cast<core::IMouseMacroCapable*>(fx.device.get());
    REQUIRE(mac != nullptr);

    std::vector<MouseMacroEvent> events;
    for (int i = 0; i < 120; ++i) {
        events.push_back(kd(static_cast<std::uint16_t>(0x04 + (i % 10))));
    }
    REQUIRE(mac->uploadMacro(/*slot=*/0, events));
    auto const& writes = fx.transport->writes();
    REQUIRE(writes.size() == 3);

    for (std::size_t i = 0; i < writes.size(); ++i) {
        CAPTURE(i);
        auto const& pkt = writes[i];
        CHECK(pkt[1] == static_cast<std::uint8_t>(FeaCmd::SetMacroSimple));
        CHECK(pkt[3] == static_cast<std::uint8_t>(i)); // chunkIdx monotonic
        bool const isLast = (i + 1 == writes.size());
        CHECK(pkt[5] == (isLast ? 1u : 0u));
        CHECK(pkt[kReportSize - 1] == expectedBit7Checksum(pkt));
    }
}

TEST_CASE("AJ-series uploadMacro clamps out-of-range slot to macroSlotCount-1",
          "[aj_series][macro][clamp]") {
    // Per sec 3.11 line 488 slot range is 0..19; out-of-range callers
    // clamp to 19 rather than corrupting an unrelated firmware slot.
    auto fx = buildFixture();
    auto* mac = dynamic_cast<core::IMouseMacroCapable*>(fx.device.get());
    REQUIRE(mac != nullptr);

    std::vector<MouseMacroEvent> const events{kd(0x04), ku(0x04)};
    REQUIRE(mac->uploadMacro(/*slot=*/99, events));
    auto const& pkt = fx.transport->writes().back();
    CHECK(pkt[2] == 19); // clamped from 99
}
