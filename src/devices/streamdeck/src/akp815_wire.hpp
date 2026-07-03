// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file akp815_wire.hpp
 * @brief AJAZZ AKP815 wire-protocol command builders and input parsing.
 *
 * The AKP815 uses the family-wide AKP framing: 512-byte packets prefixed with
 * the three ASCII bytes "CRT", two padding bytes, and a 3-byte ASCII command
 * word at offsets 5..7, with the remaining bytes zero-padded. The image opcode
 * is "BAT" (key-image transfer), and the upload burst is committed with the
 * 5-byte "ULEND" sentinel.
 *
 * This header previously lived in `akp153_protocol.hpp`: the AKP815 and AKP153
 * shared the exact same byte-level builders (the only family difference is the
 * image geometry, which the builders never touch — they take a key index and a
 * payload size). When the C++ AKP153 backend was removed in favour of the Rust
 * mirajazz sidecar, the shared builders were relocated here so the AKP815
 * carve-out owns its own wire definitions with no dangling AKP153 references.
 *
 * Geometry constants (key count, pixel dimensions, strip size, USB ids) live in
 * @ref akp815_protocol.hpp. Family-wide command words live in
 * @ref akp_common_protocol.hpp.
 *
 * Derived from USB captures and published notes; see
 * docs/protocols/streamdeck/akp815.md and akp153.md for the full reference.
 *
 * @see akp815_protocol.hpp
 * @see akp_common_protocol.hpp
 */
#pragma once

#include "akp815_protocol.hpp"
#include "akp_common_protocol.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>

namespace ajazz::streamdeck::akp815 {

// Family-wide command words shared verbatim across the AKP Stream Dock family —
// single source of truth in akp_common_protocol.hpp. The image opcode (BAT) is
// the only AKP815-specific command word.
using akp_common::CmdClear;
using akp_common::CmdLight;
using akp_common::CmdPrefix;
using akp_common::CmdStop;
using akp_common::CmdVersion;
using akp_common::UploadFinishedMarker;

/// JPEG image-transfer opcode "BAT" (bytes 5..7 of the image header packet).
inline constexpr std::array<std::uint8_t, 3> CmdBat{0x42, 0x41, 0x54};

/**
 * @brief Build the firmware-version probe (CRT VER, no payload).
 *
 * Request side only — response decoding deferred until a real-device capture
 * surfaces the input-report shape. firmwareVersion() continues to return
 * "unknown" until then.
 */
[[nodiscard]] std::array<std::uint8_t, PacketSize> buildVersionRequest();

/**
 * @brief Build the upload-finished sentinel (CRT ULEND, 5-byte at offsets 5..9).
 *
 * Emitted after every chunked image upload to commit the burst. Without it,
 * large image bursts may cause occasional firmware desync (vendor RE annotation).
 */
[[nodiscard]] std::array<std::uint8_t, PacketSize> buildUploadFinished();

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
 * The AKP815 emits one report per press/release edge; the caller is
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

} // namespace ajazz::streamdeck::akp815
