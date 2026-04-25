// SPDX-License-Identifier: GPL-3.0-or-later
//
// AJAZZ AKP03 / Mirabox N3 wire protocol.
//
// The AKP03 shares the AKP153 framing (512-byte packets, `CRT` prefix, 3-byte
// ASCII command words) but ships a 6-key layout, a rotary encoder, and PNG
// images at 72×72 instead of JPEG 85×85.
//
// This header is a clean-room reconstruction from USB captures and
// publicly-available hardware notes (see docs/protocols/streamdeck/akp03.md
// for the reference material). No code is reused from third-party projects.
//
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>

namespace ajazz::streamdeck::akp03 {

// USB identifiers (provisional — will be refined from a committed capture).
inline constexpr std::uint16_t VendorId = 0x0300;
inline constexpr std::uint16_t ProductId = 0x3001;

// Geometry.
inline constexpr std::uint8_t KeyCount = 6;     // 2 rows × 3 cols
inline constexpr std::uint8_t EncoderCount = 1; // one knob
inline constexpr std::uint16_t KeyWidthPx = 72;
inline constexpr std::uint16_t KeyHeightPx = 72;

inline constexpr std::size_t PacketSize = 512;

// Command words (same framing as AKP153 but PNG-typed image command).
inline constexpr std::array<std::uint8_t, 3> CmdPrefix{0x43, 0x52, 0x54};   // "CRT"
inline constexpr std::array<std::uint8_t, 3> CmdLight{0x4c, 0x49, 0x47};    // "LIG"
inline constexpr std::array<std::uint8_t, 3> CmdImagePng{0x50, 0x4e, 0x47}; // "PNG"
inline constexpr std::array<std::uint8_t, 3> CmdStop{0x53, 0x54, 0x50};     // "STP"
inline constexpr std::array<std::uint8_t, 3> CmdClear{0x43, 0x4c, 0x45};    // "CLE"

/// Build the zero-padded 512-byte header for a command word.
[[nodiscard]] std::array<std::uint8_t, PacketSize>
buildCmdHeader(std::array<std::uint8_t, 3> const& cmd);

/// Build a `Set Brightness` report (0..100).
[[nodiscard]] std::array<std::uint8_t, PacketSize> buildSetBrightness(std::uint8_t percent);

/// Build the `Clear all keys` report.
[[nodiscard]] std::array<std::uint8_t, PacketSize> buildClearAll();

/// Build the `Clear single key` report. `keyIndex` is 1-based.
[[nodiscard]] std::array<std::uint8_t, PacketSize> buildClearKey(std::uint8_t keyIndex);

/// First packet of a `Set PNG image` transfer. The PNG blob follows in
/// chunks of `PacketSize` bytes each, just like the AKP153 JPEG path.
[[nodiscard]] std::array<std::uint8_t, PacketSize> buildImageHeader(std::uint8_t keyIndex,
                                                                    std::uint16_t pngSize);

/// Event emitted from an input report.
struct InputEvent {
    enum class Kind : std::uint8_t {
        KeyPressed,
        KeyReleased,
        EncoderTurned,
        EncoderPressed,
        EncoderReleased,
    };
    Kind kind{Kind::KeyPressed};
    std::uint8_t index{0}; ///< 1-based key index, or encoder index for encoder events
    std::int8_t delta{0};  ///< encoder rotation; +1 = CW, -1 = CCW, 0 otherwise
};

/// Parse a raw input report. Returns std::nullopt for ACK frames and
/// malformed/short reports.
[[nodiscard]] std::optional<InputEvent> parseInputReport(std::span<std::uint8_t const> frame);

} // namespace ajazz::streamdeck::akp03
