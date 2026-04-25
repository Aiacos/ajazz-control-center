// SPDX-License-Identifier: GPL-3.0-or-later
#include "akp05_protocol.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace ajazz::streamdeck::akp05;

TEST_CASE("akp05 brightness packet has CRT prefix", "[akp05][protocol]") {
    auto const pkt = buildSetBrightness(50);
    REQUIRE(pkt.size() == PacketSize);
    REQUIRE(pkt[0] == 0x43);
    REQUIRE(pkt[1] == 0x52);
    REQUIRE(pkt[2] == 0x54);
    REQUIRE(pkt[10] == 50);
}

TEST_CASE("akp05 key image header uses BAT command", "[akp05][protocol]") {
    auto const pkt = buildKeyImageHeader(8, 0xabcd);
    REQUIRE(pkt[5] == 0x42);
    REQUIRE(pkt[6] == 0x41);
    REQUIRE(pkt[7] == 0x54);
    REQUIRE(pkt[10] == 0xab);
    REQUIRE(pkt[11] == 0xcd);
    REQUIRE(pkt[12] == 8);
}

TEST_CASE("akp05 encoder image header uses ENC command", "[akp05][protocol]") {
    auto const pkt = buildEncoderImageHeader(2, 0x0100);
    REQUIRE(pkt[5] == 0x45);
    REQUIRE(pkt[6] == 0x4e);
    REQUIRE(pkt[7] == 0x43);
    REQUIRE(pkt[10] == 0x01);
    REQUIRE(pkt[11] == 0x00);
    REQUIRE(pkt[12] == 2);
}

TEST_CASE("akp05 main image header uses MAI command", "[akp05][protocol]") {
    auto const pkt = buildMainImageHeader(0x0800);
    REQUIRE(pkt[5] == 0x4d);
    REQUIRE(pkt[6] == 0x41);
    REQUIRE(pkt[7] == 0x49);
    REQUIRE(pkt[10] == 0x08);
    REQUIRE(pkt[11] == 0x00);
}

TEST_CASE("akp05 clear-all encodes 0xff", "[akp05][protocol]") {
    auto const pkt = buildClearAll();
    REQUIRE(pkt[11] == 0xff);
}

TEST_CASE("akp05 parser rejects ACK frames", "[akp05][protocol]") {
    std::array<std::uint8_t, 16> frame{'A', 'C', 'K'};
    REQUIRE(!parseInputReport(frame).has_value());
}

TEST_CASE("akp05 parser decodes a key press", "[akp05][protocol]") {
    std::array<std::uint8_t, 16> frame{};
    frame[9] = 7;
    frame[10] = 1;
    auto const ev = parseInputReport(frame);
    REQUIRE(ev.has_value());
    REQUIRE(ev->kind == InputEvent::Kind::KeyPressed);
    REQUIRE(ev->index == 7);
}

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

TEST_CASE("akp05 parser rejects out-of-range encoder ids", "[akp05][protocol]") {
    std::array<std::uint8_t, 16> frame{};
    frame[9] = 0x24; // encoder 4 — AKP05 only has 0..3
    REQUIRE(!parseInputReport(frame).has_value());
}

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
