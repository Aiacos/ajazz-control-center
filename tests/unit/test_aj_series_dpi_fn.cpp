// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_aj_series_dpi_fn.cpp
 * @brief Byte-level tests for the two new AJ-series mouse capability
 *        surfaces: IDpiTableCapable (opcode 0x54, per-profile DPI table)
 *        and IMouseFnRemappable (opcode 0x51, Fn-layer key rebind).
 *
 * Wires MockTransport through the existing makeAjSeriesWithTransport DI
 * seam (CAPTURE-04), drives each setter, and asserts the exact byte
 * layout per docs/protocols/mouse/aj_series_opcode_table.md sec 3.8 + 3.10.
 *
 * Critical coverage:
 *   - Builder buildDpiTable lays out profile/active/count at vendor bytes
 *     1/2/3, 8 x uint16-LE DPI values at vendor bytes 8..23, and 8 x
 *     {R,G,B} indicator colours at vendor bytes 40..63.
 *   - 8th-stage B-channel collides with the BIT7 checksum slot (sec 3.10
 *     line 452 edge case) - regression guard that we replicate the vendor
 *     bug bit-for-bit so firmware behaviour stays identical.
 *   - Out-of-range scalars clamp: profile > 7 -> 7, activeStage > 7 -> 7,
 *     stageCount > 8 -> 8. Cache normalisation mirrors the wire.
 *   - buildFnLayerRemap emits opcode 0x51 with fnLayer at vendor byte 1,
 *     button at vendor byte 2, action big-endian at vendor bytes 8..11.
 *   - Fn-layer index > 7 clamps to 7 (matches profile-slot semantics on
 *     the parallel 0x50 envelope).
 *   - Capability advertisement: fnLayerCount/fnButtonCount return the
 *     AJ159 APEX defaults (1 / 16).
 *   - BIT7 checksum independently recomputed and validated on every
 *     emitted packet.
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
    id.serial = "TEST-DPI-FN";
    return id;
}

/// Independent BIT7 checksum verifier - sums pkt[1..63] & 0x7F. Mirrors
/// aj_series_protocol.cpp's stampBit7Checksum so the test does not call
/// through to the impl being verified.
[[nodiscard]] std::uint8_t expectedBit7Checksum(std::vector<std::uint8_t> const& pkt) {
    auto const sum = std::accumulate(pkt.begin() + 1, pkt.end() - 1, std::uint32_t{0});
    return static_cast<std::uint8_t>(sum & 0x7fu);
}

[[nodiscard]] std::uint8_t expectedBit7Checksum(std::array<std::uint8_t, kReportSize> const& pkt) {
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

/// Construct a deterministic DpiTable used by several byte-map tests:
/// stages = {400, 800, 1600, 3200, 6400, 12800, 25600, 42000}, colours
/// = {red, green, blue, yellow, magenta, cyan, white, grey}.
core::DpiTable makeFilledTable(std::uint8_t profile, std::uint8_t active, std::uint8_t count) {
    core::DpiTable t{};
    t.profile = profile;
    t.activeStage = active;
    t.stageCount = count;
    std::array<std::uint16_t, 8> const dpis{400, 800, 1600, 3200, 6400, 12800, 25600, 42000};
    std::array<core::Rgb, 8> const cols{{
        {0xff, 0x00, 0x00}, // red
        {0x00, 0xff, 0x00}, // green
        {0x00, 0x00, 0xff}, // blue
        {0xff, 0xff, 0x00}, // yellow
        {0xff, 0x00, 0xff}, // magenta
        {0x00, 0xff, 0xff}, // cyan
        {0xff, 0xff, 0xff}, // white
        {0x80, 0x80, 0x80}, // grey
    }};
    for (std::size_t i = 0; i < 8; ++i) {
        t.stages[i].dpi = dpis[i];
        t.stages[i].indicator = cols[i];
    }
    return t;
}

} // namespace

// ===========================================================================
// IDpiTableCapable - opcode 0x54 FEA_CMD_MOUSE_SET_OPTIONPARAM1, vendor sec 3.10
// ===========================================================================

TEST_CASE("AJ-series exposes IDpiTableCapable with vendor-default cache",
          "[aj_series][dpi-table]") {
    auto fx = buildFixture();
    auto* dpi = dynamic_cast<core::IDpiTableCapable*>(fx.device.get());
    REQUIRE(dpi != nullptr);

    auto const cached = dpi->dpiTable();
    CHECK(cached.profile == 0);
    CHECK(cached.activeStage == 0);
    CHECK(cached.stageCount == 8);
    // All 8 stages default to zero before any push lands.
    for (std::size_t i = 0; i < cached.stages.size(); ++i) {
        CAPTURE(i);
        CHECK(cached.stages[i].dpi == 0);
        CHECK(cached.stages[i].indicator.r == 0);
        CHECK(cached.stages[i].indicator.g == 0);
        CHECK(cached.stages[i].indicator.b == 0);
    }
}

TEST_CASE("AJ-series buildDpiTable pins profile/active/count at vendor bytes 1/2/3",
          "[aj_series][dpi-table][wire]") {
    // Per sec 3.10 lines 442..445: pkt[2]=profile, pkt[3]=activeStage,
    // pkt[4]=stageCount. Distinct from buildMouseSetOption1 which omits
    // the profile byte (leaving it implicit at 0).
    auto const table = makeFilledTable(/*profile=*/3, /*active=*/5, /*count=*/7);
    auto const pkt = buildDpiTable(table);
    REQUIRE(pkt[0] == kReportId);
    REQUIRE(pkt[1] == static_cast<std::uint8_t>(FeaCmd::MouseSetOption1)); // 0x54
    CHECK(pkt[2] == 3);                                                    // profile
    CHECK(pkt[3] == 5);                                                    // active stage
    CHECK(pkt[4] == 7);                                                    // stage count
    // bytes 5..8 reserved zero (vendor "bytes 4..7").
    for (std::size_t i = 5; i < 9; ++i) {
        CAPTURE(i);
        CHECK(pkt[i] == 0);
    }
}

TEST_CASE("AJ-series buildDpiTable lays out 8 uint16-LE DPI values at vendor bytes 8..23",
          "[aj_series][dpi-table][wire]") {
    // Per sec 3.10 line 446: 8 x uint16-LE DPI values at vendor bytes 8..23
    // = our pkt[9..24].
    auto const table = makeFilledTable(0, 0, 8);
    auto const pkt = buildDpiTable(table);
    std::array<std::uint16_t, 8> const expected{400, 800, 1600, 3200, 6400, 12800, 25600, 42000};
    for (std::size_t i = 0; i < expected.size(); ++i) {
        CAPTURE(i);
        std::size_t const lo = 9 + i * 2;
        std::size_t const hi = lo + 1;
        CHECK(pkt[lo] == static_cast<std::uint8_t>(expected[i] & 0xffu));
        CHECK(pkt[hi] == static_cast<std::uint8_t>((expected[i] >> 8) & 0xffu));
    }
    // Specific spot checks: 400=0x0190 LE -> 0x90 0x01 at pkt[9..10],
    // 42000=0xA410 LE -> 0x10 0xA4 at pkt[23..24].
    CHECK(pkt[9] == 0x90);
    CHECK(pkt[10] == 0x01);
    CHECK(pkt[23] == 0x10);
    CHECK(pkt[24] == 0xa4);
    // Reserved bytes 25..40 (vendor "bytes 24..39") stay zero.
    for (std::size_t i = 25; i < 41; ++i) {
        CAPTURE(i);
        CHECK(pkt[i] == 0);
    }
}

TEST_CASE("AJ-series buildDpiTable lays out 8 RGB indicators at vendor bytes 40..63",
          "[aj_series][dpi-table][wire]") {
    // Per sec 3.10 line 450: 8 x {R,G,B} at vendor bytes 40..63 = pkt[41..64].
    auto const table = makeFilledTable(0, 0, 8);
    auto const pkt = buildDpiTable(table);
    // Stage 0 = red at pkt[41..43].
    CHECK(pkt[41] == 0xff);
    CHECK(pkt[42] == 0x00);
    CHECK(pkt[43] == 0x00);
    // Stage 1 = green at pkt[44..46].
    CHECK(pkt[44] == 0x00);
    CHECK(pkt[45] == 0xff);
    CHECK(pkt[46] == 0x00);
    // Stage 2 = blue at pkt[47..49].
    CHECK(pkt[47] == 0x00);
    CHECK(pkt[48] == 0x00);
    CHECK(pkt[49] == 0xff);
    // Stage 6 = white at pkt[59..61].
    CHECK(pkt[59] == 0xff);
    CHECK(pkt[60] == 0xff);
    CHECK(pkt[61] == 0xff);
    // Stage 7 R/G at pkt[62..63] survive intact (0x80, 0x80).
    CHECK(pkt[62] == 0x80);
    CHECK(pkt[63] == 0x80);
}

TEST_CASE("AJ-series buildDpiTable 8th-stage B-channel collides with checksum slot",
          "[aj_series][dpi-table][wire]") {
    // Regression guard for the sec 3.10 line 452 edge case: pkt[64] is BOTH
    // stage-7 B-channel (per colour-table layout) AND the BIT7 checksum slot.
    // stampBit7Checksum() overwrites - this test confirms the vendor bug is
    // replicated bit-for-bit so firmware behaviour matches the vendor utility.
    core::DpiTable table{};
    table.profile = 0;
    table.activeStage = 0;
    table.stageCount = 8;
    // Try to set every stage to a uniform value so stage 7 stands out.
    for (auto& s : table.stages) {
        s.dpi = 1600;
        s.indicator = {0xab, 0xcd, 0xef};
    }
    auto const pkt = buildDpiTable(table);
    // Stage 7 R/G at pkt[62..63] are intact.
    CHECK(pkt[62] == 0xab);
    CHECK(pkt[63] == 0xcd);
    // Stage 7 B at pkt[64] = checksum slot, NOT 0xEF.
    CHECK(pkt[kReportSize - 1] != 0xef);
    CHECK(pkt[kReportSize - 1] == expectedBit7Checksum(pkt));
}

TEST_CASE("AJ-series buildDpiTable clamps profile > 7 to 7", "[aj_series][dpi-table][clamp]") {
    auto table = makeFilledTable(/*profile=*/99, 0, 8);
    auto const pkt = buildDpiTable(table);
    CHECK(pkt[2] == 7); // clamped from 99
}

TEST_CASE("AJ-series buildDpiTable clamps activeStage > 7 to 7", "[aj_series][dpi-table][clamp]") {
    auto table = makeFilledTable(0, /*active=*/99, 8);
    auto const pkt = buildDpiTable(table);
    CHECK(pkt[3] == 7); // clamped from 99
}

TEST_CASE("AJ-series buildDpiTable clamps stageCount > 8 to 8", "[aj_series][dpi-table][clamp]") {
    auto table = makeFilledTable(0, 0, /*count=*/99);
    auto const pkt = buildDpiTable(table);
    CHECK(pkt[4] == 8); // clamped from 99
}

TEST_CASE("AJ-series buildDpiTable stamps BIT7 checksum at pkt[64]",
          "[aj_series][dpi-table][checksum]") {
    auto const table = makeFilledTable(2, 3, 6);
    auto const pkt = buildDpiTable(table);
    CHECK((pkt[kReportSize - 1] & 0x80) == 0); // top bit always clear under BIT7
    CHECK(pkt[kReportSize - 1] == expectedBit7Checksum(pkt));
}

TEST_CASE("AJ-series setDpiTable emits opcode 0x54 through the transport and caches",
          "[aj_series][dpi-table][cache]") {
    auto fx = buildFixture();
    auto* dpi = dynamic_cast<core::IDpiTableCapable*>(fx.device.get());
    REQUIRE(dpi != nullptr);

    auto const table = makeFilledTable(/*profile=*/4, /*active=*/2, /*count=*/5);
    REQUIRE(dpi->setDpiTable(table));
    auto const& writes = fx.transport->writes();
    REQUIRE(writes.size() == 1);
    auto const& pkt = writes.back();
    REQUIRE(pkt.size() == kReportSize);

    CHECK(pkt[0] == kReportId);
    CHECK(pkt[1] == static_cast<std::uint8_t>(FeaCmd::MouseSetOption1)); // 0x54
    CHECK(pkt[2] == 4);                                                  // profile
    CHECK(pkt[3] == 2);                                                  // active stage
    CHECK(pkt[4] == 5);                                                  // stage count
    CHECK(pkt[kReportSize - 1] == expectedBit7Checksum(pkt));

    // Cache mirrors the wire after clamping.
    auto const cached = dpi->dpiTable();
    CHECK(cached.profile == 4);
    CHECK(cached.activeStage == 2);
    CHECK(cached.stageCount == 5);
    CHECK(cached.stages[0].dpi == 400);
    CHECK(cached.stages[7].dpi == 42000);
}

TEST_CASE("AJ-series setDpiTable normalises out-of-range scalars in the cache",
          "[aj_series][dpi-table][cache][clamp]") {
    // Caller passes profile=99, activeStage=99, stageCount=99 - the wire
    // bytes clamp via the builder, and the cache must reflect the clamped
    // values too so subsequent reads do not see the garbage input.
    auto fx = buildFixture();
    auto* dpi = dynamic_cast<core::IDpiTableCapable*>(fx.device.get());
    REQUIRE(dpi != nullptr);

    auto table = makeFilledTable(99, 99, 99);
    REQUIRE(dpi->setDpiTable(table));
    auto const cached = dpi->dpiTable();
    CHECK(cached.profile == 7);
    CHECK(cached.activeStage == 7);
    CHECK(cached.stageCount == 8);
}

// ===========================================================================
// IMouseFnRemappable - opcode 0x51 FEA_CMD_MOUSE_SET_FNMATRIX, vendor sec 3.8
// ===========================================================================

TEST_CASE("AJ-series exposes IMouseFnRemappable with vendor-default layer/button counts",
          "[aj_series][fn-layer]") {
    auto fx = buildFixture();
    auto* fn = dynamic_cast<core::IMouseFnRemappable*>(fx.device.get());
    REQUIRE(fn != nullptr);
    // AJ159 APEX: single Fn-layer, 16-button max per sec 3.7 key-matrix.
    CHECK(fn->fnLayerCount() == 1);
    CHECK(fn->fnButtonCount() == 16);
}

TEST_CASE("AJ-series buildFnLayerRemap emits opcode 0x51 with fnLayer/button at bytes 1/2",
          "[aj_series][fn-layer][wire]") {
    // Per sec 3.8 lines 366..376: opcode 0x51, vendor byte 1 = Fn-layer
    // index, vendor byte 2 = button index, vendor bytes 8..11 = action
    // big-endian. Our pkt[N+1] convention shifts every position by 1.
    constexpr std::uint32_t kAction = 0xDEADBEEFu;
    auto const pkt = buildFnLayerRemap(/*fnLayer=*/1, /*button=*/4, kAction);
    REQUIRE(pkt[0] == kReportId);
    REQUIRE(pkt[1] == static_cast<std::uint8_t>(FeaCmd::MouseSetFnMatrix)); // 0x51
    CHECK(pkt[2] == 1);                                                     // Fn-layer index
    CHECK(pkt[3] == 4);                                                     // button index
    // Bytes 4..8 must be zero (vendor "bytes 3..7" reserved).
    for (std::size_t i = 4; i < 9; ++i) {
        CAPTURE(i);
        CHECK(pkt[i] == 0);
    }
    // Action big-endian at pkt[9..12] per sec 3.6.
    CHECK(pkt[9] == 0xde);
    CHECK(pkt[10] == 0xad);
    CHECK(pkt[11] == 0xbe);
    CHECK(pkt[12] == 0xef);
    // Tail zero up to checksum.
    for (std::size_t i = 13; i < kReportSize - 1; ++i) {
        CAPTURE(i);
        CHECK(pkt[i] == 0);
    }
    CHECK(pkt[kReportSize - 1] == expectedBit7Checksum(pkt));
}

TEST_CASE("AJ-series buildFnLayerRemap clamps fnLayer > 7 to 7", "[aj_series][fn-layer][clamp]") {
    auto const pkt = buildFnLayerRemap(/*fnLayer=*/99, /*button=*/0, /*action=*/0u);
    CHECK(pkt[2] == 7); // clamped from 99
}

TEST_CASE("AJ-series buildFnLayerRemap pins each documented action type byte",
          "[aj_series][fn-layer][wire]") {
    // Per sec 3.6 action-byte table: type 0 = combo, type 1 = mouse-button,
    // type 9 = macro, etc. Test that arbitrary type bytes survive the
    // builder unmolested.
    struct Case {
        std::uint32_t action;
        std::array<std::uint8_t, 4> wire;
    };
    std::array<Case, 4> const cases{{
        {0x00112233u, {0x00, 0x11, 0x22, 0x33}}, // type 0 = combo
        {0x01000400u, {0x01, 0x00, 0x04, 0x00}}, // type 1 = mouse-button BTN_FORWARD
        {0x09010203u, {0x09, 0x01, 0x02, 0x03}}, // type 9 = macro slot
        {0xffffffffu, {0xff, 0xff, 0xff, 0xff}}, // boundary all-ones
    }};
    for (auto const& c : cases) {
        CAPTURE(c.action);
        auto const pkt = buildFnLayerRemap(/*fnLayer=*/0, /*button=*/0, c.action);
        CHECK(pkt[9] == c.wire[0]);
        CHECK(pkt[10] == c.wire[1]);
        CHECK(pkt[11] == c.wire[2]);
        CHECK(pkt[12] == c.wire[3]);
    }
}

TEST_CASE("AJ-series setFnLayerBinding emits opcode 0x51 through the transport",
          "[aj_series][fn-layer][wire]") {
    auto fx = buildFixture();
    auto* fn = dynamic_cast<core::IMouseFnRemappable*>(fx.device.get());
    REQUIRE(fn != nullptr);

    REQUIRE(fn->setFnLayerBinding(/*fnLayer=*/0, /*button=*/3, /*action=*/0x01020304u));
    auto const& writes = fx.transport->writes();
    REQUIRE(writes.size() == 1);
    auto const& pkt = writes.back();
    REQUIRE(pkt.size() == kReportSize);

    CHECK(pkt[0] == kReportId);
    CHECK(pkt[1] == static_cast<std::uint8_t>(FeaCmd::MouseSetFnMatrix)); // 0x51
    CHECK(pkt[2] == 0);                                                   // Fn-layer
    CHECK(pkt[3] == 3);                                                   // button
    CHECK(pkt[9] == 0x01);
    CHECK(pkt[10] == 0x02);
    CHECK(pkt[11] == 0x03);
    CHECK(pkt[12] == 0x04);
    CHECK(pkt[kReportSize - 1] == expectedBit7Checksum(pkt));
}

TEST_CASE("AJ-series setFnLayerBinding writes are independent of primary 0x50 path",
          "[aj_series][fn-layer][safety]") {
    // Regression guard: the Fn-layer rebind must NOT alter the primary key
    // matrix - they live on different opcodes (0x51 vs 0x50) and the
    // firmware tracks them in separate slots per sec 3.6 / 3.8.
    auto fx = buildFixture();
    auto* fn = dynamic_cast<core::IMouseFnRemappable*>(fx.device.get());
    auto* mouse = dynamic_cast<core::IMouseCapable*>(fx.device.get());
    REQUIRE(fn != nullptr);
    REQUIRE(mouse != nullptr);

    // Push primary rebind first (opcode 0x50).
    mouse->setButtonBinding(/*button=*/2, /*action=*/0x10203040u);
    // Then Fn-layer rebind (opcode 0x51) for the same button.
    REQUIRE(fn->setFnLayerBinding(/*fnLayer=*/0, /*button=*/2, /*action=*/0x50607080u));

    auto const& writes = fx.transport->writes();
    REQUIRE(writes.size() == 2);
    CHECK(writes[0][1] == static_cast<std::uint8_t>(FeaCmd::MouseSetKeyMatrix)); // 0x50
    CHECK(writes[0][9] == 0x10); // primary action top byte
    CHECK(writes[1][1] == static_cast<std::uint8_t>(FeaCmd::MouseSetFnMatrix)); // 0x51
    CHECK(writes[1][9] == 0x50); // Fn-layer action top byte
}
