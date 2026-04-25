// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file akp03_protocol.hpp
 * @brief AJAZZ AKP03 / Mirabox N3 wire protocol constants and builders.
 *
 * The AKP03 shares the AKP153 framing (512-byte packets, `CRT` prefix,
 * 3-byte ASCII command words) but ships a 6-key layout (2×3), one rotary
 * encoder, and PNG images at 72×72 instead of JPEG 85×85.
 *
 * This header is a clean-room reconstruction from USB captures and
 * publicly-available hardware notes (see docs/protocols/streamdeck/akp03.md
 * for the reference material). No code is reused from third-party projects.
 *
 * @see akp153_protocol.hpp (reference framing specification)
 */
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>

namespace ajazz::streamdeck::akp03 {

// USB identifiers (provisional — will be refined from a committed capture).
inline constexpr std::uint16_t VendorId = 0x0300;  ///< USB Vendor ID (provisional).
inline constexpr std::uint16_t ProductId = 0x3001; ///< USB Product ID (provisional).

// Physical geometry.
inline constexpr std::uint8_t KeyCount = 6;      ///< 2 rows × 3 cols.
inline constexpr std::uint8_t EncoderCount = 1;  ///< One rotary knob.
inline constexpr std::uint16_t KeyWidthPx = 72;  ///< PNG image width in pixels.
inline constexpr std::uint16_t KeyHeightPx = 72; ///< PNG image height in pixels.

/// All output and input reports are padded to this size.
inline constexpr std::size_t PacketSize = 512;

// Command words: bytes 0..2 of every packet are the ASCII prefix "CRT";
// bytes 5..7 carry the command word. All remaining bytes are zero-padded.
inline constexpr std::array<std::uint8_t, 3> CmdPrefix{0x43, 0x52, 0x54}; ///< Packet header "CRT".
inline constexpr std::array<std::uint8_t, 3> CmdLight{0x4c, 0x49, 0x47};  ///< Set brightness "LIG".
inline constexpr std::array<std::uint8_t, 3> CmdImagePng{0x50,
                                                         0x4e,
                                                         0x47}; ///< PNG image transfer "PNG".
inline constexpr std::array<std::uint8_t, 3> CmdStop{0x53, 0x54, 0x50};  ///< Flush / stop "STP".
inline constexpr std::array<std::uint8_t, 3> CmdClear{0x43, 0x4c, 0x45}; ///< Clear key(s) "CLE".

/**
 * @brief Build the zero-padded 512-byte base packet for any command word.
 *
 * Layout: bytes 0..2 = CmdPrefix ("CRT"), bytes 3..4 = 0x00, bytes 5..7 =
 * `cmd`, bytes 8..511 = 0x00. Callers fill in payload bytes starting at
 * offset 10.
 *
 * @param cmd 3-byte ASCII command identifier.
 * @return Zero-initialised 512-byte packet with the CRT prefix and cmd set.
 */
[[nodiscard]] std::array<std::uint8_t, PacketSize>
buildCmdHeader(std::array<std::uint8_t, 3> const& cmd);

/**
 * @brief Build a `Set Brightness` output report.
 *
 * Byte 10 holds the brightness value, clamped to 0..100.
 *
 * @param percent Target brightness, 0 (off) .. 100 (maximum).
 * @return 512-byte report ready for ITransport::write().
 */
[[nodiscard]] std::array<std::uint8_t, PacketSize> buildSetBrightness(std::uint8_t percent);

/**
 * @brief Build the `Clear all keys` output report.
 *
 * Byte 10 = 0x00, byte 11 = 0xFF (sentinel meaning "all").
 *
 * @return 512-byte report.
 */
[[nodiscard]] std::array<std::uint8_t, PacketSize> buildClearAll();

/**
 * @brief Build the `Clear single key` output report.
 *
 * Byte 10 = 0x00, byte 11 = `keyIndex` (1-based on-device numbering).
 *
 * @param keyIndex 1-based key index, 1..KeyCount.
 * @return 512-byte report.
 */
[[nodiscard]] std::array<std::uint8_t, PacketSize> buildClearKey(std::uint8_t keyIndex);

/**
 * @brief Build the first packet of a `Set PNG image` transfer.
 *
 * Packet layout: offset 10..11 = big-endian PNG size, offset 12 = keyIndex.
 * The raw PNG blob follows in subsequent 512-byte chunks (one per write()).
 *
 * @param keyIndex 1-based key index, 1..KeyCount.
 * @param pngSize  Total size of the PNG payload in bytes.
 * @return 512-byte header packet.
 */
[[nodiscard]] std::array<std::uint8_t, PacketSize> buildImageHeader(std::uint8_t keyIndex,
                                                                    std::uint16_t pngSize);

/**
 * @brief Input event parsed from a raw 512-byte HID input report.
 *
 * Key events and encoder events share the same report layout; the `tag`
 * byte at offset 9 discriminates them.
 */
struct InputEvent {
    /// Type of input that occurred.
    enum class Kind : std::uint8_t {
        KeyPressed,      ///< A key was pressed down.
        KeyReleased,     ///< A key was released.
        EncoderTurned,   ///< Encoder rotated; `delta` carries the signed step count.
        EncoderPressed,  ///< Encoder knob pressed down.
        EncoderReleased, ///< Encoder knob released.
    };
    Kind kind{Kind::KeyPressed};
    std::uint8_t index{0}; ///< 1-based key index, or 0-based encoder index.
    std::int8_t delta{0};  ///< Encoder rotation delta: +1 = CW, -1 = CCW, 0 otherwise.
};

/**
 * @brief Parse a raw 512-byte HID input report into an InputEvent.
 *
 * ACK frames (bytes 0..2 == "ACK") and reports shorter than 16 bytes are
 * silently discarded.
 *
 * @param frame Raw bytes from ITransport::read().
 * @return Parsed InputEvent, or std::nullopt for ACK or malformed frames.
 */
[[nodiscard]] std::optional<InputEvent> parseInputReport(std::span<std::uint8_t const> frame);

} // namespace ajazz::streamdeck::akp03
