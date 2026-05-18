// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_aj_series_wire_format.cpp
 * @brief Byte-precise wire-format tests for AJAZZ AJ-series mouse (P3.12).
 *
 * Pins every byte position the AJ159 APEX vendor firmware expects per
 * `docs/protocols/mouse/aj_series_opcode_table.md` §§3 + §5 + §6.3.
 * Acts as the regression guard so the upcoming P3.12.2 setter migration
 * cannot silently regress to the prior all-wrong opcode table.
 */
#include "aj_series_protocol.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <numeric>
#include <span>

#include <catch2/catch_test_macros.hpp>

using namespace ajazz::mouse::aj_series;

namespace {

/// Independent BIT7 checksum verifier - sums payload range pkt[1..62] & 0x7F.
[[nodiscard]] std::uint8_t expectedBit7Checksum(std::array<std::uint8_t, kReportSize> const& pkt) {
    auto const sum = std::accumulate(pkt.begin() + 1, pkt.end() - 1, std::uint32_t{0});
    return static_cast<std::uint8_t>(sum & 0x7fu);
}

} // namespace

// ---------------------------------------------------------------------------
// Envelope basics
// ---------------------------------------------------------------------------

TEST_CASE("AJ series report id at byte 0 is 0x05 - hard fact per opcode-table sec1",
          "[mouse][aj_series][wire][envelope]") {
    REQUIRE(kReportId == 0x05);
    // 65-byte buffer = 1 ReportID + 64 vendor envelope bytes
    // (vendor "byte 0..63" mapped to pkt[1..64]).
    REQUIRE(kReportSize == 65);
}

TEST_CASE("AJ series BIT7 checksum masks to 0x7F at byte 63",
          "[mouse][aj_series][wire][checksum]") {
    std::array<std::uint8_t, kReportSize> pkt{};
    pkt[0] = kReportId;
    pkt[1] = 0x80; // opcode that would set the high bit if mask were 0xFF
    pkt[2] = 0xff;
    pkt[3] = 0xff;
    stampBit7Checksum(pkt);
    REQUIRE((pkt[kReportSize - 1] & 0x80) == 0); // high bit must always be clear
    REQUIRE(pkt[kReportSize - 1] == ((0x80u + 0xffu + 0xffu) & 0x7fu));
}

TEST_CASE("AJ series checksum range is pkt[1..63] (63 bytes, opcode included)",
          "[mouse][aj_series][wire][checksum]") {
    std::array<std::uint8_t, kReportSize> a{};
    a[0] = kReportId;
    a[1] = 0x42; // opcode contributes to checksum
    std::array<std::uint8_t, kReportSize> b = a;
    a[1] = 0x42;
    b[1] = 0x43; // different opcode -> different checksum
    stampBit7Checksum(a);
    stampBit7Checksum(b);
    REQUIRE(a[kReportSize - 1] != b[kReportSize - 1]);
    // Verify against independent computation.
    REQUIRE(a[kReportSize - 1] == expectedBit7Checksum(a));
}

// ---------------------------------------------------------------------------
// §3.4 polling rate lookup
// ---------------------------------------------------------------------------

TEST_CASE("AJ series pollRateToWireCode - full _RateToNum lookup table",
          "[mouse][aj_series][wire][pollrate]") {
    REQUIRE(pollRateToWireCode(125) == 0x08);
    REQUIRE(pollRateToWireCode(250) == 0x04);
    REQUIRE(pollRateToWireCode(500) == 0x02);
    REQUIRE(pollRateToWireCode(1000) == 0x01);
    REQUIRE(pollRateToWireCode(2000) == 0x84);
    REQUIRE(pollRateToWireCode(4000) == 0x82);
    REQUIRE(pollRateToWireCode(8000) == 0x81);
    // Unknown rate falls back to 1 KHz.
    REQUIRE(pollRateToWireCode(333) == 0x01);
    REQUIRE(pollRateToWireCode(0) == 0x01);
}

TEST_CASE("AJ series setReportRate 8000 Hz emits byte 3 = 0x81",
          "[mouse][aj_series][wire][pollrate]") {
    auto const pkt = buildSetReportRate(/*profile=*/0, /*hz=*/8000);
    REQUIRE(pkt[0] == kReportId);
    REQUIRE(pkt[1] == static_cast<std::uint8_t>(FeaCmd::SetReport)); // 0x04
    REQUIRE(pkt[2] == 0);                                            // profile
    REQUIRE(pkt[3] == 0x81);                                         // 8K wire code
    // Tail must be zero up to checksum.
    for (std::size_t i = 4; i < kReportSize - 1; ++i) {
        REQUIRE(pkt[i] == 0x00);
    }
    REQUIRE(pkt[kReportSize - 1] == expectedBit7Checksum(pkt));
}

// ---------------------------------------------------------------------------
// §3.10 MouseSetOption1 (DPI table)
// ---------------------------------------------------------------------------

TEST_CASE("AJ series setOption1 - DPI table at bytes 9..24 LE + colours at 41..63",
          "[mouse][aj_series][wire][dpi]") {
    std::array<std::uint16_t, 8> dpis{400, 800, 1600, 3200, 6400, 12800, 25600, 42000};
    std::array<std::array<std::uint8_t, 3>, 8> colours{{{0xff, 0, 0},
                                                        {0, 0xff, 0},
                                                        {0, 0, 0xff},
                                                        {0xff, 0xff, 0},
                                                        {0xff, 0, 0xff},
                                                        {0, 0xff, 0xff},
                                                        {0xff, 0xff, 0xff},
                                                        {0x80, 0x80, 0x80}}};
    auto const pkt = buildMouseSetOption1(/*activeIdx=*/3, /*stageCount=*/8, dpis, colours);
    REQUIRE(pkt[0] == kReportId);
    REQUIRE(pkt[1] == static_cast<std::uint8_t>(FeaCmd::MouseSetOption1)); // 0x54
    REQUIRE(pkt[2] == 3);                                                  // activeIdx
    REQUIRE(pkt[3] == 8);                                                  // stageCount
    // DPI uint16-LE values at pkt[9..24].
    REQUIRE(pkt[9] == 0x90);
    REQUIRE(pkt[10] == 0x01); // 400 = 0x0190
    REQUIRE(pkt[11] == 0x20);
    REQUIRE(pkt[12] == 0x03); // 800 = 0x0320
    REQUIRE(pkt[13] == 0x40);
    REQUIRE(pkt[14] == 0x06); // 1600 = 0x0640
    REQUIRE(pkt[23] == 0x10);
    REQUIRE(pkt[24] == 0xa4); // 42000 = 0xA410
    // Colour table at pkt[41..64] (8 × 3 bytes).
    REQUIRE(pkt[41] == 0xff);
    REQUIRE(pkt[42] == 0x00);
    REQUIRE(pkt[43] == 0x00); // stage 0 red
    REQUIRE(pkt[44] == 0x00);
    REQUIRE(pkt[45] == 0xff);
    REQUIRE(pkt[46] == 0x00); // stage 1 green
    // Stage 7 B at pkt[64] is OVERWRITTEN by checksum on the wire - vendor
    // limitation per §3.10 edge case. We do NOT pin the byte's input value;
    // UI must surface this constraint to users.
    REQUIRE(pkt[kReportSize - 1] == expectedBit7Checksum(pkt));
}

TEST_CASE("AJ series setOption1 - 8th DPI stage B-channel collides with checksum",
          "[mouse][aj_series][wire][dpi]") {
    // Regression guard for the §3.10 edge case: pkt[64] is BOTH stage-7 B-channel
    // (per colour-table layout) AND the BIT7 checksum slot. stampBit7Checksum()
    // overwrites - this test confirms UI editors must grey out the 8th-stage
    // colour swatch (or accept that the B channel will appear as garbage on
    // device read-back).
    std::array<std::uint16_t, 8> dpis{800, 800, 800, 800, 800, 800, 800, 800};
    std::array<std::array<std::uint8_t, 3>, 8> colours{};
    colours[7] = {0xab, 0xcd, 0xef}; // try to set stage-7 B to 0xEF
    auto const pkt = buildMouseSetOption1(0, 8, dpis, colours);
    // Stage 7 R and G at pkt[62] and pkt[63] are intact.
    REQUIRE(pkt[62] == 0xab);
    REQUIRE(pkt[63] == 0xcd);
    // Stage 7 B at pkt[64] = checksum slot, NOT 0xEF.
    REQUIRE(pkt[kReportSize - 1] != 0xef);
    REQUIRE(pkt[kReportSize - 1] == expectedBit7Checksum(pkt));
}

// ---------------------------------------------------------------------------
// §3.6 MouseSetKeyMatrix - action at bytes 8..11 (our prior code had 4..7)
// ---------------------------------------------------------------------------

TEST_CASE("AJ series setKeyMatrix - action 4 bytes at pkt[9..12] not pkt[5..8]",
          "[mouse][aj_series][wire][keymap][safety]") {
    // Regression guard for §1.1 of the roadmap: our prior setButtonBinding
    // wrote action at byte 4 (offset wrong by 4). The vendor places action
    // at bytes 8..11 (vendor numbering) = pkt[9..12] in our buffer convention.
    constexpr std::uint32_t kAction = 0xDEADBEEFu;
    auto const pkt = buildMouseSetKeyMatrix(/*profile=*/2, /*button=*/5, kAction);
    REQUIRE(pkt[0] == kReportId);
    REQUIRE(pkt[1] == static_cast<std::uint8_t>(FeaCmd::MouseSetKeyMatrix)); // 0x50
    REQUIRE(pkt[2] == 2);                                                    // profile
    REQUIRE(pkt[3] == 5);                                                    // button
    // Bytes 4..8 must be zero (vendor "bytes 3..7" reserved).
    REQUIRE(pkt[4] == 0);
    REQUIRE(pkt[5] == 0);
    REQUIRE(pkt[6] == 0);
    REQUIRE(pkt[7] == 0);
    REQUIRE(pkt[8] == 0);
    // Action big-endian at pkt[9..12].
    REQUIRE(pkt[9] == 0xde);
    REQUIRE(pkt[10] == 0xad);
    REQUIRE(pkt[11] == 0xbe);
    REQUIRE(pkt[12] == 0xef);
}

TEST_CASE("AJ series setFnMatrix - opcode 0x51 with same action layout",
          "[mouse][aj_series][wire][keymap]") {
    auto const pkt = buildMouseSetFnMatrix(/*fnLayer=*/1, /*button=*/3, 0x11223344u);
    REQUIRE(pkt[1] == static_cast<std::uint8_t>(FeaCmd::MouseSetFnMatrix)); // 0x51
    REQUIRE(pkt[2] == 1);
    REQUIRE(pkt[3] == 3);
    REQUIRE(pkt[9] == 0x11);
    REQUIRE(pkt[10] == 0x22);
    REQUIRE(pkt[11] == 0x33);
    REQUIRE(pkt[12] == 0x44);
}

// ---------------------------------------------------------------------------
// §3.5 setLedParam - 8-byte LED block, pure-white sentinel
// ---------------------------------------------------------------------------

TEST_CASE("AJ series setLedParam - basic AlwaysOn red with speed encoding",
          "[mouse][aj_series][wire][led]") {
    // Effect=1 (AlwaysOn), UI speed=3 -> wire (4-3)=1, value=brightness=5,
    // modeBits=0x07 (NORMAL, no dazzle), RGB=red.
    auto const pkt = buildSetLedParam(/*effect=*/1,
                                      /*speed=*/3,
                                      /*value=*/5,
                                      /*modeBits=*/0x07,
                                      0xff,
                                      0x00,
                                      0x00);
    REQUIRE(pkt[1] == static_cast<std::uint8_t>(FeaCmd::SetLedParam)); // 0x07
    REQUIRE(pkt[2] == 1);                                              // effect AlwaysOn
    REQUIRE(pkt[3] == 1);                                              // 4 - UI speed (3) = 1
    REQUIRE(pkt[4] == 5);                                              // brightness
    REQUIRE(pkt[5] == 0x07);                                           // mode bits NORMAL
    REQUIRE(pkt[6] == 0xff);
    REQUIRE(pkt[7] == 0x00);
    REQUIRE(pkt[8] == 0x00);
}

TEST_CASE("AJ series setLedParam - pure-white 0xFFFFFF rewrites to 0xFAFAFA on the wire",
          "[mouse][aj_series][wire][led]") {
    // Vendor renderer's LED-off-detection sentinel: literal 0xFFFFFF would
    // mean "lights off" to firmware, so the wire layer remaps to 0xFAFAFA.
    auto const pkt = buildSetLedParam(1, 0, 5, 0x07, /*r=*/0xff, /*g=*/0xff, /*b=*/0xff);
    REQUIRE(pkt[6] == 0xfa);
    REQUIRE(pkt[7] == 0xfa);
    REQUIRE(pkt[8] == 0xfa);
}

// ---------------------------------------------------------------------------
// §3.1 GetRev + §3.2 SetReset
// ---------------------------------------------------------------------------

TEST_CASE("AJ series getRev - opcode 0x80 only, no payload", "[mouse][aj_series][wire][firmware]") {
    auto const pkt = buildGetRev();
    REQUIRE(pkt[0] == kReportId);
    REQUIRE(pkt[1] == 0x80);
    for (std::size_t i = 2; i < kReportSize - 1; ++i) {
        REQUIRE(pkt[i] == 0x00);
    }
    // Checksum of {0, 0x80, 0, 0, ..., 0} & 0x7F = 0x80 & 0x7F = 0x00.
    REQUIRE(pkt[kReportSize - 1] == 0x00);
}

TEST_CASE("AJ series setReset - opcode 0x02 only, no payload (factory reset)",
          "[mouse][aj_series][wire][safety]") {
    auto const pkt = buildSetReset();
    REQUIRE(pkt[1] == 0x02);
    // Checksum of {_, 0x02, 0..} & 0x7F = 0x02.
    REQUIRE(pkt[kReportSize - 1] == 0x02);
}

TEST_CASE("AJ series setProfile - clamps profile index to 7", "[mouse][aj_series][wire][profile]") {
    auto const pkt = buildSetProfile(99);
    REQUIRE(pkt[1] == 0x05);
    REQUIRE(pkt[2] == 7); // clamped from 99 to 7
}

// ---------------------------------------------------------------------------
// §3.9 MouseSetOption0 - omnibus packet
// ---------------------------------------------------------------------------

TEST_CASE("AJ series setOption0 - omnibus byte layout (profile, sensitivity, LOD)",
          "[mouse][aj_series][wire][option0]") {
    OptionPacket0 opts{};
    opts.profile = 2;
    opts.pollRateHz = 4000;
    opts.debounceMs = 3;
    opts.xSensitivity = 75;
    opts.ySensitivity = 100;
    opts.liftCutOff = 1; // 2 mm
    opts.angleSnap = 1;
    auto const pkt = buildMouseSetOption0(opts);
    REQUIRE(pkt[1] == static_cast<std::uint8_t>(FeaCmd::MouseSetOption0)); // 0x53
    REQUIRE(pkt[9] == 2);                                                  // profile
    REQUIRE(pkt[10] == 0x82);                                              // 4 KHz wire code
    REQUIRE(pkt[11] == 3);                                                 // debounce ms
    REQUIRE(pkt[51] == 75);  // X sensitivity (clamped to 100)
    REQUIRE(pkt[52] == 100); // Y sensitivity
    REQUIRE(pkt[53] == 1);   // LOD = 2mm
    REQUIRE(pkt[54] == 1);   // angle snap on
}

TEST_CASE("AJ series setOption0 - clamps sensitivities to 100% and LOD to 2",
          "[mouse][aj_series][wire][option0]") {
    OptionPacket0 opts{};
    opts.xSensitivity = 250;
    opts.ySensitivity = 250;
    opts.liftCutOff = 99;
    auto const pkt = buildMouseSetOption0(opts);
    REQUIRE(pkt[51] == 100);
    REQUIRE(pkt[52] == 100);
    REQUIRE(pkt[53] == 2);
}

TEST_CASE("AJ series setOption0 - sleep timers as LE uint16 at vendor bytes 40..47",
          "[mouse][aj_series][wire][option0][sleep]") {
    OptionPacket0 opts{};
    opts.sleepBtIdleSec = 0x1234;
    opts.sleepBtDeepSec = 0x5678;
    opts.sleep24gIdleSec = 0x9abc;
    opts.sleep24gDeepSec = 0xdef0;
    auto const pkt = buildMouseSetOption0(opts);
    // Vendor "bytes 40..41" = our pkt[41..42].
    REQUIRE(pkt[41] == 0x34);
    REQUIRE(pkt[42] == 0x12);
    REQUIRE(pkt[43] == 0x78);
    REQUIRE(pkt[44] == 0x56);
    REQUIRE(pkt[45] == 0xbc);
    REQUIRE(pkt[46] == 0x9a);
    REQUIRE(pkt[47] == 0xf0);
    REQUIRE(pkt[48] == 0xde);
}

// ---------------------------------------------------------------------------
// Opcode-table invariants - pitfall guards
// ---------------------------------------------------------------------------

TEST_CASE("AJ series opcode table - values match vendor RE", "[mouse][aj_series][wire][opcodes]") {
    REQUIRE(static_cast<std::uint8_t>(FeaCmd::GetRev) == 0x80);
    REQUIRE(static_cast<std::uint8_t>(FeaCmd::SetReset) == 0x02);
    REQUIRE(static_cast<std::uint8_t>(FeaCmd::SetProfile) == 0x05);
    REQUIRE(static_cast<std::uint8_t>(FeaCmd::SetReport) == 0x04);
    REQUIRE(static_cast<std::uint8_t>(FeaCmd::SetLedParam) == 0x07);
    REQUIRE(static_cast<std::uint8_t>(FeaCmd::MouseSetKeyMatrix) == 0x50);
    REQUIRE(static_cast<std::uint8_t>(FeaCmd::MouseSetFnMatrix) == 0x51);
    REQUIRE(static_cast<std::uint8_t>(FeaCmd::MouseSetOption0) == 0x53);
    REQUIRE(static_cast<std::uint8_t>(FeaCmd::MouseSetOption1) == 0x54);
    REQUIRE(static_cast<std::uint8_t>(FeaCmd::SetMacroSimple) == 0x16);
}

TEST_CASE("AJ series no-standalone-battery-opcode regression guard",
          "[mouse][aj_series][wire][safety]") {
    // §4 of opcode-table: there is NO standalone battery query opcode on the
    // mouse path. Battery is push-streamed from the dongle via gRPC; our prior
    // kCmdBattery=0x40 was nonexistent. The new FeaCmd enum MUST NOT contain
    // a 0x40 value - guard against future re-introduction.
    for (auto const cmd : {FeaCmd::GetRev,
                           FeaCmd::SetReset,
                           FeaCmd::SetProfile,
                           FeaCmd::SetReport,
                           FeaCmd::SetLedParam,
                           FeaCmd::MouseSetKeyMatrix,
                           FeaCmd::MouseSetFnMatrix,
                           FeaCmd::MouseSetOption0,
                           FeaCmd::MouseSetOption1,
                           FeaCmd::SetMacroSimple}) {
        REQUIRE(static_cast<std::uint8_t>(cmd) != 0x40);
    }
}

// ============================================================================
// §3.12 TFT LCD upload (opcode 0x25) — mouse with OLED basetta (clock + DPI)
// ============================================================================

TEST_CASE("AJ series buildSetTftLcdData - byte layout matches vendor RE",
          "[mouse][aj_series][wire][tft][vendor-re]") {
    // Frame 0 of 1 (still image), zero delay, chunk index 0x0102 LE = 0x02 0x01,
    // 4 bytes of dummy RGB565 payload (2 pixels).
    std::array<std::uint8_t, 4> const payload{0xAB, 0xCD, 0xEF, 0x12};
    auto const pkt = buildSetTftLcdData(/*frame*/ 0,
                                        /*frameCount*/ 1,
                                        /*frameDelayMs*/ 0,
                                        /*chunkIndex*/ 0x0102,
                                        payload);
    REQUIRE(pkt[0] == kReportId);
    REQUIRE(pkt[1] == 0x25);              // FEA_CMD_SETTFTLCDDATA
    REQUIRE(pkt[2] == 0);                 // currentFrame
    REQUIRE(pkt[3] == 1);                 // frameNum
    REQUIRE(pkt[4] == 0);                 // frameDelay
    REQUIRE(pkt[5] == 0x02);              // chunkIndex LE lo
    REQUIRE(pkt[6] == 0x01);              // chunkIndex LE hi
    REQUIRE(pkt[7] == 4);                 // chunkLen == payload.size()
    REQUIRE(pkt[8] == 0);                 // reserved
    REQUIRE(pkt[9] == 0xAB);              // payload byte 0
    REQUIRE(pkt[10] == 0xCD);
    REQUIRE(pkt[11] == 0xEF);
    REQUIRE(pkt[12] == 0x12);             // payload byte 3
    REQUIRE(pkt[13] == 0);                // first padding byte
    // pkt[62] is also pad (we sent only 4 bytes; kTftChunkPayloadBytes=54).
    REQUIRE(pkt[62] == 0);
    // Checksum is BIT7 over pkt[1..62].
    REQUIRE(pkt[63] == expectedBit7Checksum(pkt));
}

TEST_CASE("AJ series buildSetTftLcdData - chunkLen clamps to kTftChunkPayloadBytes",
          "[mouse][aj_series][wire][tft]") {
    // Caller-provided 60-byte payload exceeds the 54-byte per-packet budget;
    // the builder must truncate (not crash or write past the buffer).
    std::array<std::uint8_t, 60> oversized{};
    oversized.fill(0xFF);
    auto const pkt = buildSetTftLcdData(0, 1, 0, 0, oversized);
    REQUIRE(pkt[7] == kTftChunkPayloadBytes); // == 54
    for (std::size_t i = 0; i < kTftChunkPayloadBytes; ++i) {
        REQUIRE(pkt[9 + i] == 0xFF);
    }
    // pkt[9 + 54] == pkt[63] is the checksum slot, NOT data.
    REQUIRE(pkt[63] == expectedBit7Checksum(pkt));
}

TEST_CASE("AJ series buildSetTftLcdData - chunkIndex is little-endian",
          "[mouse][aj_series][wire][tft][endianness]") {
    auto const pkt = buildSetTftLcdData(0, 1, 0, /*chunkIndex*/ 0xBEEF, {});
    REQUIRE(pkt[5] == 0xEF); // LE lo
    REQUIRE(pkt[6] == 0xBE); // LE hi
}

TEST_CASE("AJ series TFT opcode constants - 0x25 (RGB565) and 0x29 (24-bit) distinct",
          "[mouse][aj_series][wire][tft][opcodes]") {
    // Both opcodes must coexist in FeaCmd without collision; the vendor
    // doc (line 152, 158) keeps them on separate codes so 16-bit and
    // 24-bit panels can be addressed independently.
    REQUIRE(static_cast<std::uint8_t>(FeaCmd::SetTftLcdData) == 0x25);
    REQUIRE(static_cast<std::uint8_t>(FeaCmd::GetTftLcdData) == 0xa5);
    REQUIRE(static_cast<std::uint8_t>(FeaCmd::SetScreen24Bit) == 0x29);
    REQUIRE(static_cast<std::uint8_t>(FeaCmd::GetScreen24Bit) == 0xa9);
    // The chunk payload budget must leave room for the checksum byte.
    REQUIRE(kTftChunkPayloadBytes + 9 + 1 == kReportSize); // 9 header + payload + checksum == 65
}
