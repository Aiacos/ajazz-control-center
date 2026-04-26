// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file keyboard.hpp
 * @brief Public factory functions and registration entry-point for keyboard backends.
 *
 * The two supported keyboard protocol families — VIA (open standard) and the
 * proprietary AJAZZ gaming protocol — each have a dedicated factory.  Call
 * registerAll(registry) once at start-up to populate the application's
 * DeviceRegistry with every known VID/PID pair.
 */
#pragma once

#include "ajazz/core/device.hpp"

namespace ajazz::core {
class DeviceRegistry;
}

namespace ajazz::keyboard {

/**
 * @brief Register all supported keyboard backends with the given DeviceRegistry.
 *
 * Inserts descriptors for both VIA-compatible and proprietary AJAZZ keyboards.
 * Safe to call multiple times — the registry skips already-registered entries.
 *
 * @param registry Registry to populate. Owned by the caller (typically
 *        `ajazz::app::Application`); audit finding A1 replaced the implicit
 *        singleton lookup with constructor injection.
 */
void registerAll(core::DeviceRegistry& registry);

/**
 * @brief Create a VIA-compatible keyboard backend.
 *
 * Targets the AK820 Pro, AK870, and other keyboards that implement the public
 * VIA raw-HID protocol.  Key commands used:
 * - @c 0x04  id_dynamic_keymap_get_keycode
 * - @c 0x05  id_dynamic_keymap_set_keycode
 * - @c 0x08  id_custom_set_value (RGB lighting)
 * - @c 0x0A  id_custom_save
 * - @c 0x0D  id_dynamic_keymap_macro_set_buffer
 *
 * @param d   Descriptor for the matched device (model, VID/PID, …).
 * @param id  Runtime identity including the USB serial string.
 * @return    Heap-allocated IDevice implementing IKeyRemappable, IRgbCapable,
 *            and IFirmwareCapable.
 */
[[nodiscard]] core::DevicePtr makeViaKeyboard(core::DeviceDescriptor const& d, core::DeviceId id);

/**
 * @brief Create a proprietary AJAZZ keyboard backend.
 *
 * Targets the AK680, AK510, and similar gaming keyboards that ship with the
 * closed-source Windows utility.  The wire protocol is a clean-room
 * reconstruction from USB captures; no vendor firmware or SDK code is reused.
 * See docs/protocols/keyboard/proprietary.md for the byte-level reference.
 *
 * @param d   Descriptor for the matched device.
 * @param id  Runtime identity including the USB serial string.
 * @return    Heap-allocated IDevice implementing IKeyRemappable, IRgbCapable,
 *            and IFirmwareCapable.
 */
[[nodiscard]] core::DevicePtr makeProprietaryKeyboard(core::DeviceDescriptor const& d,
                                                      core::DeviceId id);

} // namespace ajazz::keyboard
