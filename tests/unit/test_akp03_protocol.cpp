// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_akp03_protocol.cpp
 * @brief Unit tests for the AKP03 StreamDeck protocol packet builders and
 *        input-report parser.
 *
 * Verifies that every command builder produces the correct wire bytes and
 * that parseInputReport() correctly classifies key presses, key releases,
 * encoder CW/CCW turns, encoder press/release, and invalid/short frames.
 */
#include "akp03_protocol.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace ajazz::streamdeck::akp03;

/// Verify that buildSetBrightness() produces a packet with the 'CRT' prefix and 'LIG' command tag.
TEST_CASE("akp03 brightness packet has CRT prefix and LIG command", "[akp03][protocol]") {
    auto const pkt = buildSetBrightness(60);
    REQUIRE(pkt.size() == PacketSize);
    REQUIRE(pkt[0] == 0x43); // C
    REQUIRE(pkt[1] == 0x52); // R
    REQUIRE(pkt[2] == 0x54); // T
    REQUIRE(pkt[5] == 0x4c); // L
    REQUIRE(pkt[6] == 0x49); // I
    REQUIRE(pkt[7] == 0x47); // G
    REQUIRE(pkt[10] == 60);
}

/// Values above 100 must be clamped to 100 before encoding.
TEST_CASE("akp03 brightness clamps to 100", "[akp03][protocol]") {
    auto const pkt = buildSetBrightness(250);
    REQUIRE(pkt[10] == 100);
}

/// buildClearAll() must write the CLE command tag and 0xFF as the key-index sentinel.
TEST_CASE("akp03 clear-all encodes 0xff", "[akp03][protocol]") {
    auto const pkt = buildClearAll();
    REQUIRE(pkt[5] == 0x43);
    REQUIRE(pkt[6] == 0x4c);
    REQUIRE(pkt[7] == 0x45);
    REQUIRE(pkt[10] == 0x00);
    REQUIRE(pkt[11] == 0xff);
}

/// buildClearKey(n) must write the 1-based key index at byte 11.
TEST_CASE("akp03 clear-key uses 1-based key index", "[akp03][protocol]") {
    auto const pkt = buildClearKey(3);
    REQUIRE(pkt[10] == 0x00);
    REQUIRE(pkt[11] == 3);
}

/// buildImageHeader() must embed the 'PNG' command word, big-endian payload size, and key id.
TEST_CASE("akp03 image header encodes PNG word and size", "[akp03][protocol]") {
    auto const pkt = buildImageHeader(4, 0x1234);
    REQUIRE(pkt[5] == 0x50); // P
    REQUIRE(pkt[6] == 0x4e); // N
    REQUIRE(pkt[7] == 0x47); // G
    REQUIRE(pkt[10] == 0x12);
    REQUIRE(pkt[11] == 0x34);
    REQUIRE(pkt[12] == 4);
}

/// Frames starting with 'ACK' are device acknowledgements and must not produce events.
TEST_CASE("akp03 parser rejects ACK frames", "[akp03][protocol]") {
    std::array<std::uint8_t, 16> frame{'A', 'C', 'K', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    REQUIRE(!parseInputReport(frame).has_value());
}

/// A frame with byte 10 == 1 must decode as KeyPressed with the index from byte 9.
TEST_CASE("akp03 parser decodes key press", "[akp03][protocol]") {
    std::array<std::uint8_t, 16> frame{};
    frame[9] = 2;  // key index
    frame[10] = 1; // pressed
    auto const ev = parseInputReport(frame);
    REQUIRE(ev.has_value());
    REQUIRE(ev->kind == InputEvent::Kind::KeyPressed);
    REQUIRE(ev->index == 2);
}

/// A frame with byte 10 == 0 must decode as KeyReleased.
TEST_CASE("akp03 parser decodes key release", "[akp03][protocol]") {
    std::array<std::uint8_t, 16> frame{};
    frame[9] = 4;
    frame[10] = 0;
    auto const ev = parseInputReport(frame);
    REQUIRE(ev.has_value());
    REQUIRE(ev->kind == InputEvent::Kind::KeyReleased);
    REQUIRE(ev->index == 4);
}

/// Encoder frames with byte 10 == 0x01 represent a +1 (clockwise) tick.
TEST_CASE("akp03 parser decodes encoder CW rotation", "[akp03][protocol]") {
    std::array<std::uint8_t, 16> frame{};
    frame[9] = 0x20;  // encoder 0
    frame[10] = 0x01; // +1 = CW
    auto const ev = parseInputReport(frame);
    REQUIRE(ev.has_value());
    REQUIRE(ev->kind == InputEvent::Kind::EncoderTurned);
    REQUIRE(ev->index == 0);
    REQUIRE(ev->delta == 1);
}

/// Encoder frames with byte 10 == 0xFF represent a -1 (counter-clockwise) tick.
TEST_CASE("akp03 parser decodes encoder CCW rotation", "[akp03][protocol]") {
    std::array<std::uint8_t, 16> frame{};
    frame[9] = 0x20;
    frame[10] = 0xff; // -1 = CCW
    auto const ev = parseInputReport(frame);
    REQUIRE(ev.has_value());
    REQUIRE(ev->kind == InputEvent::Kind::EncoderTurned);
    REQUIRE(ev->delta == -1);
}

/// Encoder press is signalled by byte 11 == 0x01 (delta 0); release by byte 11 == 0x00.
TEST_CASE("akp03 parser decodes encoder press/release", "[akp03][protocol]") {
    std::array<std::uint8_t, 16> frame{};
    frame[9] = 0x20;
    frame[10] = 0;    // no rotation
    frame[11] = 0x01; // press
    auto const press = parseInputReport(frame);
    REQUIRE(press.has_value());
    REQUIRE(press->kind == InputEvent::Kind::EncoderPressed);

    frame[11] = 0x00;
    auto const release = parseInputReport(frame);
    REQUIRE(release.has_value());
    REQUIRE(release->kind == InputEvent::Kind::EncoderReleased);
}

/// Frames shorter than the minimum required length must return an empty optional.
TEST_CASE("akp03 parser rejects short frames", "[akp03][protocol]") {
    std::array<std::uint8_t, 8> frame{};
    REQUIRE(!parseInputReport(frame).has_value());
}
