// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file register.cpp
 * @brief Populates the global DeviceRegistry with known AJAZZ mouse VID/PID pairs.
 *
 * All listed models share the AJ-series backend (makeAjSeries()).  The table
 * is the authoritative source of VID/PID mappings for mouse devices.
 */
#include "ajazz/core/device_registry.hpp"
#include "ajazz/mouse/mouse.hpp"

namespace ajazz::mouse {

void registerAll() {
    auto& reg = core::DeviceRegistry::instance();

    static constexpr struct {
        std::uint16_t vid;
        std::uint16_t pid;
        char const* model;
        char const* codename;
    } kMice[] = {
        {0x3554, 0xf51a, "AJAZZ AJ159", "aj159"},
        {0x3554, 0xf51b, "AJAZZ AJ199", "aj199"},
        {0x3554, 0xf51c, "AJAZZ AJ339 Pro", "aj339"},
        {0x3554, 0xf51d, "AJAZZ AJ380", "aj380"},
    };

    for (auto const& m : kMice) {
        reg.registerDevice(
            core::DeviceDescriptor{
                .vendorId = m.vid,
                .productId = m.pid,
                .family = core::DeviceFamily::Mouse,
                .model = m.model,
                .codename = m.codename,
                // AJ-series mice expose 6 DPI stages with per-stage RGB.
                .dpiStageCount = 6,
                .hasRgb = true,
            },
            &makeAjSeries);
    }
}

} // namespace ajazz::mouse
