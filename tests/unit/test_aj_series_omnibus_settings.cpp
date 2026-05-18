// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_aj_series_omnibus_settings.cpp
 * @brief Byte-level tests for the AJ-series mouse omnibus settings envelope
 *        (opcode 0x53 FEA_CMD_MOUSE_SET_OPTIONPARAM0, IMouseSettingsCapable).
 *
 * Mirrors test_aj_series_settings.cpp boilerplate: drives the
 * makeAjSeriesWithTransport DI seam with a MockTransport and asserts the
 * exact byte layout per docs/protocols/mouse/aj_series_opcode_table.md
 * sec 3.9.
 *
 * Coverage:
 *   - Capability advertises with vendor-default cache.
 *   - Every documented byte slot lands at its §3.9 offset (LOD at 52,
 *     sensitivity at 50/51, sleep timers at 40..47, battery LED RGB at
 *     54..59, charging switch at 60, flag bits at 12..13).
 *   - Clamping: debounce > 10 -> 10, sensitivity > 100 -> 100, LOD enum
 *     out-of-range -> 2, with the cache normalised post-clamp.
 *   - Flag bits pack as documented (5 named bits across uint16-LE).
 *   - BIT7 checksum independently recomputed and validated.
 *   - Profile + poll-rate context carried into the omnibus packet so the
 *     wire byte reflects the live active-profile + cached poll-rate.
 */
#include "aj_series_protocol.hpp"
#include "ajazz/core/capabilities.hpp"
#include "ajazz/core/device.hpp"
#include "ajazz/mouse/mouse.hpp"
#include "fixtures/mock_transport.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <numeric>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using namespace ajazz;
using namespace ajazz::mouse::aj_series;

namespace {

core::DeviceDescriptor makeDescriptor() {
    core::DeviceDescriptor d{};
    d.vendorId = 0x3151;
    d.productId = 0x5008;
    d.family = core::DeviceFamily::Mouse;
    d.model = "AJAZZ AJ159 APEX (test)";
    d.codename = "aj159_apex";
    d.dpiStageCount = 8;
    return d;
}

core::DeviceId makeId() {
    core::DeviceId id{};
    id.vendorId = 0x3151;
    id.productId = 0x5008;
    id.serial = "TEST-OMNIBUS";
    return id;
}

/// Independent BIT7 checksum verifier - sums pkt[1..63] & 0x7F.
[[nodiscard]] std::uint8_t expectedBit7Checksum(std::vector<std::uint8_t> const& pkt) {
    auto const sum = std::accumulate(pkt.begin() + 1, pkt.end() - 1, std::uint32_t{0});
    return static_cast<std::uint8_t>(sum & 0x7fu);
}

struct Fixture {
    core::DevicePtr device;
    tests::MockTransport* transport;
};

Fixture buildFixture() {
    auto owned = std::make_unique<tests::MockTransport>();
    auto* observer = owned.get();
    owned->open();
    auto dev = mouse::makeAjSeriesWithTransport(makeDescriptor(), makeId(), std::move(owned));
    return Fixture{std::move(dev), observer};
}

} // namespace

// ===========================================================================
// Capability advertisement + vendor-default cache
// ===========================================================================

TEST_CASE("AJ-series exposes IMouseSettingsCapable with vendor-default cache",
          "[aj_series][mouse-settings]") {
    auto fx = buildFixture();
    auto* settings = dynamic_cast<core::IMouseSettingsCapable*>(fx.device.get());
    REQUIRE(settings != nullptr);

    auto const initial = settings->mouseSettings();
    // Vendor defaults per MouseSettings in-class initialisers.
    CHECK(initial.debounceMs == 1);
    CHECK(initial.lightOff == false);
    CHECK(initial.wheelLightOff == false);
    CHECK(initial.motionSmoothing == false);
    CHECK(initial.batteryLedSelect == false);
    CHECK(initial.powerSaveMode == false);
    CHECK(initial.sleepBtIdleSec == 0);
    CHECK(initial.sleepBtDeepSec == 0);
    CHECK(initial.sleep24gIdleSec == 0);
    CHECK(initial.sleep24gDeepSec == 0);
    CHECK(initial.xSensitivity == 100);
    CHECK(initial.ySensitivity == 100);
    CHECK(initial.liftOffDistance == core::LiftOffDistance::Mm1);
    CHECK(initial.angleSnap == false);
    CHECK(initial.batteryLedHigh == core::Rgb{0, 0xff, 0});
    CHECK(initial.batteryLedLow == core::Rgb{0xff, 0, 0});
    CHECK(initial.chargingSwitch == true);
}

// ===========================================================================
// setMouseSettings: complete byte map pin per sec 3.9
// ===========================================================================

TEST_CASE("AJ-series setMouseSettings emits opcode 0x53 with every documented byte slot",
          "[aj_series][mouse-settings][wire]") {
    auto fx = buildFixture();
    auto* settings = dynamic_cast<core::IMouseSettingsCapable*>(fx.device.get());
    REQUIRE(settings != nullptr);

    core::MouseSettings input{};
    input.debounceMs = 4;
    input.lightOff = true;
    input.motionSmoothing = true;
    input.batteryLedSelect = true;
    input.sleepBtIdleSec = 300;
    input.sleepBtDeepSec = 900;
    input.sleep24gIdleSec = 60;
    input.sleep24gDeepSec = 600;
    input.xSensitivity = 75;
    input.ySensitivity = 80;
    input.liftOffDistance = core::LiftOffDistance::Mm2;
    input.angleSnap = true;
    input.batteryLedHigh = core::Rgb{0x11, 0x22, 0x33};
    input.batteryLedLow = core::Rgb{0xaa, 0xbb, 0xcc};
    input.chargingSwitch = false;

    REQUIRE(settings->setMouseSettings(input));
    auto const& writes = fx.transport->writes();
    REQUIRE(writes.size() == 1);
    auto const& pkt = writes.back();
    REQUIRE(pkt.size() == kReportSize);

    // Wire envelope: ReportId + opcode.
    CHECK(pkt[0] == kReportId);                                          // 0x05
    CHECK(pkt[1] == static_cast<std::uint8_t>(FeaCmd::MouseSetOption0)); // 0x53

    // Profile + poll-rate context (vendor bytes 8..9 = our pkt[9..10]).
    CHECK(pkt[9] == 0);     // active profile defaults to 0
    CHECK(pkt[10] == 0x01); // _RateToNum[1000] (default cache)

    // Debounce at vendor byte 10 = pkt[11].
    CHECK(pkt[11] == 4);

    // Flag bits packed into vendor bytes 12..13 = pkt[13..14] (uint16-LE).
    // lightOff (bit 0) + motionSmoothing (bit 2) + batteryLedSelect (bit 3)
    // = 0b00001101 = 0x0d.
    CHECK(pkt[13] == 0x0d);
    CHECK(pkt[14] == 0x00);

    // Undecoded bytes 14..16 = pkt[15..17] — vendor defaults (1/10/10).
    CHECK(pkt[15] == 1);
    CHECK(pkt[16] == 10);
    CHECK(pkt[17] == 10);

    // Sleep block at vendor bytes 40..47 = pkt[41..48], all uint16-LE.
    CHECK(pkt[41] == static_cast<std::uint8_t>(300 & 0xff)); // BT idle lo
    CHECK(pkt[42] == static_cast<std::uint8_t>(300 >> 8));   // BT idle hi
    CHECK(pkt[43] == static_cast<std::uint8_t>(900 & 0xff)); // BT deep lo
    CHECK(pkt[44] == static_cast<std::uint8_t>(900 >> 8));   // BT deep hi
    CHECK(pkt[45] == 60);                                    // 2.4G idle lo
    CHECK(pkt[46] == 0);                                     // 2.4G idle hi
    CHECK(pkt[47] == static_cast<std::uint8_t>(600 & 0xff)); // 2.4G deep lo
    CHECK(pkt[48] == static_cast<std::uint8_t>(600 >> 8));   // 2.4G deep hi

    // Sensitivity at vendor bytes 50..51 = pkt[51..52].
    CHECK(pkt[51] == 75);
    CHECK(pkt[52] == 80);

    // LOD at vendor byte 52 = pkt[53].
    CHECK(pkt[53] == 1); // Mm2

    // Angle-snap at vendor byte 53 = pkt[54].
    CHECK(pkt[54] == 1);

    // Battery LED high at vendor bytes 54..56 = pkt[55..57].
    CHECK(pkt[55] == 0x11);
    CHECK(pkt[56] == 0x22);
    CHECK(pkt[57] == 0x33);

    // Battery LED low at vendor bytes 57..59 = pkt[58..60].
    CHECK(pkt[58] == 0xaa);
    CHECK(pkt[59] == 0xbb);
    CHECK(pkt[60] == 0xcc);

    // Charging switch at vendor byte 60 = pkt[61].
    CHECK(pkt[61] == 0);

    // BIT7 checksum at pkt[64].
    CHECK(pkt[kReportSize - 1] == expectedBit7Checksum(pkt));
}

// ===========================================================================
// Clamping behaviour
// ===========================================================================

TEST_CASE("AJ-series setMouseSettings clamps debounce above 10 to 10",
          "[aj_series][mouse-settings][clamp]") {
    auto fx = buildFixture();
    auto* settings = dynamic_cast<core::IMouseSettingsCapable*>(fx.device.get());
    REQUIRE(settings != nullptr);

    core::MouseSettings in{};
    in.debounceMs = 99;
    REQUIRE(settings->setMouseSettings(in));

    auto const& pkt = fx.transport->writes().back();
    CHECK(pkt[11] == 10); // clamped to vendor max
    CHECK(settings->mouseSettings().debounceMs == 10);
}

TEST_CASE("AJ-series setMouseSettings clamps sensitivity above 100 to 100",
          "[aj_series][mouse-settings][clamp]") {
    auto fx = buildFixture();
    auto* settings = dynamic_cast<core::IMouseSettingsCapable*>(fx.device.get());
    REQUIRE(settings != nullptr);

    core::MouseSettings in{};
    in.xSensitivity = 200;
    in.ySensitivity = 150;
    REQUIRE(settings->setMouseSettings(in));

    auto const& pkt = fx.transport->writes().back();
    CHECK(pkt[51] == 100);
    CHECK(pkt[52] == 100);
    auto const cached = settings->mouseSettings();
    CHECK(cached.xSensitivity == 100);
    CHECK(cached.ySensitivity == 100);
}

TEST_CASE("AJ-series setMouseSettings clamps LOD enum out-of-range to 2",
          "[aj_series][mouse-settings][clamp]") {
    auto fx = buildFixture();
    auto* settings = dynamic_cast<core::IMouseSettingsCapable*>(fx.device.get());
    REQUIRE(settings != nullptr);

    core::MouseSettings in{};
    // Cast a deliberately out-of-range integer through the enum to simulate
    // a buggy QML caller poking past the documented 0..2 range.
    in.liftOffDistance = static_cast<core::LiftOffDistance>(99);
    REQUIRE(settings->setMouseSettings(in));

    auto const& pkt = fx.transport->writes().back();
    CHECK(pkt[53] == 2); // clamped to LOD max (3 mm)
    CHECK(static_cast<std::uint8_t>(settings->mouseSettings().liftOffDistance) == 2);
}

// ===========================================================================
// Flag-bit packing (5 documented bits across uint16-LE bytes 12..13)
// ===========================================================================

TEST_CASE("AJ-series setMouseSettings packs all five named flag bits",
          "[aj_series][mouse-settings][flags]") {
    auto fx = buildFixture();
    auto* settings = dynamic_cast<core::IMouseSettingsCapable*>(fx.device.get());
    REQUIRE(settings != nullptr);

    core::MouseSettings in{};
    in.lightOff = true;
    in.wheelLightOff = true;
    in.motionSmoothing = true;
    in.batteryLedSelect = true;
    in.powerSaveMode = true;
    REQUIRE(settings->setMouseSettings(in));

    auto const& pkt = fx.transport->writes().back();
    // All 5 documented bits set -> 0b00011111 = 0x1f.
    CHECK(pkt[13] == 0x1f);
    CHECK(pkt[14] == 0x00); // high byte unused (bits 5..15 reserved)
}

TEST_CASE("AJ-series setMouseSettings emits zero flags when all bits cleared",
          "[aj_series][mouse-settings][flags]") {
    auto fx = buildFixture();
    auto* settings = dynamic_cast<core::IMouseSettingsCapable*>(fx.device.get());
    REQUIRE(settings != nullptr);

    core::MouseSettings in{}; // all flags default false
    REQUIRE(settings->setMouseSettings(in));

    auto const& pkt = fx.transport->writes().back();
    CHECK(pkt[13] == 0x00);
    CHECK(pkt[14] == 0x00);
}

// ===========================================================================
// Profile + poll-rate context carried into the omnibus packet
// ===========================================================================

TEST_CASE("AJ-series setMouseSettings carries the active profile slot at byte 9",
          "[aj_series][mouse-settings][profile]") {
    auto fx = buildFixture();
    auto* prof = dynamic_cast<core::IProfileSelectCapable*>(fx.device.get());
    auto* settings = dynamic_cast<core::IMouseSettingsCapable*>(fx.device.get());
    REQUIRE(prof != nullptr);
    REQUIRE(settings != nullptr);

    REQUIRE(prof->setActiveOnboardProfile(6));
    core::MouseSettings in{};
    REQUIRE(settings->setMouseSettings(in));

    auto const& writes = fx.transport->writes();
    REQUIRE(writes.size() == 2); // profile push + omnibus
    auto const& pkt = writes.back();
    CHECK(pkt[1] == static_cast<std::uint8_t>(FeaCmd::MouseSetOption0));
    CHECK(pkt[9] == 6); // active profile followed into the omnibus packet
}

TEST_CASE("AJ-series setMouseSettings carries the cached poll-rate at byte 10",
          "[aj_series][mouse-settings][polling]") {
    auto fx = buildFixture();
    auto* rate = dynamic_cast<core::IPollingRateCapable*>(fx.device.get());
    auto* settings = dynamic_cast<core::IMouseSettingsCapable*>(fx.device.get());
    REQUIRE(rate != nullptr);
    REQUIRE(settings != nullptr);

    REQUIRE(rate->setPollingRateHz(4000));
    core::MouseSettings in{};
    REQUIRE(settings->setMouseSettings(in));

    auto const& pkt = fx.transport->writes().back();
    CHECK(pkt[1] == static_cast<std::uint8_t>(FeaCmd::MouseSetOption0));
    CHECK(pkt[10] == 0x82); // _RateToNum[4000] mirrored into omnibus byte 10
}

// ===========================================================================
// BIT7 checksum invariant under arbitrary field values
// ===========================================================================

TEST_CASE("AJ-series setMouseSettings BIT7 checksum stays masked to 7 bits",
          "[aj_series][mouse-settings][checksum]") {
    auto fx = buildFixture();
    auto* settings = dynamic_cast<core::IMouseSettingsCapable*>(fx.device.get());
    REQUIRE(settings != nullptr);

    // Pick values that maximise the raw 8-bit sum so bit 7 is plausibly set.
    core::MouseSettings in{};
    in.batteryLedHigh = core::Rgb{0xff, 0xff, 0xff};
    in.batteryLedLow = core::Rgb{0xff, 0xff, 0xff};
    in.sleepBtIdleSec = 0xffff;
    in.sleepBtDeepSec = 0xffff;
    in.sleep24gIdleSec = 0xffff;
    in.sleep24gDeepSec = 0xffff;
    REQUIRE(settings->setMouseSettings(in));

    auto const& pkt = fx.transport->writes().back();
    std::uint8_t const cs = pkt[kReportSize - 1];
    CHECK((cs & 0x80) == 0); // BIT7 mask must clear the top bit
    CHECK(cs == expectedBit7Checksum(pkt));
}
