// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_ak980_firmware_lighting.cpp
 * @brief End-to-end byte-level test of the AK980 PRO 20-mode firmware
 *        lighting picker (opcode 0x13 envelope, IFirmwareLightingCapable).
 *
 * Pins the 4-packet envelope `setFirmwareLightingMode()` emits
 * (START 0x18 -> MODE_BEGIN 0x13 -> DATA -> SAVE 0x02), plus the 20
 * vendor-defined mode catalogue exposed via
 * `availableFirmwareModes()`. Catches regressions in either the wire
 * format or the QML-facing service contract.
 */
#include "ajazz/core/capabilities.hpp"
#include "ajazz/core/device.hpp"
#include "ajazz/keyboard/ak980_lighting.hpp"
#include "ajazz/keyboard/keyboard.hpp"
#include "fixtures/mock_transport.hpp"

#include <cstdint>
#include <memory>
#include <utility>

#include <catch2/catch_test_macros.hpp>

using namespace ajazz;

namespace {

core::DeviceDescriptor makeDescriptor() {
    core::DeviceDescriptor d{};
    d.vendorId = 0x0c45;
    d.productId = 0x8009;
    d.family = core::DeviceFamily::Keyboard;
    d.model = "AK980 PRO (test)";
    d.codename = "ak980pro";
    return d;
}

core::DeviceId makeId() {
    core::DeviceId id{};
    id.vendorId = 0x0c45;
    id.productId = 0x8009;
    id.serial = "TEST";
    return id;
}

} // namespace

TEST_CASE("AK980 PRO advertises IFirmwareLightingCapable with 20 vendor modes",
          "[ak980][lighting][CAPTURE-04][vendor-re]") {
    auto transport = std::make_unique<tests::MockTransport>();
    transport->open();
    auto device = keyboard::makeProprietaryKeyboardWithTransport(makeDescriptor(), makeId(),
                                                                 std::move(transport));
    REQUIRE(device != nullptr);

    auto* lighting = dynamic_cast<core::IFirmwareLightingCapable*>(device.get());
    REQUIRE(lighting != nullptr);

    auto const modes = lighting->availableFirmwareModes();
    REQUIRE(modes.size() == 20);

    // First entry is Static = 0x00; last is LedOff = 0x13 (per
    // ak980_lighting.hpp enum order).
    REQUIRE(modes.front().id ==
            static_cast<std::uint8_t>(keyboard::AK980LightingMode::Static));
    REQUIRE(modes.back().id ==
            static_cast<std::uint8_t>(keyboard::AK980LightingMode::LedOff));

    REQUIRE(lighting->brightnessMax() == keyboard::kAK980LightingBrightnessMax);
    REQUIRE(lighting->speedMax() == keyboard::kAK980LightingSpeedMax);
}

TEST_CASE("AK980 PRO setFirmwareLightingMode emits the 4-packet envelope",
          "[ak980][lighting][CAPTURE-04][vendor-re]") {
    auto transport = std::make_unique<tests::MockTransport>();
    auto* observer = transport.get();
    transport->open();
    auto device = keyboard::makeProprietaryKeyboardWithTransport(makeDescriptor(), makeId(),
                                                                 std::move(transport));
    auto* lighting = dynamic_cast<core::IFirmwareLightingCapable*>(device.get());
    REQUIRE(lighting != nullptr);

    bool const ok = lighting->setFirmwareLightingMode(
        static_cast<std::uint8_t>(keyboard::AK980LightingMode::Breath),
        /*brightness*/ 3, /*speed*/ 2);
    REQUIRE(ok);

    auto const& writes = observer->writes();
    REQUIRE(writes.size() == 4);
    REQUIRE(observer->writeFeatureCount() == 4);

    // Packet 1: START control (ReportId=0x04, opcode 0x18, marker 0x01).
    auto const& p1 = writes.at(0);
    REQUIRE(p1.size() == 64);
    REQUIRE(p1[0] == 0x04);
    REQUIRE(p1[1] == 0x18);
    REQUIRE(p1[8] == 0x01);

    // Packet 2: MODE_BEGIN (opcode 0x13 at byte 1).
    auto const& p2 = writes.at(1);
    REQUIRE(p2.size() == 64);
    REQUIRE(p2[0] == 0x04);
    REQUIRE(p2[1] == 0x13);

    // Packet 3: DATA - mode id at byte 1, RGB white tint at 2..4,
    // brightness at byte 9, speed at byte 10, magic 0x55/0xAA at 14..15.
    auto const& p3 = writes.at(2);
    REQUIRE(p3.size() == 64);
    REQUIRE(p3[0] == 0x04);
    REQUIRE(p3[1] == static_cast<std::uint8_t>(keyboard::AK980LightingMode::Breath));
    REQUIRE(p3[2] == 0xff); // R
    REQUIRE(p3[3] == 0xff); // G
    REQUIRE(p3[4] == 0xff); // B
    REQUIRE(p3[9] == 3);    // brightness
    REQUIRE(p3[10] == 2);   // speed
    REQUIRE(p3[14] == 0x55);
    REQUIRE(p3[15] == 0xaa);

    // Packet 4: SAVE (opcode 0x02).
    auto const& p4 = writes.at(3);
    REQUIRE(p4.size() == 64);
    REQUIRE(p4[0] == 0x04);
    REQUIRE(p4[1] == 0x02);
}

TEST_CASE("AK980 PRO setFirmwareLightingMode clamps brightness/speed beyond ceiling",
          "[ak980][lighting][robustness]") {
    auto transport = std::make_unique<tests::MockTransport>();
    auto* observer = transport.get();
    transport->open();
    auto device = keyboard::makeProprietaryKeyboardWithTransport(makeDescriptor(), makeId(),
                                                                 std::move(transport));
    auto* lighting = dynamic_cast<core::IFirmwareLightingCapable*>(device.get());
    REQUIRE(lighting != nullptr);

    // Pass huge values; buildSetRgbModeData internally clamps to 5.
    REQUIRE(lighting->setFirmwareLightingMode(
        static_cast<std::uint8_t>(keyboard::AK980LightingMode::Rotating),
        /*brightness*/ 200, /*speed*/ 200));

    auto const& writes = observer->writes();
    REQUIRE(writes.size() == 4);
    auto const& dataPkt = writes.at(2);
    REQUIRE(dataPkt[9] == keyboard::kAK980LightingBrightnessMax);
    REQUIRE(dataPkt[10] == keyboard::kAK980LightingSpeedMax);
}
