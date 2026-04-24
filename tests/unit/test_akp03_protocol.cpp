// SPDX-License-Identifier: GPL-3.0-or-later
#include "akp03_protocol.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace ajazz::streamdeck::akp03;

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

TEST_CASE("akp03 brightness clamps to 100", "[akp03][protocol]") {
    auto const pkt = buildSetBrightness(250);
    REQUIRE(pkt[10] == 100);
}

TEST_CASE("akp03 clear-all encodes 0xff", "[akp03][protocol]") {
    auto const pkt = buildClearAll();
    REQUIRE(pkt[5] == 0x43);
    REQUIRE(pkt[6] == 0x4c);
    REQUIRE(pkt[7] == 0x45);
    REQUIRE(pkt[10] == 0x00);
    REQUIRE(pkt[11] == 0xff);
}

TEST_CASE("akp03 clear-key uses 1-based key index", "[akp03][protocol]") {
    auto const pkt = buildClearKey(3);
    REQUIRE(pkt[10] == 0x00);
    REQUIRE(pkt[11] == 3);
}

TEST_CASE("akp03 image header encodes PNG word and size", "[akp03][protocol]") {
    auto const pkt = buildImageHeader(4, 0x1234);
    REQUIRE(pkt[5] == 0x50); // P
    REQUIRE(pkt[6] == 0x4e); // N
    REQUIRE(pkt[7] == 0x47); // G
    REQUIRE(pkt[10] == 0x12);
    REQUIRE(pkt[11] == 0x34);
    REQUIRE(pkt[12] == 4);
}

TEST_CASE("akp03 parser rejects ACK frames", "[akp03][protocol]") {
    std::array<std::uint8_t, 16> frame{'A', 'C', 'K', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    REQUIRE(!parseInputReport(frame).has_value());
}

TEST_CASE("akp03 parser decodes key press", "[akp03][protocol]") {
    std::array<std::uint8_t, 16> frame{};
    frame[9] = 2;   // key index
    frame[10] = 1;  // pressed
    auto const ev = parseInputReport(frame);
    REQUIRE(ev.has_value());
    REQUIRE(ev->kind == InputEvent::Kind::KeyPressed);
    REQUIRE(ev->index == 2);
}

TEST_CASE("akp03 parser decodes key release", "[akp03][protocol]") {
    std::array<std::uint8_t, 16> frame{};
    frame[9] = 4;
    frame[10] = 0;
    auto const ev = parseInputReport(frame);
    REQUIRE(ev.has_value());
    REQUIRE(ev->kind == InputEvent::Kind::KeyReleased);
    REQUIRE(ev->index == 4);
}

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

TEST_CASE("akp03 parser decodes encoder CCW rotation", "[akp03][protocol]") {
    std::array<std::uint8_t, 16> frame{};
    frame[9] = 0x20;
    frame[10] = 0xff; // -1 = CCW
    auto const ev = parseInputReport(frame);
    REQUIRE(ev.has_value());
    REQUIRE(ev->kind == InputEvent::Kind::EncoderTurned);
    REQUIRE(ev->delta == -1);
}

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

TEST_CASE("akp03 parser rejects short frames", "[akp03][protocol]") {
    std::array<std::uint8_t, 8> frame{};
    REQUIRE(!parseInputReport(frame).has_value());
}
