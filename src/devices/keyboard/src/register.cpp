// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file register.cpp
 * @brief Populates the global DeviceRegistry with known keyboard VID/PID pairs.
 *
 * Contains representative static entries for the VIA and proprietary families.
 * The full list is extended at runtime from
 * @c resources/device-db/keyboards.json once the device database ships.
 */
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
            .hasRgb = true,
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
            .hasRgb = true,
        },
        &makeProprietaryKeyboard);

    // AJAZZ AK980 PRO (Microdia chipset 0x0c45). Confirmed via real-device
    // lsusb enumeration; protocol mapping is still scaffolded — keys / RGB
    // are routed through the proprietary backend until a captured trace
    // either confirms VIA compatibility or motivates a dedicated handler.
    reg.registerDevice(
        core::DeviceDescriptor{
            .vendorId = 0x0c45, // Microdia (chip vendor used by AK980 PRO)
            .productId = 0x8009,
            .family = core::DeviceFamily::Keyboard,
            .model = "AJAZZ AK980 PRO",
            .codename = "ak980pro",
            .hasRgb = true,
        },
        &makeProprietaryKeyboard);
}

} // namespace ajazz::keyboard
