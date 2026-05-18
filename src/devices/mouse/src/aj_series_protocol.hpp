// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file aj_series_protocol.hpp
 * @brief Vendor-correct wire-format primitives for AJAZZ AJ-series mice (P3.12).
 *
 * Byte-precise reimplementation of the AJ159 APEX vendor wire format per
 * `docs/protocols/mouse/aj_series_opcode_table.md`. Replaces the broken
 * opcode table in `aj_series.cpp` (every prior opcode was wrong against
 * AJ159 firmware — see roadmap §1.1-§1.3).
 *
 * This header is currently ADDITIVE: it ships the new builders + opcode
 * table + checksum function without modifying the existing
 * `AjSeriesMouse` class. The setter migration is the follow-up commit
 * (P3.12.2) — reviewing the new wire format in isolation first lets us
 * land the byte-precise spec without breaking-change scope-creep.
 *
 * @note Transport: AJ-series mice use **HID OUTPUT REPORTS** (`hid_write`,
 *       interrupt-OUT endpoint) — NOT feature reports (`hid_send_feature_report`).
 *       This was reverse-engineered by Ghidra audit of iot_driver.exe
 *       (`FUN_00702e40` = hid_write wrapper, called via gRPC `sendMsg`).
 *       The 65-byte SET_REPORT label in the vendor doc refers to the on-wire
 *       packet shape, NOT to the HID Feature Report transport type.
 */
#pragma once

#include "ajazz/core/capabilities.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace ajazz::mouse::aj_series {

/// Standard 65-byte report buffer per vendor envelope (`aj_series_opcode_table.md` §1):
///   - pkt[0]:    HID Report ID (0x05)
///   - pkt[1]:    opcode (vendor "byte 0" in the per-opcode tables)
///   - pkt[2..63]: payload bytes (vendor "byte 1..62")
///   - pkt[64]:   checksum (vendor "byte 63")
/// Vendor per-opcode tables use 0-based numbering STARTING FROM THE OPCODE,
/// so `vendor byte N` → `pkt[N + 1]` for N in [0, 63]. The +1 shift accounts
/// for the HID Report ID prefix that hidapi prepends on the wire.
inline constexpr std::size_t kReportSize = 65;

/// HID Report ID stamped at byte 0 of every output report. "Hard fact" per
/// `aj_series_opcode_table.md` §1: every mouse opcode rides Report ID 0x05.
inline constexpr std::uint8_t kReportId = 0x05;

/**
 * @brief Vendor-correct opcode table (replaces the all-wrong `CommandId` enum
 *        in aj_series.cpp).
 *
 * Citations are line-precise into `aj_series_opcode_table.md` §3.
 */
enum class FeaCmd : std::uint8_t {
    GetRev = 0x80,      ///< §3.1 — firmware version query (response uint16-LE at byte 1..2).
    SetReset = 0x02,    ///< §3.2 — factory reset.
    SetProfile = 0x05,  ///< §3.3 — active profile select.
    SetReport = 0x04,   ///< §3.4 — polling rate via _RateToNum lookup.
    SetLedParam = 0x07, ///< §3.5 — 8-byte LED setting block.
    MouseSetKeyMatrix = 0x50, ///< §3.6 — single button rebind (action at bytes 8..11).
    MouseGetKeyMatrix = 0xd0, ///< §3.7 — full key-matrix read.
    MouseSetFnMatrix = 0x51,  ///< §3.8 — Fn-layer key rebind (same shape as 0x50).
    MouseSetOption0 = 0x53,   ///< §3.9 — omnibus settings (LOD/sensitivity/sleep/battery LED).
    MouseGetOption0 = 0xd3,   ///< §3.9 — GET counterpart.
    MouseSetOption1 = 0x54,   ///< §3.10 — DPI table (8 × uint16-LE + 8 × {R,G,B}).
    MouseGetOption1 = 0xd4,   ///< §3.10 — GET counterpart.
    SetMacroSimple = 0x16,    ///< §3.11 — chunked macro upload (5 chunks × 56 bytes).
    GetMacro = 0x96,          ///< §3.11 — macro read-back.
    SetTftLcdData = 0x25,     ///< §3.12 — 16-bit RGB565 TFT LCD upload (chunked).
    GetTftLcdData = 0xa5,     ///< §3.12 — RGB565 TFT readback.
    SetScreen24Bit = 0x29,    ///< §3.12 — 24-bit RGB888 TFT upload (chunked variant).
    GetScreen24Bit = 0xa9,    ///< §3.12 — RGB888 TFT readback.
};

/// Max payload bytes per TFT chunk. Wire layout (our `pkt[]` indexing,
/// which is vendor "byte N" -> `pkt[N+1]` because pkt[0] is the HID
/// Report ID prefix):
///   - pkt[1]      vendor 0 = opcode 0x25
///   - pkt[2]      vendor 1 = currentFrame
///   - pkt[3]      vendor 2 = frameNum
///   - pkt[4]      vendor 3 = frameDelay
///   - pkt[5..6]   vendor 4..5 = chunkIndex (uint16-LE)
///   - pkt[7]      vendor 6 = chunkLen
///   - pkt[8]      vendor 7 = reserved
///   - pkt[9..63]  vendor 8..62 = up to 55 bytes of RGB565 pixel data
///   - pkt[64]     vendor 63 = BIT7 checksum (stamped after build by
///                              stampBit7Checksum; kReportSize-1)
/// 55 bytes / 2 = 27.5 RGB565 pixels per packet; the vendor pipeline
/// is byte-oriented (chunk_index drives reassembly), so pixels can
/// straddle packet boundaries.
inline constexpr std::size_t kTftChunkPayloadBytes = 55;

/// Polling-rate lookup table (`_RateToNum` per `aj_series_opcode_table.md` §3.4).
/// Maps Hz value → wire byte code. Returns 0x01 (1000 Hz default) for unknown
/// inputs to keep the wire packet well-formed.
[[nodiscard]] std::uint8_t pollRateToWireCode(std::uint16_t hz) noexcept;

/**
 * @brief Compute the BIT7 checksum and stamp it at byte 63 of @p pkt.
 *
 * Checksum = `sum(pkt[1..62]) & 0x7F` per `aj_series_opcode_table.md` §5
 * (CheckSumType.BIT7 = 0 — confirmed by 98-site renderer-bundle census;
 * zero call sites use BIT8 on the mouse path).
 *
 * @note Range pkt[1..62] matches our prior `aj_series.cpp` accumulator
 *       semantics. The spec acknowledges a residual ambiguity over
 *       pkt[0..62] vs pkt[1..62]; the difference is only the opcode byte
 *       (pkt[1]) — to be confirmed by USB capture once hardware is available.
 *
 * @param pkt 64-byte report buffer (pkt[0]=ReportId, pkt[1]=opcode, ...).
 */
void stampBit7Checksum(std::array<std::uint8_t, kReportSize>& pkt) noexcept;

// ---------------------------------------------------------------------------
// Per-opcode builders — every byte position cites `aj_series_opcode_table.md`.
// All builders use OUR buffer convention: pkt[0]=ReportId(0x05), pkt[1]=opcode,
// then vendor-spec "byte N" maps to pkt[N+1] for N >= 1 (vendor counts the
// opcode as their byte 0; their byte 63 = our pkt[63] checksum slot).
// ---------------------------------------------------------------------------

/// §3.1 GetRev — `[0x05, 0x80, 0, …, 0, checksum]`.
[[nodiscard]] std::array<std::uint8_t, kReportSize> buildGetRev();

/// §3.2 SetReset — `[0x05, 0x02, 0, …, 0, checksum]`. Factory reset (destructive!).
[[nodiscard]] std::array<std::uint8_t, kReportSize> buildSetReset();

/// §3.2 FEA_CMD_SET_RESERT — destructive "Restore defaults" packet.
///
/// Convenience alias over @ref buildSetReset that names the user-visible
/// semantics (factory reset). Wire body identical: opcode 0x02, no payload,
/// BIT7 checksum at pkt[64].
[[nodiscard]] std::array<std::uint8_t, kReportSize> buildFactoryReset();

/// §3.3 SetProfile — switches the active configuration slot.
/// pkt[1]=0x05 (opcode), pkt[2]=profile (0..7).
[[nodiscard]] std::array<std::uint8_t, kReportSize> buildSetProfile(std::uint8_t profile);

/// §3.4 SetReport — polling rate via `_RateToNum` lookup.
/// pkt[1]=0x04, pkt[2]=profile, pkt[3]=pollRateToWireCode(hz).
[[nodiscard]] std::array<std::uint8_t, kReportSize> buildSetReportRate(std::uint8_t profile,
                                                                       std::uint16_t hz);

/// §3.5 SetLedParam — 8-byte LED setting block.
/// pkt[1]=0x07, pkt[2..9] = [effect, (4-speed), value, mode_bits, R, G, B].
/// @param effect       Effect enum (0=Off, 1=AlwaysOn, 2=Breath, 3=Neon, 4=Wave,
///                     5=Dazzle, 6=Laser, 7=MusicFollow, 8=ScreenColor, 9=MusicFollow2).
/// @param speed        UI speed 0..4 (encoded as `4 - speed` per vendor).
/// @param value        Mode-specific value (brightness for AlwaysOn; option for others).
/// @param modeBits     Pre-encoded byte 4 (option-nibble << 4 | mode-bits low nibble).
[[nodiscard]] std::array<std::uint8_t, kReportSize> buildSetLedParam(std::uint8_t effect,
                                                                     std::uint8_t speed,
                                                                     std::uint8_t value,
                                                                     std::uint8_t modeBits,
                                                                     std::uint8_t r,
                                                                     std::uint8_t g,
                                                                     std::uint8_t b);

/// §3.6 MouseSetKeyMatrix — single button rebind. Action at bytes 8..11 (NOT
/// bytes 4..7 as our prior impl had it — that was the kCmdButton=0x24 bug).
/// pkt[1]=0x50, pkt[2]=profile, pkt[3]=button, pkt[9..12]=action 4 bytes.
[[nodiscard]] std::array<std::uint8_t, kReportSize>
buildMouseSetKeyMatrix(std::uint8_t profile, std::uint8_t button, std::uint32_t actionBE);

/// §3.8 MouseSetFnMatrix — Fn-layer key rebind (same shape as 0x50; profile slot
/// instead holds the Fn-layer index).
/// pkt[1]=0x51, pkt[2]=fnLayer, pkt[3]=button, pkt[9..12]=action 4 bytes.
[[nodiscard]] std::array<std::uint8_t, kReportSize>
buildMouseSetFnMatrix(std::uint8_t fnLayer, std::uint8_t button, std::uint32_t actionBE);

/**
 * @brief §3.8 thin alias for @ref buildMouseSetFnMatrix using the host-facing
 *        @ref ajazz::core::IMouseFnRemappable naming.
 *
 * Surfaces the Fn-layer rebind builder under the spelling the capability
 * mix-in uses (@c setFnLayerBinding → @c buildFnLayerRemap) without losing
 * the existing low-level builder name that other code already calls.
 * Clamps the Fn-layer index to 0..7 (same envelope shape as opcode 0x50,
 * whose profile slot is similarly clamped).
 *
 * @param fnLayer     Fn-layer index (clamped to 0..7 inside the builder).
 * @param buttonIndex Physical button index (vendor byte 2).
 * @param actionBE    4-byte action descriptor; emitted big-endian at vendor
 *                    bytes 8..11 per §3.6 (type / subtype / keyA / keyB).
 */
[[nodiscard]] std::array<std::uint8_t, kReportSize>
buildFnLayerRemap(std::uint8_t fnLayer, std::uint8_t buttonIndex, std::uint32_t actionBE);

/// §3.10 MouseSetOption1 — DPI table. Up to 8 stages, atomic upload.
/// pkt[1]=0x54, pkt[2]=activeIdx, pkt[3]=stageCount,
/// pkt[9..24]=8×uint16-LE DPI values, pkt[41..63]=8×{R,G,B} colour table.
///
/// ⚠ The 8th stage's B-channel (pkt[63]) is OVERWRITTEN by the BIT7
/// checksum on the wire (`aj_series_opcode_table.md` §3.10 edge case).
/// UI editors must grey out the 8th-stage colour swatch or omit it from
/// the picker entirely. Vendor app's firmware currently uses 7 visible
/// stages with the 8th reserved.
///
/// @param activeIdx  Currently-active DPI stage (0..7).
/// @param stageCount Number of enabled stages (1..8).
/// @param dpiValues  8 DPI values (50..42000); shorter spans zero-pad the tail.
/// @param colours    8 RGB triples; shorter spans zero-pad the tail.
[[nodiscard]] std::array<std::uint8_t, kReportSize>
buildMouseSetOption1(std::uint8_t activeIdx,
                     std::uint8_t stageCount,
                     std::span<std::uint16_t const> dpiValues,
                     std::span<std::array<std::uint8_t, 3> const> colours);

/**
 * @brief §3.10 spec-correct DPI table builder — carries the per-profile slot
 *        byte that @ref buildMouseSetOption1 omits.
 *
 * Companion to the legacy @ref buildMouseSetOption1 (which leaves the profile
 * slot implicit at 0 to preserve existing test wire bytes). The new
 * @ref ajazz::core::IDpiTableCapable surface carries a @c profile field on
 * @ref ajazz::core::DpiTable; this builder honours it at vendor byte 1 per
 * §3.10 line 442. Wire layout (our @c pkt[N+1] convention vs vendor byte N):
 *
 *   - pkt[1]      = 0x54 (FEA_CMD_MOUSE_SET_OPTIONPARAM1, vendor byte 0)
 *   - pkt[2]      = profile (clamped 0..7,                vendor byte 1)
 *   - pkt[3]      = activeStage (clamped 0..7,            vendor byte 2)
 *   - pkt[4]      = stageCount (clamped 1..8,             vendor byte 3)
 *   - pkt[5..8]   = 0 reserved                            (vendor bytes 4..7)
 *   - pkt[9..24]  = 8 × uint16-LE DPI values              (vendor bytes 8..23)
 *   - pkt[25..40] = 0 reserved (X/Y split future use)     (vendor bytes 24..39)
 *   - pkt[41..64] = 8 × {R,G,B} indicator colours         (vendor bytes 40..63)
 *   - pkt[64]     = BIT7 checksum (stamps over stage-7 B  per §3.10 line 452)
 *
 * Out-of-range scalars (@c profile/activeStage > 7, @c stageCount > 8) are
 * clamped inside the builder so the firmware always sees an acceptable wire
 * byte. DPI values are passed through unclamped — the wire format is uint16
 * and callers know their sensor's accepted bound (50..42000 on AJ159 APEX).
 *
 * @note Per §3.10 line 452, the 8th stage B-channel (pkt[64]) is overwritten
 *       by the BIT7 checksum stamp. Same vendor bug as @ref buildMouseSetOption1.
 */
[[nodiscard]] std::array<std::uint8_t, kReportSize>
buildDpiTable(ajazz::core::DpiTable const& table);

/**
 * @brief §3.9 MouseSetOption0 — omnibus settings packet.
 *
 * The vendor's "everything in one save" command. Carries DPI active stage,
 * polling rate, debounce, LOD, sensitivity X/Y, angle snap, sleep timers
 * (BT idle/deep, 2.4G idle/deep), battery LED colours, and the LED + logo-LED
 * sub-blocks in a single 64-byte packet.
 *
 * See `aj_series_opcode_table.md` §3.9 for the complete byte map.
 */
struct OptionPacket0 {
    std::uint8_t profile{0};        ///< pkt[9] — active profile.
    std::uint16_t pollRateHz{1000}; ///< pkt[10] — encoded via pollRateToWireCode.
    std::uint8_t debounceMs{1};     ///< pkt[11] — typical 0..10.
    /// pkt[13..14] — uint16-LE flags. Bit 0=lightOff, 1=wheelLightOff,
    /// 2=smooth, 3=ledSelect (battery-LED RGB enable), 4=powerSaveMode.
    std::uint16_t flags{0};
    std::uint8_t buttonChange{1};               ///< pkt[15].
    std::uint8_t wheelToButton{10};             ///< pkt[16].
    std::uint8_t buttonToWheel{10};             ///< pkt[17].
    std::array<std::uint8_t, 8> ledBlock{};     ///< pkt[25..32] — LightSettingToBuffer.
    std::array<std::uint8_t, 8> logoLedBlock{}; ///< pkt[33..40] — LogoLightToBuffer.
    std::uint16_t sleepBtIdleSec{0};            ///< pkt[41..42] uint16-LE.
    std::uint16_t sleepBtDeepSec{0};            ///< pkt[43..44] uint16-LE.
    std::uint16_t sleep24gIdleSec{0};           ///< pkt[45..46] uint16-LE.
    std::uint16_t sleep24gDeepSec{0};           ///< pkt[47..48] uint16-LE.
    std::uint8_t xSensitivity{100};             ///< pkt[51] — 0..100%.
    std::uint8_t ySensitivity{100};             ///< pkt[52] — 0..100%.
    std::uint8_t liftCutOff{0};                 ///< pkt[53] — 0=1mm, 1=2mm, 2=3mm.
    std::uint8_t angleSnap{0};                  ///< pkt[54] — 0/1.
    std::array<std::uint8_t, 3> batteryColorHigh{0, 0xff, 0}; ///< pkt[55..57] — high-charge RGB.
    std::array<std::uint8_t, 3> batteryColorLow{0xff, 0, 0};  ///< pkt[58..60] — low-charge RGB.
    std::uint8_t chargingSwitch{1}; ///< pkt[61] — LED-on-while-charging.
};

/// §3.9 build the omnibus packet from a populated @ref OptionPacket0.
[[nodiscard]] std::array<std::uint8_t, kReportSize> buildMouseSetOption0(OptionPacket0 const& opts);

/**
 * @brief §3.9 build the omnibus packet from a user-facing @ref ajazz::core::MouseSettings.
 *
 * High-level convenience over @ref buildMouseSetOption0: maps the semantic
 * @c MouseSettings fields (LOD enum, sleep timeouts, named flag bits,
 * sensitivity, battery LED colours, charging-LED master switch) onto the
 * vendor byte layout per `aj_series_opcode_table.md` §3.9, clamping each
 * field to its documented valid range so the firmware always sees an
 * acceptable wire byte. Fields the §3.9 byte map lists but does not
 * decode (@c buttonChange / @c wheelToButton / @c buttonToWheel at bytes
 * 14/15/16) are emitted at vendor defaults (1 / 10 / 10).
 *
 * The LED + logo-LED sub-blocks (bytes 24..31 and 32..39) are NOT carried
 * by @ref ajazz::core::MouseSettings — those live in @ref IRgbCapable and
 * propagate through this packet only via the host-side @ref OptionPacket0
 * cache. The builder leaves both sub-blocks zero so this entry point can
 * be called in isolation; the @c AjSeriesMouse setter wires the cached
 * blocks back in to keep the LED state coherent across commits.
 *
 * @param profile  Active profile slot (clamped to 0..7).
 * @param pollRate Poll rate in Hz; out-of-table values fall back to 1 KHz
 *                 via @ref pollRateToWireCode.
 * @param settings Field values; out-of-range entries clamped per byte.
 */
[[nodiscard]] std::array<std::uint8_t, kReportSize>
buildMouseSettings(std::uint8_t profile, std::uint16_t pollRate,
                   ajazz::core::MouseSettings const& settings);

/**
 * @brief §3.12 build one chunk of the TFT LCD upload envelope (opcode 0x25,
 *        16-bit RGB565).
 *
 * Vendor app's `_oledUpgrade`-style flow per `aj_series_opcode_table.md`
 * §3.12. One single-frame still image (clock + DPI widget) needs
 * `ceil(width * height * 2 / kTftChunkPayloadBytes)` packets. The receiver
 * stitches them by `chunkIndex` and refreshes the panel when the final
 * chunk arrives.
 *
 * Wire layout (vendor byte → our pkt[byte+1]):
 *   - pkt[1]  = 0x25 (FEA_CMD_SETTFTLCDDATA)
 *   - pkt[2]  = currentFrame (animation index, 0 for stills)
 *   - pkt[3]  = frameNum (total frames, 1 for stills)
 *   - pkt[4]  = frameDelay (ms between frames, 0 for stills)
 *   - pkt[5..6] = uint16-LE chunkIndex
 *   - pkt[7]  = chunkLen (bytes of valid data in this packet, <= kTftChunkPayloadBytes)
 *   - pkt[8]  = reserved (0)
 *   - pkt[9..63] = up to 55 bytes of RGB565 (little-endian uint16 per pixel)
 *   - pkt[64] = BIT7 checksum (stamped by stampBit7Checksum after build)
 *
 * @param frame      Animation frame index (0-based; pass 0 for static images).
 * @param frameCount Total frames (>= 1; pass 1 for static images).
 * @param frameDelayMs Inter-frame delay in milliseconds (ignored when frameCount==1).
 * @param chunkIndex Zero-based packet index inside the frame.
 * @param payload    Up to @ref kTftChunkPayloadBytes bytes of RGB565 pixel data.
 *                   Shorter payloads zero-pad the unused slots.
 * @return Fully-formed 65-byte HID Output Report, ready for
 *         @c stampBit7Checksum() + @c ITransport::write().
 */
[[nodiscard]] std::array<std::uint8_t, kReportSize>
buildSetTftLcdData(std::uint8_t frame, std::uint8_t frameCount, std::uint8_t frameDelayMs,
                   std::uint16_t chunkIndex, std::span<std::uint8_t const> payload);

} // namespace ajazz::mouse::aj_series
