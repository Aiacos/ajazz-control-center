// SPDX-License-Identifier: GPL-3.0-or-later
//
// Proprietary AJAZZ keyboard wire protocol. See
// docs/protocols/keyboard/proprietary.md for the byte-level reference.
//
// Clean-room reconstruction from USB captures; no vendor code is reused.
//
#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace ajazz::keyboard::proprietary {

inline constexpr std::size_t ReportSize = 64;
inline constexpr std::uint8_t ReportId = 0x04;

// Command ids (host → device).
inline constexpr std::uint8_t CmdGetFirmwareVersion = 0x01;
inline constexpr std::uint8_t CmdSetKeycode = 0x05;
inline constexpr std::uint8_t CmdSetRgbStatic = 0x08;
inline constexpr std::uint8_t CmdSetRgbEffect = 0x09;
inline constexpr std::uint8_t CmdSetRgbBuffer = 0x0a;
inline constexpr std::uint8_t CmdSetRgbBrightness = 0x0b;
inline constexpr std::uint8_t CmdSetLayer = 0x0c;
inline constexpr std::uint8_t CmdUploadMacro = 0x0d;
inline constexpr std::uint8_t CmdCommitEeprom = 0x0e;

// RGB zone ids.
inline constexpr std::uint8_t ZoneKeys = 0x00;
inline constexpr std::uint8_t ZoneSides = 0x01;
inline constexpr std::uint8_t ZoneLogo = 0x02;

// LED counts per zone (matches docs/protocols/keyboard/proprietary.md).
inline constexpr std::uint16_t LedCountKeys = 104;
inline constexpr std::uint16_t LedCountSides = 18;
inline constexpr std::uint16_t LedCountLogo = 4;

// Chunk sizes: an output report holds 64 bytes including report id, command
// id and sub-ids. For the RGB LED buffer we reserve 4 header bytes
// (command + zone + offset high/low) leaving 60 bytes; for macros we
// reserve 8 bytes (command + slot + offset high/low + length + 3 reserved),
// leaving 56 bytes.
inline constexpr std::size_t RgbBufferChunk = 60;
inline constexpr std::size_t MacroChunk = 56;

inline constexpr std::uint8_t MaxLayers = 4;

/// Build a zero-padded 64-byte report with the given command id.
[[nodiscard]] inline std::array<std::uint8_t, ReportSize> makeReport(std::uint8_t cmd) {
    std::array<std::uint8_t, ReportSize> pkt{};
    pkt[0] = ReportId;
    pkt[1] = cmd;
    return pkt;
}

// ---------------------------------------------------------------------------
// Builders (pure; unit-testable).
// ---------------------------------------------------------------------------
[[nodiscard]] std::array<std::uint8_t, ReportSize> buildGetFirmwareVersion();

[[nodiscard]] std::array<std::uint8_t, ReportSize>
buildSetKeycode(std::uint8_t layer, std::uint8_t row, std::uint8_t col, std::uint16_t keycode);

[[nodiscard]] std::array<std::uint8_t, ReportSize>
buildSetRgbStatic(std::uint8_t zone, std::uint8_t r, std::uint8_t g, std::uint8_t b);

[[nodiscard]] std::array<std::uint8_t, ReportSize>
buildSetRgbEffect(std::uint8_t zone, std::uint8_t effectId, std::uint8_t speed);

[[nodiscard]] std::array<std::uint8_t, ReportSize> buildSetRgbBrightness(std::uint8_t percent);

[[nodiscard]] std::array<std::uint8_t, ReportSize> buildSetLayer(std::uint8_t layer);

[[nodiscard]] std::array<std::uint8_t, ReportSize> buildCommitEeprom();

/// Map `ledCount` from a zone id. Returns 0 for unknown zones.
[[nodiscard]] std::uint16_t ledCountForZone(std::uint8_t zone);

/// Map a human-readable zone name ("keys", "sides", "logo") to its id.
/// Returns 0xFF when unknown.
[[nodiscard]] std::uint8_t zoneIdFromName(std::string_view name);

} // namespace ajazz::keyboard::proprietary
