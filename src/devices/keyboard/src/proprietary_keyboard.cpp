// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file proprietary_keyboard.cpp
 * @brief IDevice backend for AJAZZ proprietary-protocol keyboards.
 *
 * Covers the AK680, AK510, and similar "gaming" keyboards that ship with the
 * closed-source Windows configuration tool.  The wire protocol is a clean-room
 * reconstruction from USB captures; see
 * docs/protocols/keyboard/proprietary.md for the authoritative byte-level
 * reference.  No vendor firmware, driver, or SDK code is reused.
 *
 * The file is split into two parts:
 * -# Pure protocol helpers (namespace proprietary) — stateless, unit-testable.
 * -# ProprietaryKeyboard class — the IDevice implementation that owns the
 *    HID transport and routes I/O to those helpers.
 */
//
#include "ajazz/core/capabilities.hpp"
#include "ajazz/core/device.hpp"
#include "ajazz/core/hid_transport.hpp"
#include "ajazz/core/logger.hpp"
#include "ajazz/keyboard/ak980_lighting.hpp"
#include "ajazz/keyboard/keyboard.hpp"
#include "proprietary_protocol.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <mutex>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace ajazz::keyboard {

namespace proprietary {

// -----------------------------------------------------------------------------
// Pure protocol helpers — every command below is verifiable in isolation.
// Implementations live here; declarations and contracts in proprietary_protocol.hpp.
// -----------------------------------------------------------------------------

/**
 * @brief Build the firmware-version query report (CmdGetFirmwareVersion).
 * @return 64-byte report with command 0x01 and all payload bytes zeroed.
 */
std::array<std::uint8_t, ReportSize> buildGetFirmwareVersion() {
    return makeReport(CmdGetFirmwareVersion);
}

/**
 * @brief Build a set-keycode report (CmdSetKeycode, 0x05).
 *
 * Payload layout: layer(2) row(3) col(4) keycode-hi(5) keycode-lo(6).
 * The keycode is encoded big-endian.
 *
 * @param layer    Layer index (0-based).
 * @param row      Key-matrix row.
 * @param col      Key-matrix column.
 * @param keycode  HID usage code.
 */
std::array<std::uint8_t, ReportSize>
buildSetKeycode(std::uint8_t layer, std::uint8_t row, std::uint8_t col, std::uint16_t keycode) {
    auto pkt = makeReport(CmdSetKeycode);
    pkt[2] = layer;
    pkt[3] = row;
    pkt[4] = col;
    pkt[5] = static_cast<std::uint8_t>((keycode >> 8) & 0xffu);
    pkt[6] = static_cast<std::uint8_t>(keycode & 0xffu);
    return pkt;
}

/**
 * @brief Build a static RGB report (CmdSetRgbStatic, 0x08).
 *
 * Payload layout: zone(2) R(3) G(4) B(5).
 *
 * @param zone  Zone id (ZoneKeys / ZoneSides / ZoneLogo).
 * @param r     Red component 0–255.
 * @param g     Green component 0–255.
 * @param b     Blue component 0–255.
 */
std::array<std::uint8_t, ReportSize>
buildSetRgbStatic(std::uint8_t zone, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    auto pkt = makeReport(CmdSetRgbStatic);
    pkt[2] = zone;
    pkt[3] = r;
    pkt[4] = g;
    pkt[5] = b;
    return pkt;
}

/**
 * @brief Build an RGB animation report (CmdSetRgbEffect, 0x09).
 *
 * Payload layout: zone(2) effect-id(3) speed(4).
 *
 * @param zone      Zone id.
 * @param effectId  Firmware animation preset index.
 * @param speed     Animation speed 0–255.
 */
std::array<std::uint8_t, ReportSize>
buildSetRgbEffect(std::uint8_t zone, std::uint8_t effectId, std::uint8_t speed) {
    auto pkt = makeReport(CmdSetRgbEffect);
    pkt[2] = zone;
    pkt[3] = effectId;
    pkt[4] = speed;
    return pkt;
}

/**
 * @brief Build a brightness report (CmdSetRgbBrightness, 0x0b).
 *
 * @param percent  Global brightness 0–100; clamped before encoding.
 */
std::array<std::uint8_t, ReportSize> buildSetRgbBrightness(std::uint8_t percent) {
    auto pkt = makeReport(CmdSetRgbBrightness);
    pkt[2] = std::min<std::uint8_t>(percent, 100);
    return pkt;
}

/**
 * @brief Build a layer-switch report (CmdSetLayer, 0x0c).
 *
 * @param layer  Target layer index; clamped to MaxLayers–1 (3) if out of range.
 */
std::array<std::uint8_t, ReportSize> buildSetLayer(std::uint8_t layer) {
    auto pkt = makeReport(CmdSetLayer);
    pkt[2] = std::min<std::uint8_t>(layer, static_cast<std::uint8_t>(MaxLayers - 1));
    return pkt;
}

/**
 * @brief Build an EEPROM commit report (CmdCommitEeprom, 0x0e).
 *
 * Instructs the firmware to persist any staged configuration changes.
 * Must be issued after all key-remap or macro upload reports.
 */
std::array<std::uint8_t, ReportSize> buildCommitEeprom() {
    return makeReport(CmdCommitEeprom);
}

/**
 * @brief Build the time-sync preamble packet — control packet for opcode 0x28.
 *
 * Uses the default ReportId=0x04 + CmdSetTime=0x28 at byte 1 + sentinel 0x01 at
 * byte 8. The firmware reads this as "next packet is a CMD_TIME configuration
 * data block, not a CMD_SAVE acknowledgement".
 */
// Time-sync packets are 65-byte feature reports whose HID Report ID is 0x00
// (pkt[0], from value-init). The 0x04 (ReportId constant) that other commands
// place at byte 0 is, for these reports, the first *data* byte at pkt[1] —
// hardware-verified via a Frida capture of DeviceDriver.exe (2026-05-21): only
// this layout makes the AK980 PRO TFT clock follow an injected time. The earlier
// ARCH-05.1 layout (report id 0x04, 64-byte) was off by one and silently ignored.
std::array<std::uint8_t, TimeReportSize> buildSetTimeStart() {
    std::array<std::uint8_t, TimeReportSize> pkt{};
    pkt[1] = ReportId;     // 0x04 — first data byte (HID report id is pkt[0]=0x00)
    pkt[2] = CmdStartTime; // 0x18
    return pkt;
}

std::array<std::uint8_t, TimeReportSize> buildSetTimePreamble() {
    std::array<std::uint8_t, TimeReportSize> pkt{};
    pkt[1] = ReportId;   // 0x04
    pkt[2] = CmdSetTime; // 0x28
    pkt[9] = 0x01;       // configure-mode marker
    return pkt;
}

/**
 * @brief Build the 65-byte time-data packet (HID Report ID 0x00, magic 0x5A).
 *
 * See proprietary_protocol.hpp for the full byte spec. Year saturates at the
 * 2000 floor so calling with std::chrono::system_clock epoch (year 1970) does
 * not underflow into a uint8 wrap.
 */
std::array<std::uint8_t, TimeReportSize> buildSetTimeData(std::uint16_t year,
                                                          std::uint8_t month,
                                                          std::uint8_t day,
                                                          std::uint8_t hour,
                                                          std::uint8_t minute,
                                                          std::uint8_t second,
                                                          std::uint8_t dayOfWeek) {
    std::array<std::uint8_t, TimeReportSize> pkt{};
    // pkt[0] = 0x00 HID report id (value-init).
    pkt[1] = 0x00;
    pkt[2] = 0x01; // LCD-select index + 1 (single-LCD device => 1)
    pkt[3] = 0x5a; // magic
    pkt[4] = (year >= 2000) ? static_cast<std::uint8_t>(year - 2000) : 0;
    pkt[5] = month;
    pkt[6] = day;
    pkt[7] = hour;
    pkt[8] = minute;
    pkt[9] = second;
    pkt[10] = 0x00;
    pkt[11] = (dayOfWeek <= 6) ? dayOfWeek : 0; // 0=Sunday..6=Saturday
    // bytes 12..62 stay 0x00 from value-init.
    pkt[TimeReportSize - 2] = 0xaa; // [63]
    pkt[TimeReportSize - 1] = 0x55; // [64]
    return pkt;
}

/**
 * @brief Build the time-sync save packet — wire bytes `00 04 02 …`.
 *
 * Distinct from buildCommitEeprom() (opcode 0x0E for keymap / RGB / macro
 * state). The RTC has its own dedicated save opcode 0x02.
 */
std::array<std::uint8_t, TimeReportSize> buildSetTimeSave() {
    std::array<std::uint8_t, TimeReportSize> pkt{};
    pkt[1] = ReportId;   // 0x04
    pkt[2] = CmdSaveRtc; // 0x02
    return pkt;
}

/**
 * @brief Build the AK980 PRO settings-batch DATA packet (opcode 0x07 sub 0x10).
 *
 * Byte layout per ak980pro_vendor.md §13.2 (Ghidra decompile of
 * DeviceDriver.exe FUN_0044eed0 callers):
 *
 *   [0]  ReportId 0x04
 *   [1]  CmdSettingsBatch 0x07
 *   [2]  SettingsBatchSub 0x10
 *   [6]  disableWinKey  (0/1)
 *   [7]  disableAltF4   (0/1)
 *   [8]  disableAltTab  (0/1)
 *   [9]  fnLayerSwitch  (0=hold, 1=toggle)
 *   [10] sleepTimerMinutes (vendor enum: 0/1/3/5/10/30)
 *   [12] keyResponseTimeLevel (1..5)
 *   [18] SettingsBatchTrailerHi 0xAA
 *   [19] SettingsBatchTrailerLo 0x55
 *
 * All other bytes are zero-initialised. Issue #57 / P3.x.
 */
std::array<std::uint8_t, ReportSize> buildSettingsBatch(std::uint8_t fnLayerSwitch,
                                                        std::uint8_t sleepTimerMinutes,
                                                        std::uint8_t keyResponseTimeLevel) {
    auto pkt = makeReport(CmdSettingsBatch);
    pkt[2] = SettingsBatchSub;
    pkt[kSettingsByteFnSwitch] = fnLayerSwitch;
    pkt[kSettingsByteSleepTime] = sleepTimerMinutes;
    // Vendor clamps response-time to [1..5]; 0 falls back to the default 3.
    std::uint8_t const responseClamped =
        std::clamp<std::uint8_t>(keyResponseTimeLevel == 0 ? 3 : keyResponseTimeLevel, 1, 5);
    pkt[kSettingsByteKeyResponseTime] = responseClamped;
    pkt[kSettingsByteTrailerHi] = SettingsBatchTrailerHi;
    pkt[kSettingsByteTrailerLo] = SettingsBatchTrailerLo;
    return pkt;
}

std::array<std::uint8_t, ReportSize> buildBatteryQuery() {
    // byte 0 = 0x00 HID report id (FUN_004358c0:26 leaves it zero), opcode 0x20
    // at byte 1, sub 0x01 at byte 2. NOT makeReport (which puts 0x04 at byte 0):
    // hardware-confirmed 2026-05-21 the device only replies when the feature
    // report carries report id 0x00.
    std::array<std::uint8_t, ReportSize> pkt{};
    pkt[1] = CmdBatteryQuery; // 0x20
    pkt[2] = BatteryQuerySub; // 0x01 — discriminates battery from per-key RGB (sub 0x04)
    return pkt;
}

std::array<std::uint8_t, ReportSize> buildPerKeyRgbWriteHeader(bool isWireless) {
    auto pkt = makeReport(kCmdPerKeyRgbWrite);
    pkt[2] = kPerKeyRgbSub; // 0x04 — discriminates per-key RGB from battery query (sub 0x01)
    pkt[9] = isWireless ? kPerKeyModeWireless : kPerKeyModeWired;
    return pkt;
}

std::array<std::uint8_t, ReportSize> buildPerKeyRgbReadback(bool isWireless) {
    auto pkt = makeReport(kCmdPerKeyRgbReadback);
    pkt[2] = isWireless ? kPerKeyReadbackWirelessSub : kPerKeyReadbackWiredSub;
    return pkt;
}

// Stamp the chunked-TFT transport checksum at byte 32.
//
// The vendor's output-report helper FUN_0044f5f0 sums every byte of its 65-byte
// report and stores the low 8 bits at `param_1[8]` — byte offset 32 — right
// before the 33-byte WriteFile (FUN_0044f5f0:43-50,68). The checksum slot is
// zero at sum time and the report tail is zero, so for our packets this reduces
// to sum(bytes[0..31]) mod 256. PROVISIONAL: the firmware may ignore it (no
// hardware capture yet) — see ak980pro_tft_protocol.md §7.
void stampTftChecksum(std::array<std::uint8_t, ReportSize>& pkt) {
    constexpr std::size_t kChecksumByte = 32;
    std::uint32_t sum = 0;
    for (std::size_t i = 0; i < pkt.size(); ++i) {
        if (i == kChecksumByte) {
            continue;
        }
        sum += pkt[i];
    }
    pkt[kChecksumByte] = static_cast<std::uint8_t>(sum & 0xffu);
}

std::array<std::uint8_t, 3> encodeTftChunkIndex(std::uint32_t chunkIdx) {
    // Vendor layout (FUN_004231c0:284,287): the 0x80 chunk marker shares byte 1
    // with the high 7 bits of the index, byte 2 carries the low 8 bits, byte 3
    // the middle 8 bits. Decoder: idx = byte2 | (byte3<<8) | ((byte1 & 0x7f)<<16).
    return {
        static_cast<std::uint8_t>(0x80u |
                                  ((chunkIdx >> 16) & 0x7fu)), // byte 1: 0x80 marker | high 7 bits
        static_cast<std::uint8_t>(chunkIdx & 0xffu),           // byte 2: low 8 bits
        static_cast<std::uint8_t>((chunkIdx >> 8) & 0xffu),    // byte 3: middle 8 bits
    };
}

std::array<std::uint8_t, ReportSize> buildTftChunkedHeader(std::uint8_t lcdSelect,
                                                           std::uint32_t totalChunks) {
    std::array<std::uint8_t, ReportSize> pkt{};
    // ReportId byte 0 stays 0x00 per ak980pro_tft_protocol.md §2 (the TFT path
    // does NOT use the default 0x04 ReportId; the vendor's FUN_004231c0 fills
    // the local report buffer with byte 0 = 0x00).
    pkt[0] = 0x00;
    pkt[1] = CmdScreenHeader;   // 0x7F  (FUN_004231c0:252, low byte of 0x037F)
    pkt[2] = CmdScreenSubBegin; // 0x03  (FUN_004231c0:252, high byte of 0x037F)
    pkt[3] = 0x00;              // FUN_004231c0:257 (local_58._3_1_ = 0)
    // LCD-select is 1-based at byte 4 (FUN_004231c0:253-255), NOT byte 3.
    pkt[4] = static_cast<std::uint8_t>(lcdSelect + 1u);
    // 24-bit total-chunk count at bytes 5..7 (FUN_004231c0:253-256). A 140-frame
    // GIF caps at ~324 100 chunks, so 24 bits is ample.
    pkt[5] = static_cast<std::uint8_t>(totalChunks & 0xffu);
    pkt[6] = static_cast<std::uint8_t>((totalChunks >> 8) & 0xffu);
    pkt[7] = static_cast<std::uint8_t>((totalChunks >> 16) & 0xffu);
    stampTftChecksum(pkt);
    return pkt;
}

std::array<std::uint8_t, ReportSize>
buildTftChunkedPayload(std::uint32_t chunkIdx,
                       std::span<std::uint8_t const, kTftChunkPayload> payload) {
    std::array<std::uint8_t, ReportSize> pkt{};
    // ReportId byte 0 stays 0x00 (same rule as the chunked header — §2).
    pkt[0] = 0x00;
    auto const idx = encodeTftChunkIndex(chunkIdx);
    pkt[1] = idx[0]; // 0x80 marker | high 7 bits of chunk index
    pkt[2] = idx[1]; // chunk index low 8 bits
    pkt[3] = idx[2]; // chunk index middle 8 bits
    // 28-byte RGB565 payload at bytes 4..31 (§3.3). Byte 32 then carries the
    // transport checksum; bytes 33..63 stay zero from value-init.
    std::memcpy(pkt.data() + 4, payload.data(), kTftChunkPayload);
    stampTftChecksum(pkt);
    return pkt;
}

std::vector<std::uint8_t>
encodeRgb565(std::span<std::uint8_t const> rgba, std::uint16_t width, std::uint16_t height) {
    if (width == 0 || height == 0 || rgba.empty()) {
        return {};
    }
    auto const expected = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u;
    if (rgba.size() != expected) {
        return {};
    }
    std::vector<std::uint8_t> out;
    out.reserve(kTftFrameBytes);
    // Nearest-neighbour resample to the panel's native 240x135 + per-pixel
    // BE RGB565 pack, fused into a single top-down row-major walk so we
    // only allocate the output buffer once. Source coordinates are computed
    // with the half-pixel offset (+0.5 then floor) so a 1:1 source maps
    // exactly without an off-by-one bias on the right/bottom edges.
    auto const srcW = static_cast<std::size_t>(width);
    auto const srcH = static_cast<std::size_t>(height);
    for (std::size_t dy = 0; dy < kTftHeight; ++dy) {
        // sy = floor((dy + 0.5) * srcH / kTftHeight) without floats:
        //     = ((dy * 2 + 1) * srcH) / (2 * kTftHeight)
        std::size_t const sy =
            std::min<std::size_t>(((dy * 2u + 1u) * srcH) / (2u * kTftHeight), srcH - 1u);
        for (std::size_t dx = 0; dx < kTftWidth; ++dx) {
            std::size_t const sx =
                std::min<std::size_t>(((dx * 2u + 1u) * srcW) / (2u * kTftWidth), srcW - 1u);
            std::size_t const pix = (sy * srcW + sx) * 4u;
            auto const r = static_cast<std::uint16_t>((rgba[pix + 0] >> 3) & 0x1fu);
            auto const g = static_cast<std::uint16_t>((rgba[pix + 1] >> 2) & 0x3fu);
            auto const b = static_cast<std::uint16_t>((rgba[pix + 2] >> 3) & 0x1fu);
            auto const rgb565 = static_cast<std::uint16_t>((r << 11) | (g << 5) | b);
            // Big-endian on the wire per §6: high byte first, low byte second.
            out.push_back(static_cast<std::uint8_t>((rgb565 >> 8) & 0xffu));
            out.push_back(static_cast<std::uint8_t>(rgb565 & 0xffu));
        }
    }
    return out;
}

std::array<std::uint8_t, ReportSize> buildScreenBulkBegin(std::uint8_t lcdSelect,
                                                          std::uint16_t total4kChunks) {
    // Bulk-begin is a FEATURE report (FUN_00422920 -> FUN_0044eed0 ->
    // HidD_SetFeature), unlike the chunked path's output reports. FUN_0044eed0
    // memsets a 65-byte buffer (byte 0 = report id 0x00) and copies the caller
    // frame starting at byte 1, so the on-wire layout is:
    //   [0]=0x00 report id  [1]=0x04 frame byte  [2]=0x72 opcode
    //   [3]=lcdSelect (1-based)  [9]=count low  [10]=count high
    // (FUN_00422920:267-270 stores 0x7204 + lcd@offset2 + count@offset8/9 in
    // the pre-prepend buffer, which the +1 report-id shift maps to wire 3/9/10).
    // PROVISIONAL: scaffolded, not wired into uploadTftImage — no hardware
    // capture of the bulk path yet. See ak980pro_tft_protocol.md §4.
    std::array<std::uint8_t, ReportSize> pkt{};
    pkt[0] = 0x00;
    pkt[1] = ReportId;           // 0x04
    pkt[2] = CmdScreenBulkBegin; // 0x72
    pkt[3] = static_cast<std::uint8_t>(lcdSelect + 1u);
    pkt[9] = static_cast<std::uint8_t>(total4kChunks & 0xffu);
    pkt[10] = static_cast<std::uint8_t>((total4kChunks >> 8) & 0xffu);
    return pkt;
}

std::array<std::uint8_t, ReportSize> buildSetRgbModeData(std::uint8_t modeId,
                                                         std::uint8_t r,
                                                         std::uint8_t g,
                                                         std::uint8_t b,
                                                         std::uint8_t rainbow,
                                                         std::uint8_t brightness,
                                                         std::uint8_t speed,
                                                         std::uint8_t direction) {
    std::array<std::uint8_t, ReportSize> pkt{};
    pkt[0] = ReportId;
    // mode_id lives at byte 1 per ak980pro_vendor.md §3.4 (NOT at byte 2 like
    // the battery query sub-cmd — the 0x13 opcode is the MODE_BEGIN packet
    // that immediately precedes this DATA packet; the data itself uses byte 1
    // for the actual mode value).
    pkt[1] = modeId;
    pkt[2] = r;
    pkt[3] = g;
    pkt[4] = b;
    pkt[8] = (rainbow != 0) ? 0x01 : 0x00;
    pkt[9] = std::min<std::uint8_t>(brightness, 5);
    pkt[10] = std::min<std::uint8_t>(speed, 5);
    pkt[11] = std::min<std::uint8_t>(direction, 3);
    pkt[14] = 0x55;
    pkt[15] = 0xaa;
    return pkt;
}

/**
 * @brief Return the LED count for a given zone id.
 *
 * @param zone  Zone id constant (ZoneKeys, ZoneSides, or ZoneLogo).
 * @return      LED count, or 0 for an unrecognised zone.
 */
std::uint16_t ledCountForZone(std::uint8_t zone) {
    switch (zone) {
    case ZoneKeys:
        return LedCountKeys;
    case ZoneSides:
        return LedCountSides;
    case ZoneLogo:
        return LedCountLogo;
    default:
        return 0;
    }
}

/**
 * @brief Translate a zone name string to its numeric id.
 *
 * @param name  One of @c "keys", @c "sides", or @c "logo" (case-sensitive).
 * @return      Numeric zone id, or @c 0xFF if the name is not recognised.
 */
std::uint8_t zoneIdFromName(std::string_view name) {
    if (name == "keys") {
        return ZoneKeys;
    }
    if (name == "sides") {
        return ZoneSides;
    }
    if (name == "logo") {
        return ZoneLogo;
    }
    return 0xff;
}

} // namespace proprietary

namespace {

using namespace ajazz::core;
using namespace ajazz::keyboard::proprietary;

/**
 * @brief IDevice backend for proprietary-protocol AJAZZ keyboards.
 *
 * Implements IDevice, IKeyRemappable, IRgbCapable, and IFirmwareCapable
 * using the reverse-engineered HID command set documented in
 * docs/protocols/keyboard/proprietary.md.
 *
 * @note Thread-safe for concurrent onEvent() / poll() calls; a single
 *       mutex guards the event callback.
 * @see makeProprietaryKeyboard()
 */
class ProprietaryKeyboard final : public IDevice,
                                  public IKeyRemappable,
                                  public IRgbCapable,
                                  public IFirmwareCapable,
                                  public IClockCapable,
                                  public IBatteryCapable,
                                  public IFirmwareLightingCapable,
                                  public ISettingsCapable,
                                  public ITftDisplayCapable {
public:
    /** Production constructor — creates a real HID transport. */
    ProprietaryKeyboard(DeviceDescriptor descriptor, DeviceId id)
        : ProprietaryKeyboard(descriptor,
                              id,
                              makeHidTransport(id.vendorId,
                                               id.productId,
                                               id.serial,
                                               descriptor.controlUsagePage,
                                               descriptor.controlUsage)) {}

    /** Test constructor — accepts an injected transport (DI for unit tests). */
    ProprietaryKeyboard(DeviceDescriptor descriptor, DeviceId id, TransportPtr transport)
        : m_descriptor(std::move(descriptor)), m_id(std::move(id)),
          m_transport(std::move(transport)) {}

    // ---- IDevice ------------------------------------------------------------
    [[nodiscard]] DeviceDescriptor const& descriptor() const noexcept override {
        return m_descriptor;
    }
    [[nodiscard]] DeviceId id() const noexcept override { return m_id; }

    [[nodiscard]] std::string firmwareVersion() const override {
        auto const pkt = buildGetFirmwareVersion();
        try {
            (void)m_transport->write(pkt);
            std::array<std::uint8_t, ReportSize> resp{};
            auto const n = m_transport->read(resp, std::chrono::milliseconds{100});
            if (n >= 5) {
                char buf[16]{};
                (void)std::snprintf(buf, sizeof(buf), "%u.%u.%u", resp[2], resp[3], resp[4]);
                return std::string{buf};
            }
        } catch (std::exception const& e) {
            // Log the I/O failure (WR-03): a silent catch made a device-yank /
            // transport error indistinguishable from a genuinely unparsable
            // version. Mirrors batteryPercent() / setTime().
            AJAZZ_LOG_WARN("keyboard.ak980", "firmwareVersion: HID I/O failed: {}", e.what());
        }
        return "unknown";
    }

    void open() override {
        if (!m_transport->isOpen()) {
            m_transport->open();
            AJAZZ_LOG_INFO("kbd/proprietary", "device opened: {}", m_descriptor.model);
        }
    }

    void close() override {
        if (m_transport->isOpen()) {
            m_transport->close();
        }
    }

    [[nodiscard]] bool isOpen() const noexcept override { return m_transport->isOpen(); }

    void onEvent(EventCallback cb) override {
        std::lock_guard const lock(m_mutex);
        m_callback = std::move(cb);
    }

    std::size_t poll() override {
        std::array<std::uint8_t, ReportSize> buf{};
        std::size_t emitted = 0;
        for (int i = 0; i < 4; ++i) {
            auto const n = m_transport->read(buf, std::chrono::milliseconds{0});
            if (n == 0) {
                break;
            }
            // Input reports currently surface only the active layer change;
            // per-key HID events travel on the keyboard's standard
            // boot-protocol interface and are consumed by the OS.
            if (n >= 3 && buf[0] == ReportId && buf[1] == CmdSetLayer) {
                DeviceEvent devEv{};
                devEv.kind = DeviceEvent::Kind::KeyPressed;
                devEv.index = 0;
                devEv.value = buf[2];
                EventCallback cb;
                {
                    std::lock_guard const lock(m_mutex);
                    cb = m_callback;
                }
                if (cb) {
                    cb(devEv);
                }
                ++emitted;
            }
        }
        return emitted;
    }

    // ---- IKeyRemappable -----------------------------------------------------
    [[nodiscard]] KeyboardLayout layout() const noexcept override {
        // Conservative TKL layout that fits every supported model; the real
        // numbers come from resources/device-db/keyboards.json once the
        // device database ships.
        return KeyboardLayout{.rows = 6, .cols = 17, .layers = MaxLayers};
    }

    void setKeycode(std::uint8_t layer,
                    std::uint8_t row,
                    std::uint8_t col,
                    std::uint16_t keycode) override {
        auto const pkt = buildSetKeycode(layer, row, col, keycode);
        (void)m_transport->write(pkt);
    }

    [[nodiscard]] std::uint16_t
    keycode(std::uint8_t /*layer*/, std::uint8_t /*row*/, std::uint8_t /*col*/) const override {
        // Read-back uses command 0x04 but its payload format is not yet
        // confirmed across all models; defer until a capture lands.
        throw std::runtime_error("proprietary keyboard keycode read-back not yet implemented");
    }

    void setMacro(std::uint8_t slot, std::span<std::uint8_t const> bytes) override {
        std::size_t offset = 0;
        while (offset < bytes.size()) {
            auto pkt = makeReport(CmdUploadMacro);
            pkt[2] = slot;
            pkt[3] = static_cast<std::uint8_t>((offset >> 8) & 0xffu);
            pkt[4] = static_cast<std::uint8_t>(offset & 0xffu);
            auto const take = std::min<std::size_t>(MacroChunk, bytes.size() - offset);
            pkt[5] = static_cast<std::uint8_t>(take);
            // Bytes [6..7] reserved; payload starts at byte 8.
            std::memcpy(pkt.data() + 8, bytes.data() + offset, take);
            (void)m_transport->write(pkt);
            offset += take;
        }
    }

    void commit() override {
        auto const pkt = buildCommitEeprom();
        (void)m_transport->write(pkt);
    }

    // ---- IRgbCapable --------------------------------------------------------
    [[nodiscard]] std::vector<RgbZone> rgbZones() const override {
        return {RgbZone{.name = "keys", .ledCount = LedCountKeys},
                RgbZone{.name = "sides", .ledCount = LedCountSides},
                RgbZone{.name = "logo", .ledCount = LedCountLogo}};
    }

    void setRgbStatic(std::string_view zone, Rgb color) override {
        auto const zoneId = zoneIdFromName(zone);
        if (zoneId == 0xff) {
            throw std::invalid_argument("unknown RGB zone");
        }
        auto const pkt = buildSetRgbStatic(zoneId, color.r, color.g, color.b);
        (void)m_transport->write(pkt);
    }

    void setRgbEffect(std::string_view zone, RgbEffect effect, std::uint8_t speed) override {
        auto const zoneId = zoneIdFromName(zone);
        if (zoneId == 0xff) {
            throw std::invalid_argument("unknown RGB zone");
        }
        auto const pkt = buildSetRgbEffect(zoneId, static_cast<std::uint8_t>(effect), speed);
        (void)m_transport->write(pkt);
    }

    // KNOWN ISSUE — DEFERRED (Phase 12 code-review CR-01, RE cross-check 2026-05-22).
    // This zone-addressed 0x0A SET_RGB_BUFFER path has TWO problems and is
    // intentionally left unchanged until a hardware round-trip on a physical
    // AK980 PRO resolves them:
    //   1. Off-by-two: RgbBufferChunk is 60 but the header is 6 bytes (id, cmd,
    //      zone, off-hi, off-lo, len), so only ReportSize-6 = 58 payload bytes
    //      fit. The loop writes pkt[5]=take (up to 60) and advances offset by
    //      `take`, while the memcpy below clamps to 58 — dropping 2 bytes/chunk
    //      and claiming a length the report doesn't carry.
    //   2. Wire divergence: docs/protocols/keyboard/ak980pro_vendor.md flags
    //      0x0A as "similar idea, unify" with the Ghidra-confirmed per-key RGB
    //      protocol 0x20/sub-0x04 (see ak980pro_perkey_rgb_protocol.md, which
    //      supersedes vendor §3.7-3.8). That RE-correct path is already built
    //      here as buildPerKeyRgbWriteHeader() (feature reports, 64-byte chunks,
    //      mode byte at pkt[9], NO length byte).
    // This method currently has NO live caller (IRgbCapable::setRgbBuffer is a
    // stub/unused surface) and NO unit test, so the off-by-two corrupts nothing
    // in production today. Do NOT "fix" the constant in isolation: changing a
    // provisional/competing wire value blind risks a regression the RE can't
    // confirm. The proper resolution is to unify on 0x20/0x04 and verify the
    // wired LED-to-byte mapping (RE-flagged unconfirmed) against hardware.
    void setRgbBuffer(std::string_view zone, std::span<Rgb const> colors) override {
        auto const zoneId = zoneIdFromName(zone);
        if (zoneId == 0xff) {
            throw std::invalid_argument("unknown RGB zone");
        }
        auto const expected = ledCountForZone(zoneId);
        if (colors.size() != expected) {
            throw std::invalid_argument("RGB buffer size mismatch for zone");
        }

        // Flatten to a contiguous RGB8 byte buffer, then upload in 60-byte
        // chunks (20 LEDs per report).
        std::vector<std::uint8_t> flat;
        flat.reserve(colors.size() * 3);
        for (auto const& c : colors) {
            flat.push_back(c.r);
            flat.push_back(c.g);
            flat.push_back(c.b);
        }

        std::size_t offset = 0;
        while (offset < flat.size()) {
            auto pkt = makeReport(CmdSetRgbBuffer);
            pkt[2] = zoneId;
            pkt[3] = static_cast<std::uint8_t>((offset >> 8) & 0xffu);
            pkt[4] = static_cast<std::uint8_t>(offset & 0xffu);
            auto const take = std::min<std::size_t>(RgbBufferChunk, flat.size() - offset);
            // Byte 5 reports the length of this chunk.
            pkt[5] = static_cast<std::uint8_t>(take);
            std::memcpy(
                pkt.data() + 6, flat.data() + offset, std::min<std::size_t>(take, ReportSize - 6));
            (void)m_transport->write(pkt);
            offset += take;
        }
    }

    void setRgbBrightness(std::uint8_t percent) override {
        auto const pkt = buildSetRgbBrightness(percent);
        (void)m_transport->write(pkt);
    }

    // ---- IFirmwareCapable ---------------------------------------------------
    [[nodiscard]] FirmwareInfo firmwareInfo() const override {
        return FirmwareInfo{
            .version = firmwareVersion(), .buildDate = {}, .bootloaderAvailable = false};
    }

    std::uint32_t beginFirmwareUpdate(std::span<std::uint8_t const>) override {
        throw std::runtime_error(
            "proprietary keyboard firmware update not yet supported (bootloader unknown)");
    }

    [[nodiscard]] std::uint8_t firmwareUpdateProgress(std::uint32_t) const override { return 0; }

    // ---- IBatteryCapable ----------------------------------------------------
    //
    // AK980 PRO charge level via opcode 0x20 sub 0x01 (request) + feature read
    // (response). Wire format reverse-engineered from DeviceDriver.exe
    // FUN_004358c0 (Ghidra, 2026-05-17) — see docs/protocols/keyboard/
    // ak980pro_vendor.md §3 (opcode table row 0x20 0x01) + roadmap §11.2.
    //
    // Response byte 3 carries the charge percent (0..100). Byte 0 of the
    // response should echo 0x20 — we use that as a sanity check to differentiate
    // a real charge reply from a stray "no battery" feature read (0x00 echo +
    // 0x00 charge means "wired, no battery"; we surface std::nullopt rather
    // than a misleading 0%).
    //
    // Polling cadence is the caller's responsibility — vendor app polls 15 s
    // when wireless. Our QML BatteryIndicator will subscribe to a
    // BatteryService that owns the QTimer.
    [[nodiscard]] std::optional<std::uint8_t> batteryPercent() override {
        try {
            (void)m_transport->writeFeature(buildBatteryQuery());
            // Request/response handshake (like the time-sync path): the device
            // needs a settle window before the GET_FEATURE reply is ready, so we
            // poll. hardware-confirmed 2026-05-21 — a read with no delay returns
            // no usable data. hidapi's get_feature_report returns the report WITH
            // the leading report-id byte (0x00) at index 0 (it does NOT strip it
            // the way the vendor's ReadFile does, FUN_00451300:43-48), so the
            // opcode echo lands at resp[1] and the charge percent at resp[4] —
            // the decompile's resp[3] shifted up by one.
            // 65-byte buffer: hid_get_feature_report on Windows needs at least
            // the device's FeatureReportByteLength (65) or HidD_GetFeature fails.
            // A 64-byte buffer was the silent cause of every GET throwing.
            std::array<std::uint8_t, TimeReportSize> resp{};
            for (int attempt = 0; attempt < 6; ++attempt) {
                std::this_thread::sleep_for(std::chrono::milliseconds{30});
                resp.fill(0); // resp[0]=0x00 = unnumbered feature report id to fetch
                std::size_t n = 0;
                try {
                    n = m_transport->readFeature(resp);
                } catch (std::exception const&) {
                    continue; // transient GET failure; retry within the poll budget
                }
                if (n < 5 || resp[1] != CmdBatteryQuery) {
                    continue; // reply not ready yet / wrong report
                }
                auto const pct = resp[4];
                if (pct == 0) {
                    return std::nullopt; // "no battery" sentinel; do not surface as 0%
                }
                // Wired + full reports 0xFF here; the firmware/vendor app clamp
                // out-of-range readings to 100. Per-percent wireless encoding is
                // unverified (can't drain over USB) — see ak980pro_vendor.md §13.5.
                return std::min<std::uint8_t>(pct, 100);
            }
            return std::nullopt; // no valid reply within the poll budget
        } catch (std::exception const& e) {
            AJAZZ_LOG_WARN(
                "keyboard.ak980", "batteryPercent: HID feature I/O failed: {}", e.what());
            return std::nullopt;
        }
    }

    // ---- IClockCapable ------------------------------------------------------
    //
    // ARCH-05.1 amendment (2026-05-17): AK980 PRO (Sonix SN32F299 family,
    // VID:PID 0x0c45:0x8009) has a firmware RTC reachable via opcode 0x28.
    // Wire format from two independent reverse-engineering corpora:
    //   - github.com/gohv/EPOMAKER-Ajazz-AK820-Pro (src/protocol.rs + usb.rs)
    //   - github.com/KyleBoyer/TFTTimeSync-node    (src/packets.ts + device.ts)
    //
    // 4-packet envelope, each 64 bytes, all sent via HID SET_FEATURE (Control
    // transfer with hid_send_feature_report), NOT HID Output Reports:
    //   1. START    — ReportId=0x04 CMD_START=0x18 byte[8]=0x01
    //   2. PREAMBLE — ReportId=0x04 CMD_TIME=0x28  byte[8]=0x01
    //   3. DATA     — ReportId=0x00 magic 0x5A + year-2000/mm/dd/hh/mm/ss + 0xAA 0x55
    //   4. SAVE     — ReportId=0x04 CMD_SAVE=0x02
    // Followed by a 100ms sleep so the firmware has time to commit before any
    // subsequent HID write reaches it (gohv usb.rs:set_time pattern).
    //
    // CRITICAL: this MUST use writeFeature() (hid_send_feature_report ⇒
    // USB SET_REPORT on the control endpoint), not write() (hid_write ⇒
    // interrupt OUT endpoint). Agent B disassembly of vendor DeviceDriver.exe
    // confirmed it imports HidD_SetFeature for this code path. Earlier draft
    // used write() and was a silent no-op against firmware.
    //
    // VIA-protocol keyboards (ViaKeyboard) still do NOT inherit IClockCapable
    // per D-03 — they are QMK-style with no vendor clock surface, untouched.
    //
    // Vendor app sends LOCAL time (KyleBoyer + gohv both pass through local
    // Date components without UTC normalisation), so we convert from
    // std::chrono::system_clock::time_point via localtime_s/localtime_r.
    [[nodiscard]] TimeSyncResult setTime(std::chrono::system_clock::time_point tp) override {
        auto const tt = std::chrono::system_clock::to_time_t(tp);
        std::tm local{};
#ifdef _WIN32
        if (::localtime_s(&local, &tt) != 0) {
            AJAZZ_LOG_WARN("keyboard.ak980", "setTime: localtime_s failed");
            return TimeSyncResult::IoError;
        }
#else
        if (::localtime_r(&tt, &local) == nullptr) {
            AJAZZ_LOG_WARN("keyboard.ak980", "setTime: localtime_r failed");
            return TimeSyncResult::IoError;
        }
#endif
        auto const year = static_cast<std::uint16_t>(local.tm_year + 1900);
        auto const month = static_cast<std::uint8_t>(local.tm_mon + 1);
        auto const day = static_cast<std::uint8_t>(local.tm_mday);
        auto const hour = static_cast<std::uint8_t>(local.tm_hour);
        auto const minute = static_cast<std::uint8_t>(local.tm_min);
        auto const second = static_cast<std::uint8_t>(local.tm_sec);
        // tm_wday is 0..6 (Sunday..Saturday) per POSIX/Win32.
        auto const dayOfWeek = static_cast<std::uint8_t>(local.tm_wday);

        // HARDWARE-CONFIRMED 2026-05-21 (live AK980 PRO, 0xFF13 collection): the
        // time-sync is a request/RESPONSE handshake. The vendor's FUN_0044eed0
        // does Sleep -> SET_REPORT -> GET_REPORT per packet; sending the four
        // writeFeature()s back-to-back with no readback is a silent no-op on
        // real hardware (the firmware clock does NOT move). Empirically: a
        // per-packet settle delay + a readFeature() readback is what actually
        // commits the new time. readback-only (no delay) and delay-only without
        // a prior session both failed; delay + readback works. See
        // docs/protocols/keyboard/proprietary.md and scripts/ak980_tft_probe.py.
        try {
            std::array<std::uint8_t, TimeReportSize> ack{};
            auto const sendWithAck = [&](std::array<std::uint8_t, TimeReportSize> const& pkt) {
                std::this_thread::sleep_for(std::chrono::milliseconds{30});
                (void)m_transport->writeFeature(pkt);
                // Best-effort GET_REPORT handshake — gives the firmware its
                // inter-packet settle window (mirrors FUN_0044eed0). A readback
                // failure must NOT fail the sync: the writeFeature above is the
                // actual command, and on some firmware states (notably right
                // after SAVE, while the RTC commits to NV-RAM) GET_FEATURE
                // returns no report. Swallowing it keeps a completed write
                // sequence reported as success (green toast, not red IoError).
                try {
                    (void)m_transport->readFeature(ack);
                } catch (std::exception const&) {
                    // handshake-only readback; ignore transport errors here
                }
            };
            sendWithAck(buildSetTimeStart());
            sendWithAck(buildSetTimePreamble());
            sendWithAck(buildSetTimeData(year, month, day, hour, minute, second, dayOfWeek));
            sendWithAck(buildSetTimeSave());
        } catch (std::exception const& e) {
            AJAZZ_LOG_WARN("keyboard.ak980", "setTime: HID writeFeature failed: {}", e.what());
            return TimeSyncResult::IoError;
        }
        // Settle window — firmware commits RTC to NV-RAM, racing a subsequent
        // HID write here can drop the save (gohv usb.rs pattern).
        std::this_thread::sleep_for(std::chrono::milliseconds{100});

        AJAZZ_LOG_INFO("keyboard.ak980",
                       "setTime → device clock set to {:04}-{:02}-{:02} {:02}:{:02}:{:02} (local)",
                       year,
                       month,
                       day,
                       hour,
                       minute,
                       second);
        return TimeSyncResult::Ok;
    }

    // ---- IFirmwareLightingCapable (20 built-in modes via opcode 0x13) -----
    //
    // 5-packet envelope per ak980pro_vendor.md §3.4: CMD_START (0x18) ->
    // CMD_MODE_BEGIN (0x13) -> DATA (mode_id, RGB, brightness, speed) ->
    // CMD_SAVE (0x02) -> CMD_FINISH (0xF0) end-of-envelope sentinel. FINISH
    // was wired in for issue #58 and IS shipped here; the e2e test asserts
    // the full 5-packet sequence ending in 0xF0. (The RTC time-sync path uses
    // the shorter 4-packet variant without FINISH — see setTime() above.)
    [[nodiscard]] std::vector<FirmwareLightingMode> availableFirmwareModes() const override {
        // Names match the vendor 1033.lan English strings (cross-referenced
        // with the Chinese originals documented in ak980_lighting.hpp).
        return {
            {static_cast<std::uint8_t>(AK980LightingMode::Static), "Static"},
            {static_cast<std::uint8_t>(AK980LightingMode::SingleOn), "Single light on"},
            {static_cast<std::uint8_t>(AK980LightingMode::SingleOff), "Single light off"},
            {static_cast<std::uint8_t>(AK980LightingMode::Glittering), "Glittering stars"},
            {static_cast<std::uint8_t>(AK980LightingMode::Falling), "Falling pixels"},
            {static_cast<std::uint8_t>(AK980LightingMode::Colourful), "Rainbow blanket"},
            {static_cast<std::uint8_t>(AK980LightingMode::Breath), "Dynamic breath"},
            {static_cast<std::uint8_t>(AK980LightingMode::Spectrum), "Spectrum rings"},
            {static_cast<std::uint8_t>(AK980LightingMode::Outward), "Outward wave"},
            {static_cast<std::uint8_t>(AK980LightingMode::Scrolling), "Horizontal scroll"},
            {static_cast<std::uint8_t>(AK980LightingMode::Rolling), "Rolling glow"},
            {static_cast<std::uint8_t>(AK980LightingMode::Rotating), "Rotating accents"},
            {static_cast<std::uint8_t>(AK980LightingMode::Explode), "Press burst"},
            {static_cast<std::uint8_t>(AK980LightingMode::Launch), "Launch trail"},
            {static_cast<std::uint8_t>(AK980LightingMode::Ripples), "Ripples"},
            {static_cast<std::uint8_t>(AK980LightingMode::Flowing), "Continuous flow"},
            {static_cast<std::uint8_t>(AK980LightingMode::Pulsating), "Layered pulse"},
            {static_cast<std::uint8_t>(AK980LightingMode::Tilt), "Diagonal sweep"},
            {static_cast<std::uint8_t>(AK980LightingMode::Shuttle), "Shuttle"},
            {static_cast<std::uint8_t>(AK980LightingMode::LedOff), "LEDs off"},
        };
    }

    bool setFirmwareLightingMode(std::uint8_t modeId,
                                 std::uint8_t brightness,
                                 std::uint8_t speed) override {
        try {
            // P1: START (opcode 0x18, marker 0x01). 64-byte envelope (report id
            // 0x04) — NOT the 65-byte report-id-0x00 time-sync START. The RGB /
            // settings envelope wire format is not yet hardware-verified (only
            // time-sync is, 2026-05-21); kept as-shipped to avoid regressing an
            // unverified path. See the TimeReportSize note in proprietary_protocol.hpp.
            auto envStart = makeReport(CmdStartTime);
            envStart[8] = 0x01;
            (void)m_transport->writeFeature(envStart);
            // P2: MODE_BEGIN (opcode 0x13)
            auto modeBegin = makeReport(CmdSetRgbMode);
            (void)m_transport->writeFeature(modeBegin);
            // P3: DATA - colour 0x00ffffff (white tint) by default; future
            // QML enhancement will plumb an RGB picker through.
            auto data = buildSetRgbModeData(modeId,
                                            /*r*/ 0xff,
                                            /*g*/ 0xff,
                                            /*b*/ 0xff,
                                            /*rainbow*/ 0,
                                            brightness,
                                            speed,
                                            /*direction*/ 0);
            // Re-write byte 0 to the data ReportId variant; vendor uses
            // the default 0x04 for this packet so makeReport already
            // covers us here (matches buildSetRgbModeData internal layout).
            data[0] = ReportId;
            (void)m_transport->writeFeature(data);
            // P4: SAVE (opcode 0x02). 64-byte envelope (see P1 note above).
            (void)m_transport->writeFeature(makeReport(CmdSaveRtc));
            // P5: FINISH (opcode 0xF0) - end-of-envelope sentinel per
            // ak980pro_vendor.md §13.7. Vendor's standard config-commit
            // envelope is 5 packets; the 4-packet RTC variant works
            // because firmware accepts an early stop on SAVE, but for
            // non-RTC commits (lighting mode, settings batches) FINISH
            // is load-bearing on some firmware revisions. Match vendor
            // exactly to stay safe (issue #58 / P3.6).
            (void)m_transport->writeFeature(makeReport(CmdFinish));
        } catch (std::exception const& e) {
            AJAZZ_LOG_WARN(
                "keyboard.ak980", "setFirmwareLightingMode: HID writeFeature failed: {}", e.what());
            return false;
        }
        AJAZZ_LOG_INFO("keyboard.ak980",
                       "firmware lighting mode {} set (brightness={}, speed={})",
                       static_cast<unsigned>(modeId),
                       static_cast<unsigned>(brightness),
                       static_cast<unsigned>(speed));
        return true;
    }

    [[nodiscard]] std::uint8_t brightnessMax() const noexcept override {
        return kAK980LightingBrightnessMax;
    }
    [[nodiscard]] std::uint8_t speedMax() const noexcept override { return kAK980LightingSpeedMax; }

    // ---- ISettingsCapable (settings batch opcode 0x07 sub 0x10) -----------
    // Vendor's "Settings" tab commits fn-switch + sleep-timer +
    // key-response-time in one 33-byte short report. Same 5-packet
    // envelope as setFirmwareLightingMode (START / DATA / SAVE / FINISH);
    // the wire format is documented in ak980pro_vendor.md §13.2 and the
    // byte map lives in proprietary_protocol.hpp Settings constants
    // (issue #57 / P3.x).
    bool setKeyboardSettings(core::KeyboardSettings const& settings) override {
        try {
            // P1: START. 64-byte envelope (report id 0x04) — see the note in
            // setFirmwareLightingMode; the settings envelope is not yet
            // hardware-verified, kept as-shipped.
            auto envStart = makeReport(CmdStartTime);
            envStart[8] = 0x01;
            (void)m_transport->writeFeature(envStart);
            // P2: SETTINGS-DATA (opcode 0x07 sub 0x10)
            auto data = buildSettingsBatch(
                settings.fnLayerSwitch, settings.sleepTimerMinutes, settings.keyResponseTimeLevel);
            (void)m_transport->writeFeature(data);
            // P3: SAVE (opcode 0x02). 64-byte envelope (see P1 note above).
            (void)m_transport->writeFeature(makeReport(CmdSaveRtc));
            // P4: FINISH (opcode 0xF0) - end-of-envelope sentinel per
            // ak980pro_vendor.md §13.7 (same rule that issue #58 / P3.6
            // applied to setFirmwareLightingMode).
            (void)m_transport->writeFeature(makeReport(CmdFinish));
        } catch (std::exception const& e) {
            AJAZZ_LOG_WARN(
                "keyboard.ak980", "setKeyboardSettings: HID writeFeature failed: {}", e.what());
            return false;
        }
        m_settingsCache = settings;
        // Normalise the cached entry so subsequent reads see the clamped
        // values the device actually persisted (response-time in [1..5];
        // 0 → vendor default 3 per buildSettingsBatch).
        m_settingsCache.keyResponseTimeLevel =
            settings.keyResponseTimeLevel == 0
                ? static_cast<std::uint8_t>(3)
                : std::clamp<std::uint8_t>(settings.keyResponseTimeLevel, 1, 5);
        AJAZZ_LOG_INFO("keyboard.ak980",
                       "settings batch sent: fn={}, sleep={}min, response={}",
                       static_cast<unsigned>(settings.fnLayerSwitch),
                       static_cast<unsigned>(settings.sleepTimerMinutes),
                       static_cast<unsigned>(m_settingsCache.keyResponseTimeLevel));
        return true;
    }

    [[nodiscard]] core::KeyboardSettings keyboardSettings() const override {
        return m_settingsCache;
    }

    // ---- ITftDisplayCapable (chunked 240x135 RGB565 path, opcode 0x7F) ----
    //
    // Slow-but-universal upload path for the 1.14" TFT panel on AK980 PRO and
    // siblings. Per docs/protocols/keyboard/ak980pro_tft_protocol.md §3 the
    // sequence is: HEADER (opcode 0x7F sub 0x03 with the 24-bit chunk count at
    // bytes 5..7 and LCD-select at byte 4) + N chunk PAYLOADs (each 28 bytes of
    // RGB565 pixel data at bytes 4..31, big-endian, with the 24-bit chunk index
    // split across bytes 1/2/3 and the 0x80 marker on byte 1). Byte 32 carries
    // the transport checksum. The wire layout was rebuilt byte-for-byte from the
    // vendor decompile FUN_004231c0 (the previous layout had byte 1/2 swapped on
    // the index and the header fields shifted by one).
    //
    // TRANSPORT — PROVISIONAL: the decompile's chunked path is an *output*
    // report (FUN_004231c0 -> FUN_0044f5f0 -> FUN_00451220 = WriteFile,
    // length 0x21), so we send via write(), NOT writeFeature(). This differs
    // from the hardware-verified time-sync path (IOCTL_HID_SET_FEATURE, 65-byte
    // feature reports on the 0xFF13 collection). The TFT panel is known to
    // accept feature reports for time-sync, but whether it accepts output
    // reports for image upload is UNVERIFIED — no USB/Frida capture exists yet.
    // If a capture shows the device only takes feature reports here, flip this
    // back to writeFeature() and drop the byte-32 checksum (see §2/§7).
    //
    // Inter-chunk Sleep is intentionally omitted — the vendor's 2 ms pace is a
    // USB-side rate-limit hidapi already handles via the OS write queue.
    //
    // The bulk path (opcode 0x72, 143x faster) is the preferred upload
    // mechanism but requires a bulk-write transport surface that ITransport
    // does not yet expose; it is documented + scaffolded
    // (buildScreenBulkBegin) but not wired here — see v1.2.x devices.yaml.
    [[nodiscard]] core::TftPanelInfo tftPanelInfo() const noexcept override {
        return core::TftPanelInfo{
            .widthPx = static_cast<std::uint16_t>(kTftWidth),
            .heightPx = static_cast<std::uint16_t>(kTftHeight),
        };
    }

    bool uploadTftImage(std::span<std::uint8_t const> rgba,
                        std::uint16_t width,
                        std::uint16_t height) override {
        if (width == 0 || height == 0 || rgba.empty()) {
            AJAZZ_LOG_WARN("keyboard.ak980",
                           "uploadTftImage: empty/zero-dim image ignored ({}x{}, {} bytes)",
                           static_cast<unsigned>(width),
                           static_cast<unsigned>(height),
                           rgba.size());
            return false;
        }
        auto const expectedBytes =
            static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u;
        if (rgba.size() != expectedBytes) {
            AJAZZ_LOG_WARN("keyboard.ak980",
                           "uploadTftImage: rgba size mismatch (got {}, expected {})",
                           rgba.size(),
                           expectedBytes);
            return false;
        }

        // Pure-C++ nearest-neighbour resize + big-endian RGB565 pack. Keeps
        // the keyboard backend Qt-free (proprietary_protocol.hpp / .cpp do
        // not depend on Qt6::Gui — only the test fixture uses QImage to
        // construct the source pixel buffer).
        auto const pixelStream = encodeRgb565(rgba, width, height);
        if (pixelStream.empty() || pixelStream.size() != kTftFrameBytes) {
            AJAZZ_LOG_WARN("keyboard.ak980",
                           "uploadTftImage: encodeRgb565 produced {} bytes (expected {})",
                           pixelStream.size(),
                           kTftFrameBytes);
            return false;
        }

        // Slice into ceil(64800 / 28) = 2 315 chunks. Last chunk is the same
        // 28-byte fixed size; any tail shorter than 28 bytes is zero-padded
        // so the wire format stays uniform (vendor white-fills the scratch
        // buffer with 0xFF before copying pixel data — we use 0x00 because
        // the firmware doesn't render the trailing slack region anyway).
        std::size_t const totalChunks =
            (pixelStream.size() + kTftChunkPayload - 1) / kTftChunkPayload;

        try {
            // P1: HEADER (opcode 0x7F sub 0x03, 24-bit chunk count at bytes 5..7).
            // Output report per the decompile (see TRANSPORT note above).
            (void)m_transport->write(
                buildTftChunkedHeader(/*lcdSelect=*/0, static_cast<std::uint32_t>(totalChunks)));
            // P2..ON: chunk PAYLOADs (28-byte RGB565 slices).
            std::array<std::uint8_t, kTftChunkPayload> slice{};
            for (std::size_t i = 0; i < totalChunks; ++i) {
                std::size_t const off = i * kTftChunkPayload;
                std::size_t const take =
                    std::min<std::size_t>(kTftChunkPayload, pixelStream.size() - off);
                slice.fill(0);
                std::memcpy(slice.data(), pixelStream.data() + off, take);
                (void)m_transport->write(
                    buildTftChunkedPayload(static_cast<std::uint32_t>(i),
                                           std::span<std::uint8_t const, kTftChunkPayload>{slice}));
            }
        } catch (std::exception const& e) {
            AJAZZ_LOG_WARN("keyboard.ak980", "uploadTftImage: HID write failed: {}", e.what());
            return false;
        }
        AJAZZ_LOG_INFO("keyboard.ak980",
                       "uploadTftImage: {}x{} pushed as {} chunks ({} bytes)",
                       static_cast<unsigned>(width),
                       static_cast<unsigned>(height),
                       totalChunks,
                       pixelStream.size());
        return true;
    }

private:
    DeviceDescriptor m_descriptor;
    DeviceId m_id;
    TransportPtr m_transport;
    EventCallback m_callback;
    std::mutex m_mutex;
    /// Last-known settings cache. Initialised to vendor defaults so
    /// keyboardSettings() returns sensible values before any push.
    core::KeyboardSettings m_settingsCache{};
};

} // namespace

core::DevicePtr makeProprietaryKeyboard(core::DeviceDescriptor const& d, core::DeviceId id) {
    return std::make_shared<ProprietaryKeyboard>(d, std::move(id));
}

core::DevicePtr makeProprietaryKeyboardWithTransport(core::DeviceDescriptor const& d,
                                                     core::DeviceId id,
                                                     core::TransportPtr transport) {
    return std::make_shared<ProprietaryKeyboard>(d, std::move(id), std::move(transport));
}

} // namespace ajazz::keyboard
