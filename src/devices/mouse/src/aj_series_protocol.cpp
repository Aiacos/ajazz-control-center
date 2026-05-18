// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file aj_series_protocol.cpp
 * @brief Vendor-correct wire-format builders for AJAZZ AJ-series mice (P3.12).
 */
#include "aj_series_protocol.hpp"

#include <algorithm>
#include <cstring>
#include <numeric>

namespace ajazz::mouse::aj_series {

namespace {

/// Zero-initialised buffer with ReportId at byte 0 and the chosen opcode at byte 1.
/// Builders fill in payload bytes by index then call stampBit7Checksum() last.
[[nodiscard]] std::array<std::uint8_t, kReportSize> startReport(FeaCmd opcode) {
    std::array<std::uint8_t, kReportSize> pkt{};
    pkt[0] = kReportId;
    pkt[1] = static_cast<std::uint8_t>(opcode);
    return pkt;
}

/// Little-endian uint16 write helper.
constexpr void writeUInt16LE(std::array<std::uint8_t, kReportSize>& pkt,
                             std::size_t offset,
                             std::uint16_t value) noexcept {
    pkt[offset + 0] = static_cast<std::uint8_t>(value & 0xffu);
    pkt[offset + 1] = static_cast<std::uint8_t>((value >> 8) & 0xffu);
}

} // namespace

std::uint8_t pollRateToWireCode(std::uint16_t hz) noexcept {
    // Per aj_series_opcode_table.md §3.4 — `_RateToNum` lookup at js:920911.
    // High bit set in the wire code distinguishes 2/4/8 KHz from lower rates
    // (firmware uses bit 7 as "high-rate enable").
    switch (hz) {
    case 125:
        return 0x08;
    case 250:
        return 0x04;
    case 500:
        return 0x02;
    case 1000:
        return 0x01;
    case 2000:
        return 0x84;
    case 4000:
        return 0x82;
    case 8000:
        return 0x81;
    default:
        return 0x01; // unknown → fall back to 1 KHz (sane default)
    }
}

void stampBit7Checksum(std::array<std::uint8_t, kReportSize>& pkt) noexcept {
    // Sum pkt[1..63] (63 bytes = vendor "bytes 0..62"), mask to 7 bits per
    // BIT7 enum value. The Report ID at pkt[0] is excluded; the checksum
    // itself at pkt[64] is excluded. See aj_series_opcode_table.md §5.
    auto const sum = std::accumulate(pkt.begin() + 1, pkt.end() - 1, std::uint32_t{0});
    pkt[kReportSize - 1] = static_cast<std::uint8_t>(sum & 0x7fu);
}

std::array<std::uint8_t, kReportSize> buildGetRev() {
    auto pkt = startReport(FeaCmd::GetRev);
    stampBit7Checksum(pkt);
    return pkt;
}

std::array<std::uint8_t, kReportSize> buildSetReset() {
    auto pkt = startReport(FeaCmd::SetReset);
    stampBit7Checksum(pkt);
    return pkt;
}

std::array<std::uint8_t, kReportSize> buildFactoryReset() {
    // §3.2 FEA_CMD_SET_RESERT: opcode 0x02, no payload, BIT7 checksum.
    // Delegates to buildSetReset() so the byte layout lives in one place.
    return buildSetReset();
}

std::array<std::uint8_t, kReportSize> buildSetProfile(std::uint8_t profile) {
    auto pkt = startReport(FeaCmd::SetProfile);
    pkt[2] = std::min<std::uint8_t>(profile, 7); // 8 profiles per AJ159 device matrix
    stampBit7Checksum(pkt);
    return pkt;
}

std::array<std::uint8_t, kReportSize> buildSetReportRate(std::uint8_t profile, std::uint16_t hz) {
    auto pkt = startReport(FeaCmd::SetReport);
    pkt[2] = std::min<std::uint8_t>(profile, 7);
    pkt[3] = pollRateToWireCode(hz);
    stampBit7Checksum(pkt);
    return pkt;
}

std::array<std::uint8_t, kReportSize> buildSetLedParam(std::uint8_t effect,
                                                       std::uint8_t speed,
                                                       std::uint8_t value,
                                                       std::uint8_t modeBits,
                                                       std::uint8_t r,
                                                       std::uint8_t g,
                                                       std::uint8_t b) {
    auto pkt = startReport(FeaCmd::SetLedParam);
    constexpr std::uint8_t kMaxUiSpeed = 4;
    auto const uiSpeed = std::min<std::uint8_t>(speed, kMaxUiSpeed);
    pkt[2] = effect;                                           // byte 1 in vendor numbering
    pkt[3] = static_cast<std::uint8_t>(kMaxUiSpeed - uiSpeed); // byte 2: 4-speed
    pkt[4] = value;                                            // byte 3: brightness or option
    pkt[5] = modeBits;                                         // byte 4: option-nibble | mode-bits
    // LED-off sentinel: renderer rewrites pure white 0xFFFFFF to 0xFAFAFA on
    // the wire (`aj_series_opcode_table.md` §3.5). Replicate here so the
    // firmware does not interpret 0xFF/0xFF/0xFF as "lights off".
    bool const isPureWhite = (r == 0xff && g == 0xff && b == 0xff);
    pkt[6] = isPureWhite ? 0xfa : r;
    pkt[7] = isPureWhite ? 0xfa : g;
    pkt[8] = isPureWhite ? 0xfa : b;
    stampBit7Checksum(pkt);
    return pkt;
}

std::array<std::uint8_t, kReportSize>
buildMouseSetKeyMatrix(std::uint8_t profile, std::uint8_t button, std::uint32_t actionBE) {
    auto pkt = startReport(FeaCmd::MouseSetKeyMatrix);
    pkt[2] = std::min<std::uint8_t>(profile, 7);
    pkt[3] = button;
    // bytes 4..8 stay zero (vendor "bytes 3..7")
    // Action 4 bytes at vendor "bytes 8..11" = our pkt[9..12], big-endian.
    pkt[9] = static_cast<std::uint8_t>((actionBE >> 24) & 0xffu);
    pkt[10] = static_cast<std::uint8_t>((actionBE >> 16) & 0xffu);
    pkt[11] = static_cast<std::uint8_t>((actionBE >> 8) & 0xffu);
    pkt[12] = static_cast<std::uint8_t>(actionBE & 0xffu);
    stampBit7Checksum(pkt);
    return pkt;
}

std::array<std::uint8_t, kReportSize>
buildMouseSetFnMatrix(std::uint8_t fnLayer, std::uint8_t button, std::uint32_t actionBE) {
    auto pkt = startReport(FeaCmd::MouseSetFnMatrix);
    pkt[2] = fnLayer;
    pkt[3] = button;
    pkt[9] = static_cast<std::uint8_t>((actionBE >> 24) & 0xffu);
    pkt[10] = static_cast<std::uint8_t>((actionBE >> 16) & 0xffu);
    pkt[11] = static_cast<std::uint8_t>((actionBE >> 8) & 0xffu);
    pkt[12] = static_cast<std::uint8_t>(actionBE & 0xffu);
    stampBit7Checksum(pkt);
    return pkt;
}

std::array<std::uint8_t, kReportSize>
buildFnLayerRemap(std::uint8_t fnLayer, std::uint8_t buttonIndex, std::uint32_t actionBE) {
    // Thin alias over the low-level builder so the new IMouseFnRemappable
    // surface can dispatch under the spelling the capability mix-in uses.
    // Fn-layer index clamped to 0..7 to match the envelope's profile-slot
    // semantics on opcode 0x50 (vendor byte 1, same byte position).
    return buildMouseSetFnMatrix(std::min<std::uint8_t>(fnLayer, 7), buttonIndex, actionBE);
}

std::array<std::uint8_t, kReportSize>
buildMouseSetOption1(std::uint8_t activeIdx,
                     std::uint8_t stageCount,
                     std::span<std::uint16_t const> dpiValues,
                     std::span<std::array<std::uint8_t, 3> const> colours) {
    auto pkt = startReport(FeaCmd::MouseSetOption1);
    pkt[2] = std::min<std::uint8_t>(activeIdx, 7);
    pkt[3] = std::min<std::uint8_t>(stageCount, 8);
    // DPI values: 8 × uint16-LE at vendor "bytes 8..23" = our pkt[9..24].
    for (std::size_t i = 0; i < 8 && i < dpiValues.size(); ++i) {
        writeUInt16LE(pkt, 9 + i * 2, dpiValues[i]);
    }
    // Colour table: 8 × {R,G,B} at vendor "bytes 40..63" = our pkt[41..64].
    // Stage 7 B-channel at pkt[64] = checksum slot — stampBit7Checksum()
    // overwrites it on the wire. Vendor app tolerates this; UI must surface
    // the limitation to users (8th-stage colour swatch greyed out).
    for (std::size_t i = 0; i < 8 && i < colours.size(); ++i) {
        std::size_t const base = 41 + i * 3;
        if (base < kReportSize)
            pkt[base + 0] = colours[i][0];
        if (base + 1 < kReportSize)
            pkt[base + 1] = colours[i][1];
        // base+2 may reach pkt[64] which is the checksum slot — write it,
        // accept that stampBit7Checksum() will overwrite for stage 7.
        if (base + 2 < kReportSize)
            pkt[base + 2] = colours[i][2];
    }
    stampBit7Checksum(pkt);
    return pkt;
}

std::array<std::uint8_t, kReportSize> buildDpiTable(ajazz::core::DpiTable const& table) {
    // Spec-correct §3.10 envelope (companion to buildMouseSetOption1, which
    // omits the per-profile slot byte). Byte map: pkt[2]=profile,
    // pkt[3]=activeStage, pkt[4]=stageCount, pkt[9..24]=8×uint16-LE DPI,
    // pkt[41..64]=8×{R,G,B} indicator colours. See aj_series_protocol.hpp
    // doc-comment for the full vendor-byte → pkt index mapping.
    auto pkt = startReport(FeaCmd::MouseSetOption1);
    pkt[2] = std::min<std::uint8_t>(table.profile, 7);     // vendor byte 1
    pkt[3] = std::min<std::uint8_t>(table.activeStage, 7); // vendor byte 2
    // stageCount: vendor docs say 1..8 enabled stages; clamp to 8 (upper),
    // leave 0 alone — callers may legitimately disable every stage.
    pkt[4] = std::min<std::uint8_t>(table.stageCount, 8);  // vendor byte 3
    // bytes 5..8 stay zero (vendor "bytes 4..7" reserved).
    // 8 × uint16-LE DPI values at vendor "bytes 8..23" = our pkt[9..24].
    for (std::size_t i = 0; i < table.stages.size(); ++i) {
        writeUInt16LE(pkt, 9 + i * 2, table.stages[i].dpi);
    }
    // 8 × {R,G,B} indicator colours at vendor "bytes 40..63" = pkt[41..64].
    // Stage 7 B-channel at pkt[64] = checksum slot — stampBit7Checksum()
    // overwrites it on the wire. Same vendor bug as buildMouseSetOption1.
    for (std::size_t i = 0; i < table.stages.size(); ++i) {
        std::size_t const base = 41 + i * 3;
        if (base < kReportSize)
            pkt[base + 0] = table.stages[i].indicator.r;
        if (base + 1 < kReportSize)
            pkt[base + 1] = table.stages[i].indicator.g;
        // base+2 may reach pkt[64] which is the checksum slot — write it,
        // accept that stampBit7Checksum() will overwrite for stage 7.
        if (base + 2 < kReportSize)
            pkt[base + 2] = table.stages[i].indicator.b;
    }
    stampBit7Checksum(pkt);
    return pkt;
}

std::array<std::uint8_t, kReportSize> buildMouseSetOption0(OptionPacket0 const& opts) {
    auto pkt = startReport(FeaCmd::MouseSetOption0);
    // bytes 2..8 stay zero (vendor "bytes 1..7")
    pkt[9] = std::min<std::uint8_t>(opts.profile, 7); // vendor byte 8
    pkt[10] = pollRateToWireCode(opts.pollRateHz);    // vendor byte 9
    pkt[11] = opts.debounceMs;                        // vendor byte 10
    // pkt[12] zero
    writeUInt16LE(pkt, 13, opts.flags); // vendor bytes 12..13
    pkt[15] = opts.buttonChange;        // vendor byte 14
    pkt[16] = opts.wheelToButton;       // vendor byte 15
    pkt[17] = opts.buttonToWheel;       // vendor byte 16
    // pkt[18..24] zero (vendor bytes 17..23)
    std::memcpy(&pkt[25], opts.ledBlock.data(), 8);     // vendor bytes 24..31
    std::memcpy(&pkt[33], opts.logoLedBlock.data(), 8); // vendor bytes 32..39
    writeUInt16LE(pkt, 41, opts.sleepBtIdleSec);        // vendor bytes 40..41
    writeUInt16LE(pkt, 43, opts.sleepBtDeepSec);        // vendor bytes 42..43
    writeUInt16LE(pkt, 45, opts.sleep24gIdleSec);       // vendor bytes 44..45
    writeUInt16LE(pkt, 47, opts.sleep24gDeepSec);       // vendor bytes 46..47
    // pkt[49..50] zero (vendor bytes 48..49)
    pkt[51] = std::min<std::uint8_t>(opts.xSensitivity, 100); // vendor byte 50
    pkt[52] = std::min<std::uint8_t>(opts.ySensitivity, 100); // vendor byte 51
    pkt[53] = std::min<std::uint8_t>(opts.liftCutOff, 2);     // vendor byte 52
    pkt[54] = opts.angleSnap ? 1 : 0;                         // vendor byte 53
    pkt[55] = opts.batteryColorHigh[0];                       // vendor byte 54
    pkt[56] = opts.batteryColorHigh[1];                       // vendor byte 55
    pkt[57] = opts.batteryColorHigh[2];                       // vendor byte 56
    pkt[58] = opts.batteryColorLow[0];                        // vendor byte 57
    pkt[59] = opts.batteryColorLow[1];                        // vendor byte 58
    pkt[60] = opts.batteryColorLow[2];                        // vendor byte 59
    pkt[61] = opts.chargingSwitch ? 1 : 0;                    // vendor byte 60
    // pkt[62] zero (vendor byte 61)
    stampBit7Checksum(pkt);
    return pkt;
}

std::array<std::uint8_t, kReportSize> buildMouseSettings(std::uint8_t profile,
                                                          std::uint16_t pollRate,
                                                          ajazz::core::MouseSettings const& s) {
    // Clamp scalar fields to their documented valid ranges so the firmware
    // sees only bytes it accepts. Out-of-range inputs MUST produce on-wire
    // bytes the firmware accepts (per task spec). The host-side cache reader
    // mirrors this clamping in AjSeriesMouse::setMouseSettings().
    constexpr std::uint8_t kDebounceMaxMs = 10; // §3.9: "0..10 typical".
    constexpr std::uint8_t kSensitivityMaxPercent = 100;
    constexpr std::uint8_t kLodMax = 2; // 0=1mm, 1=2mm, 2=3mm.

    OptionPacket0 opts{};
    opts.profile = profile;
    opts.pollRateHz = pollRate;
    opts.debounceMs = std::min<std::uint8_t>(s.debounceMs, kDebounceMaxMs);

    // Pack five named flag bits per §3.9 (bits 0..4 of bytes 12..13 uint16-LE).
    // Bits 5..15 are reserved/undecoded and intentionally left zero.
    std::uint16_t flags = 0;
    if (s.lightOff)         flags |= 1u << 0;
    if (s.wheelLightOff)    flags |= 1u << 1;
    if (s.motionSmoothing)  flags |= 1u << 2;
    if (s.batteryLedSelect) flags |= 1u << 3;
    if (s.powerSaveMode)    flags |= 1u << 4;
    opts.flags = flags;

    // Vendor defaults for the three undecoded bytes (§3.9: "default 1",
    // "default 10", "default 10"). Exposed on OptionPacket0 already so we
    // simply mirror its defaults verbatim — no MouseSettings field decoded.
    opts.buttonChange = 1;
    opts.wheelToButton = 10;
    opts.buttonToWheel = 10;

    // LED + logo-LED sub-blocks intentionally zero — see header doc; the
    // AjSeriesMouse setter wires its cached blocks back in before send.

    opts.sleepBtIdleSec = s.sleepBtIdleSec;
    opts.sleepBtDeepSec = s.sleepBtDeepSec;
    opts.sleep24gIdleSec = s.sleep24gIdleSec;
    opts.sleep24gDeepSec = s.sleep24gDeepSec;

    opts.xSensitivity = std::min<std::uint8_t>(s.xSensitivity, kSensitivityMaxPercent);
    opts.ySensitivity = std::min<std::uint8_t>(s.ySensitivity, kSensitivityMaxPercent);

    opts.liftCutOff =
        std::min<std::uint8_t>(static_cast<std::uint8_t>(s.liftOffDistance), kLodMax);
    opts.angleSnap = s.angleSnap ? 1 : 0;

    opts.batteryColorHigh = {s.batteryLedHigh.r, s.batteryLedHigh.g, s.batteryLedHigh.b};
    opts.batteryColorLow = {s.batteryLedLow.r, s.batteryLedLow.g, s.batteryLedLow.b};

    opts.chargingSwitch = s.chargingSwitch ? 1 : 0;

    // Delegate to the existing low-level builder so the byte layout +
    // BIT7 checksum stay defined in one place (§3.9). Any future tweak
    // to the byte map need only land in buildMouseSetOption0().
    return buildMouseSetOption0(opts);
}

std::array<std::uint8_t, kReportSize>
buildSetTftLcdData(std::uint8_t frame, std::uint8_t frameCount, std::uint8_t frameDelayMs,
                   std::uint16_t chunkIndex, std::span<std::uint8_t const> payload) {
    std::array<std::uint8_t, kReportSize> pkt{};
    pkt[0] = kReportId;
    pkt[1] = static_cast<std::uint8_t>(FeaCmd::SetTftLcdData); // vendor byte 0
    pkt[2] = frame;                                            // vendor byte 1
    pkt[3] = frameCount;                                       // vendor byte 2
    pkt[4] = frameDelayMs;                                     // vendor byte 3
    pkt[5] = static_cast<std::uint8_t>(chunkIndex & 0xFFU);    // vendor byte 4 (LE lo)
    pkt[6] = static_cast<std::uint8_t>((chunkIndex >> 8U) & 0xFFU); // vendor byte 5 (LE hi)
    auto const n = std::min(payload.size(), kTftChunkPayloadBytes);
    pkt[7] = static_cast<std::uint8_t>(n);                     // vendor byte 6 (chunkLen)
    pkt[8] = 0;                                                 // vendor byte 7 (reserved)
    for (std::size_t i = 0; i < n; ++i) {
        pkt[9 + i] = payload[i];                                // vendor bytes 8..(8+n-1)
    }
    // pkt[9 + n .. 63] are left zero so the BIT7 checksum at pkt[64] is
    // deterministic regardless of how many bytes the caller supplied.
    stampBit7Checksum(pkt);
    return pkt;
}

} // namespace ajazz::mouse::aj_series
