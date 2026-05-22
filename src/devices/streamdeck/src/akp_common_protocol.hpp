// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file akp_common_protocol.hpp
 * @brief Command words shared verbatim across the AJAZZ AKP Stream Dock family.
 *
 * The AKP03 / AKP05 / AKP153 backends share an identical packet framing — the
 * "CRT" prefix plus a set of 3-byte ASCII command words — confirmed
 * byte-for-byte across all three headers and against the SDLibrary1.dll Ghidra
 * audit (akp05_vendor.md §3, 2026-05-17). These constants used to be
 * copy-pasted into each per-device header; they now live here as the single
 * source of truth and are pulled into each device namespace with `using`
 * declarations.
 *
 * Device-SPECIFIC opcodes (image transfer, encoder/main-screen, secondary
 * touch strip, boot logo, display init, sleep, …) deliberately stay in their
 * respective per-device headers because they diverge across the family — e.g.
 * the image opcode is "BAT" on AKP03/AKP153 but split into key/encoder/main
 * variants on AKP05.
 *
 * @see akp03_protocol.hpp / akp05_protocol.hpp / akp153_protocol.hpp
 */
#pragma once

#include <array>
#include <cstdint>

namespace ajazz::streamdeck::akp_common {

inline constexpr std::array<std::uint8_t, 3> CmdPrefix{0x43, 0x52, 0x54}; ///< Packet header "CRT".
inline constexpr std::array<std::uint8_t, 3> CmdLight{0x4c, 0x49, 0x47};  ///< Set brightness "LIG".
inline constexpr std::array<std::uint8_t, 3> CmdStop{0x53, 0x54, 0x50};   ///< Flush / stop "STP".
inline constexpr std::array<std::uint8_t, 3> CmdClear{0x43, 0x4c, 0x45};  ///< Clear key(s) "CLE".
inline constexpr std::array<std::uint8_t, 3> CmdVersion{0x56,
                                                        0x45,
                                                        0x52}; ///< Firmware version "VER".
inline constexpr std::array<std::uint8_t, 5> UploadFinishedMarker{
    0x55,
    0x4c,
    0x45,
    0x4e,
    0x44}; ///< End-of-image-burst commit sentinel "ULEND" (5 bytes).

} // namespace ajazz::streamdeck::akp_common
