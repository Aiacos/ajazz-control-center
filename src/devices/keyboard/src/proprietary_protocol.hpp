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

// ---------------------------------------------------------------------------
// Time-sync wire format (ARCH-05 amendment, 2026-05-17).
//
// AK980 PRO + AK820 Pro family (Sonix SN32F299 MCU, VID:PID 0x0c45:0x8009)
// expose a host-settable RTC via a three-packet sequence. Source-level
// cross-corroboration:
//
//   - github.com/gohv/EPOMAKER-Ajazz-AK820-Pro (Rust, src/protocol.rs)
//   - github.com/KyleBoyer/TFTTimeSync-node    (TypeScript, src/packets.ts)
//
// Both expose identical byte layouts. The control packets (preamble + save)
// use the default ReportId=0x04; the time-data packet uses ReportId=0x00
// (the magic 0x5A at byte 2 is the firmware's discriminator).
// ---------------------------------------------------------------------------
inline constexpr std::uint8_t CmdBatteryQuery =
    0x20; ///< Battery / per-key-RGB family opcode (sub-cmd byte 2 discriminates).
inline constexpr std::uint8_t BatteryQuerySub =
    0x01; ///< Sub-command for charge-level read (response byte 3 = percent 0..100).
inline constexpr std::uint8_t CmdStartTime =
    0x18; ///< Reset / "begin time-sync session" opcode — first packet of the 4-packet envelope.
inline constexpr std::uint8_t CmdSetTime =
    0x28; ///< Configure RTC opcode (preamble control packet, byte 1).
inline constexpr std::uint8_t CmdSaveRtc =
    0x02; ///< Persist RTC value to firmware NV-RAM (distinct from CmdCommitEeprom=0x0E).
inline constexpr std::uint8_t TimeDataReportId =
    0x00; ///< HID Report ID for the time-data packet (not the default 0x04).

// ---------------------------------------------------------------------------
// Settings batch (cmd 0x07 0x10) — single-shot save of fn / sleep / key-response /
// disable-winkey / disable-alt-f4 / disable-alt-tab + 0xAA 0x55 trailer.
//
// Wire layout (33-byte short report, BIT-7 checksum-style; verified via Ghidra
// decompile of DeviceDriver.exe FUN_0044eed0 callers — see
// docs/protocols/keyboard/ak980pro_vendor.md §13.2). Prior pass had the byte
// map wrong (claimed fn/sleep/delay at 5/6/7); deep RE corrected to the
// layout below. Constants only here — wiring deferred to a follow-up.
// ---------------------------------------------------------------------------
inline constexpr std::uint8_t CmdSettingsBatch = 0x07;   ///< Settings batch opcode.
inline constexpr std::uint8_t SettingsBatchSub = 0x10;   ///< Sub-cmd for save-batch.
inline constexpr std::uint8_t SettingsBatchTrailerHi = 0xaa; ///< Trailer high (byte 18).
inline constexpr std::uint8_t SettingsBatchTrailerLo = 0x55; ///< Trailer low  (byte 19).
inline constexpr std::size_t kSettingsByteDisableWinKey = 6;     ///< Bool: disable Windows key.
inline constexpr std::size_t kSettingsByteDisableAltF4 = 7;      ///< Bool: disable Alt+F4.
inline constexpr std::size_t kSettingsByteDisableAltTab = 8;     ///< Bool: disable Alt+Tab.
inline constexpr std::size_t kSettingsByteFnSwitch = 9;          ///< Fn-layer switch.
inline constexpr std::size_t kSettingsByteSleepTime = 10;        ///< Sleep timer (enum).
inline constexpr std::size_t kSettingsByteKeyResponseTime = 12;  ///< Key response level (1..5).
inline constexpr std::size_t kSettingsByteTrailerHi = 18;        ///< Trailer high offset.
inline constexpr std::size_t kSettingsByteTrailerLo = 19;        ///< Trailer low offset.

// ---------------------------------------------------------------------------
// TFT screen image-upload opcodes (240x135 RGB565 — AK980 PRO 1.14" TFT).
//
// TWO upload paths discovered (docs/protocols/keyboard/ak980pro_tft_protocol.md):
//   (a) 28-byte chunked path: opcode 0x7F 0x03 + 0x80|chunkIdx — 2 315 chunks
//       per frame, ~10.8 min for full 140-frame GIF. SLOW but works on every
//       firmware revision.
//   (b) 4 KiB bulk path: opcode 0x72 begin + bulk-write — ~4.5 s per frame,
//       **143× faster**. Preferred when available; fall back to (a) otherwise.
//
// 24-bit chunk index for (a) is split across bytes 1 / 3 / low-7-bits of
// byte 2 with the 0x80 marker. RGB565 big-endian, row-major top-down.
// ---------------------------------------------------------------------------
inline constexpr std::uint8_t CmdScreenHeader = 0x7f;     ///< Chunked path begin.
inline constexpr std::uint8_t CmdScreenSubBegin = 0x03;   ///< Sub-cmd for chunked begin.
inline constexpr std::uint8_t CmdScreenChunkMarker = 0x80;///< OR-mask for chunk index byte.
inline constexpr std::uint8_t CmdScreenBulkBegin = 0x72;  ///< 4 KiB bulk path begin (143× faster).
inline constexpr std::uint8_t CmdScreenSave = 0x02; ///< Persist screen state (semantic alias for CmdSaveRtc).

inline constexpr std::uint16_t kTftWidth = 240;         ///< TFT panel width in pixels.
inline constexpr std::uint16_t kTftHeight = 135;        ///< TFT panel height in pixels.
inline constexpr std::size_t kTftFrameBytes = 240u * 135u * 2u;  ///< RGB565 frame size (64 800 B).
inline constexpr std::size_t kTftChunkPayload = 28;     ///< Payload bytes per 64-byte chunked report.
inline constexpr std::size_t kTftBulkChunkSize = 4096;  ///< Bulk-path chunk size (4 KiB).
inline constexpr std::size_t kTftMaxFrames = 140;       ///< Max GIF frames the firmware accepts.
inline constexpr std::size_t kTftGifHeader = 256;       ///< GIF89a header byte count.
inline constexpr std::size_t kTftInterChunkMs = 2;      ///< Inter-chunk delay (ms).

// ---------------------------------------------------------------------------
// Wireless macro upload (cmd 0x19 0x04 + cmd 0x15 0x04 — DIFFERENT transport
// from wired 0x09 0x1C). See docs/protocols/keyboard/ak980pro_macros_protocol.md.
//
// LCD-aware time-sync alias (cmd 0x0C 0x10) — variant of CmdSetTime=0x28 used
// on LCD-display models like AK980 PRO. The 0x28 envelope we ship works; this
// constant is documented for future investigation whether the LCD variant
// drives a richer display-side clock animation.
// ---------------------------------------------------------------------------
inline constexpr std::uint8_t CmdSetRgbMode = 0x13;             ///< Firmware RGB lighting mode (opcode in 5-packet envelope; see ak980_lighting.hpp for the 20-mode enum).
inline constexpr std::uint8_t CmdFinish = 0xf0;                 ///< End-of-envelope sentinel (vendor sends after every multi-packet commit; not yet emitted by us — see ak980pro_vendor.md §13.7).
// Per-key RGB upload — opcode 0x20 multiplexed with battery query (sub 0x01)
// via the sub-cmd byte. See ak980pro_perkey_rgb_protocol.md §§1-3 for the
// full envelope (write header → RGB blob chunks → save). NOTE: wired path is
// MONOCHROMATIC ONLY (1 byte per LED, firmware limitation per §3.1); only the
// wireless path supports full per-key RGB color.
inline constexpr std::uint8_t kCmdPerKeyRgbWrite = 0x20;        ///< Per-key RGB write opcode (same as CmdBatteryQuery; discriminated by sub).
inline constexpr std::uint8_t kPerKeyRgbSub = 0x04;             ///< Sub-cmd for per-key RGB write.
inline constexpr std::uint8_t kCmdPerKeyRgbReadback = 0xf5;     ///< Per-key RGB read-back opcode (cmd 0xF5).
inline constexpr std::uint8_t kPerKeyReadbackWiredSub = 0x03;   ///< Read-back sub for wired path.
inline constexpr std::uint8_t kPerKeyReadbackWirelessSub = 0x09;///< Read-back sub for 2.4 G wireless path.
inline constexpr std::uint8_t kPerKeyModeWired = 0x03;          ///< Mode-byte value at packet offset 9 (wired).
inline constexpr std::uint8_t kPerKeyModeWireless = 0x08;       ///< Mode-byte value at packet offset 9 (2.4 G wireless).
inline constexpr std::size_t kPerKeyWiredBlobSize = 0xc0;       ///< 192 B — 1 byte/LED × 192 LEDs (MONOCHROME).
inline constexpr std::size_t kPerKeyWirelessBlobSize = 0x200;   ///< 512 B — 4 byte/LED [reserved=0, R, G, B] × 128 LEDs.
inline constexpr std::size_t kPerKeyWiredChunkCount = 3;        ///< 192 B / 64 B chunks = 3 chunks (wired).
inline constexpr std::size_t kPerKeyWirelessChunkCount = 8;     ///< 512 B / 64 B chunks = 8 chunks (NOT 6 as prior doc — corrected by deep RE per ak980pro_perkey_rgb_protocol.md §2).

inline constexpr std::uint8_t CmdMacroBeginWireless = 0x19;     ///< Wireless macro upload begin.
inline constexpr std::uint8_t MacroBeginWirelessSub = 0x04;     ///< Sub-cmd for wireless begin.
inline constexpr std::uint8_t CmdMacroChunkInfoWireless = 0x15; ///< Wireless macro chunk-info.
inline constexpr std::uint8_t MacroChunkInfoWirelessSub = 0x04; ///< Sub-cmd for chunk-info.
inline constexpr std::uint8_t CmdSetTimeLcd = 0x0c;             ///< LCD-aware time-sync alias.
inline constexpr std::uint8_t CmdSetTimeLcdSub = 0x10;          ///< Sub-cmd for LCD time-sync.

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
 * @brief Build a battery-level query feature report (opcode 0x20, sub 0x01).
 *
 * Sent via @c ITransport::writeFeature(); the response (also a 65-byte feature
 * report) is read via @c readFeature() and carries the charge percentage at
 * byte 3 (0..100; 0 means "no battery" e.g. wired-only operation).
 *
 * Vendor poll cadence on AK980 PRO is once per 15 s while wireless — see
 * ak980pro_vendor.md §3 (FUN_004358c0) and clean-reimplementation-roadmap.md
 * §11.2.
 */
[[nodiscard]] std::array<std::uint8_t, ReportSize> buildBatteryQuery();

/**
 * @brief Build the DATA packet for a firmware lighting-mode change (opcode 0x13).
 *
 * Per `ak980pro_vendor.md` §3.4 (`FUN_0042b0a0`): the full envelope is the
 * 5-packet sequence `CMD_START 0x18 → CMD_MODE_BEGIN 0x13 → DATA → CMD_SAVE
 * 0x02 → CMD_FINISH 0xF0`. This builder produces the third packet (DATA)
 * with byte-precise layout:
 *
 *  - byte 0:  HID Report ID (default 0x04 per our convention)
 *  - byte 1:  mode_id (see @ref AK980LightingMode)
 *  - byte 2:  R component (only honoured by modes that accept a tint)
 *  - byte 3:  G component
 *  - byte 4:  B component
 *  - byte 8:  rainbow flag (0 = solid tint, 1 = cycle hue)
 *  - byte 9:  brightness 0..5 (clamped; vendor enforces brightness_max=5)
 *  - byte 10: speed 0..5 (clamped; vendor enforces speed_max=5)
 *  - byte 11: direction (Left=0, Down=1, Up=2, Right=3)
 *  - bytes 14..15: trailer 0x55 0xAA
 *  - all other bytes 0x00
 *
 * The remaining 4 envelope packets are built by reusing existing helpers:
 * `buildSetTimeStart()` produces a valid `CMD_START 0x18` control packet;
 * the MODE_BEGIN, SAVE, and FINISH packets are simple `makeReport(cmd)` calls
 * with their respective opcodes. The full envelope is wired in
 * `ProprietaryKeyboard::setLightingMode()` (added in a follow-up commit once
 * a real-device hardware witness verifies the byte layout).
 *
 * @param modeId     One of the 20 @ref AK980LightingMode values.
 * @param r          Tint red component.
 * @param g          Tint green component.
 * @param b          Tint blue component.
 * @param rainbow    Non-zero to cycle hues instead of using the tint.
 * @param brightness 0..5 (clamped).
 * @param speed      0..5 (clamped).
 * @param direction  0..3 (Left/Down/Up/Right).
 *
 * @see ak980_lighting.hpp for the mode enum, ak980pro_vendor.md §3.4.1.
 */
[[nodiscard]] std::array<std::uint8_t, ReportSize>
buildSetRgbModeData(std::uint8_t modeId,
                    std::uint8_t r,
                    std::uint8_t g,
                    std::uint8_t b,
                    std::uint8_t rainbow,
                    std::uint8_t brightness,
                    std::uint8_t speed,
                    std::uint8_t direction);

/**
 * @brief Build the per-key RGB WRITE-HEADER packet (opcode 0x20 0x04).
 *
 * First packet of the 3-step per-key RGB envelope per
 * `ak980pro_perkey_rgb_protocol.md` §1-3:
 *   1. WRITE HEADER (this builder): ReportId=0x04, byte 1=0x20, byte 2=0x04,
 *      byte 9 = mode (kPerKeyModeWired=0x03 OR kPerKeyModeWireless=0x08).
 *   2. RGB blob chunks: 192 B (wired) or 512 B (wireless) sent as 64-byte
 *      slices via the feature-report path. NO opcode prefix on the chunks
 *      themselves; firmware infers them as continuation of step 1.
 *   3. SAVE: `buildCommitEeprom()` (opcode 0x0E) OR `makeReport(CmdSaveRtc)`
 *      (opcode 0x02). Per the vendor doc §3.7 the per-key RGB save uses 0x02
 *      (semantic alias for both RTC-save and per-key-RGB save).
 *
 * Wired path is MONOCHROMATIC ONLY (1 byte per LED, firmware limitation —
 * NOT a bug we introduce; per `ak980pro_perkey_rgb_protocol.md` §3.1).
 *
 * @param isWireless Pick the wireless mode byte (0x08) instead of wired (0x03).
 */
[[nodiscard]] std::array<std::uint8_t, ReportSize> buildPerKeyRgbWriteHeader(bool isWireless);

/**
 * @brief Build the per-key RGB read-back request packet (opcode 0xF5).
 *
 * Sent BEFORE reading the chunked feature-report response carrying the
 * current on-device per-key RGB state. Sub-cmd discriminates wired (0x03)
 * from wireless (0x09) per `ak980pro_perkey_rgb_protocol.md` §6.2.
 *
 * @param isWireless Pick the wireless read-back sub (0x09) instead of wired (0x03).
 */
[[nodiscard]] std::array<std::uint8_t, ReportSize> buildPerKeyRgbReadback(bool isWireless);

/**
 * @brief Build the time-sync start packet — first of the 4-packet envelope.
 *
 * ReportId=0x04, opcode 0x18 (CMD_START), byte[8]=0x01. Resets the firmware's
 * time-sync state machine and signals "the next 3 packets are a time-set
 * sequence". Without this packet the firmware ignores the subsequent
 * preamble + data + save (per gohv/EPOMAKER-Ajazz-AK820-Pro USB capture).
 */
[[nodiscard]] std::array<std::uint8_t, ReportSize> buildSetTimeStart();

/**
 * @brief Build the time-sync preamble packet (ReportId=0x04, opcode 0x28, byte[8]=0x01).
 *
 * Second of the 4-packet envelope. Sent AFTER buildSetTimeStart() and BEFORE
 * buildSetTimeData(). Tells the firmware "next packet is a CMD_TIME configuration
 * data block".
 */
[[nodiscard]] std::array<std::uint8_t, ReportSize> buildSetTimePreamble();

/**
 * @brief Build the 64-byte time-data packet (ReportId=0x00, magic 0x5A).
 *
 * Byte layout:
 *  - byte 0:  0x00 (HID Report ID — NOT the default 0x04 used by other commands)
 *  - byte 1:  0x01 (fixed marker)
 *  - byte 2:  0x5A (magic / firmware discriminator)
 *  - byte 3:  year - 2000 (single byte; years < 2000 saturate to 0)
 *  - byte 4:  month (1..12)
 *  - byte 5:  day (1..31)
 *  - byte 6:  hour (0..23)
 *  - byte 7:  minute (0..59)
 *  - byte 8:  second (0..59)
 *  - byte 9:  0x00
 *  - byte 10: wDayOfWeek (0=Sunday..6=Saturday)
 *             NB: gohv corpus hard-codes 0x04 here; that only matches the
 *             vendor on a Thursday. Ghidra decompile of DeviceDriver.exe
 *             (Agent C, 2026-05-17) confirmed the vendor reads the real
 *             day-of-week. See ARCH-05.2 in docs/protocols/keyboard/ak980pro_vendor.md.
 *  - bytes 11..61: 0x00
 *  - byte 62: 0xAA (delimiter high)
 *  - byte 63: 0x55 (delimiter low)
 *
 * @param year   Calendar year (e.g. 2026). Saturates at 2000 floor.
 * @param month  1..12.
 * @param day    1..31.
 * @param hour   0..23.
 * @param minute 0..59.
 * @param second 0..59.
 */
[[nodiscard]] std::array<std::uint8_t, ReportSize>
buildSetTimeData(std::uint16_t year,
                 std::uint8_t month,
                 std::uint8_t day,
                 std::uint8_t hour,
                 std::uint8_t minute,
                 std::uint8_t second,
                 std::uint8_t dayOfWeek = 0);

/**
 * @brief Build the time-sync save packet (ReportId=0x04, opcode 0x02).
 *
 * Sent AFTER the time-data packet. Instructs the firmware to persist the RTC
 * value to NV-RAM so it survives a power cycle.
 *
 * @note Distinct from buildCommitEeprom() (which uses opcode 0x0E for keymap +
 *       RGB + macro state). The RTC has its own save opcode 0x02.
 */
[[nodiscard]] std::array<std::uint8_t, ReportSize> buildSetTimeSave();

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
