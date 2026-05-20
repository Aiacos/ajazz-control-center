// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file akp05_protocol.hpp
 * @brief AJAZZ AKP05 / AKP05E / Mirabox N4 "Stream Dock Plus"-class wire protocol.
 *
 * Hardware layout (per `[opendeck-akp05]`, `[mirabox-n4]`, `[companion]`):
 *
 *   - **10 LCD keys** in a 2 rows × 5 columns grid (NOT 15 — earlier
 *     versions of this header modelled the device incorrectly).
 *   - **4 endless rotary encoders** with push function; each encoder maps
 *     to one of 4 touch zones on the LCD strip below it.
 *   - **LCD touchscreen strip** 110 × 14 mm physical, ≈ 800 × 480 px on
 *     the underlying display panel, split into 4 touch zones aligned to
 *     the 4 encoders (Stream Deck Plus-class architecture).
 *   - Bundled USB-2 hub (2× USB-A + 2× USB-C) — not addressed via HID.
 *
 * Framing reuses the AKP family `CRT` prefix + 3-byte command word.
 * Per `[mirajazz]`'s protocol-version table the AKP05 family is a
 * **protocol_version 3** device: 1024-byte packets, native press/release
 * states. We send 1024-byte output reports accordingly. This is
 * firmware-confirmed: a live `CRT VER` handshake against an AKP05E
 * (USB 0x0300:0x3004) on 2026-05-20 returned "V3.AKP05E.01.007", and the
 * RE corpus shows every AKP05 / Mirabox N4 SKU is protocol_version 3
 * (see `akp_device_matrix.md` — no 512-byte AKP05 variant exists).
 *
 * NOTE the endpoint asymmetry: the vendor control OUT endpoint is 1024
 * bytes, but the IN endpoint is 512 bytes. `PacketSize` sizes the OUT
 * reports (and their zero-padding); input reports are parsed from a span
 * sized to the bytes actually read, so `PacketSize` is merely a safe upper
 * bound for the read buffer on the input side.
 *
 * @see akp153_protocol.hpp (shared framing conventions)
 * @see akp03_protocol.hpp  (sister v2 device on the AKP family)
 * @see ../_research-sources.md (citation tags resolved here)
 */
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>

namespace ajazz::streamdeck::akp05 {

// USB identifiers.
// `[opendeck-akp05]/40-opendeck-akp05.rules` lists Mirabox N4 as
// `0x6603:0x1007`; the AJAZZ-branded AKP05 / AKP05E PID is not yet
// public (no hardware sample in any of our reference projects).
inline constexpr std::uint16_t VendorIdMiraboxN4 = 0x6603;  ///< Mirabox N4.
inline constexpr std::uint16_t ProductIdMiraboxN4 = 0x1007; ///< Mirabox N4.
inline constexpr std::uint16_t VendorId = 0x0300;           ///< Provisional, see README.
inline constexpr std::uint16_t ProductId = 0x5001;          ///< Provisional, see README.

// Physical geometry — corrected after 2026-05-14 research pass.
// Sources: `[opendeck-akp05]`, `[mirabox-n4]`, `[companion]`.
inline constexpr std::uint8_t KeyCount = 10;             ///< LCD keys (2×5 grid).
inline constexpr std::uint8_t KeyRows = 2;               ///< Physical rows of LCD keys.
inline constexpr std::uint8_t KeyCols = 5;               ///< Physical columns of LCD keys.
inline constexpr std::uint8_t EncoderCount = 4;          ///< Endless rotary encoders.
inline constexpr std::uint8_t TouchZoneCount = 4;        ///< Touch-strip zones aligned to encoders.
inline constexpr std::uint16_t KeyWidthPx = 85;          ///< Per-key JPEG dimension (legacy).
inline constexpr std::uint16_t KeyHeightPx = 85;         ///< Per-key JPEG dimension (legacy).
inline constexpr std::uint16_t TouchStripWidthPx = 800;  ///< LCD strip width.
inline constexpr std::uint16_t TouchStripHeightPx = 480; ///< LCD strip height.
inline constexpr std::uint16_t MainDisplayWidthPx = 800; ///< Legacy alias for code that wrote
                                                         ///< to the strip as a "main" display.
inline constexpr std::uint16_t MainDisplayHeightPx = 100; ///< Legacy alias (full strip height
                                                          ///< is 480 px — UI uses ~100 px band).
inline constexpr std::uint16_t TouchStripRangeX = 640;    ///< Touch X coordinate range, 0..639
                                                          ///< (preserved for backwards-compat
                                                          ///< tests; capture pending).

// Per-encoder LCD overlay rendered as a slice of the LCD strip. Stream Deck
// Plus uses 200×100 per encoder; the Mirabox N4 strip is 4 × (200×480) when
// expressed as zones over the underlying 800×480 panel. We expose 100×100
// for backwards-compat with the legacy code paths until the v3 capture
// makes the real per-zone resolution explicit.
inline constexpr std::uint16_t EncoderScreenWidthPx = 100;
inline constexpr std::uint16_t EncoderScreenHeightPx = 100;

/// Output reports are padded to this size. The protocol_version 3 AKP05
/// family uses a 1024-byte vendor OUT endpoint (the IN endpoint is 512 B;
/// see the file header note on the OUT/IN asymmetry).
inline constexpr std::size_t PacketSize = 1024;

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

// Vendor-RE-discovered opcodes (akp05_vendor.md §1.5 + §3, 2026-05-17).
// These extend the AKP-family command surface beyond what our v1.x backend
// shipped against. CmdVersion is the firmware-version probe sent at open
// time (vendor sends it as the first command); UploadFinishedMarker is the
// 5-byte "ULEND" commit-after-image-burst sentinel that the vendor sends
// after every chunked image upload (we previously only emitted STP, which
// per the vendor RE may explain occasional firmware desync on large bursts).
inline constexpr std::array<std::uint8_t, 3> CmdVersion{0x56,
                                                        0x45,
                                                        0x52}; ///< Firmware version "VER".
inline constexpr std::array<std::uint8_t, 5> UploadFinishedMarker{
    0x55,
    0x4c,
    0x45,
    0x4e,
    0x44}; ///< End-of-image-burst commit sentinel "ULEND" (5 bytes).
inline constexpr std::array<std::uint8_t, 3> CmdSecondaryScreen{
    0x44,
    0x52,
    0x41}; ///< Touch-strip rect-addressable image "DRA".
inline constexpr std::array<std::uint8_t, 3> CmdLogo{0x4c,
                                                     0x4f,
                                                     0x47}; ///< Firmware boot-logo upload "LOG".

/**
 * @brief Build the zero-padded 1024-byte base packet for any command word.
 *
 * Bytes 0..2 = CmdPrefix ("CRT"), 3..4 = 0x00, 5..7 = `cmd`, 8..511 = 0x00.
 *
 * @param cmd 3-byte ASCII command identifier.
 * @return Zero-initialised 1024-byte packet.
 */
[[nodiscard]] std::array<std::uint8_t, PacketSize>
buildCmdHeader(std::array<std::uint8_t, 3> const& cmd);

/**
 * @brief Build a `Set Brightness` report; byte 10 = percent (clamped 0..100).
 * @param percent Target brightness.
 * @return 1024-byte report.
 */
[[nodiscard]] std::array<std::uint8_t, PacketSize> buildSetBrightness(std::uint8_t percent);

/**
 * @brief Build the `Clear all keys` report (byte 10 = 0, byte 11 = 0xFF).
 * @return 1024-byte report.
 */
[[nodiscard]] std::array<std::uint8_t, PacketSize> buildClearAll();

/**
 * @brief Build the `Clear single key` report.
 * @param keyIndex 1-based key index, 1..KeyCount.
 * @return 1024-byte report.
 */
[[nodiscard]] std::array<std::uint8_t, PacketSize> buildClearKey(std::uint8_t keyIndex);

/**
 * @brief Build the first packet of a `Set key image` transfer.
 *
 * Offsets 10..11 = big-endian JPEG size, offset 12 = keyIndex. The JPEG
 * payload follows in 1024-byte chunks (identical format to AKP153).
 *
 * @param keyIndex  1-based key index, 1..KeyCount.
 * @param jpegSize  Total JPEG payload size in bytes.
 * @return 1024-byte header packet.
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
 * @return 1024-byte header packet.
 */
[[nodiscard]] std::array<std::uint8_t, PacketSize>
buildEncoderImageHeader(std::uint8_t encoderIndex, std::uint16_t jpegSize);

/**
 * @brief Build the firmware-version probe packet (CRT VER, no payload).
 *
 * Sent at device-open time by the vendor app — the device responds with
 * a 512-byte input report whose payload carries a version string. The
 * response format is not yet decoded in our Ghidra dump; this builder
 * only covers the request side. Backends that consume the response will
 * land in a follow-up commit per roadmap §11.3.
 */
[[nodiscard]] std::array<std::uint8_t, PacketSize> buildVersionRequest();

/**
 * @brief Build the upload-finished sentinel packet (CRT ULEND, no payload).
 *
 * 5-byte command word "ULEND" at offsets 5..9 (NOT the standard 3-byte
 * command at offsets 5..7 — ULEND is one of two AKP-family opcodes with
 * a wider command field; the other is "QUCMD"). Emitted by the vendor
 * after every chunked image-upload burst. We previously only emitted the
 * 3-byte STP flush; per akp05_vendor.md §3 row 193 this may explain
 * occasional firmware desync on large image bursts.
 */
[[nodiscard]] std::array<std::uint8_t, PacketSize> buildUploadFinished();

/**
 * @brief Build the header packet for a touch-strip rect-addressable image (DRA).
 *
 * Per vendor RE (akp05_vendor.md §3 row 190, SDDevice::getSecondaryScreenPicInfo):
 * the AKP05/Mirabox N4 800×480 touch-strip supports partial-update via the
 * "DRA" opcode — a 4-zone-aligned rect-addressable image upload that avoids
 * re-encoding+re-uploading the whole 800×480 panel on every redraw. Massive
 * bandwidth win when only one encoder's overlay zone changed.
 *
 * Wire layout (single 1024-byte header packet):
 *  - bytes 0..2:  "CRT" prefix
 *  - bytes 3..4:  0x00 0x00
 *  - bytes 5..7:  "DRA" (CmdSecondaryScreen)
 *  - bytes 8..11: BE32 JPEG payload size
 *  - byte  12:    location id (zone discriminator; vendor uses 0x12 to flag a
 *                 boot-logo variant routed through the separate M_V packet —
 *                 callers wanting that path should use a dedicated builder
 *                 once captured)
 *  - bytes 13..14: BE16 rect width
 *  - bytes 15..16: BE16 rect height
 *  - bytes 17..18: BE16 rect x origin
 *  - bytes 19..20: BE16 rect y origin
 *  - bytes 21..511: 0x00 padding
 *
 * Followed by JPEG payload in 1024-byte chunks via the standard sendImage()
 * path, then the ULEND commit sentinel (buildUploadFinished()).
 *
 * @param location  Zone id / location discriminator. 0x12 triggers vendor's
 *                  M_V boot-logo variant (separate packet shape, not handled
 *                  here); typical zone ids are small non-zero values.
 *                  Caller is responsible for NOT passing 0x12 to this builder.
 * @param width     Rect width in pixels (BE16; the device's strip is 800 px
 *                  wide so width ≤ 800).
 * @param height    Rect height in pixels (BE16; ≤ 480).
 * @param x         Rect x origin in pixels (BE16).
 * @param y         Rect y origin in pixels (BE16).
 * @param jpegSize  Total JPEG payload size in bytes (BE32; max 0xFFFFFFFF
 *                  but practically capped by HID transfer rate).
 * @return 1024-byte header packet.
 */
[[nodiscard]] std::array<std::uint8_t, PacketSize>
buildSecondaryScreenHeader(std::uint8_t location,
                           std::uint16_t width,
                           std::uint16_t height,
                           std::uint16_t x,
                           std::uint16_t y,
                           std::uint32_t jpegSize);

/**
 * @brief Build the first packet of a main-LCD image transfer.
 *
 * Offsets 10..11 = big-endian JPEG size. The 800×100 JPEG payload follows
 * in 1024-byte chunks.
 *
 * @param jpegSize Total JPEG payload size in bytes.
 * @return 1024-byte header packet.
 */
[[nodiscard]] std::array<std::uint8_t, PacketSize> buildMainImageHeader(std::uint16_t jpegSize);

/**
 * @brief Build the header packet for a firmware boot-logo upload (CRT LOG).
 *
 * Per vendor RE (akp05_vendor.md §2 row 188, SDDevice::sendLogoSizeCommand at
 * 0x180023a70): the AKP05/Mirabox N4 accepts a custom "boot logo / splash"
 * JPEG that the firmware displays at power-on. The header carries the total
 * JPEG byte count at bytes 10..11 (BE16, mirroring the buildKeyImageHeader
 * shape); the JPEG itself follows in 1024-byte chunks through the standard
 * sendImage() path, then the ULEND commit sentinel.
 *
 * @param jpegSize Total JPEG payload size in bytes (BE16; capped at 0xFFFF).
 * @return 1024-byte header packet.
 */
[[nodiscard]] std::array<std::uint8_t, PacketSize> buildLogoSizeHeader(std::uint32_t jpegSize);

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
