// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_akp05_input_corrections.cpp
 * @brief Executable companion to docs/protocols/streamdeck/akp05_input_corrections.md.
 *
 * Two groups:
 *
 *  - GROUP A "[akp05][vendor-re]" — invariants the vendor binary (and, for VER,
 *    live hardware) CONFIRMED. These pass against the current code and guard it
 *    against regression.
 *
 *  - GROUP B "[!shouldfail]" — the encoder/touch decoding DEFECTS the RE proved.
 *    Each is tagged Catch2 `[!shouldfail]`, so it is GREEN in CI *because the
 *    failure is expected* (the current parser is wrong). When `parseInputReport`
 *    is reworked per the note, these will start passing -> Catch2 reports an
 *    "unexpected success" (red) -> drop the `[!shouldfail]` tag and keep them as
 *    normal regression tests. They assert only RE-CONFIRMED *structural*
 *    invariants (no rotation-magnitude byte; touch X is a single byte at offset
 *    10) that do NOT depend on the still-PROVISIONAL exact AKP05E key codes.
 *
 * RE evidence (clean-room):
 *  - SDDevice::readDataFromHidDevice @0x180021280 emits
 *    sigHandleKeyEvents(keyCode=report[9], state=report[10]); ACK at report[0..2].
 *  - SDActionCanvasWidget::handleKeyEvents @0x1400d02b0: encoder direction is a
 *    distinct keyCode -> KnobClockwiseRotation / KnobCounterclockwiseRotation /
 *    KnobPressed (no magnitude byte); touch X is the single `state` arg passed to
 *    SDActionTouchBarWidget::getTouchbarLoactionFromX(state).
 *  - SDDevice::sendGetHardwareFirmwareVersion @0x180023440: VER packet =
 *    [0]=reportId [1..3]="CRT" [6..8]="VER".
 */
#include "akp05_protocol.hpp"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <string>

#include <catch2/catch_test_macros.hpp>

using namespace ajazz::streamdeck::akp05;

namespace {
// The exact 512-byte CRT VER response captured from a physical AKP05E
// (0300:3004) on 2026-05-27 via GET_REPORT: leading 0x00 report-id byte, then
// the ASCII firmware string, then zero padding. (See ver_probe.py session.)
std::array<std::uint8_t, 17> makeRealVerResponsePrefix() {
    return {0x00, 0x56, 0x33, 0x2e, 0x41, 0x4b, 0x50, 0x30, 0x35,
            0x45, 0x2e, 0x30, 0x31, 0x2e, 0x30, 0x30, 0x37}; // "\0V3.AKP05E.01.007"
}
} // namespace

// ===========================================================================
// GROUP A — CONFIRMED invariants (must pass against current code)
// ===========================================================================

TEST_CASE("akp05 input: ACK frames are discarded (report[0..2]=='ACK')",
          "[akp05][vendor-re][input]") {
    std::array<std::uint8_t, 16> frame{'A', 'C', 'K'};
    REQUIRE_FALSE(parseInputReport(frame).has_value());
}

TEST_CASE("akp05 input: reports shorter than 16 bytes are rejected",
          "[akp05][vendor-re][input]") {
    std::array<std::uint8_t, 8> tooShort{};
    REQUIRE_FALSE(parseInputReport(tooShort).has_value());
}

TEST_CASE("akp05 input: key code lives at report[9], edge at report[10]",
          "[akp05][vendor-re][input]") {
    // Confirmed: readDataFromHidDevice forwards report[9] (code) + report[10]
    // (state) to handleKeyEvents; for keys the state is the press/release edge.
    for (std::uint8_t key : {std::uint8_t{1}, std::uint8_t{KeyCount}}) {
        std::array<std::uint8_t, 16> down{};
        down[9] = key;
        down[10] = 0x01;
        auto const pressed = parseInputReport(down);
        REQUIRE(pressed.has_value());
        REQUIRE(pressed->kind == InputEvent::Kind::KeyPressed);
        REQUIRE(pressed->index == key);

        std::array<std::uint8_t, 16> up{};
        up[9] = key;
        up[10] = 0x00;
        auto const released = parseInputReport(up);
        REQUIRE(released.has_value());
        REQUIRE(released->kind == InputEvent::Kind::KeyReleased);
        REQUIRE(released->index == key);
    }
}

TEST_CASE("akp05 input: VER request is reportId + 'CRT' + 'VER' on the wire",
          "[akp05][vendor-re][input]") {
    // Vendor sendGetHardwareFirmwareVersion builds buf[_packetSize+1] with
    // buf[0]=reportId, buf[1..3]="CRT", buf[6..8]="VER". Our builder produces the
    // packet CONTENT (no report-id); the transport prepends 0x00. Simulate that.
    auto const content = buildVersionRequest();
    std::array<std::uint8_t, PacketSize + 1> wire{};
    wire[0] = 0x00; // hidraw report-id (device has no numbered reports)
    for (std::size_t i = 0; i < PacketSize; ++i) {
        wire[i + 1] = content[i];
    }
    REQUIRE(wire[0] == 0x00);
    REQUIRE(wire[1] == 'C');
    REQUIRE(wire[2] == 'R');
    REQUIRE(wire[3] == 'T');
    REQUIRE(wire[4] == 0x00);
    REQUIRE(wire[5] == 0x00);
    REQUIRE(wire[6] == 'V');
    REQUIRE(wire[7] == 'E');
    REQUIRE(wire[8] == 'R');
}

TEST_CASE("akp05 input: parseVersionResponse decodes the live AKP05E firmware string",
          "[akp05][vendor-re][input]") {
    // Real bytes captured from the device (GET_REPORT), zero-padded to 512.
    std::array<std::uint8_t, 512> resp{};
    auto const prefix = makeRealVerResponsePrefix();
    for (std::size_t i = 0; i < prefix.size(); ++i) {
        resp[i] = prefix[i];
    }
    auto const ver = parseVersionResponse(resp);
    REQUIRE(ver.has_value());
    REQUIRE(*ver == "V3.AKP05E.01.007");
}

// ===========================================================================
// GROUP B — encoder/touch DEFECTS (expected-fail until parseInputReport rework)
// ===========================================================================

TEST_CASE("akp05 input: encoder rotation has NO magnitude byte (always +/-1 step)",
          "[akp05][input-rework][!shouldfail]") {
    // RE (handleKeyEvents): a rotation report = exactly one detent in a fixed
    // direction encoded by the keyCode itself (KnobClockwise/Counterclockwise);
    // there is NO signed-delta byte. The current parser wrongly reads report[10]
    // as an int8 magnitude, so feeding 5 yields value==5. The corrected parser
    // must yield a unit step regardless of report[10].
    std::array<std::uint8_t, 16> frame{};
    frame[9] = 0x20;  // (current code's encoder tag; the real AKP05E codes are TBD)
    frame[10] = 5;    // a value the current code mis-reads as a magnitude of 5
    auto const ev = parseInputReport(frame);
    REQUIRE(ev.has_value());
    REQUIRE(ev->kind == InputEvent::Kind::EncoderTurned);
    REQUIRE(std::abs(static_cast<int>(ev->value)) == 1); // FAILS today (value==5)
}

TEST_CASE("akp05 input: touch X is a single byte at report[10], not BE16 at [10..11]",
          "[akp05][input-rework][!shouldfail]") {
    // RE (handleKeyEvents): touch X is the single `state` arg (report[10]) passed
    // to SDActionTouchBarWidget::getTouchbarLoactionFromX(state) -> 0..255. The
    // current parser reads a big-endian 16-bit X from report[10..11] (0..639),
    // which a one-byte field cannot carry. The corrected parser must take X from
    // report[10] only and never exceed 0xFF.
    std::array<std::uint8_t, 16> frame{};
    frame[9] = 0x30;   // (current code's touch tag; real AKP05E codes are TBD)
    frame[10] = 0x01;  // corrected X == 1
    frame[11] = 0x40;  // current code folds this in -> X == 0x0140 == 320
    auto const ev = parseInputReport(frame);
    REQUIRE(ev.has_value());
    REQUIRE(ev->value == frame[10]);          // FAILS today (value==320)
    REQUIRE(ev->value <= 0xFF);               // FAILS today (320 > 255)
}
