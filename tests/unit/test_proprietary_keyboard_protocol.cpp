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
#include "ajazz/keyboard/ak980_lighting.hpp"
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
    REQUIRE(pkt[0] == ReportId);     // 0x04 - default report id
    REQUIRE(pkt[1] == CmdStartTime); // 0x18 - resets firmware time-sync state machine
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
    REQUIRE(pkt[0] == ReportId);   // 0x04 - default report id
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
    // 2026-05-17 14:23:45 - that day was a Sunday (wDayOfWeek=0).
    auto const pkt = buildSetTimeData(2026, 5, 17, 14, 23, 45, /*dayOfWeek=*/0);
    REQUIRE(pkt.size() == ReportSize);
    REQUIRE(pkt[0] == TimeDataReportId); // 0x00 - distinct from default ReportId=0x04
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

    // Out-of-range values are clamped to 0 (defensive - tm_wday is guaranteed
    // 0..6 by the C library but a malformed caller must not corrupt the wire).
    auto const bad = buildSetTimeData(2026, 1, 1, 0, 0, 0, 99);
    REQUIRE(bad[10] == 0x00);
}

TEST_CASE("ak980 setTime data packet saturates pre-2000 years to floor",
          "[proprietary][protocol][clock]") {
    // 1999 must NOT underflow to 0xFF - it must clamp to year-2000 == 0.
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
    REQUIRE(pkt[1] == CmdSaveRtc); // 0x02 - distinct from CmdCommitEeprom=0x0E
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
    REQUIRE(pkt[2] == BatteryQuerySub);  // 0x01 - discriminates from per-key RGB (sub 0x04)
    // All other bytes must be zero.
    for (std::size_t i = 3; i < ReportSize; ++i) {
        REQUIRE(pkt[i] == 0x00);
    }
}

// ---------------------------------------------------------------------------
// RGB firmware lighting mode (opcode 0x13) - P3.10 from
// docs/research/phase3-patch-sequence.md.
// ---------------------------------------------------------------------------

TEST_CASE("ak980 setRgbMode data packet byte layout - Static + tint + flags",
          "[proprietary][protocol][lighting]") {
    // Mode 0x06 (Breath) with red tint, no rainbow, brightness 3, speed 4,
    // direction Up (2).
    auto const pkt = buildSetRgbModeData(/*modeId=*/0x06,
                                         /*r=*/0xff, /*g=*/0x00, /*b=*/0x00,
                                         /*rainbow=*/0,
                                         /*brightness=*/3,
                                         /*speed=*/4,
                                         /*direction=*/2);
    REQUIRE(pkt.size() == ReportSize);
    REQUIRE(pkt[0] == ReportId); // 0x04
    REQUIRE(pkt[1] == 0x06);     // mode_id (Breath)
    REQUIRE(pkt[2] == 0xff);     // R
    REQUIRE(pkt[3] == 0x00);     // G
    REQUIRE(pkt[4] == 0x00);     // B
    REQUIRE(pkt[8] == 0x00);     // rainbow flag (off)
    REQUIRE(pkt[9] == 3);        // brightness
    REQUIRE(pkt[10] == 4);       // speed
    REQUIRE(pkt[11] == 2);       // direction (Up)
    REQUIRE(pkt[14] == 0x55);    // trailer hi
    REQUIRE(pkt[15] == 0xaa);    // trailer lo
    // Slots that should remain zero.
    REQUIRE(pkt[5] == 0x00);
    REQUIRE(pkt[6] == 0x00);
    REQUIRE(pkt[7] == 0x00);
    REQUIRE(pkt[12] == 0x00);
    REQUIRE(pkt[13] == 0x00);
    // Trailing pad.
    for (std::size_t i = 16; i < ReportSize; ++i) {
        REQUIRE(pkt[i] == 0x00);
    }
}

TEST_CASE("ak980 setRgbMode rainbow flag normalises to 0/1",
          "[proprietary][protocol][lighting]") {
    auto const pkt = buildSetRgbModeData(0x07, 0, 0, 0, /*rainbow=*/123, 5, 5, 0);
    REQUIRE(pkt[8] == 0x01); // any non-zero rainbow input normalises to 0x01
}

TEST_CASE("ak980 setRgbMode clamps brightness / speed / direction",
          "[proprietary][protocol][lighting]") {
    // brightness_max = 5, speed_max = 5, direction range 0..3.
    auto const clamped =
        buildSetRgbModeData(0x06, 0, 0, 0, 0, /*brightness=*/250, /*speed=*/99, /*direction=*/9);
    REQUIRE(clamped[9] == 5);   // brightness clamp
    REQUIRE(clamped[10] == 5);  // speed clamp
    REQUIRE(clamped[11] == 3);  // direction clamp
}

TEST_CASE("ak980 lighting enum range covers all 20 firmware modes",
          "[proprietary][protocol][lighting]") {
    using ajazz::keyboard::AK980LightingMode;
    // Sanity-check enum integer values match the wire-format expectations
    // pinned in ak980_lighting.hpp. The full list (1033.lan 521-540) is
    // documented in the header; this guards against accidental renumbering.
    REQUIRE(static_cast<std::uint8_t>(AK980LightingMode::Static) == 0x00);
    REQUIRE(static_cast<std::uint8_t>(AK980LightingMode::Breath) == 0x06);
    REQUIRE(static_cast<std::uint8_t>(AK980LightingMode::Rotating) == 0x0b);
    REQUIRE(static_cast<std::uint8_t>(AK980LightingMode::LedOff) == 0x13);
}

TEST_CASE("ak980 lighting CmdSetRgbMode opcode is 0x13 + CmdFinish is 0xF0",
          "[proprietary][protocol][lighting]") {
    REQUIRE(CmdSetRgbMode == 0x13);
    REQUIRE(CmdFinish == 0xf0);
    // 0x13 must NOT collide with the per-key RGB upload (0x20 sub 0x04) or
    // the time-data magic at byte 2 of the time-sync data packet (0x5A).
    REQUIRE(CmdSetRgbMode != CmdBatteryQuery);
    REQUIRE(CmdSetRgbMode != CmdSetTime);
}

// ---------------------------------------------------------------------------
// Per-key RGB upload header + readback (opcode 0x20 0x04 + 0xF5 0x03/0x09).
// P3.11 - corrects the wireless chunk count to 8 (NOT 6 as prior pass).
// ---------------------------------------------------------------------------

TEST_CASE("ak980 per-key RGB write header - wired mode byte at offset 9 is 0x03",
          "[proprietary][protocol][perkey-rgb]") {
    auto const pkt = buildPerKeyRgbWriteHeader(/*isWireless=*/false);
    REQUIRE(pkt.size() == ReportSize);
    REQUIRE(pkt[0] == ReportId);
    REQUIRE(pkt[1] == kCmdPerKeyRgbWrite);  // 0x20
    REQUIRE(pkt[2] == kPerKeyRgbSub);        // 0x04
    REQUIRE(pkt[9] == kPerKeyModeWired);     // 0x03
    // All other bytes must be zero.
    for (std::size_t i = 3; i < ReportSize; ++i) {
        if (i == 9) {
            continue;
        }
        REQUIRE(pkt[i] == 0x00);
    }
}

TEST_CASE("ak980 per-key RGB write header - wireless mode byte at offset 9 is 0x08",
          "[proprietary][protocol][perkey-rgb]") {
    auto const pkt = buildPerKeyRgbWriteHeader(/*isWireless=*/true);
    REQUIRE(pkt[0] == ReportId);
    REQUIRE(pkt[1] == kCmdPerKeyRgbWrite);  // 0x20
    REQUIRE(pkt[2] == kPerKeyRgbSub);        // 0x04
    REQUIRE(pkt[9] == kPerKeyModeWireless);  // 0x08 (not 0x03)
}

TEST_CASE("ak980 per-key RGB readback - wired uses sub 0x03, wireless sub 0x09",
          "[proprietary][protocol][perkey-rgb]") {
    auto const wired = buildPerKeyRgbReadback(/*isWireless=*/false);
    REQUIRE(wired[0] == ReportId);
    REQUIRE(wired[1] == kCmdPerKeyRgbReadback); // 0xF5
    REQUIRE(wired[2] == kPerKeyReadbackWiredSub); // 0x03

    auto const wireless = buildPerKeyRgbReadback(/*isWireless=*/true);
    REQUIRE(wireless[1] == kCmdPerKeyRgbReadback); // 0xF5
    REQUIRE(wireless[2] == kPerKeyReadbackWirelessSub); // 0x09 (NOT 0x03)
}

TEST_CASE("ak980 per-key RGB blob sizes - wired 192 B + wireless 512 B (NOT 384 / 6 chunks)",
          "[proprietary][protocol][perkey-rgb]") {
    // Wired = monochrome only, 1 byte/LED × 192 LEDs = 192 B = 3 chunks of 64 B.
    REQUIRE(kPerKeyWiredBlobSize == 192);
    REQUIRE(kPerKeyWiredChunkCount == 3);
    REQUIRE(kPerKeyWiredBlobSize == kPerKeyWiredChunkCount * 64);

    // Wireless = full color, 4 byte/LED ([reserved=0, R, G, B]) × 128 LEDs
    // = 512 B = 8 chunks of 64 B. The prior doc said 6 chunks (which would be
    // 384 B), which is WRONG per deep RE - 8 chunks is correct.
    REQUIRE(kPerKeyWirelessBlobSize == 512);
    REQUIRE(kPerKeyWirelessChunkCount == 8);
    REQUIRE(kPerKeyWirelessBlobSize == kPerKeyWirelessChunkCount * 64);
}

TEST_CASE("ak980 per-key RGB opcode 0x20 sub 0x04 != battery query opcode 0x20 sub 0x01",
          "[proprietary][protocol][perkey-rgb]") {
    // Pitfall guard: 0x20 multiplexes battery query (sub 0x01, single-shot read)
    // and per-key RGB upload (sub 0x04, multi-packet envelope). A future
    // contributor must not collapse them.
    REQUIRE(kCmdPerKeyRgbWrite == CmdBatteryQuery);
    REQUIRE(kPerKeyRgbSub != BatteryQuerySub);
    REQUIRE(kPerKeyRgbSub == 0x04);
    REQUIRE(BatteryQuerySub == 0x01);
}

// ---------------------------------------------------------------------------
// TFT screen image upload primitives - P3.9 scaled-down: chunk index encoder
// (24-bit split across bytes 1/2/3 with 0x80 marker) + bulk-path BEGIN packet
// (opcode 0x72 - 143× faster than the chunked 0x7F path).
// ---------------------------------------------------------------------------

TEST_CASE("ak980 TFT chunk-index encoder - round-trip for canonical values",
          "[proprietary][protocol][tft]") {
    auto decode = [](std::array<std::uint8_t, 3> const& enc) -> std::uint32_t {
        return static_cast<std::uint32_t>(enc[0]) |
               (static_cast<std::uint32_t>(enc[2]) << 8) |
               ((static_cast<std::uint32_t>(enc[1]) & 0x7fu) << 16);
    };
    // 0 - first chunk of first frame.
    auto e0 = encodeTftChunkIndex(0);
    REQUIRE(e0[0] == 0x00);
    REQUIRE(e0[1] == 0x80); // marker bit set, high 7 bits = 0
    REQUIRE(e0[2] == 0x00);
    REQUIRE(decode(e0) == 0);

    // 2 314 - last chunk of a single 240x135 RGB565 frame (2 315 total).
    auto e2314 = encodeTftChunkIndex(2314);
    REQUIRE(decode(e2314) == 2314);
    REQUIRE((e2314[1] & 0x80) == 0x80); // marker always set

    // 324 099 - last chunk of a full 140-frame GIF (140 × 2 315 = 324 100).
    auto eMax = encodeTftChunkIndex(324099);
    REQUIRE(decode(eMax) == 324099);

    // 0x7FFFFF - effective max (23 bits: 7-bit upper + 8-bit middle + 8-bit
    // low; the MSB of byte 2 is the chunk marker, NOT part of the index).
    // Inputs above this saturate (the high bit is dropped by the encoder).
    auto eMaxMax = encodeTftChunkIndex(0x7fffff);
    REQUIRE(decode(eMaxMax) == 0x7fffff);
    REQUIRE(eMaxMax[1] == 0xff); // 0x80 marker | 0x7F upper-7 bits
}

TEST_CASE("ak980 TFT chunk-index encoder - values above 23-bit max saturate",
          "[proprietary][protocol][tft]") {
    // 24-bit max (0xFFFFFF) cannot be represented because the high bit of
    // byte 2 is the chunk marker. A future contributor implementing the
    // chunked upload path MUST cap chunk_idx at 0x7FFFFF (8 388 607); the
    // practical limit is well below that (140 frames × 2 315 chunks/frame
    // = 324 100 chunks << 0x7FFFFF).
    auto const enc24bit = encodeTftChunkIndex(0xffffff);
    // The encoder silently truncates the high bit: enc[1] = 0x80 | (0xFF & 0x7F)
    // = 0x80 | 0x7F = 0xFF - same as encoding 0x7FFFFF.
    REQUIRE(enc24bit[1] == 0xff);
    auto const enc23bit = encodeTftChunkIndex(0x7fffff);
    REQUIRE(enc24bit == enc23bit); // 0xFFFFFF and 0x7FFFFF produce identical wire bytes
}

TEST_CASE("ak980 TFT chunk-index encoder always sets the 0x80 marker",
          "[proprietary][protocol][tft]") {
    // The marker bit distinguishes a chunk packet from the header packet
    // (whose byte 2 = 0x03 per ak980pro_tft_protocol.md §3.1). Critical
    // invariant: every chunk encoding must have byte 1 (output[1]) with
    // 0x80 set.
    for (std::uint32_t idx : {0u, 1u, 100u, 2314u, 65535u, 324099u, 0xffffffu}) {
        auto const enc = encodeTftChunkIndex(idx);
        REQUIRE((enc[1] & 0x80) == 0x80);
    }
}

TEST_CASE("ak980 TFT bulk-begin packet - opcode 0x72 + LCD-select + total chunks",
          "[proprietary][protocol][tft]") {
    // Single-LCD AK980 PRO (most common): pass lcdSelect=0 -> wire byte 3 = 1.
    // 240×135 RGB565 single frame = 64 800 B / 4 096 B = 16 chunks (rounded up).
    auto const pkt = buildScreenBulkBegin(/*lcdSelect=*/0, /*total4kChunks=*/16);
    REQUIRE(pkt.size() == ReportSize);
    REQUIRE(pkt[0] == ReportId);            // 0x04
    REQUIRE(pkt[1] == CmdScreenBulkBegin);  // 0x72
    REQUIRE(pkt[2] == 0x00);
    REQUIRE(pkt[3] == 0x01);                // lcdSelect 0 -> wire 1
    REQUIRE(pkt[4] == 0x10);                // total_4k_chunks low (16)
    REQUIRE(pkt[5] == 0x00);                // total_4k_chunks high
    // Trailing pad.
    for (std::size_t i = 6; i < ReportSize; ++i) {
        REQUIRE(pkt[i] == 0x00);
    }
}

TEST_CASE("ak980 TFT bulk-begin - large frame counts encode LE correctly",
          "[proprietary][protocol][tft]") {
    // Full 140-frame GIF: ~64 800 B/frame × 140 = 9 072 000 B / 4 096 = 2 215 chunks
    // 2 215 = 0x08A7 -> low 0xA7, high 0x08
    auto const pkt = buildScreenBulkBegin(0, 2215);
    REQUIRE(pkt[4] == 0xa7);
    REQUIRE(pkt[5] == 0x08);
}

TEST_CASE("ak980 TFT opcode constants - bulk (0x72) and chunked (0x7F) coexist",
          "[proprietary][protocol][tft]") {
    // Pitfall guard: a future contributor must not collapse the two TFT
    // upload paths. They use distinct opcodes and distinct transport sizes
    // (33-byte chunked vs 4097-byte bulk per ak980pro_tft_protocol.md §§3-4).
    REQUIRE(CmdScreenBulkBegin == 0x72);
    REQUIRE(CmdScreenHeader == 0x7f);
    REQUIRE(CmdScreenSubBegin == 0x03);
    REQUIRE(CmdScreenChunkMarker == 0x80);
    REQUIRE(CmdScreenBulkBegin != CmdScreenHeader);
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
