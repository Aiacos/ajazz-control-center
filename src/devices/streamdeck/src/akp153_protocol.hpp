// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file akp153_protocol.hpp
 * @brief AJAZZ AKP153 / Mirabox HSV293S wire protocol constants and builders.
 *
 * The AKP153 is the reference device for the AKP framing: 512-byte packets
 * prefixed with the three ASCII bytes "CRT", followed by two padding bytes
 * and a 3-byte ASCII command word at offsets 5..7. All remaining bytes are
 * zero-padded. Every other AKP protocol header in this codebase follows the
 * same layout.
 *
 * Derived from USB captures and published notes (see
 * docs/protocols/streamdeck/akp153.md for the full reference). Every
 * constant and routine here has a citation in that document.
 *
 * @see akp03_protocol.hpp, akp05_protocol.hpp
 */
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>

namespace ajazz::streamdeck::akp153 {

// USB ids observed on the HSV293S hardware family.
inline constexpr std::uint16_t VendorId = 0x0300;               ///< USB Vendor ID.
inline constexpr std::uint16_t ProductIdInternational = 0x1001; ///< AKP153 (international market).
inline constexpr std::uint16_t ProductIdChinese = 0x1002;       ///< AKP153E (China market variant).

// Display geometry: 15 keys arranged in a 3×5 grid, each 85×85 pixels, JPEG-encoded.
inline constexpr std::uint8_t KeyCount = 15;     ///< Total number of key slots.
inline constexpr std::uint16_t KeyWidthPx = 85;  ///< JPEG image width in pixels.
inline constexpr std::uint16_t KeyHeightPx = 85; ///< JPEG image height in pixels.

/// All output and input reports are padded to exactly 512 bytes.
inline constexpr std::size_t PacketSize = 512;

// Command words: bytes 0..2 of every output packet are the ASCII prefix "CRT";
// bytes 3..4 are 0x00; bytes 5..7 carry the ASCII command word.
inline constexpr std::array<std::uint8_t, 3> CmdPrefix{0x43, 0x52, 0x54}; ///< Packet header "CRT".
inline constexpr std::array<std::uint8_t, 3> CmdLight{0x4c, 0x49, 0x47};  ///< Set brightness "LIG".
inline constexpr std::array<std::uint8_t, 3> CmdBat{0x42,
                                                    0x41,
                                                    0x54}; ///< JPEG image transfer "BAT".
inline constexpr std::array<std::uint8_t, 3> CmdStop{0x53, 0x54, 0x50};  ///< Flush / stop "STP".
inline constexpr std::array<std::uint8_t, 3> CmdClear{0x43, 0x4c, 0x45}; ///< Clear key(s) "CLE".

/**
 * @brief Build a `Set Brightness` output report.
 *
 * Byte 10 holds the brightness level, clamped to 0..100.
 *
 * @param percent Target brightness level, 0 (off) .. 100 (maximum).
 * @return 512-byte zero-padded report ready for ITransport::write().
 */
[[nodiscard]] std::array<std::uint8_t, PacketSize> buildSetBrightness(std::uint8_t percent);

/**
 * @brief Build the `Clear all keys` output report.
 *
 * Byte 10 = 0x00, byte 11 = 0xFF (sentinel meaning "all keys").
 *
 * @return 512-byte report.
 */
[[nodiscard]] std::array<std::uint8_t, PacketSize> buildClearAll();

/**
 * @brief Build the `Clear single key` output report.
 *
 * Byte 10 = 0x00, byte 11 = `keyIndex` (1-based, matching on-device numbering).
 *
 * @param keyIndex 1-based key index, 1..KeyCount.
 * @return 512-byte report.
 */
[[nodiscard]] std::array<std::uint8_t, PacketSize> buildClearKey(std::uint8_t keyIndex);

/**
 * @brief Build the `Show logo` output report.
 *
 * Bytes 10..11 = 0x44, 0x43 ("DC" magic). Instructs the device to display
 * its built-in AJAZZ splash screen.
 *
 * @return 512-byte report.
 */
[[nodiscard]] std::array<std::uint8_t, PacketSize> buildShowLogo();

/**
 * @brief Build the first packet of a `Set Image` transfer.
 *
 * Packet layout: offsets 10..11 = big-endian JPEG payload size, offset 12 =
 * `keyIndex`. The raw JPEG blob follows in subsequent 512-byte chunk writes.
 *
 * @param keyIndex 1-based key index, 1..KeyCount.
 * @param jpegSize Total size of the JPEG payload in bytes.
 * @return 512-byte header packet.
 */
[[nodiscard]] std::array<std::uint8_t, PacketSize> buildImageHeader(std::uint8_t keyIndex,
                                                                    std::uint16_t jpegSize);

/**
 * @brief Result of parsing a single HID input report.
 *
 * The AKP153 emits one report per press/release edge; the caller is
 * responsible for maintaining a press/release state machine if needed.
 */
struct KeyEvent {
    std::uint8_t keyIndex; ///< 1-based key number, 1..KeyCount.
    bool pressed;          ///< True on press, false on release.
};

/**
 * @brief Parse a raw HID input report into a KeyEvent.
 *
 * ACK frames (bytes 0..2 == "ACK") and reports with a key index outside
 * the valid range are silently discarded.
 *
 * @param frame Raw bytes from ITransport::read().
 * @return Parsed KeyEvent, or std::nullopt on ACK or malformed packets.
 */
[[nodiscard]] std::optional<KeyEvent> parseInputReport(std::span<std::uint8_t const> frame);

} // namespace ajazz::streamdeck::akp153
