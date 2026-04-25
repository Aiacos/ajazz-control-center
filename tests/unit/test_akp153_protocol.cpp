// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_akp153_protocol.cpp
 * @brief Unit tests for the AKP153 StreamDeck protocol packet builders and
 *        input-report parser.
 *
 * Covers brightness (including clamp), clear-all, clear-key (1-based index),
 * image header encoding, ACK rejection, key-index decoding, and short-frame
 * rejection.
 */
#include "akp153_protocol.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace ajazz::streamdeck::akp153;

/// buildSetBrightness() must produce a 'CRT'+'LIG' packet with the value at byte 10.
TEST_CASE("set brightness packet matches spec", "[akp153][protocol]") {
    auto const pkt = buildSetBrightness(75);
    REQUIRE(pkt.size() == PacketSize);
    REQUIRE(pkt[0] == 0x43); // C
    REQUIRE(pkt[1] == 0x52); // R
    REQUIRE(pkt[2] == 0x54); // T
    REQUIRE(pkt[5] == 0x4c); // L
    REQUIRE(pkt[6] == 0x49); // I
    REQUIRE(pkt[7] == 0x47); // G
    REQUIRE(pkt[10] == 75);
}

/// Values > 100 must be clamped to 100 before writing.
TEST_CASE("brightness is clamped to 100", "[akp153][protocol]") {
    auto const pkt = buildSetBrightness(200);
    REQUIRE(pkt[10] == 100);
}

/// buildClearAll() must emit the 'CLE' command tag and 0xFF sentinel at byte 11.
TEST_CASE("clear-all frame encodes 0xff", "[akp153][protocol]") {
    auto const pkt = buildClearAll();
    REQUIRE(pkt[5] == 0x43);
    REQUIRE(pkt[6] == 0x4c);
    REQUIRE(pkt[7] == 0x45);
    REQUIRE(pkt[10] == 0x00);
    REQUIRE(pkt[11] == 0xff);
}

/// buildClearKey(n) encodes the raw 1-based index at byte 11.
TEST_CASE("clear single key uses 1-based index", "[akp153][protocol]") {
    auto const pkt = buildClearKey(7);
    REQUIRE(pkt[11] == 7);
}

/// buildImageHeader() must place the 'BAT' command word, big-endian payload size, and key id.
TEST_CASE("image header encodes big-endian size and key id", "[akp153][protocol]") {
    auto const pkt = buildImageHeader(4, 0x1234);
    REQUIRE(pkt[5] == 0x42);
    REQUIRE(pkt[6] == 0x41);
    REQUIRE(pkt[7] == 0x54);
    REQUIRE(pkt[10] == 0x12);
    REQUIRE(pkt[11] == 0x34);
    REQUIRE(pkt[12] == 4);
}

/// Frames containing 'ACK'+'OK' must be silently discarded.
TEST_CASE("parser rejects ACK frames", "[akp153][protocol]") {
    std::array<std::uint8_t, 16> frame{'A', 'C', 'K', 0, 0, 'O', 'K', 0, 0, 0, 0, 0, 0, 0, 0, 0};
    auto const ev = parseInputReport(frame);
    REQUIRE(!ev.has_value());
}

/// Valid frames must decode the key index from byte 9 into the keyIndex field.
TEST_CASE("parser decodes key index from byte 9", "[akp153][protocol]") {
    std::array<std::uint8_t, 16> frame{};
    frame[9] = 5;
    auto const ev = parseInputReport(frame);
    REQUIRE(ev.has_value());
    REQUIRE(ev->keyIndex == 5);
}

/// Frames shorter than 16 bytes must return an empty optional.
TEST_CASE("parser rejects frames shorter than 16 bytes", "[akp153][protocol]") {
    std::array<std::uint8_t, 8> frame{};
    auto const ev = parseInputReport(frame);
    REQUIRE(!ev.has_value());
}
