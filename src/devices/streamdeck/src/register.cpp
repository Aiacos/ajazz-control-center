// SPDX-License-Identifier: GPL-3.0-or-later
#include "ajazz/core/device_registry.hpp"
#include "ajazz/streamdeck/streamdeck.hpp"
#include "akp153_protocol.hpp"

namespace ajazz::streamdeck {

void registerAll() {
    auto& reg = core::DeviceRegistry::instance();

    // AKP153 (international)
    reg.registerDevice(
        core::DeviceDescriptor{
            .vendorId = akp153::VendorId,
            .productId = akp153::ProductIdInternational,
            .family = core::DeviceFamily::StreamDeck,
            .model = "AJAZZ AKP153 / Mirabox HSV293S",
            .codename = "akp153",
        },
        &makeAkp153);

    // AKP153E (China variant — same protocol, different PID)
    reg.registerDevice(
        core::DeviceDescriptor{
            .vendorId = akp153::VendorId,
            .productId = akp153::ProductIdChinese,
            .family = core::DeviceFamily::StreamDeck,
            .model = "AJAZZ AKP153E",
            .codename = "akp153e",
        },
        &makeAkp153);

    // AKP03 (Mirabox N3) — placeholder VID/PID; refine after capture.
    reg.registerDevice(
        core::DeviceDescriptor{
            .vendorId = 0x0300,
            .productId = 0x3001,
            .family = core::DeviceFamily::StreamDeck,
            .model = "AJAZZ AKP03 / Mirabox N3",
            .codename = "akp03",
        },
        &makeAkp03);

    // AKP05 (Stream Dock Plus-class) — placeholder VID/PID.
    reg.registerDevice(
        core::DeviceDescriptor{
            .vendorId = 0x0300,
            .productId = 0x5001,
            .family = core::DeviceFamily::StreamDeck,
            .model = "AJAZZ AKP05 / AKP05E",
            .codename = "akp05",
        },
        &makeAkp05);
}

} // namespace ajazz::streamdeck
