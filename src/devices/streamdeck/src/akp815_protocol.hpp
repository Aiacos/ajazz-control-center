// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file akp815_protocol.hpp
 * @brief AJAZZ AKP815 wire-protocol constants.
 *
 * The AKP815 sits next to the AKP153 in the AJAZZ catalogue:
 *
 * - 15 LCD keys in a **5 rows × 3 columns** grid (vs. the AKP153's 3×5)
 * - 100 × 100 px JPEG keys, `Rot180`, no mirror (vs. 85×85 `Rot90`+mirror)
 * - 800 × 480 px addressable LCD strip (vs. the AKP153's 854×480)
 * - Mirabox V1 USB vendor (`0x5548`), PID `0x6672`
 *
 * Source: `[ajazz-sdk]/src/info.rs::Kind::Akp815` cross-referenced with
 * `[ajazz-sdk]/src/protocol/codes.rs::PID_AJAZZ_AKP815`. Detailed
 * specifications live in `docs/protocols/streamdeck/akp815.md`.
 *
 * Framing reuses the AKP153 packet layout (512-byte CRT-prefixed packets);
 * the only differences are the image geometry and the strip dimensions —
 * see `akp153_protocol.hpp` for the shared opcodes.
 *
 * @see akp153_protocol.hpp
 * @see ../_research-sources.md
 */
#pragma once

#include <cstddef>
#include <cstdint>

namespace ajazz::streamdeck::akp815 {

// USB identifiers per `[ajazz-sdk]/protocol/codes.rs`.
inline constexpr std::uint16_t VendorId = 0x5548;  ///< Mirabox V1 vendor.
inline constexpr std::uint16_t ProductId = 0x6672; ///< AKP815 product.

// Physical geometry.
inline constexpr std::uint8_t KeyCount = 15;        ///< Total LCD keys.
inline constexpr std::uint8_t KeyRows = 5;          ///< Physical rows (taller than wide).
inline constexpr std::uint8_t KeyCols = 3;          ///< Physical columns.
inline constexpr std::uint16_t KeyWidthPx = 100;    ///< Per-key JPEG dimension.
inline constexpr std::uint16_t KeyHeightPx = 100;   ///< Per-key JPEG dimension.
inline constexpr std::uint16_t StripWidthPx = 800;  ///< Addressable LCD-strip width.
inline constexpr std::uint16_t StripHeightPx = 480; ///< Addressable LCD-strip height.

/// 512-byte v1-API packets — identical to AKP153.
inline constexpr std::size_t PacketSize = 512;

} // namespace ajazz::streamdeck::akp815
