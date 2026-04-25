// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_proprietary_keyboard_protocol.cpp
 * @brief Unit tests for the AJAZZ proprietary keyboard protocol builders
 *        and zone helpers.
 *
 * Verifies wire-format correctness (byte positions, endianness, clamping)
 * for every builder in proprietary_protocol.hpp, plus zone-name and
 * LED-count lookup helpers.
 */
#include "proprietary_protocol.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace ajazz::keyboard::proprietary;

/// buildGetFirmwareVersion() must begin with ReportId and CmdGetFirmwareVersion.
TEST_CASE("proprietary keyboard firmware query has correct report id", "[proprietary][protocol]") {
    auto const pkt = buildGetFirmwareVersion();
    REQUIRE(pkt.size() == ReportSize);
    REQUIRE(pkt[0] == ReportId);
    REQUIRE(pkt[1] == CmdGetFirmwareVersion);
}

/// buildSetKeycode() must encode layer/row/col and the keycode big-endian at bytes 5–6.
TEST_CASE("proprietary keyboard keycode packet is big-endian", "[proprietary][protocol]") {
    auto const pkt = buildSetKeycode(1, 2, 3, 0xabcd);
    REQUIRE(pkt[0] == ReportId);
    REQUIRE(pkt[1] == CmdSetKeycode);
    REQUIRE(pkt[2] == 1);
    REQUIRE(pkt[3] == 2);
    REQUIRE(pkt[4] == 3);
    REQUIRE(pkt[5] == 0xab);
    REQUIRE(pkt[6] == 0xcd);
}

/// buildSetRgbStatic() must write zone id at byte 2 and R/G/B at bytes 3–5.
TEST_CASE("proprietary keyboard static RGB packet carries zone+color", "[proprietary][protocol]") {
    auto const pkt = buildSetRgbStatic(ZoneKeys, 0xaa, 0xbb, 0xcc);
    REQUIRE(pkt[1] == CmdSetRgbStatic);
    REQUIRE(pkt[2] == ZoneKeys);
    REQUIRE(pkt[3] == 0xaa);
    REQUIRE(pkt[4] == 0xbb);
    REQUIRE(pkt[5] == 0xcc);
}

/// buildSetRgbEffect() must write zone id, effect id, and speed at bytes 2–4.
TEST_CASE("proprietary keyboard effect packet carries zone+effect+speed",
          "[proprietary][protocol]") {
    auto const pkt = buildSetRgbEffect(ZoneSides, 0x03, 0x80);
    REQUIRE(pkt[1] == CmdSetRgbEffect);
    REQUIRE(pkt[2] == ZoneSides);
    REQUIRE(pkt[3] == 0x03);
    REQUIRE(pkt[4] == 0x80);
}

/// buildSetRgbBrightness() must clamp values above 100 to 100.
TEST_CASE("proprietary keyboard brightness clamps to 100", "[proprietary][protocol]") {
    auto const pkt = buildSetRgbBrightness(200);
    REQUIRE(pkt[1] == CmdSetRgbBrightness);
    REQUIRE(pkt[2] == 100);
}

/// buildSetLayer() must clamp the layer index to MaxLayers–1 (currently 3).
TEST_CASE("proprietary keyboard layer command clamps to MaxLayers-1", "[proprietary][protocol]") {
    auto const pkt = buildSetLayer(10);
    REQUIRE(pkt[1] == CmdSetLayer);
    REQUIRE(pkt[2] == MaxLayers - 1);
}

/// buildCommitEeprom() must produce a well-formed report with CmdCommitEeprom at byte 1.
TEST_CASE("proprietary keyboard commit packet is well formed", "[proprietary][protocol]") {
    auto const pkt = buildCommitEeprom();
    REQUIRE(pkt[0] == ReportId);
    REQUIRE(pkt[1] == CmdCommitEeprom);
}

/// zoneIdFromName() must map all three known names and return 0xFF for unknown strings.
TEST_CASE("zone ids round-trip through the name helper", "[proprietary][protocol]") {
    REQUIRE(zoneIdFromName("keys") == ZoneKeys);
    REQUIRE(zoneIdFromName("sides") == ZoneSides);
    REQUIRE(zoneIdFromName("logo") == ZoneLogo);
    REQUIRE(zoneIdFromName("nope") == 0xff);
}

/// ledCountForZone() must return the documented LED counts and 0 for unknown zones.
TEST_CASE("ledCountForZone matches documentation", "[proprietary][protocol]") {
    REQUIRE(ledCountForZone(ZoneKeys) == LedCountKeys);
    REQUIRE(ledCountForZone(ZoneSides) == LedCountSides);
    REQUIRE(ledCountForZone(ZoneLogo) == LedCountLogo);
    REQUIRE(ledCountForZone(0x7f) == 0);
}
