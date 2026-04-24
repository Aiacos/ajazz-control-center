// SPDX-License-Identifier: GPL-3.0-or-later
#include "ajazz/core/device_registry.hpp"
#include "ajazz/keyboard/keyboard.hpp"

namespace ajazz::keyboard {

void registerAll() {
    auto& reg = core::DeviceRegistry::instance();

    // Representative VIA-compatible entry (AK820 Pro). Real VID/PID pairs
    // are populated from `resources/device-db/keyboards.json` at runtime.
    reg.registerDevice(
        core::DeviceDescriptor{
            .vendorId = 0x3151, // SONiX vendor prefix used by VIA
            .productId = 0x4021,
            .family = core::DeviceFamily::Keyboard,
            .model = "AJAZZ AK820 Pro (VIA)",
            .codename = "ak820pro",
        },
        &makeViaKeyboard);

    // Placeholder for proprietary keyboards — VID/PID refined per model
    // via the device database.
    reg.registerDevice(
        core::DeviceDescriptor{
            .vendorId = 0x05ac,
            .productId = 0x024f,
            .family = core::DeviceFamily::Keyboard,
            .model = "AJAZZ AK (proprietary)",
            .codename = "ak-proprietary",
        },
        &makeProprietaryKeyboard);
}

} // namespace ajazz::keyboard
