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

// ---------------------------------------------------------------------------
// Time-sync wire format (ARCH-05 amendment, AK980 PRO Sonix SN32F299 family).
//
// Source-level corroboration: gohv/EPOMAKER-Ajazz-AK820-Pro/src/protocol.rs +
// KyleBoyer/TFTTimeSync-node/src/packets.ts (identical byte layouts).
// ---------------------------------------------------------------------------

TEST_CASE("ak980 setTime start packet carries ReportId=0x04 + opcode 0x18 + marker 0x01",
          "[proprietary][protocol][clock]") {
    auto const pkt = buildSetTimeStart();
    REQUIRE(pkt.size() == ReportSize);
    REQUIRE(pkt[0] == ReportId);     // 0x04 — default report id
    REQUIRE(pkt[1] == CmdStartTime); // 0x18 — resets firmware time-sync state machine
    REQUIRE(pkt[8] == 0x01);         // configure-mode marker (gohv control_packet pattern)
    for (std::size_t i = 0; i < ReportSize; ++i) {
        if (i == 0 || i == 1 || i == 8) {
            continue;
        }
        REQUIRE(pkt[i] == 0x00);
    }
}

TEST_CASE("ak980 setTime opcodes are mutually distinct",
          "[proprietary][protocol][clock]") {
    // Pitfall guard: future contributors must not coalesce the 4 opcodes into
    // shared constants. START / TIME / SAVE / EEPROM-commit all touch different
    // firmware state.
    REQUIRE(CmdStartTime != CmdSetTime);
    REQUIRE(CmdSetTime != CmdSaveRtc);
    REQUIRE(CmdStartTime != CmdSaveRtc);
    REQUIRE(CmdStartTime != CmdCommitEeprom);
    REQUIRE(CmdSaveRtc != CmdCommitEeprom);
    REQUIRE(CmdStartTime == 0x18);
    REQUIRE(CmdSetTime == 0x28);
    REQUIRE(CmdSaveRtc == 0x02);
    REQUIRE(CmdCommitEeprom == 0x0e);
}

TEST_CASE("ak980 setTime preamble carries ReportId=0x04 + opcode 0x28 + marker 0x01",
          "[proprietary][protocol][clock]") {
    auto const pkt = buildSetTimePreamble();
    REQUIRE(pkt.size() == ReportSize);
    REQUIRE(pkt[0] == ReportId);   // 0x04 — default report id
    REQUIRE(pkt[1] == CmdSetTime); // 0x28
    REQUIRE(pkt[8] == 0x01);       // configure-mode marker
    // All other bytes must be zero.
    for (std::size_t i = 0; i < ReportSize; ++i) {
        if (i == 0 || i == 1 || i == 8) {
            continue;
        }
        REQUIRE(pkt[i] == 0x00);
    }
}

TEST_CASE("ak980 setTime data packet has report id 0x00 + magic 0x5A + delimiter 0xAA 0x55",
          "[proprietary][protocol][clock]") {
    // 2026-05-17 14:23:45 — that day was a Sunday (wDayOfWeek=0).
    auto const pkt = buildSetTimeData(2026, 5, 17, 14, 23, 45, /*dayOfWeek=*/0);
    REQUIRE(pkt.size() == ReportSize);
    REQUIRE(pkt[0] == TimeDataReportId); // 0x00 — distinct from default ReportId=0x04
    REQUIRE(pkt[1] == 0x01);
    REQUIRE(pkt[2] == 0x5a);
    REQUIRE(pkt[3] == 26); // year-2000
    REQUIRE(pkt[4] == 5);  // month
    REQUIRE(pkt[5] == 17); // day
    REQUIRE(pkt[6] == 14); // hour
    REQUIRE(pkt[7] == 23); // minute
    REQUIRE(pkt[8] == 45); // second
    REQUIRE(pkt[9] == 0x00);
    REQUIRE(pkt[10] == 0x00); // wDayOfWeek (Sunday)
    // Bytes 11..61 must be zero padding.
    for (std::size_t i = 11; i <= 61; ++i) {
        REQUIRE(pkt[i] == 0x00);
    }
    REQUIRE(pkt[62] == 0xaa); // delimiter high
    REQUIRE(pkt[63] == 0x55); // delimiter low
}

TEST_CASE("ak980 setTime data packet encodes wDayOfWeek correctly",
          "[proprietary][protocol][clock]") {
    // Per Ghidra decompile of DeviceDriver.exe (2026-05-17, ak980pro_vendor.md):
    // vendor writes the actual day-of-week at byte 10, NOT the hard-coded 0x04
    // that the gohv corpus uses. Default value of 0 must produce Sunday.
    auto const sunday = buildSetTimeData(2026, 1, 4, 0, 0, 0); // default 0
    REQUIRE(sunday[10] == 0x00);

    for (std::uint8_t dow = 0; dow <= 6; ++dow) {
        auto const pkt = buildSetTimeData(2026, 1, 1, 0, 0, 0, dow);
        REQUIRE(pkt[10] == dow);
    }

    // Out-of-range values are clamped to 0 (defensive — tm_wday is guaranteed
    // 0..6 by the C library but a malformed caller must not corrupt the wire).
    auto const bad = buildSetTimeData(2026, 1, 1, 0, 0, 0, 99);
    REQUIRE(bad[10] == 0x00);
}

TEST_CASE("ak980 setTime data packet saturates pre-2000 years to floor",
          "[proprietary][protocol][clock]") {
    // 1999 must NOT underflow to 0xFF — it must clamp to year-2000 == 0.
    auto const pkt = buildSetTimeData(1999, 1, 1, 0, 0, 0);
    REQUIRE(pkt[3] == 0);
    // 1970 (unix epoch) likewise clamps.
    auto const pkt2 = buildSetTimeData(1970, 1, 1, 0, 0, 0);
    REQUIRE(pkt2[3] == 0);
    // Year 2000 itself encodes as 0.
    auto const pkt3 = buildSetTimeData(2000, 1, 1, 0, 0, 0);
    REQUIRE(pkt3[3] == 0);
    // Year 2001 encodes as 1.
    auto const pkt4 = buildSetTimeData(2001, 1, 1, 0, 0, 0);
    REQUIRE(pkt4[3] == 1);
}

TEST_CASE("ak980 setTime data packet encodes max representable year (2255 = 0xFF)",
          "[proprietary][protocol][clock]") {
    // year-2000 is a single byte; the high boundary 0xFF (255) corresponds to year 2255.
    auto const pkt = buildSetTimeData(2255, 12, 31, 23, 59, 59);
    REQUIRE(pkt[3] == 0xff);
}

TEST_CASE("ak980 setTime save packet carries ReportId=0x04 + opcode 0x02",
          "[proprietary][protocol][clock]") {
    auto const pkt = buildSetTimeSave();
    REQUIRE(pkt.size() == ReportSize);
    REQUIRE(pkt[0] == ReportId);   // 0x04
    REQUIRE(pkt[1] == CmdSaveRtc); // 0x02 — distinct from CmdCommitEeprom=0x0E
    // All other bytes must be zero.
    for (std::size_t i = 2; i < ReportSize; ++i) {
        REQUIRE(pkt[i] == 0x00);
    }
}

// ---------------------------------------------------------------------------
// Battery query wire format (roadmap §11.2; ak980pro_vendor.md row 0x20 0x01).
// ---------------------------------------------------------------------------

TEST_CASE("ak980 battery query packet carries ReportId=0x04 + opcode 0x20 + sub 0x01",
          "[proprietary][protocol][battery]") {
    auto const pkt = buildBatteryQuery();
    REQUIRE(pkt.size() == ReportSize);
    REQUIRE(pkt[0] == ReportId);         // 0x04
    REQUIRE(pkt[1] == CmdBatteryQuery);  // 0x20
    REQUIRE(pkt[2] == BatteryQuerySub);  // 0x01 — discriminates from per-key RGB (sub 0x04)
    // All other bytes must be zero.
    for (std::size_t i = 3; i < ReportSize; ++i) {
        REQUIRE(pkt[i] == 0x00);
    }
}

TEST_CASE("ak980 battery sub-command is distinct from per-key RGB sub",
          "[proprietary][protocol][battery]") {
    // Pitfall guard: opcode 0x20 multiplexes battery query (sub 0x01) and per-key
    // RGB upload (sub 0x04). A future contributor must not collapse them into one
    // constant. Per-key RGB lands as a separate roadmap commit.
    REQUIRE(BatteryQuerySub == 0x01);
    REQUIRE(CmdBatteryQuery == 0x20);
    // Distinct from the time-sync sub-byte semantics on opcode 0x28 / 0x18.
    REQUIRE(BatteryQuerySub != 0x04);
}

TEST_CASE("ak980 setTime save opcode is distinct from CmdCommitEeprom",
          "[proprietary][protocol][clock]") {
    // Pitfall guard: a future contributor must not consolidate the two save opcodes
    // into one. RTC save (0x02) and EEPROM commit (0x0E) target different firmware
    // state and must stay separate.
    REQUIRE(CmdSaveRtc != CmdCommitEeprom);
    REQUIRE(CmdSaveRtc == 0x02);
    REQUIRE(CmdCommitEeprom == 0x0e);
}
