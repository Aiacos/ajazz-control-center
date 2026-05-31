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

#include "akp_common_protocol.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace ajazz::streamdeck::akp05 {

// Family-wide command words shared verbatim across AKP03/AKP05/AKP153 — single
// source of truth in akp_common_protocol.hpp. The image opcodes below
// (BAT/ENC/MAI), the touch-strip DRA, and the boot logo LOG are AKP05-specific.
using akp_common::CmdClear;
using akp_common::CmdLight;
using akp_common::CmdPrefix;
using akp_common::CmdStop;
using akp_common::CmdVersion;
using akp_common::UploadFinishedMarker;

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
inline constexpr std::uint16_t TouchStripRangeX =
    256; ///< Touch X is single byte (0..255), per akp05_input_corrections.md §4 (was 640 — wrong
         ///< BE16 model) (preserved for backwards-compat tests; capture pending).

// Per-encoder strip zone. The AKP05E touch strip carries 4 zones aligned to the
// 4 encoders (akp_device_matrix §4 — "no separate encoder LCD"). Hardware-probed
// 2026-05-31 on 0x0300:0x3004: each zone displays a ~128×128 square 1:1 (85 px
// left visible gaps, 200 px overflowed into the neighbour). Rendered via the
// SAME BAT opcode as keys at wire bytes 1..4 — the vendor ENC opcode does not
// paint on this firmware.
inline constexpr std::uint16_t EncoderScreenWidthPx = 128;
inline constexpr std::uint16_t EncoderScreenHeightPx = 128;

/// Output reports are padded to this size. The protocol_version 3 AKP05
/// family uses a 1024-byte vendor OUT endpoint (the IN endpoint is 512 B;
/// see the file header note on the OUT/IN asymmetry).
inline constexpr std::size_t PacketSize = 1024;

// Command words: bytes 0..2 = "CRT" prefix, bytes 5..7 = command. The image
// opcodes below are AKP05-specific; the shared CmdPrefix/CmdLight/CmdStop/
// CmdClear come from the akp_common `using` block above.
inline constexpr std::array<std::uint8_t, 3> CmdKeyImage{0x42,
                                                         0x41,
                                                         0x54}; ///< Key JPEG transfer "BAT".
inline constexpr std::array<std::uint8_t, 3> CmdEncImage{0x45,
                                                         0x4e,
                                                         0x43}; ///< Encoder LCD transfer "ENC".
inline constexpr std::array<std::uint8_t, 3> CmdMainImage{0x4d,
                                                          0x41,
                                                          0x49}; ///< Main LCD transfer "MAI".

// Vendor-RE-discovered opcodes (akp05_vendor.md §1.5 + §3, 2026-05-17). The
// firmware-version probe (CmdVersion "VER") and the 5-byte "ULEND" commit
// sentinel (UploadFinishedMarker) are family-wide and come from the akp_common
// `using` block above. The touch-strip and boot-logo opcodes below are
// AKP05-specific.
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
 * @brief Map a 1-based logical key index to the AKP05E firmware's wire byte.
 *
 * IDisplayCapable addresses keys 1..KeyCount row-major (1 = top-left, KeyCols =
 * top-right, KeyCols+1 = bottom-left, KeyCount = bottom-right). The AKP05E
 * firmware does NOT address them linearly: hardware-confirmed on a live AKP05E
 * (0x0300:0x3004, commit 037bd8d) and cross-checked against opendeck-akp05's
 * position table, the BAT key byte addresses encoder LCDs at 1..4, the touch
 * strip at 5, the bottom row at 6..10, and the top row at 11..15. Passing the
 * raw logical index sends key 1's image to an encoder/strip slot instead of the
 * key. This is a device-protocol fact, identical on every OS (NOT platform-gated,
 * unlike the report-id prefix). Re-validated 2026-05-29 via the color round-trip
 * harness (all 15 surfaces rendered). See akp05_input_corrections.md §7.1.
 *
 * @param keyIndex 1-based logical key index, 1..KeyCount.
 * @return The firmware wire byte to pass to buildKeyImageHeader / buildClearKey.
 */
[[nodiscard]] constexpr std::uint8_t akp05KeyWire(std::uint8_t keyIndex) noexcept {
    // Top row (1..KeyCols) -> 11..15; bottom row (KeyCols+1..KeyCount) -> 6..10.
    // The +10 offset on the top row = encoders(4) + strip(1) + bottom row(5).
    constexpr std::uint8_t kTopRowWireOffset = 10U;
    return (keyIndex <= KeyCols) ? static_cast<std::uint8_t>(kTopRowWireOffset + keyIndex)
                                 : keyIndex;
}

/**
 * @brief Build the first packet of a `Set key image` transfer.
 *
 * Offsets 10..11 = big-endian JPEG size, offset 12 = the wire key byte. The
 * JPEG payload follows in 1024-byte chunks (identical format to AKP153).
 *
 * @param keyIndex  Firmware wire key byte (see akp05KeyWire()); callers map a
 *                  1-based logical index through akp05KeyWire() before this call.
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
 * This is the vendor app's open-time VER request (akp05_init_sequence.md §3.2).
 * Our production backend does NOT use it: open() pulls the version via a HID
 * GET_FEATURE (report id 0x01) instead, matching mirajazz, because the device
 * answers the version over GET_REPORT rather than the interrupt-IN endpoint.
 * This builder is retained as the documented vendor VER packet (covered by the
 * protocol tests) and decoded by parseVersionResponse() (live-confirmed
 * "V3.AKP05E.01.007"); keep it for the vendor-parity wire reference.
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

// ---------------------------------------------------------------------------
// Input action codes carried at report[9].
//
// Per the vendor RE (SDActionCanvasWidget::handleKeyEvents @0x1400d02b0,
// akp05_input_corrections.md §3): an encoder report carries NO rotation-
// magnitude byte — direction AND which-encoder are BOTH encoded by the
// report[9] action code, and one report == exactly one detent (value = ±1).
// The four encoders' codes CONVERGE across two independent corpora (the
// 2026-05-27 Ghidra jump-table DAT_1400d9ef4 and opendeck-akp05's inputs.rs),
// but the per-encoder index assignment and CW/CCW polarity follow opendeck's
// convention and remain [PROVISIONAL] until a retail AKP05E / Mirabox N4 unit
// confirms them on the wire — the 0x0300:0x3004 demo unit's input path is
// stubbed (akp05_input_corrections.md §7.1; re-confirmed live 2026-05-29).
inline constexpr std::uint8_t ActionEncoder0Ccw = 0xA0;   ///< Encoder 1 rotate CCW [PROVISIONAL].
inline constexpr std::uint8_t ActionEncoder0Cw = 0xA1;    ///< Encoder 1 rotate CW  [PROVISIONAL].
inline constexpr std::uint8_t ActionEncoder1Ccw = 0x50;   ///< Encoder 2 rotate CCW [PROVISIONAL].
inline constexpr std::uint8_t ActionEncoder1Cw = 0x51;    ///< Encoder 2 rotate CW  [PROVISIONAL].
inline constexpr std::uint8_t ActionEncoder2Ccw = 0x90;   ///< Encoder 3 rotate CCW [PROVISIONAL].
inline constexpr std::uint8_t ActionEncoder2Cw = 0x91;    ///< Encoder 3 rotate CW  [PROVISIONAL].
inline constexpr std::uint8_t ActionEncoder3Ccw = 0x70;   ///< Encoder 4 rotate CCW [PROVISIONAL].
inline constexpr std::uint8_t ActionEncoder3Cw = 0x71;    ///< Encoder 4 rotate CW  [PROVISIONAL].
inline constexpr std::uint8_t ActionEncoder0Press = 0x37; ///< Encoder 1 press [PROVISIONAL].
inline constexpr std::uint8_t ActionEncoder1Press = 0x35; ///< Encoder 2 press [PROVISIONAL].
inline constexpr std::uint8_t ActionEncoder2Press = 0x33; ///< Encoder 3 press [PROVISIONAL].
inline constexpr std::uint8_t ActionEncoder3Press = 0x36; ///< Encoder 4 press [PROVISIONAL].

// Touch-strip action codes (report[9]). CONFIRMED by the vendor decompile
// (handleKeyEvents @0x1400d02b0, akp05_input_corrections.md §4): the firmware
// emits only down/move/up — there is NO tap/swipe/long-press on the wire. Those
// are host-side gestures synthesised from the down->up X delta (the input
// service owns that synthesis). Touch X is the SINGLE byte report[10] (0..255),
// NOT a BE16 value. 0x78/0x79 (setCoreX) and 0xB1/0xB2 (N4-Pro touchbar-mode
// toggle) exist but are not surfaced as input events here.
inline constexpr std::uint8_t ActionTouchMove = 0x97; ///< Touch contact moved; report[10] = X.
inline constexpr std::uint8_t ActionTouchDown = 0x98; ///< Touch press began; report[10] = X.
inline constexpr std::uint8_t ActionTouchUp = 0x99;   ///< Touch press ended; report[10] = X.

/**
 * @brief Parsed input event from a raw 512-byte HID input report.
 *
 * The action code at report[9] discriminates event types:
 *   - 1..KeyCount          : key event; report[10] = press (non-zero) / release (0).
 *   - ActionEncoderN{Ccw,Cw,Press} : encoder event; the code itself carries
 *                            direction + encoder index (no magnitude byte).
 *   - ActionTouch{Down,Move,Up}    : raw touch-strip event; report[10] = X (0..255).
 */
struct InputEvent {
    /// Discriminates the input source and action.
    enum class Kind : std::uint8_t {
        KeyPressed,      ///< Key pressed; `index` = 1-based key number.
        KeyReleased,     ///< Key released; `index` = 1-based key number.
        EncoderTurned,   ///< Encoder rotated; `value` = signed step count (+1 CW, -1 CCW).
        EncoderPressed,  ///< Encoder knob depressed.
        EncoderReleased, ///< Encoder knob released.
        TouchDown,       ///< Touch-strip press began; `value` = X coordinate (0..255).
        TouchMove,       ///< Touch-strip contact moved; `value` = X coordinate (0..255).
        TouchUp,         ///< Touch-strip press ended;  `value` = X coordinate (0..255).
    };
    Kind kind{Kind::KeyPressed};
    std::uint8_t index{0}; ///< Key or encoder index (meaning depends on Kind).
    std::int16_t value{0}; ///< Encoder step (+/-1) or touch X coordinate (0..255).
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

/**
 * @brief Parse the firmware-version string from a CRT VER response.
 *
 * The device answers the buildVersionRequest() probe via a HID GET_REPORT
 * pull (ITransport::readFeature), NOT on the interrupt-IN endpoint. The
 * response is a leading report-id byte (0x00) followed by an ASCII version
 * string such as "V3.AKP05E.01.007", NUL-terminated and zero-padded
 * (confirmed on a physical AKP05E 2026-05-20). Leading non-printable bytes
 * (the report-id) are skipped; the printable ASCII run is returned trimmed.
 *
 * @param frame Raw bytes from ITransport::readFeature().
 * @return The trimmed ASCII version string, or std::nullopt if the frame
 *         carries no printable payload.
 */
[[nodiscard]] std::optional<std::string> parseVersionResponse(std::span<std::uint8_t const> frame);

} // namespace ajazz::streamdeck::akp05
