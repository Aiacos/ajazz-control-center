// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file akp03_protocol.hpp
 * @brief AJAZZ AKP03 / Mirabox N3 wire protocol constants and builders.
 *
 * The AKP03 shares the AKP153 framing (512-byte packets, `CRT` prefix,
 * 3-byte ASCII command words) but ships **6 LCD keys + 3 non-LCD side
 * buttons + 3 rotary encoders** behind a single HID descriptor (see
 * `[ajazz-sdk]` `Kind::Akp03`: `key_count = 6 + 3`, `encoder_count = 3`).
 *
 * Two-stage protocol abstraction tracked in `[mirajazz]` README:
 * - "v2" (this family): 1024-byte packets, no release events for keys
 * - "v3" (AKP03R rev. 2 and Mirabox N4): 1024-byte packets, full press/release
 *
 * We currently model only the v2-equivalent surface; image rotation,
 * larger 1024-byte packets, and v3 release events are tracked in
 * `TODO.md` under "AKP03 protocol version upgrade".
 *
 * Image format (per `[ajazz-sdk]`): 60×60 JPEG, `Rot0`, no mirror. The
 * `Akp03RRev2` revision uses 64×64 `Rot90` — handled by a future subclass.
 *
 * @see akp153_protocol.hpp (shared framing specification)
 * @see ../_research-sources.md (citation tags resolved here)
 */
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>

namespace ajazz::streamdeck::akp03 {

// USB identifiers — AJAZZ AKP03 family (Mirabox V2 vendor space `0x0300`).
//
// Multiple PIDs share an identical wire protocol because they correspond to
// firmware revisions and regional variants. The full list comes from
// `[ajazz-sdk]/src/protocol/codes.rs` and `[opendeck-akp03]`'s udev rules
// (see `docs/protocols/streamdeck/_research-sources.md`).
inline constexpr std::uint16_t VendorId = 0x0300; ///< Mirabox V2 vendor (`VENDOR_ID_MIRABOX_V2`).
inline constexpr std::uint16_t ProductIdAkp03 = 0x1001;      ///< AJAZZ AKP03.
inline constexpr std::uint16_t ProductIdAkp03E = 0x3002;     ///< AJAZZ AKP03E.
inline constexpr std::uint16_t ProductIdAkp03R = 0x1003;     ///< AJAZZ AKP03R.
inline constexpr std::uint16_t ProductIdAkp03RRev2 = 0x3003; ///< AJAZZ AKP03R rev. 2.
// NOTE: PID 0x3004 ("HOTSPOTEKUSB HID DEMO") was briefly filed here as an AKP03
// sibling; a 2026-05-20 firmware handshake ("V3.AKP05E.01.007") proved it is an
// AKP05E. It now lives in the AKP05 family — see register.cpp + akp05_protocol.hpp.
inline constexpr std::uint16_t ProductId = ProductIdAkp03; ///< Legacy alias.

// Physical geometry.
// `[ajazz-sdk]/info.rs::Kind::Akp03::key_count == 6 + 3` — 6 LCD keys *plus*
// 3 non-LCD side buttons. `display_key_count() == 6`, `encoder_count() == 3`.
// The N3 / Companion mapping (`[companion]`) confirms the 2x3 LCD grid plus
// a row of 3 plain buttons underneath.
inline constexpr std::uint8_t DisplayKeyCount = 6; ///< Number of LCD keys (2x3 grid).
inline constexpr std::uint8_t SideButtonCount = 3; ///< Non-LCD buttons below the grid.
inline constexpr std::uint8_t KeyCount = DisplayKeyCount + SideButtonCount; ///< Total buttons.
inline constexpr std::uint8_t EncoderCount = 3;  ///< Rotary encoders (1 large + 2 small).
inline constexpr std::uint16_t KeyWidthPx = 60;  ///< Native LCD key size in pixels.
inline constexpr std::uint16_t KeyHeightPx = 60; ///< Native LCD key size in pixels.

// `[ajazz-sdk]` declares the AKP03 family as `is_v2_api()`, which means
// 1024-byte packets. Our backend still sends 512-byte packets today; the
// constant below is kept at 512 for backward compatibility but a planned
// follow-up will gate this on protocol-version detection (TODO.md →
// "AKP03 v2 framing migration").
inline constexpr std::size_t PacketSize = 512;

// Action codes carried at byte 9 of an input report.
// Source: `[ajazz-sdk]/protocol/codes.rs`, cross-checked against
// `[opendeck-akp03]/src/lib.rs` and `tomekceszke/ajazz-akp03e` Python SDK.
inline constexpr std::uint8_t ActionNop = 0x00;           ///< Idle / keep-alive frame.
inline constexpr std::uint8_t ActionLcdKey1 = 0x01;       ///< Top-left LCD key.
inline constexpr std::uint8_t ActionLcdKey6 = 0x06;       ///< Bottom-right LCD key.
inline constexpr std::uint8_t ActionSideButton7 = 0x25;   ///< Non-LCD button 7 (left).
inline constexpr std::uint8_t ActionSideButton8 = 0x30;   ///< Non-LCD button 8 (centre).
inline constexpr std::uint8_t ActionSideButton9 = 0x31;   ///< Non-LCD button 9 (right).
inline constexpr std::uint8_t ActionEncoder0Press = 0x33; ///< Encoder 0 press (left/large).
inline constexpr std::uint8_t ActionEncoder1Press = 0x35; ///< Encoder 1 press (middle/top).
inline constexpr std::uint8_t ActionEncoder2Press = 0x34; ///< Encoder 2 press (right).
inline constexpr std::uint8_t ActionEncoder0Ccw = 0x90;   ///< Encoder 0 rotate CCW.
inline constexpr std::uint8_t ActionEncoder0Cw = 0x91;    ///< Encoder 0 rotate CW.
inline constexpr std::uint8_t ActionEncoder1Ccw = 0x50;   ///< Encoder 1 rotate CCW.
inline constexpr std::uint8_t ActionEncoder1Cw = 0x51;    ///< Encoder 1 rotate CW.
inline constexpr std::uint8_t ActionEncoder2Ccw = 0x60;   ///< Encoder 2 rotate CCW.
inline constexpr std::uint8_t ActionEncoder2Cw = 0x61;    ///< Encoder 2 rotate CW.

// Command words: bytes 0..2 of every packet are the ASCII prefix "CRT";
// bytes 5..7 carry the command word. All remaining bytes are zero-padded.
//
// Wire-format research from `[ajazz-sdk]/protocol/codes.rs` shows the AKP03
// family actually shares the **AKP153 opcode set** — image transfers use
// `BAT` (the JPEG batch-transfer mnemonic), not a separate PNG opcode.
// The previous `CmdImagePng` constant was a guess from when `akp03.md`
// said the device expected PNG; the real on-wire image format is **JPEG**
// with per-revision rotation flags (see `key_image_format()` in
// `[ajazz-sdk]`).
inline constexpr std::array<std::uint8_t, 3> CmdPrefix{0x43, 0x52, 0x54}; ///< Packet header "CRT".
inline constexpr std::array<std::uint8_t, 3> CmdLight{0x4c, 0x49, 0x47};  ///< Set brightness "LIG".
inline constexpr std::array<std::uint8_t, 3> CmdImage{0x42,
                                                      0x41,
                                                      0x54}; ///< JPEG image transfer "BAT".
/// @deprecated Kept as an alias for the old `CmdImagePng` name; remove once
///             callers migrate to @ref CmdImage.
inline constexpr std::array<std::uint8_t, 3> CmdImagePng = CmdImage;
inline constexpr std::array<std::uint8_t, 3> CmdStop{0x53, 0x54, 0x50}; ///< Flush / stop "STP".

// Vendor-RE-discovered opcodes (akp05_vendor.md §3, 2026-05-17). Same wire
// format applies to the whole AKP family (AKP03/AKP05/AKP153/AKP815) per
// the SDLibrary1.dll Ghidra audit. See roadmap §11.3.
inline constexpr std::array<std::uint8_t, 3> CmdVersion{0x56,
                                                        0x45,
                                                        0x52}; ///< Firmware version "VER".
inline constexpr std::array<std::uint8_t, 5> UploadFinishedMarker{
    0x55,
    0x4c,
    0x45,
    0x4e,
    0x44}; ///< End-of-image-burst commit sentinel "ULEND" (5 bytes).
inline constexpr std::array<std::uint8_t, 3> CmdClear{0x43, 0x4c, 0x45}; ///< Clear key(s) "CLE".
inline constexpr std::array<std::uint8_t, 3> CmdInit{0x44, 0x49, 0x53};  ///< Display init "DIS".
inline constexpr std::array<std::uint8_t, 3> CmdSleep{0x48, 0x41, 0x4e}; ///< Sleep "HAN".

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
 * @brief Build the firmware-version probe (CRT VER, no payload).
 *
 * Vendor sends at open time; we wire only the request side per roadmap §11.3.
 */
[[nodiscard]] std::array<std::uint8_t, PacketSize> buildVersionRequest();

/**
 * @brief Build the upload-finished sentinel (CRT ULEND, 5-byte command at 5..9).
 *
 * Emitted after every chunked image upload to commit the burst. Vendor
 * RE annotation in akp05_vendor.md §3 notes that missing this may cause
 * firmware desync on large bursts.
 */
[[nodiscard]] std::array<std::uint8_t, PacketSize> buildUploadFinished();

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
 * The AKP03 family multiplexes three input surfaces over the single tag
 * byte at offset 9 — see the `ActionXxx` table above and the per-event
 * action codes in `[ajazz-sdk]/protocol/codes.rs`.
 *
 * `[companion]` documents that **side buttons (`0x25`/`0x30`/`0x31`) emit
 * only on release** and rotation events arrive one-per-detent. The parser
 * fires `KeyPressed` for the release edge of those buttons to preserve a
 * uniform UX; higher layers can synthesise a paired `KeyReleased` if a
 * dwell-time is required.
 */
struct InputEvent {
    /// Type of input that occurred.
    enum class Kind : std::uint8_t {
        KeyPressed,      ///< LCD key pressed; `index` = 1-based key number 1..6.
        KeyReleased,     ///< LCD key released; `index` = 1-based key number 1..6.
        SideButton,      ///< Non-LCD side button fired (release-only); `index` = 7..9.
        EncoderTurned,   ///< Encoder rotated; `index` = 0-based encoder number 0..2,
                         ///< `delta` = +1 (CW) or -1 (CCW).
        EncoderPressed,  ///< Encoder knob pressed down; `index` = 0..2.
        EncoderReleased, ///< Encoder knob released; `index` = 0..2.
        Nop,             ///< Idle / keep-alive frame; consumers ignore.
    };
    Kind kind{Kind::KeyPressed};
    std::uint8_t index{0}; ///< Key (1-based) or encoder (0-based) index.
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
