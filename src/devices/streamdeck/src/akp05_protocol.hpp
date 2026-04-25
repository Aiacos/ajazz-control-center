// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file akp05_protocol.hpp
 * @brief AJAZZ AKP05 / AKP05E "Stream Dock Plus"-class wire protocol.
 *
 * 15 main keys (85×85 JPEG, identical format to AKP153), 4 endless rotary
 * encoders with 100×100 JPEG LCDs, one horizontal touch strip, and one
 * main 800×100 LCD. Framing uses the same 512-byte CRT-prefixed packet as
 * the rest of the AKP family, but with additional command words for the
 * encoder and main-LCD image paths.
 *
 * Clean-room reconstruction from docs/protocols/streamdeck/akp05.md. See
 * that document for the authoritative byte-level reference.
 *
 * @see akp153_protocol.hpp (shared framing conventions)
 */
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>

namespace ajazz::streamdeck::akp05 {

// USB identifiers (provisional).
inline constexpr std::uint16_t VendorId = 0x0300;  ///< USB Vendor ID (provisional).
inline constexpr std::uint16_t ProductId = 0x5001; ///< USB Product ID (provisional).

// Physical geometry: 15 main keys (3×5), 4 encoders, one touch strip, one main LCD.
inline constexpr std::uint8_t KeyCount = 15;                ///< Number of main key slots.
inline constexpr std::uint8_t EncoderCount = 4;             ///< Number of rotary encoders.
inline constexpr std::uint16_t KeyWidthPx = 85;             ///< Key JPEG width in pixels.
inline constexpr std::uint16_t KeyHeightPx = 85;            ///< Key JPEG height in pixels.
inline constexpr std::uint16_t EncoderScreenWidthPx = 100;  ///< Encoder LCD width in pixels.
inline constexpr std::uint16_t EncoderScreenHeightPx = 100; ///< Encoder LCD height in pixels.
inline constexpr std::uint16_t MainDisplayWidthPx = 800;    ///< Main LCD width in pixels.
inline constexpr std::uint16_t MainDisplayHeightPx = 100;   ///< Main LCD height in pixels.
inline constexpr std::uint16_t TouchStripRangeX = 640;      ///< Touch X coordinate range, 0..639.

/// All output and input reports are padded to this size.
inline constexpr std::size_t PacketSize = 512;

// Command words: bytes 0..2 = "CRT" prefix, bytes 5..7 = command.
inline constexpr std::array<std::uint8_t, 3> CmdPrefix{0x43, 0x52, 0x54}; ///< Packet header "CRT".
inline constexpr std::array<std::uint8_t, 3> CmdLight{0x4c, 0x49, 0x47};  ///< Set brightness "LIG".
inline constexpr std::array<std::uint8_t, 3> CmdKeyImage{0x42,
                                                         0x41,
                                                         0x54}; ///< Key JPEG transfer "BAT".
inline constexpr std::array<std::uint8_t, 3> CmdEncImage{0x45,
                                                         0x4e,
                                                         0x43}; ///< Encoder LCD transfer "ENC".
inline constexpr std::array<std::uint8_t, 3> CmdMainImage{0x4d,
                                                          0x41,
                                                          0x49}; ///< Main LCD transfer "MAI".
inline constexpr std::array<std::uint8_t, 3> CmdStop{0x53, 0x54, 0x50};  ///< Flush / stop "STP".
inline constexpr std::array<std::uint8_t, 3> CmdClear{0x43, 0x4c, 0x45}; ///< Clear key(s) "CLE".

/**
 * @brief Build the zero-padded 512-byte base packet for any command word.
 *
 * Bytes 0..2 = CmdPrefix ("CRT"), 3..4 = 0x00, 5..7 = `cmd`, 8..511 = 0x00.
 *
 * @param cmd 3-byte ASCII command identifier.
 * @return Zero-initialised 512-byte packet.
 */
[[nodiscard]] std::array<std::uint8_t, PacketSize>
buildCmdHeader(std::array<std::uint8_t, 3> const& cmd);

/**
 * @brief Build a `Set Brightness` report; byte 10 = percent (clamped 0..100).
 * @param percent Target brightness.
 * @return 512-byte report.
 */
[[nodiscard]] std::array<std::uint8_t, PacketSize> buildSetBrightness(std::uint8_t percent);

/**
 * @brief Build the `Clear all keys` report (byte 10 = 0, byte 11 = 0xFF).
 * @return 512-byte report.
 */
[[nodiscard]] std::array<std::uint8_t, PacketSize> buildClearAll();

/**
 * @brief Build the `Clear single key` report.
 * @param keyIndex 1-based key index, 1..KeyCount.
 * @return 512-byte report.
 */
[[nodiscard]] std::array<std::uint8_t, PacketSize> buildClearKey(std::uint8_t keyIndex);

/**
 * @brief Build the first packet of a `Set key image` transfer.
 *
 * Offsets 10..11 = big-endian JPEG size, offset 12 = keyIndex. The JPEG
 * payload follows in 512-byte chunks (identical format to AKP153).
 *
 * @param keyIndex  1-based key index, 1..KeyCount.
 * @param jpegSize  Total JPEG payload size in bytes.
 * @return 512-byte header packet.
 */
[[nodiscard]] std::array<std::uint8_t, PacketSize> buildKeyImageHeader(std::uint8_t keyIndex,
                                                                       std::uint16_t jpegSize);

/**
 * @brief Build the first packet of an encoder-LCD image transfer.
 *
 * Offsets 10..11 = big-endian JPEG size, offset 12 = encoderIndex.
 *
 * @param encoderIndex 0-based encoder index, 0..EncoderCount-1.
 * @param jpegSize     Total JPEG payload size in bytes.
 * @return 512-byte header packet.
 */
[[nodiscard]] std::array<std::uint8_t, PacketSize>
buildEncoderImageHeader(std::uint8_t encoderIndex, std::uint16_t jpegSize);

/**
 * @brief Build the first packet of a main-LCD image transfer.
 *
 * Offsets 10..11 = big-endian JPEG size. The 800×100 JPEG payload follows
 * in 512-byte chunks.
 *
 * @param jpegSize Total JPEG payload size in bytes.
 * @return 512-byte header packet.
 */
[[nodiscard]] std::array<std::uint8_t, PacketSize> buildMainImageHeader(std::uint16_t jpegSize);

/**
 * @brief Parsed input event from a raw 512-byte HID input report.
 *
 * The tag byte at offset 9 discriminates event types:
 *   - 1..KeyCount          : key event; byte 10 = press (0x01) / release (0x00).
 *   - 0x20..0x2F           : encoder event; low nibble = encoder index.
 *   - 0x30..0x3F           : touch-strip event; low nibble = gesture code.
 */
struct InputEvent {
    /// Discriminates the input source and action.
    enum class Kind : std::uint8_t {
        KeyPressed,      ///< Key pressed; `index` = 1-based key number.
        KeyReleased,     ///< Key released; `index` = 1-based key number.
        EncoderTurned,   ///< Encoder rotated; `value` = signed step count (+1 CW, -1 CCW).
        EncoderPressed,  ///< Encoder knob depressed.
        EncoderReleased, ///< Encoder knob released.
        TouchTap,        ///< Single tap on touch strip; `value` = X coordinate.
        TouchSwipeLeft,  ///< Left swipe gesture; `value` = X start position.
        TouchSwipeRight, ///< Right swipe gesture; `value` = X start position.
        TouchLongPress,  ///< Long-press gesture on touch strip; `value` = X coordinate.
    };
    Kind kind{Kind::KeyPressed};
    std::uint8_t index{0}; ///< Key or encoder index (meaning depends on Kind).
    std::int16_t value{0}; ///< Encoder delta or touch X coordinate, 0..TouchStripRangeX-1.
};

/**
 * @brief Parse a raw 512-byte HID input report into an InputEvent.
 *
 * ACK frames (bytes 0..2 == "ACK") and reports shorter than 16 bytes are
 * silently discarded.
 *
 * @param frame Raw bytes from ITransport::read().
 * @return Parsed InputEvent, or std::nullopt for ACK or unrecognised frames.
 */
[[nodiscard]] std::optional<InputEvent> parseInputReport(std::span<std::uint8_t const> frame);

} // namespace ajazz::streamdeck::akp05
