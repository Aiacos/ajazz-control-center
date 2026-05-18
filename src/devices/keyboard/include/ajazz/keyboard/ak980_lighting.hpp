// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file ak980_lighting.hpp
 * @brief AJAZZ AK980 PRO firmware-side RGB lighting mode taxonomy (opcode 0x13).
 *
 * The firmware ships 20 built-in lighting effects accessible via the
 * five-packet envelope `CMD_START (0x18) → CMD_MODE_BEGIN (0x13) → DATA →
 * CMD_SAVE (0x02) → CMD_FINISH (0xF0)`. This header carries the strong-typed
 * enum + the byte-precise DATA packet builder.
 *
 * Mode names come verbatim from the vendor's `1033.lan` UI string table
 * (lines 521-540) cross-corroborated by Ghidra decompile of `FUN_0042b0a0`
 * — see
 * [`docs/protocols/keyboard/ak980pro_vendor.md`](../../../../../docs/protocols/keyboard/ak980pro_vendor.md)
 * §3.4 + §3.4.1.
 *
 * Distinct from `mui.dll`'s 9 host-side "Real-time Lighting" effects
 * (Star/Rain/Breath/Spring/Vertical/Roll/Rotate/...) which are computed by
 * the vendor's UI and then pushed via the per-key RGB upload path (cmd
 * `0x20 0x04`) — see
 * [`ak980pro_mui_dll.md`](../../../../../docs/protocols/keyboard/ak980pro_mui_dll.md).
 */
#pragma once

#include <cstdint>

namespace ajazz::keyboard {

/**
 * @brief 20 firmware-built-in RGB lighting modes exposed by the AK980 PRO
 *        via opcode `0x13` (MODE_COMMAND).
 *
 * The byte values are the on-wire `mode_id` written to byte 1 of the DATA
 * packet. Modes 0x00..0x12 are animated effects with parameter knobs
 * (RGB tint when supported, rainbow flag, brightness 0..5, speed 0..5,
 * direction). Mode 0x13 is the no-op "lights off" sentinel.
 *
 * @see buildSetRgbModeData(), proprietary_protocol::CmdSetRgbMode
 */
enum class AK980LightingMode : std::uint8_t {
    Static = 0x00,     ///< 常亮 — all keys steady RGB tint.
    SingleOn = 0x01,   ///< 单点亮 — light up only the key being pressed.
    SingleOff = 0x02,  ///< 单熄灭 — extinguish only the key being pressed.
    Glittering = 0x03, ///< 繁星点点 — random twinkle.
    Falling = 0x04,    ///< 漫天飞舞 — falling pixels.
    Colourful = 0x05,  ///< 万紫千红 — rainbow blanket.
    Breath = 0x06,     ///< 动感呼吸 — fade in/out.
    Spectrum = 0x07,   ///< 光圈循环 — colour-cycle rings.
    Outward = 0x08,    ///< 彩浪涌动 — outward wave from centre.
    Scrolling = 0x09,  ///< 万彩纵横 — horizontal scroll.
    Rolling = 0x0a,    ///< 寂静流光 — slow rolling glow.
    Rotating = 0x0b,   ///< 峰回环绕 — rotating accents.
    Explode = 0x0c,    ///< 一触即发 — burst from struck key.
    Launch = 0x0d,     ///< 一触惊艳 — launch trail from struck key.
    Ripples = 0x0e,    ///< 涟漪扩散 — ripple from struck key.
    Flowing = 0x0f,    ///< 川流不息 — continuous flow.
    Pulsating = 0x10,  ///< 重峰叠叠 — layered pulse.
    Tilt = 0x11,       ///< 斜风细雨 — diagonal sweep.
    Shuttle = 0x12,    ///< 来回穿梭 — shuttle back-and-forth.
    LedOff = 0x13,     ///< 关闭背光 — no-op disable (lights off).
};

/// Maximum brightness level accepted by the firmware (per `config.xml
/// brightness_max="5"`). Higher values are clamped.
inline constexpr std::uint8_t kAK980LightingBrightnessMax = 5;

/// Maximum speed level accepted by the firmware (per `config.xml
/// speed_max="5"`). Higher values are clamped.
inline constexpr std::uint8_t kAK980LightingSpeedMax = 5;

/// Default mode at first boot (per `config.xml default_mode="11"` =
/// `AK980LightingMode::Rotating`).
inline constexpr AK980LightingMode kAK980LightingDefaultMode = AK980LightingMode::Rotating;

} // namespace ajazz::keyboard
