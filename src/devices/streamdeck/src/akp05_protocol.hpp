// SPDX-License-Identifier: GPL-3.0-or-later
//
// AJAZZ AKP05 / AKP05E "Stream Dock Plus"-class wire protocol.
//
// 15 keys (85×85 JPEG, identical to AKP153), 4 endless rotary encoders with
// tiny LCDs above them, and a horizontal touch strip. Framing uses the same
// 512-byte CRT-prefixed packet as the rest of the AKP family.
//
// Clean-room reconstruction from docs/protocols/streamdeck/akp05.md. See
// that document for the authoritative byte-level reference.
//
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>

namespace ajazz::streamdeck::akp05 {

// USB identifiers (provisional).
inline constexpr std::uint16_t VendorId = 0x0300;
inline constexpr std::uint16_t ProductId = 0x5001;

// Geometry: 15 main keys (3×5), 4 encoders, one touch strip, one main LCD.
inline constexpr std::uint8_t KeyCount = 15;
inline constexpr std::uint8_t EncoderCount = 4;
inline constexpr std::uint16_t KeyWidthPx = 85;
inline constexpr std::uint16_t KeyHeightPx = 85;
inline constexpr std::uint16_t EncoderScreenWidthPx = 100;
inline constexpr std::uint16_t EncoderScreenHeightPx = 100;
inline constexpr std::uint16_t MainDisplayWidthPx = 800;
inline constexpr std::uint16_t MainDisplayHeightPx = 100;
inline constexpr std::uint16_t TouchStripRangeX = 640; ///< 0..639

inline constexpr std::size_t PacketSize = 512;

// Command words.
inline constexpr std::array<std::uint8_t, 3> CmdPrefix{0x43, 0x52, 0x54};   // "CRT"
inline constexpr std::array<std::uint8_t, 3> CmdLight{0x4c, 0x49, 0x47};    // "LIG"
inline constexpr std::array<std::uint8_t, 3> CmdKeyImage{0x42, 0x41, 0x54}; // "BAT"
inline constexpr std::array<std::uint8_t, 3> CmdEncImage{0x45, 0x4e, 0x43}; // "ENC"
inline constexpr std::array<std::uint8_t, 3> CmdMainImage{0x4d, 0x41, 0x49}; // "MAI"
inline constexpr std::array<std::uint8_t, 3> CmdStop{0x53, 0x54, 0x50};     // "STP"
inline constexpr std::array<std::uint8_t, 3> CmdClear{0x43, 0x4c, 0x45};    // "CLE"

/// Zero-padded 512-byte command header for `cmd`.
[[nodiscard]] std::array<std::uint8_t, PacketSize>
buildCmdHeader(std::array<std::uint8_t, 3> const& cmd);

[[nodiscard]] std::array<std::uint8_t, PacketSize> buildSetBrightness(std::uint8_t percent);
[[nodiscard]] std::array<std::uint8_t, PacketSize> buildClearAll();
[[nodiscard]] std::array<std::uint8_t, PacketSize> buildClearKey(std::uint8_t keyIndex);

/// Image header for the main 85×85 key grid. Payload follows in 512-byte
/// chunks (JPEG bytes, as on the AKP153).
[[nodiscard]] std::array<std::uint8_t, PacketSize> buildKeyImageHeader(std::uint8_t keyIndex,
                                                                      std::uint16_t jpegSize);

/// Image header for an encoder's screen (0..3).
[[nodiscard]] std::array<std::uint8_t, PacketSize>
buildEncoderImageHeader(std::uint8_t encoderIndex, std::uint16_t jpegSize);

/// Image header for the main 800×100 LCD strip.
[[nodiscard]] std::array<std::uint8_t, PacketSize> buildMainImageHeader(std::uint16_t jpegSize);

/// Parsed input report.
struct InputEvent {
    enum class Kind : std::uint8_t {
        KeyPressed,
        KeyReleased,
        EncoderTurned,
        EncoderPressed,
        EncoderReleased,
        TouchTap,
        TouchSwipeLeft,
        TouchSwipeRight,
        TouchLongPress,
    };
    Kind kind{Kind::KeyPressed};
    std::uint8_t index{0};    ///< key or encoder index
    std::int16_t value{0};    ///< encoder delta or touch x-coordinate
};

[[nodiscard]] std::optional<InputEvent> parseInputReport(std::span<std::uint8_t const> frame);

} // namespace ajazz::streamdeck::akp05
