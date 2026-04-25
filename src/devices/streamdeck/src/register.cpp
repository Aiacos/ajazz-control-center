// SPDX-License-Identifier: GPL-3.0-or-later
/** @file register.cpp
 *  @brief Device registration for all AJAZZ Stream Dock variants.
 *
 *  Calls @ref core::DeviceRegistry::registerDevice() for every known AKP
 *  device family so that the registry can match USB enumeration results to
 *  the correct factory function at runtime.
 *
 *  @note  VID/PID values for the AKP03 and AKP05 are provisional placeholders
 *         captured from early hardware samples; update after final capture.
 */
#include "ajazz/core/device_registry.hpp"
#include "ajazz/streamdeck/streamdeck.hpp"
#include "akp153_protocol.hpp"

namespace ajazz::streamdeck {

/** @brief Register all known Stream Dock device descriptors with the global
 *         @ref core::DeviceRegistry.
 *
 *  This function must be called once during application initialisation (before
 *  USB enumeration begins).  Subsequent calls are safe but redundant — the
 *  registry silently replaces duplicate VID/PID entries.
 *
 *  Registered devices:
 *  - **AKP153** (VID=akp153::VendorId, PID=0x1001) — international variant
 *  - **AKP153E** (VID=akp153::VendorId, PID=0x1002) — China market variant
 *  - **AKP03 / Mirabox N3** (VID=0x0300, PID=0x3001) — provisional IDs
 *  - **AKP05 / AKP05E** (VID=0x0300, PID=0x5001) — provisional IDs
 *
 *  @see core::DeviceRegistry::registerDevice()
 *  @see makeAkp153(), makeAkp03(), makeAkp05()
 */
void registerAll() {
    auto& reg = core::DeviceRegistry::instance();

    // AKP153 (international) — 15 LCD keys (5×3), no encoder.
    reg.registerDevice(
        core::DeviceDescriptor{
            .vendorId = akp153::VendorId,
            .productId = akp153::ProductIdInternational,
            .family = core::DeviceFamily::StreamDeck,
            .model = "AJAZZ AKP153 / Mirabox HSV293S",
            .codename = "akp153",
            .keyCount = 15,
            .gridColumns = 5,
            .encoderCount = 0,
        },
        &makeAkp153);

    // AKP153E (China variant — same protocol, different PID).
    reg.registerDevice(
        core::DeviceDescriptor{
            .vendorId = akp153::VendorId,
            .productId = akp153::ProductIdChinese,
            .family = core::DeviceFamily::StreamDeck,
            .model = "AJAZZ AKP153E",
            .codename = "akp153e",
            .keyCount = 15,
            .gridColumns = 5,
            .encoderCount = 0,
        },
        &makeAkp153);

    // AKP03 (Mirabox N3) — 6 LCD keys (3×2) + 1 encoder.
    reg.registerDevice(
        core::DeviceDescriptor{
            .vendorId = 0x0300,
            .productId = 0x3001,
            .family = core::DeviceFamily::StreamDeck,
            .model = "AJAZZ AKP03 / Mirabox N3",
            .codename = "akp03",
            .keyCount = 6,
            .gridColumns = 3,
            .encoderCount = 1,
        },
        &makeAkp03);

    // AKP05 (Stream Dock Plus-class) — 5 LCD keys + 4 encoders + touch strip.
    reg.registerDevice(
        core::DeviceDescriptor{
            .vendorId = 0x0300,
            .productId = 0x5001,
            .family = core::DeviceFamily::StreamDeck,
            .model = "AJAZZ AKP05 / AKP05E",
            .codename = "akp05",
            .keyCount = 5,
            .gridColumns = 5,
            .encoderCount = 4,
            .hasTouchStrip = true,
        },
        &makeAkp05);
}

} // namespace ajazz::streamdeck
