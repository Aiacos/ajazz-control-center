// SPDX-License-Identifier: GPL-3.0-or-later
//
// AJAZZ AKP153 / Mirabox HSV293S wire protocol.
//
// Derived from USB captures and published notes (see
// docs/protocols/streamdeck/akp153.md for the full reference). Every
// constant and routine here has a citation in that document.
//
#pragma once

#include <array>
#include <cstdint>
#include <span>

namespace ajazz::streamdeck::akp153 {

// USB ids observed on the HSV293S hardware family.
inline constexpr std::uint16_t VendorId              = 0x0300;
inline constexpr std::uint16_t ProductIdInternational = 0x1001;  // AKP153
inline constexpr std::uint16_t ProductIdChinese      = 0x1002;  // AKP153E

// Display: 15 keys, 85×85 JPEG-encoded.
inline constexpr std::uint8_t  KeyCount     = 15;
inline constexpr std::uint16_t KeyWidthPx   = 85;
inline constexpr std::uint16_t KeyHeightPx  = 85;

inline constexpr std::size_t   PacketSize   = 512;

// Command prefixes observed in captures.
inline constexpr std::array<std::uint8_t, 3> CmdPrefix{0x43, 0x52, 0x54};  // "CRT"

inline constexpr std::array<std::uint8_t, 3> CmdLight{0x4c, 0x49, 0x47};   // "LIG"
inline constexpr std::array<std::uint8_t, 3> CmdBat  {0x42, 0x41, 0x54};   // "BAT"
inline constexpr std::array<std::uint8_t, 3> CmdStop {0x53, 0x54, 0x50};   // "STP"
inline constexpr std::array<std::uint8_t, 3> CmdClear{0x43, 0x4c, 0x45};   // "CLE"

/// Build a `Set Brightness` output report (512 bytes, zero padded).
[[nodiscard]] std::array<std::uint8_t, PacketSize> buildSetBrightness(std::uint8_t percent);

/// Build the `Clear All` output report.
[[nodiscard]] std::array<std::uint8_t, PacketSize> buildClearAll();

/// Build the `Clear single key` output report. `keyIndex` is 1-based to
/// match the on-device numbering documented in the protocol notes.
[[nodiscard]] std::array<std::uint8_t, PacketSize> buildClearKey(std::uint8_t keyIndex);

/// Build the `Show logo` output report.
[[nodiscard]] std::array<std::uint8_t, PacketSize> buildShowLogo();

/// First packet of a `Set Image` transfer. The JPEG payload follows in
/// chunks of `PacketSize` bytes each.
[[nodiscard]] std::array<std::uint8_t, PacketSize> buildImageHeader(std::uint8_t keyIndex,
                                                                    std::uint16_t jpegSize);

/// Input-report parser: returns the 1-based key index and whether the
/// state is pressed (true) or released (false). Returns std::nullopt on
/// ACK frames or malformed packets.
struct KeyEvent {
    std::uint8_t keyIndex;
    bool         pressed;
};
[[nodiscard]] std::optional<KeyEvent> parseInputReport(std::span<std::uint8_t const> frame);

}  // namespace ajazz::streamdeck::akp153

#include <optional>  // IWYU pragma: keep
