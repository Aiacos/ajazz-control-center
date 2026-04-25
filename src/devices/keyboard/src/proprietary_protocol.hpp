// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file proprietary_protocol.hpp
 * @brief Wire-protocol constants and pure packet builders for proprietary AJAZZ keyboards.
 *
 * Clean-room reconstruction from USB captures of the AK680, AK510, and
 * similar gaming keyboards; no vendor firmware or SDK code is reused.
 * See docs/protocols/keyboard/proprietary.md for the authoritative
 * byte-level reference.
 *
 * All builder functions are pure (no I/O) so they can be unit-tested in
 * isolation without a physical device.
 */
#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace ajazz::keyboard::proprietary {

inline constexpr std::size_t ReportSize = 64;  ///< HID output report length in bytes.
inline constexpr std::uint8_t ReportId = 0x04; ///< HID report ID placed at byte 0.

// Command ids (host → device), placed at byte 1 of every output report.
inline constexpr std::uint8_t CmdGetFirmwareVersion =
    0x01; ///< Query firmware version; response carries major.minor.patch in bytes 2–4.
inline constexpr std::uint8_t CmdSetKeycode =
    0x05; ///< Remap a single key: layer(2) row(3) col(4) keycode-hi(5) keycode-lo(6).
inline constexpr std::uint8_t CmdSetRgbStatic =
    0x08; ///< Set a zone to a solid colour: zone(2) R(3) G(4) B(5).
inline constexpr std::uint8_t CmdSetRgbEffect =
    0x09; ///< Activate an RGB animation: zone(2) effect-id(3) speed(4).
inline constexpr std::uint8_t CmdSetRgbBuffer =
    0x0a; ///< Upload per-LED RGB8 data in 60-byte chunks.
inline constexpr std::uint8_t CmdSetRgbBrightness =
    0x0b; ///< Set global brightness 0–100: value clamped at byte 2.
inline constexpr std::uint8_t CmdSetLayer =
    0x0c; ///< Switch the active layer (0–MaxLayers-1): index at byte 2.
inline constexpr std::uint8_t CmdUploadMacro =
    0x0d; ///< Upload macro data in 56-byte chunks: slot(2) offset-hi(3) offset-lo(4) len(5).
inline constexpr std::uint8_t CmdCommitEeprom =
    0x0e; ///< Flush staged configuration to EEPROM (no payload).

// RGB zone identifiers used with CmdSetRgbStatic, CmdSetRgbEffect, and CmdSetRgbBuffer.
inline constexpr std::uint8_t ZoneKeys = 0x00;  ///< Main key matrix (104 LEDs).
inline constexpr std::uint8_t ZoneSides = 0x01; ///< Side-lighting strip (18 LEDs).
inline constexpr std::uint8_t ZoneLogo = 0x02;  ///< Logo badge (4 LEDs).

// LED counts per zone, verified against docs/protocols/keyboard/proprietary.md.
inline constexpr std::uint16_t LedCountKeys = 104; ///< LEDs in the ZoneKeys matrix.
inline constexpr std::uint16_t LedCountSides = 18; ///< LEDs in the ZoneSides strip.
inline constexpr std::uint16_t LedCountLogo = 4;   ///< LEDs in the ZoneLogo badge.

// Chunk sizes derived from the 64-byte report capacity.
// RGB: 4 header bytes (cmd + zone + offset-hi + offset-lo) → 60 bytes payload.
// Macro: 8 header bytes (cmd + slot + offset-hi + offset-lo + len + 3 rsvd) → 56 bytes payload.
inline constexpr std::size_t RgbBufferChunk =
    60; ///< RGB8 bytes that fit in one CmdSetRgbBuffer report.
inline constexpr std::size_t MacroChunk =
    56; ///< Macro bytes that fit in one CmdUploadMacro report.

inline constexpr std::uint8_t MaxLayers =
    4; ///< Maximum number of remappable key layers supported by the firmware.

/**
 * @brief Build a zero-padded 64-byte HID output report.
 *
 * Fills byte 0 with ReportId and byte 1 with @p cmd; all remaining bytes
 * are zero so callers only need to set the fields that differ.
 *
 * @param cmd  Command id to place at byte 1.
 * @return     64-byte array ready for ITransport::write().
 */
[[nodiscard]] inline std::array<std::uint8_t, ReportSize> makeReport(std::uint8_t cmd) {
    std::array<std::uint8_t, ReportSize> pkt{};
    pkt[0] = ReportId;
    pkt[1] = cmd;
    return pkt;
}

// ---------------------------------------------------------------------------
// Packet builders (pure; unit-testable without a physical device).
// ---------------------------------------------------------------------------

/// @brief Build a CmdGetFirmwareVersion (0x01) report.
[[nodiscard]] std::array<std::uint8_t, ReportSize> buildGetFirmwareVersion();

/**
 * @brief Build a CmdSetKeycode (0x05) report.
 *
 * @param layer    Layer index (0-based, < MaxLayers).
 * @param row      Row index within the key matrix.
 * @param col      Column index within the key matrix.
 * @param keycode  HID usage code, big-endian in bytes 5–6.
 */
[[nodiscard]] std::array<std::uint8_t, ReportSize>
buildSetKeycode(std::uint8_t layer, std::uint8_t row, std::uint8_t col, std::uint16_t keycode);

/**
 * @brief Build a CmdSetRgbStatic (0x08) report.
 *
 * @param zone  Zone id (ZoneKeys / ZoneSides / ZoneLogo).
 * @param r     Red component 0–255.
 * @param g     Green component 0–255.
 * @param b     Blue component 0–255.
 */
[[nodiscard]] std::array<std::uint8_t, ReportSize>
buildSetRgbStatic(std::uint8_t zone, std::uint8_t r, std::uint8_t g, std::uint8_t b);

/**
 * @brief Build a CmdSetRgbEffect (0x09) report.
 *
 * @param zone      Zone id.
 * @param effectId  Animation preset id (firmware-defined).
 * @param speed     Animation speed 0–255.
 */
[[nodiscard]] std::array<std::uint8_t, ReportSize>
buildSetRgbEffect(std::uint8_t zone, std::uint8_t effectId, std::uint8_t speed);

/**
 * @brief Build a CmdSetRgbBrightness (0x0b) report.
 *
 * @param percent  Brightness level 0–100; values above 100 are clamped.
 */
[[nodiscard]] std::array<std::uint8_t, ReportSize> buildSetRgbBrightness(std::uint8_t percent);

/**
 * @brief Build a CmdSetLayer (0x0c) report.
 *
 * @param layer  Desired active layer (clamped to MaxLayers–1).
 */
[[nodiscard]] std::array<std::uint8_t, ReportSize> buildSetLayer(std::uint8_t layer);

/// @brief Build a CmdCommitEeprom (0x0e) report (no payload).
[[nodiscard]] std::array<std::uint8_t, ReportSize> buildCommitEeprom();

/**
 * @brief Return the LED count for a zone id.
 *
 * @param zone  One of ZoneKeys, ZoneSides, or ZoneLogo.
 * @return      Number of LEDs in the zone, or 0 for an unknown zone id.
 */
[[nodiscard]] std::uint16_t ledCountForZone(std::uint8_t zone);

/**
 * @brief Map a human-readable zone name to its numeric id.
 *
 * @param name  One of @c "keys", @c "sides", or @c "logo".
 * @return      Corresponding zone id, or @c 0xFF when the name is unknown.
 */
[[nodiscard]] std::uint8_t zoneIdFromName(std::string_view name);

} // namespace ajazz::keyboard::proprietary
