// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ajazz/core/device.hpp"

namespace ajazz::keyboard {

/// Register all supported keyboard backends (VIA and proprietary).
void registerAll();

/// VIA-compatible keyboards (AK820 Pro, AK870, ...). Uses the published
/// raw HID VIA protocol (command 0x04: dynamic keymap get/set, 0x08: RGB).
[[nodiscard]] core::DevicePtr makeViaKeyboard(core::DeviceDescriptor const& d, core::DeviceId id);

/// Proprietary AJAZZ keyboards that ship with a closed Windows tool. The
/// backend uses a reverse-engineered superset of the VIA protocol plus
/// manufacturer-specific RGB commands.
[[nodiscard]] core::DevicePtr makeProprietaryKeyboard(core::DeviceDescriptor const& d,
                                                      core::DeviceId id);

}  // namespace ajazz::keyboard
