// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file register.cpp
 * @brief Populates the caller-owned DeviceRegistry with known keyboard VID/PID pairs.
 *
 * Contains representative static entries for the VIA and proprietary families.
 * The full list is extended at runtime from
 * @c resources/device-db/keyboards.json once the device database ships.
 */
#include "ajazz/core/device_registry.hpp"
#include "ajazz/keyboard/keyboard.hpp"

namespace ajazz::keyboard {

void registerAll(core::DeviceRegistry& registry) {
    auto& reg = registry;

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

    // (Removed 2026-04-29) The "AJAZZ AK (proprietary)" placeholder
    // entry under VID 0x05AC PID 0x024F was deleted: 0x05AC is Apple's
    // IEEE-assigned vendor ID, not AJAZZ. The intent was a stand-in
    // until the device database lands, but the live entry would have
    // misclassified an Apple keyboard with PID 0x024F as an AJAZZ
    // proprietary keyboard. AK820 / AK820 Pro / AK820 Max coverage
    // arrives via the VIA entry above (vendor uses SONiX prefix
    // 0x3151 across the line); per-model entries are added when the
    // device database wires in the proprietary backend's
    // model-specific routing logic. Tracking entry: TODO.md →
    // "Apple VID placeholder in keyboard registration".

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
