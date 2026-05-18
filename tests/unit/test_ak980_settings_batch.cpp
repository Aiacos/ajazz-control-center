// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_ak980_settings_batch.cpp
 * @brief Byte-level test of the AK980 PRO settings batch envelope
 *        (opcode 0x07 sub 0x10, ISettingsCapable — issue #57 / P3.x).
 *
 * Pins the 4-packet envelope `setKeyboardSettings()` emits
 * (START 0x18 -> SETTINGS-DATA 0x07 0x10 -> SAVE 0x02 -> FINISH 0xF0)
 * plus the field offsets inside the DATA packet (fn / sleep / response
 * at bytes 9 / 10 / 12, trailer 0xAA 0x55 at bytes 18 / 19) and the
 * host-side cache contract (clamping + persistence across reads).
 */
#include "ajazz/core/capabilities.hpp"
#include "ajazz/core/device.hpp"
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

TEST_CASE("AK980 PRO advertises ISettingsCapable with vendor-default cache",
          "[ak980][settings][issue-57]") {
    auto transport = std::make_unique<tests::MockTransport>();
    transport->open();
    auto device = keyboard::makeProprietaryKeyboardWithTransport(makeDescriptor(), makeId(),
                                                                 std::move(transport));
    REQUIRE(device != nullptr);

    auto* settings = dynamic_cast<core::ISettingsCapable*>(device.get());
    REQUIRE(settings != nullptr);

    auto const initial = settings->keyboardSettings();
    REQUIRE(initial.fnLayerSwitch == 0);
    REQUIRE(initial.sleepTimerMinutes == 0);
    REQUIRE(initial.keyResponseTimeLevel == 3); // vendor default
}

TEST_CASE("AK980 PRO setKeyboardSettings emits the 4-packet envelope",
          "[ak980][settings][issue-57]") {
    auto transport = std::make_unique<tests::MockTransport>();
    auto* observer = transport.get();
    transport->open();
    auto device = keyboard::makeProprietaryKeyboardWithTransport(makeDescriptor(), makeId(),
                                                                 std::move(transport));
    auto* settings = dynamic_cast<core::ISettingsCapable*>(device.get());
    REQUIRE(settings != nullptr);

    core::KeyboardSettings input{};
    input.fnLayerSwitch = 1;
    input.sleepTimerMinutes = 5;
    input.keyResponseTimeLevel = 4;

    REQUIRE(settings->setKeyboardSettings(input));

    auto const& writes = observer->writes();
    REQUIRE(writes.size() == 4);
    REQUIRE(observer->writeFeatureCount() == 4);

    // Packet 1: START control (ReportId=0x04, opcode 0x18, marker 0x01).
    auto const& p1 = writes.at(0);
    REQUIRE(p1.size() == 64);
    REQUIRE(p1[0] == 0x04);
    REQUIRE(p1[1] == 0x18);
    REQUIRE(p1[8] == 0x01);

    // Packet 2: SETTINGS-DATA (opcode 0x07 sub 0x10, fn/sleep/response
    // at bytes 9/10/12, trailer 0xAA 0x55 at 18/19).
    auto const& p2 = writes.at(1);
    REQUIRE(p2.size() == 64);
    REQUIRE(p2[0] == 0x04);
    REQUIRE(p2[1] == 0x07);
    REQUIRE(p2[2] == 0x10);
    REQUIRE(p2[9] == 1);    // fnLayerSwitch
    REQUIRE(p2[10] == 5);   // sleepTimerMinutes
    REQUIRE(p2[12] == 4);   // keyResponseTimeLevel
    REQUIRE(p2[18] == 0xaa);
    REQUIRE(p2[19] == 0x55);

    // Packet 3: SAVE (opcode 0x02).
    auto const& p3 = writes.at(2);
    REQUIRE(p3.size() == 64);
    REQUIRE(p3[0] == 0x04);
    REQUIRE(p3[1] == 0x02);

    // Packet 4: FINISH (opcode 0xF0) - same end-of-envelope sentinel as #58.
    auto const& p4 = writes.at(3);
    REQUIRE(p4.size() == 64);
    REQUIRE(p4[0] == 0x04);
    REQUIRE(p4[1] == 0xf0);

    // Cache reflects what was pushed.
    auto const cached = settings->keyboardSettings();
    REQUIRE(cached.fnLayerSwitch == 1);
    REQUIRE(cached.sleepTimerMinutes == 5);
    REQUIRE(cached.keyResponseTimeLevel == 4);
}

TEST_CASE("AK980 PRO setKeyboardSettings clamps response-time + normalises 0 to default 3",
          "[ak980][settings][issue-57]") {
    auto transport = std::make_unique<tests::MockTransport>();
    auto* observer = transport.get();
    transport->open();
    auto device = keyboard::makeProprietaryKeyboardWithTransport(makeDescriptor(), makeId(),
                                                                 std::move(transport));
    auto* settings = dynamic_cast<core::ISettingsCapable*>(device.get());
    REQUIRE(settings != nullptr);

    // Each setKeyboardSettings call emits 4 packets (START / DATA / SAVE /
    // FINISH). DATA is the second packet of the call, so the offset from
    // the end is -3 (writes[N-3] is DATA, [N-2] is SAVE, [N-1] is FINISH).
    auto const dataPacketOffsetFromEnd = [&observer]() -> std::vector<std::uint8_t> const& {
        REQUIRE(observer->writes().size() >= 3);
        return observer->writes().at(observer->writes().size() - 3);
    };

    // Response 0 - normalises to 3 (vendor default); 99 - clamps to 5.
    core::KeyboardSettings zero{};
    zero.keyResponseTimeLevel = 0;
    REQUIRE(settings->setKeyboardSettings(zero));
    REQUIRE(dataPacketOffsetFromEnd()[12] == 3);
    REQUIRE(settings->keyboardSettings().keyResponseTimeLevel == 3);

    core::KeyboardSettings over{};
    over.keyResponseTimeLevel = 99;
    REQUIRE(settings->setKeyboardSettings(over));
    REQUIRE(dataPacketOffsetFromEnd()[12] == 5);
    REQUIRE(settings->keyboardSettings().keyResponseTimeLevel == 5);
}
