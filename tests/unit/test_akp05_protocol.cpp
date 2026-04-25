// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_akp05_protocol.cpp
 * @brief Unit tests for the AKP05 StreamDeck protocol packet builders and
 *        input-report parser.
 *
 * Covers brightness, image headers for keys / encoders / main display,
 * clear-all, ACK rejection, key press, encoder turn (all four indices),
 * out-of-range encoder ids, touch tap, swipe, and long-press events.
 */
#include "akp05_protocol.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace ajazz::streamdeck::akp05;

/// buildSetBrightness() must produce a 'CRT' prefixed packet with value at byte 10.
TEST_CASE("akp05 brightness packet has CRT prefix", "[akp05][protocol]") {
    auto const pkt = buildSetBrightness(50);
    REQUIRE(pkt.size() == PacketSize);
    REQUIRE(pkt[0] == 0x43);
    REQUIRE(pkt[1] == 0x52);
    REQUIRE(pkt[2] == 0x54);
    REQUIRE(pkt[10] == 50);
}

/// Key image header must use 'BAT' command word with big-endian size and key id.
TEST_CASE("akp05 key image header uses BAT command", "[akp05][protocol]") {
    auto const pkt = buildKeyImageHeader(8, 0xabcd);
    REQUIRE(pkt[5] == 0x42);
    REQUIRE(pkt[6] == 0x41);
    REQUIRE(pkt[7] == 0x54);
    REQUIRE(pkt[10] == 0xab);
    REQUIRE(pkt[11] == 0xcd);
    REQUIRE(pkt[12] == 8);
}

/// Encoder image header must use 'ENC' command word.
TEST_CASE("akp05 encoder image header uses ENC command", "[akp05][protocol]") {
    auto const pkt = buildEncoderImageHeader(2, 0x0100);
    REQUIRE(pkt[5] == 0x45);
    REQUIRE(pkt[6] == 0x4e);
    REQUIRE(pkt[7] == 0x43);
    REQUIRE(pkt[10] == 0x01);
    REQUIRE(pkt[11] == 0x00);
    REQUIRE(pkt[12] == 2);
}

/// Main display image header must use 'MAI' command word.
TEST_CASE("akp05 main image header uses MAI command", "[akp05][protocol]") {
    auto const pkt = buildMainImageHeader(0x0800);
    REQUIRE(pkt[5] == 0x4d);
    REQUIRE(pkt[6] == 0x41);
    REQUIRE(pkt[7] == 0x49);
    REQUIRE(pkt[10] == 0x08);
    REQUIRE(pkt[11] == 0x00);
}

/// buildClearAll() must set the key-index sentinel 0xFF at byte 11.
TEST_CASE("akp05 clear-all encodes 0xff", "[akp05][protocol]") {
    auto const pkt = buildClearAll();
    REQUIRE(pkt[11] == 0xff);
}

/// ACK frames must be rejected (return empty optional).
TEST_CASE("akp05 parser rejects ACK frames", "[akp05][protocol]") {
    std::array<std::uint8_t, 16> frame{'A', 'C', 'K'};
    REQUIRE(!parseInputReport(frame).has_value());
}

/// Key press frame: index from byte 9, pressed flag from byte 10.
TEST_CASE("akp05 parser decodes a key press", "[akp05][protocol]") {
    std::array<std::uint8_t, 16> frame{};
    frame[9] = 7;
    frame[10] = 1;
    auto const ev = parseInputReport(frame);
    REQUIRE(ev.has_value());
    REQUIRE(ev->kind == InputEvent::Kind::KeyPressed);
    REQUIRE(ev->index == 7);
}

/// All four encoder indices (0x20–0x23) must decode as EncoderTurned with correct index and value.
TEST_CASE("akp05 parser decodes all four encoders", "[akp05][protocol]") {
    for (std::uint8_t enc = 0; enc < 4; ++enc) {
        std::array<std::uint8_t, 16> frame{};
        frame[9] = static_cast<std::uint8_t>(0x20u | enc);
        frame[10] = 0x01;
        auto const ev = parseInputReport(frame);
        REQUIRE(ev.has_value());
        REQUIRE(ev->kind == InputEvent::Kind::EncoderTurned);
        REQUIRE(ev->index == enc);
        REQUIRE(ev->value == 1);
    }
}

/// Encoder ids ≥ 0x24 exceed the AKP05 encoder count and must return an empty optional.
TEST_CASE("akp05 parser rejects out-of-range encoder ids", "[akp05][protocol]") {
    std::array<std::uint8_t, 16> frame{};
    frame[9] = 0x24; // encoder 4 — AKP05 only has 0..3
    REQUIRE(!parseInputReport(frame).has_value());
}

/// Touch tap frame (0x30) must decode the 16-bit big-endian X coordinate from bytes 10–11.
TEST_CASE("akp05 parser decodes a touch tap with x coordinate", "[akp05][protocol]") {
    std::array<std::uint8_t, 16> frame{};
    frame[9] = 0x30; // tap
    frame[10] = 0x01;
    frame[11] = 0x40; // 0x0140 = 320
    auto const ev = parseInputReport(frame);
    REQUIRE(ev.has_value());
    REQUIRE(ev->kind == InputEvent::Kind::TouchTap);
    REQUIRE(ev->value == 320);
}

/// Touch swipe-left (0x31), swipe-right (0x32), and long-press (0x33) must each decode correctly.
TEST_CASE("akp05 parser decodes swipe and long-press", "[akp05][protocol]") {
    std::array<std::uint8_t, 16> frame{};
    frame[9] = 0x31; // swipe-left
    auto const left = parseInputReport(frame);
    REQUIRE(left.has_value());
    REQUIRE(left->kind == InputEvent::Kind::TouchSwipeLeft);

    frame[9] = 0x32; // swipe-right
    auto const right = parseInputReport(frame);
    REQUIRE(right.has_value());
    REQUIRE(right->kind == InputEvent::Kind::TouchSwipeRight);

    frame[9] = 0x33; // long-press
    auto const longp = parseInputReport(frame);
    REQUIRE(longp.has_value());
    REQUIRE(longp->kind == InputEvent::Kind::TouchLongPress);
}
