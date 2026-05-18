// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_aj_series_settings.cpp
 * @brief End-to-end byte-level tests for the two new AJ-series mouse
 *        capability surfaces: IPollingRateCapable (opcode 0x04, _RateToNum
 *        encoding) and IProfileSelectCapable (opcode 0x05, 8 onboard slots).
 *
 * Wires MockTransport through the existing makeAjSeriesWithTransport DI
 * seam (CAPTURE-04), drives each setter, and asserts the exact byte
 * layout per docs/protocols/mouse/aj_series_opcode_table.md §3.3 + §3.4.
 *
 * Critical coverage:
 *   - All seven supported poll rates (125 / 250 / 500 / 1000 / 2000 /
 *     4000 / 8000 Hz) emit the right _RateToNum wire byte.
 *   - The 0x80-MSB encoding for >=2000 Hz survives the BIT7 checksum
 *     pass (sum + 0x80-set bytes -> mask -> 0x7F-valid checksum).
 *   - Out-of-table Hz values clamp to the nearest supported entry rather
 *     than silently falling back to 1 KHz.
 *   - Profile index clamps to onboardProfileCount() - 1 (= 7) so a
 *     buggy caller cannot poke profile 99 and corrupt firmware state.
 *   - Host-side cache (pollingRateHz() / activeOnboardProfile()) tracks
 *     the last successful write.
 *   - setButtonBinding uses the active profile so the per-button rebind
 *     lands in the correct slot.
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
    id.serial = "TEST-SETTINGS";
    return id;
}

/// Independent BIT7 checksum verifier - sums pkt[1..63] & 0x7F. Mirrors
/// aj_series_protocol.cpp's stampBit7Checksum so the test does not call
/// through to the impl being verified.
[[nodiscard]] std::uint8_t expectedBit7Checksum(std::vector<std::uint8_t> const& pkt) {
    auto const sum = std::accumulate(pkt.begin() + 1, pkt.end() - 1, std::uint32_t{0});
    return static_cast<std::uint8_t>(sum & 0x7fu);
}

/// Build an AjSeriesMouse wired to a fresh MockTransport. Returns the
/// observer pointer alongside the (shared_ptr-owned) device so tests can
/// hold both without the unique_ptr disposal dance the legacy fixture
/// required. The device is held via shared_ptr per ARCH-03; tests just
/// keep the shared_ptr alive for the duration of the TEST_CASE body.
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
// IPollingRateCapable - opcode 0x04 FEA_CMD_SET_REPORT, vendor sec 3.4
// ===========================================================================

TEST_CASE("AJ-series exposes IPollingRateCapable with the seven RateToNum entries",
          "[aj_series][settings][polling]") {
    auto fx = buildFixture();
    auto* rate = dynamic_cast<core::IPollingRateCapable*>(fx.device.get());
    REQUIRE(rate != nullptr);

    auto const supported = rate->supportedPollingRatesHz();
    REQUIRE(supported.size() == 7);
    CHECK(supported[0] == 125);
    CHECK(supported[1] == 250);
    CHECK(supported[2] == 500);
    CHECK(supported[3] == 1000);
    CHECK(supported[4] == 2000);
    CHECK(supported[5] == 4000);
    CHECK(supported[6] == 8000);
}

TEST_CASE("AJ-series default pollingRateHz is 1000 before any push",
          "[aj_series][settings][polling][cache]") {
    auto fx = buildFixture();
    auto* rate = dynamic_cast<core::IPollingRateCapable*>(fx.device.get());
    REQUIRE(rate != nullptr);
    CHECK(rate->pollingRateHz() == 1000);
}

TEST_CASE("AJ-series setPollingRateHz emits opcode 0x04 with profile + RateToNum byte",
          "[aj_series][settings][polling][wire]") {
    auto fx = buildFixture();
    auto* rate = dynamic_cast<core::IPollingRateCapable*>(fx.device.get());
    REQUIRE(rate != nullptr);

    REQUIRE(rate->setPollingRateHz(1000));
    auto const& writes = fx.transport->writes();
    REQUIRE(writes.size() == 1);
    auto const& pkt = writes.back();
    REQUIRE(pkt.size() == kReportSize);

    CHECK(pkt[0] == kReportId);                                    // 0x05
    CHECK(pkt[1] == static_cast<std::uint8_t>(FeaCmd::SetReport)); // 0x04
    CHECK(pkt[2] == 0);                                            // profile 0 default
    CHECK(pkt[3] == 0x01);                                         // _RateToNum[1000]
    for (std::size_t i = 4; i < kReportSize - 1; ++i) {
        CAPTURE(i);
        CHECK(pkt[i] == 0);
    }
    CHECK(pkt[kReportSize - 1] == expectedBit7Checksum(pkt));
    CHECK(rate->pollingRateHz() == 1000);
}

TEST_CASE("AJ-series setPollingRateHz pins RateToNum bytes for all seven rates",
          "[aj_series][settings][polling][wire]") {
    struct Case {
        std::uint16_t hz;
        std::uint8_t wire;
    };
    std::array<Case, 7> const cases{{
        {125, 0x08},
        {250, 0x04},
        {500, 0x02},
        {1000, 0x01},
        {2000, 0x84}, // high-bit-set encoding for >= 2000 Hz
        {4000, 0x82},
        {8000, 0x81},
    }};

    auto fx = buildFixture();
    auto* rate = dynamic_cast<core::IPollingRateCapable*>(fx.device.get());
    REQUIRE(rate != nullptr);

    for (auto const& c : cases) {
        CAPTURE(c.hz);
        REQUIRE(rate->setPollingRateHz(c.hz));
        auto const& pkt = fx.transport->writes().back();
        REQUIRE(pkt.size() == kReportSize);
        CHECK(pkt[1] == static_cast<std::uint8_t>(FeaCmd::SetReport));
        CHECK(pkt[3] == c.wire);
        CHECK(rate->pollingRateHz() == c.hz);
    }

    // 7 successful writes, one per rate.
    CHECK(fx.transport->writes().size() == cases.size());
}

TEST_CASE("AJ-series setPollingRateHz preserves BIT7 checksum despite 0x80-MSB rates",
          "[aj_series][settings][polling][checksum]") {
    // The high-bit-set wire encoding (0x84/0x82/0x81 for 2/4/8 KHz) means a
    // naive 8-bit sum checksum would have bit 7 set. BIT7 masking (& 0x7F)
    // must clear it before transmission per opcode-table sec 5.
    auto fx = buildFixture();
    auto* rate = dynamic_cast<core::IPollingRateCapable*>(fx.device.get());
    REQUIRE(rate != nullptr);

    REQUIRE(rate->setPollingRateHz(8000));
    auto const& pkt = fx.transport->writes().back();
    // Independent BIT7 checksum recomputation.
    std::uint8_t const cs = pkt[kReportSize - 1];
    CHECK((cs & 0x80) == 0); // top bit always clear under BIT7
    CHECK(cs == expectedBit7Checksum(pkt));
    // Raw 8-bit sum has bit 7 set (opcode 0x04 + rate 0x81 = 0x85, bit 7 set).
    std::uint32_t raw = 0;
    for (std::size_t i = 1; i < kReportSize - 1; ++i) {
        raw += pkt[i];
    }
    CHECK((raw & 0x80u) != 0u);
}

TEST_CASE("AJ-series setPollingRateHz clamps out-of-table values to nearest supported",
          "[aj_series][settings][polling][clamp]") {
    auto fx = buildFixture();
    auto* rate = dynamic_cast<core::IPollingRateCapable*>(fx.device.get());
    REQUIRE(rate != nullptr);

    // 333 Hz is between 250 (delta 83) and 500 (delta 167) -> clamps to 250.
    REQUIRE(rate->setPollingRateHz(333));
    auto const& pkt250 = fx.transport->writes().back();
    CHECK(pkt250[3] == 0x04); // _RateToNum[250]
    CHECK(rate->pollingRateHz() == 250);

    // 7000 Hz is between 4000 (delta 3000) and 8000 (delta 1000) -> 8000.
    REQUIRE(rate->setPollingRateHz(7000));
    auto const& pkt8k = fx.transport->writes().back();
    CHECK(pkt8k[3] == 0x81); // _RateToNum[8000]
    CHECK(rate->pollingRateHz() == 8000);

    // 0 Hz is closest to 125 (the lowest supported entry).
    REQUIRE(rate->setPollingRateHz(0));
    auto const& pkt125 = fx.transport->writes().back();
    CHECK(pkt125[3] == 0x08); // _RateToNum[125]
    CHECK(rate->pollingRateHz() == 125);
}

TEST_CASE("AJ-series legacy IMouseCapable setPollRateHz delegates to clamped path",
          "[aj_series][settings][polling][legacy]") {
    // The legacy IMouseCapable convenience must share the cache + clamp
    // semantics with the canonical IPollingRateCapable path so QML pickers
    // built against either surface produce identical wire behaviour.
    auto fx = buildFixture();
    auto* mouse = dynamic_cast<core::IMouseCapable*>(fx.device.get());
    auto* rate = dynamic_cast<core::IPollingRateCapable*>(fx.device.get());
    REQUIRE(mouse != nullptr);
    REQUIRE(rate != nullptr);

    mouse->setPollRateHz(4000);
    auto const& pkt = fx.transport->writes().back();
    CHECK(pkt[1] == static_cast<std::uint8_t>(FeaCmd::SetReport));
    CHECK(pkt[3] == 0x82); // _RateToNum[4000]
    // Both getters report the new cached value.
    CHECK(mouse->pollRateHz() == 4000);
    CHECK(rate->pollingRateHz() == 4000);
}

// ===========================================================================
// IProfileSelectCapable - opcode 0x05 FEA_CMD_SET_PROFILE, vendor sec 3.3
// ===========================================================================

TEST_CASE("AJ-series exposes IProfileSelectCapable with 8 onboard slots",
          "[aj_series][settings][profile]") {
    auto fx = buildFixture();
    auto* prof = dynamic_cast<core::IProfileSelectCapable*>(fx.device.get());
    REQUIRE(prof != nullptr);
    CHECK(prof->onboardProfileCount() == 8);
    CHECK(prof->activeOnboardProfile() == 0); // default slot
}

TEST_CASE("AJ-series setActiveOnboardProfile emits opcode 0x05 with slot at byte 2",
          "[aj_series][settings][profile][wire]") {
    auto fx = buildFixture();
    auto* prof = dynamic_cast<core::IProfileSelectCapable*>(fx.device.get());
    REQUIRE(prof != nullptr);

    REQUIRE(prof->setActiveOnboardProfile(3));
    auto const& writes = fx.transport->writes();
    REQUIRE(writes.size() == 1);
    auto const& pkt = writes.back();
    REQUIRE(pkt.size() == kReportSize);

    CHECK(pkt[0] == kReportId);                                    // 0x05
    CHECK(pkt[1] == static_cast<std::uint8_t>(FeaCmd::SetProfile)); // 0x05
    CHECK(pkt[2] == 3);                                            // requested slot
    for (std::size_t i = 3; i < kReportSize - 1; ++i) {
        CAPTURE(i);
        CHECK(pkt[i] == 0);
    }
    CHECK(pkt[kReportSize - 1] == expectedBit7Checksum(pkt));
    CHECK(prof->activeOnboardProfile() == 3);
}

TEST_CASE("AJ-series setActiveOnboardProfile pins each of the 8 slot indices",
          "[aj_series][settings][profile][wire]") {
    auto fx = buildFixture();
    auto* prof = dynamic_cast<core::IProfileSelectCapable*>(fx.device.get());
    REQUIRE(prof != nullptr);

    for (std::uint8_t i = 0; i < 8; ++i) {
        CAPTURE(i);
        REQUIRE(prof->setActiveOnboardProfile(i));
        auto const& pkt = fx.transport->writes().back();
        CHECK(pkt[1] == static_cast<std::uint8_t>(FeaCmd::SetProfile));
        CHECK(pkt[2] == i);
        CHECK(prof->activeOnboardProfile() == i);
    }
    CHECK(fx.transport->writes().size() == 8u);
}

TEST_CASE("AJ-series setActiveOnboardProfile clamps out-of-range slot to 7",
          "[aj_series][settings][profile][clamp]") {
    auto fx = buildFixture();
    auto* prof = dynamic_cast<core::IProfileSelectCapable*>(fx.device.get());
    REQUIRE(prof != nullptr);

    REQUIRE(prof->setActiveOnboardProfile(99));
    auto const& pkt = fx.transport->writes().back();
    CHECK(pkt[1] == static_cast<std::uint8_t>(FeaCmd::SetProfile));
    CHECK(pkt[2] == 7); // clamped from 99
    CHECK(prof->activeOnboardProfile() == 7);
}

TEST_CASE("AJ-series setButtonBinding follows the active onboard profile",
          "[aj_series][settings][profile][keymap]") {
    // After switching profiles, per-button rebinds must land in the new
    // slot so the QML profile picker and per-button editor stay coherent.
    auto fx = buildFixture();
    auto* prof = dynamic_cast<core::IProfileSelectCapable*>(fx.device.get());
    auto* mouse = dynamic_cast<core::IMouseCapable*>(fx.device.get());
    REQUIRE(prof != nullptr);
    REQUIRE(mouse != nullptr);

    REQUIRE(prof->setActiveOnboardProfile(5));
    mouse->setButtonBinding(/*button=*/2, /*action=*/0x01020304u);
    auto const& writes = fx.transport->writes();
    REQUIRE(writes.size() == 2); // profile + rebind

    auto const& rebind = writes.back();
    CHECK(rebind[1] == static_cast<std::uint8_t>(FeaCmd::MouseSetKeyMatrix)); // 0x50
    CHECK(rebind[2] == 5); // profile slot followed
    CHECK(rebind[3] == 2); // button index
    // Action big-endian at pkt[9..12] per vendor sec 3.6.
    CHECK(rebind[9] == 0x01);
    CHECK(rebind[10] == 0x02);
    CHECK(rebind[11] == 0x03);
    CHECK(rebind[12] == 0x04);
}

TEST_CASE("AJ-series setPollingRateHz follows the active onboard profile",
          "[aj_series][settings][profile][polling]") {
    // pkt[2] of opcode 0x04 carries the profile index per vendor sec 3.4
    // (the byte the rate code lands in is pkt[3]). After switching profile,
    // poll-rate writes must address the new slot.
    auto fx = buildFixture();
    auto* prof = dynamic_cast<core::IProfileSelectCapable*>(fx.device.get());
    auto* rate = dynamic_cast<core::IPollingRateCapable*>(fx.device.get());
    REQUIRE(prof != nullptr);
    REQUIRE(rate != nullptr);

    REQUIRE(prof->setActiveOnboardProfile(6));
    REQUIRE(rate->setPollingRateHz(2000));
    auto const& pkt = fx.transport->writes().back();
    CHECK(pkt[1] == static_cast<std::uint8_t>(FeaCmd::SetReport));
    CHECK(pkt[2] == 6);    // profile slot
    CHECK(pkt[3] == 0x84); // _RateToNum[2000]
}
