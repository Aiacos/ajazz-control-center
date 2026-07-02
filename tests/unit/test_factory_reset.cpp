// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_factory_reset.cpp
 * @brief Byte-level tests for IFactoryResettable on AjSeriesMouse (AJ-series
 *        opcode 0x02 / FEA_CMD_SET_RESERT per
 *        docs/protocols/mouse/aj_series_opcode_table.md sec 3.2). Pins the
 *        no-payload packet shape and BIT7 checksum.
 *
 * Drives the COD-026 DI seam via MockTransport - no real HID hardware required.
 * Test tag `[factory-reset]` gates the suite for targeted runs.
 *
 * NOTE: this file previously also covered IBootLogoCapable on the C++ Akp05Device
 * ("LOG" opcode). That AKP05 wire backend was removed in favour of the mirajazz
 * sidecar (experiment/mirajazz Slice D); the boot-logo wire coverage now lives in
 * the sidecar's cargo tests.
 */
#include "ajazz/core/capabilities.hpp"
#include "ajazz/core/device.hpp"
#include "ajazz/mouse/mouse.hpp"
#include "fixtures/mock_transport.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <numeric>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using namespace ajazz;

// ===========================================================================
// Feature 1 - AJ-series factory reset (opcode 0x02)
// ===========================================================================

namespace {

core::DeviceDescriptor makeMouseDescriptor() {
    core::DeviceDescriptor d{};
    d.vendorId = 0x3151;
    d.productId = 0x5008;
    d.family = core::DeviceFamily::Mouse;
    d.model = "AJAZZ AJ159 APEX (test)";
    d.codename = "aj159_apex";
    d.dpiStageCount = 8;
    return d;
}

core::DeviceId makeMouseId() {
    core::DeviceId id{};
    id.vendorId = 0x3151;
    id.productId = 0x5008;
    id.serial = "TEST-RESET";
    return id;
}

/// Independent BIT7 checksum verifier: sum(pkt[1..63]) & 0x7F per
/// aj_series_opcode_table.md sec 5.
[[nodiscard]] std::uint8_t expectedBit7Checksum(std::vector<std::uint8_t> const& pkt) {
    auto const sum = std::accumulate(pkt.begin() + 1, pkt.end() - 1, std::uint32_t{0});
    return static_cast<std::uint8_t>(sum & 0x7fu);
}

struct MouseFixture {
    core::DevicePtr device;
    tests::MockTransport* transport;
};

MouseFixture buildMouseFixture() {
    auto owned = std::make_unique<tests::MockTransport>();
    auto* observer = owned.get();
    owned->open();
    auto dev =
        mouse::makeAjSeriesWithTransport(makeMouseDescriptor(), makeMouseId(), std::move(owned));
    return MouseFixture{std::move(dev), observer};
}

} // namespace

TEST_CASE("AJ-series factoryReset emits opcode 0x02 with no payload",
          "[factory-reset][aj_series][wire]") {
    auto fx = buildMouseFixture();
    auto* reset = dynamic_cast<core::IFactoryResettable*>(fx.device.get());
    REQUIRE(reset != nullptr);

    REQUIRE(reset->factoryReset());

    auto const& writes = fx.transport->writes();
    REQUIRE(writes.size() == 1);
    auto const& pkt = writes.back();

    // Standard AJ-series envelope: 1-byte HID Report ID + 64-byte body.
    REQUIRE(pkt.size() == 65);

    // pkt[0] = HID Report ID (0x05 per aj_series_opcode_table.md sec 1).
    CHECK(pkt[0] == 0x05);

    // pkt[1] = opcode 0x02 (FEA_CMD_SET_RESERT per sec 3.2).
    CHECK(pkt[1] == 0x02);

    // No-payload command: every byte from pkt[2] through pkt[63] must be 0.
    // (pkt[63] is the last payload slot; pkt[64] is the BIT7 checksum.)
    for (std::size_t i = 2; i < 64; ++i) {
        CAPTURE(i);
        CHECK(pkt[i] == 0x00);
    }

    // BIT7 checksum at pkt[64]: sum(pkt[1..63]) & 0x7F. Since the only
    // non-zero payload byte is the opcode (0x02), the checksum is
    // 0x02 & 0x7F = 0x02. Top bit MUST be clear (BIT7 mask, not BIT8).
    CHECK(pkt[64] == 0x02);
    CHECK((pkt[64] & 0x80u) == 0u);
    CHECK(pkt[64] == expectedBit7Checksum(pkt));
}

TEST_CASE("AJ-series factoryReset returns true on successful transport write",
          "[factory-reset][aj_series]") {
    // Sanity guard: a clean fixture without any throwing transport should
    // always return true. The negative-path coverage lives implicitly in
    // the WARN-log branch which is exercised by the integration suite.
    auto fx = buildMouseFixture();
    auto* reset = dynamic_cast<core::IFactoryResettable*>(fx.device.get());
    REQUIRE(reset != nullptr);
    CHECK(reset->factoryReset() == true);
    CHECK(fx.transport->writes().size() == 1);
}
