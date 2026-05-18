// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_aj_series_keymatrix_readback.cpp
 * @brief Byte-level tests for the AJ-series mouse key-matrix read-back
 *        capability (IMouseKeyMatrixReadable, opcode 0xD0 /
 *        FEA_CMD_MOUSE_GET_KEYMATRIX).
 *
 * Wires MockTransport through the existing makeAjSeriesWithTransport DI
 * seam (CAPTURE-04), drives the request builder + response parser in
 * isolation, and round-trips the full readKeyMatrix() via the queued-read
 * channel (MockTransport::enqueueRead) to exercise the AjSeriesMouse
 * orchestration end-to-end.
 *
 * Critical coverage:
 *   - Builder buildKeyMatrixRequest emits opcode 0xD0 at pkt[1], profile
 *     at pkt[2], everything else zero up to pkt[63], BIT7 checksum at
 *     pkt[64] per sec 3.7 lines 357..361.
 *   - Builder clamps profile > 7 to 7 (same envelope semantics as 0x05).
 *   - Parser accepts the bare 64-byte response shape AND the 65-byte
 *     envelope with a leading HID Report ID prefix (libhidapi pattern).
 *   - Parser rejects too-short responses (returns nullopt rather than
 *     silently truncating).
 *   - Parser rejects oversize responses (>65 bytes is a wire-shape
 *     mismatch worth surfacing).
 *   - Parser is wire-byte transparent: every action record's 4 bytes
 *     survive the round-trip verbatim.
 *   - Full round-trip via AjSeriesMouse::readKeyMatrix() exercises the
 *     write → read → parse orchestration: queues a synthetic response
 *     with MockTransport::enqueueRead, asserts the captured request
 *     packet shape, and asserts the decoded MouseKeyMatrix.
 *   - Round-trip failure mode: empty read queue surfaces as nullopt
 *     (transport returns 0 bytes = timeout, parser sees empty span).
 *   - BIT7 checksum independently recomputed on the request packet.
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
#include <optional>
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
    id.serial = "TEST-KEYMATRIX";
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

/// Build a deterministic synthetic 64-byte response payload: each slot's
/// 4-byte record is `{slot, slot+1, slot+2, slot+3}` so the parser's
/// per-slot indexing is easy to verify visually. Stays well under 0x80 so
/// no byte aliases the BIT7 checksum sentinel by accident.
std::vector<std::uint8_t> makeSyntheticResponseBody() {
    std::vector<std::uint8_t> body(kKeyMatrixResponseBytes, 0u);
    for (std::size_t slot = 0; slot < kKeyMatrixSlotCount; ++slot) {
        std::size_t const base = slot * kMouseActionBytes;
        body[base + 0] = static_cast<std::uint8_t>(slot);
        body[base + 1] = static_cast<std::uint8_t>(slot + 1);
        body[base + 2] = static_cast<std::uint8_t>(slot + 2);
        body[base + 3] = static_cast<std::uint8_t>(slot + 3);
    }
    return body;
}

} // namespace

// ===========================================================================
// IMouseKeyMatrixReadable capability surface
// ===========================================================================

TEST_CASE("AJ-series exposes IMouseKeyMatrixReadable",
          "[aj_series][key-matrix]") {
    auto fx = buildFixture();
    auto* km = dynamic_cast<core::IMouseKeyMatrixReadable*>(fx.device.get());
    REQUIRE(km != nullptr);
}

// ===========================================================================
// buildKeyMatrixRequest - opcode 0xD0 request packet shape (sec 3.7)
// ===========================================================================

TEST_CASE("AJ-series buildKeyMatrixRequest emits opcode 0xD0 at pkt[1]",
          "[aj_series][key-matrix][wire]") {
    // Per sec 3.7 lines 357..361: pkt[1]=0xD0, pkt[2]=profile, every other
    // body byte zero, BIT7 checksum at pkt[64].
    auto const pkt = buildKeyMatrixRequest(/*profile=*/2);
    REQUIRE(pkt[0] == kReportId);
    REQUIRE(pkt[1] == static_cast<std::uint8_t>(FeaCmd::MouseGetKeyMatrix)); // 0xD0
    CHECK(pkt[2] == 2); // profile idx at vendor byte 1
    // Every other body byte zero (vendor "bytes 2..62" = our pkt[3..63]).
    for (std::size_t i = 3; i < kReportSize - 1; ++i) {
        CAPTURE(i);
        CHECK(pkt[i] == 0);
    }
    // BIT7 checksum at pkt[64] = vendor byte 63.
    CHECK(pkt[kReportSize - 1] == expectedBit7Checksum(pkt));
    // Top bit always clear under BIT7 masking.
    CHECK((pkt[kReportSize - 1] & 0x80) == 0);
}

TEST_CASE("AJ-series buildKeyMatrixRequest clamps profile > 7 to 7",
          "[aj_series][key-matrix][clamp]") {
    // Same 8-slot ceiling as the profile picker (opcode 0x05, sec 3.3).
    auto const pkt = buildKeyMatrixRequest(/*profile=*/99);
    CHECK(pkt[2] == 7);
}

TEST_CASE("AJ-series buildKeyMatrixRequest stamps BIT7 checksum independently of profile",
          "[aj_series][key-matrix][checksum]") {
    // The checksum drifts with the profile byte. Verify for several profile
    // values that the BIT7 mask is honoured (top bit clear, value matches
    // independent recompute).
    for (std::uint8_t profile = 0; profile < 8; ++profile) {
        CAPTURE(static_cast<unsigned>(profile));
        auto const pkt = buildKeyMatrixRequest(profile);
        CHECK((pkt[kReportSize - 1] & 0x80) == 0);
        CHECK(pkt[kReportSize - 1] == expectedBit7Checksum(pkt));
    }
}

// ===========================================================================
// parseKeyMatrixResponse - sec 3.7 response decoding (16 x 4-byte records)
// ===========================================================================

TEST_CASE("AJ-series parseKeyMatrixResponse accepts 64-byte body",
          "[aj_series][key-matrix][parse]") {
    // Per sec 3.7 line 363: "Response: 64 bytes = 16 x 4-byte action records".
    auto const body = makeSyntheticResponseBody();
    REQUIRE(body.size() == kKeyMatrixResponseBytes);
    auto const decoded = parseKeyMatrixResponse(body);
    REQUIRE(decoded.has_value());
    REQUIRE(decoded->bindings.size() == kKeyMatrixSlotCount);
    // Spot-check each slot's 4 bytes round-trip verbatim.
    for (std::size_t slot = 0; slot < kKeyMatrixSlotCount; ++slot) {
        CAPTURE(slot);
        auto const& action = decoded->bindings[slot];
        CHECK(action.bytes[0] == static_cast<std::uint8_t>(slot));
        CHECK(action.bytes[1] == static_cast<std::uint8_t>(slot + 1));
        CHECK(action.bytes[2] == static_cast<std::uint8_t>(slot + 2));
        CHECK(action.bytes[3] == static_cast<std::uint8_t>(slot + 3));
    }
}

TEST_CASE("AJ-series parseKeyMatrixResponse accepts 65-byte envelope with Report ID prefix",
          "[aj_series][key-matrix][parse]") {
    // libhidapi's hid_read_timeout on a numbered-report device returns the
    // Report ID as the first byte; the 64-byte body follows. The parser
    // detects this shape and strips the prefix.
    auto const body = makeSyntheticResponseBody();
    std::vector<std::uint8_t> envelope;
    envelope.reserve(kKeyMatrixResponseBytes + 1);
    envelope.push_back(kReportId); // 0x05 leading Report ID
    envelope.insert(envelope.end(), body.begin(), body.end());
    REQUIRE(envelope.size() == kKeyMatrixResponseBytes + 1);
    auto const decoded = parseKeyMatrixResponse(envelope);
    REQUIRE(decoded.has_value());
    // First slot's 4 bytes start at envelope[1..4] (after the Report ID).
    CHECK(decoded->bindings[0].bytes[0] == 0);
    CHECK(decoded->bindings[0].bytes[1] == 1);
    CHECK(decoded->bindings[0].bytes[2] == 2);
    CHECK(decoded->bindings[0].bytes[3] == 3);
    // Last slot's 4 bytes at envelope[61..64].
    CHECK(decoded->bindings[15].bytes[0] == 15);
    CHECK(decoded->bindings[15].bytes[1] == 16);
    CHECK(decoded->bindings[15].bytes[2] == 17);
    CHECK(decoded->bindings[15].bytes[3] == 18);
}

TEST_CASE("AJ-series parseKeyMatrixResponse rejects too-short response",
          "[aj_series][key-matrix][parse][safety]") {
    // Anything < 64 bytes is a truncated / NAK response - reject rather than
    // silently zero-pad the trailing slots.
    std::vector<std::uint8_t> const empty;
    CHECK_FALSE(parseKeyMatrixResponse(empty).has_value());

    std::vector<std::uint8_t> const oneByte{0x00};
    CHECK_FALSE(parseKeyMatrixResponse(oneByte).has_value());

    std::vector<std::uint8_t> const almost(kKeyMatrixResponseBytes - 1, 0xAAu);
    CHECK_FALSE(parseKeyMatrixResponse(almost).has_value());
}

TEST_CASE("AJ-series parseKeyMatrixResponse rejects oversize response",
          "[aj_series][key-matrix][parse][safety]") {
    // > 65 bytes is a wire-shape mismatch worth surfacing - the parser
    // refuses rather than silently picking the first 64.
    std::vector<std::uint8_t> const oversize(kKeyMatrixResponseBytes + 2, 0xBBu);
    CHECK_FALSE(parseKeyMatrixResponse(oversize).has_value());

    std::vector<std::uint8_t> const huge(256, 0xCCu);
    CHECK_FALSE(parseKeyMatrixResponse(huge).has_value());
}

TEST_CASE("AJ-series parseKeyMatrixResponse is wire-byte transparent for arbitrary records",
          "[aj_series][key-matrix][parse]") {
    // Stress every action-byte table entry from sec 3.6 (type 0 combo,
    // type 1 mouse-button, type 9 macro, all-zero "forbidden", boundary
    // 0xFF) by packing each record explicitly and confirming the parser
    // surfaces them unmolested in the decoded matrix.
    std::array<std::array<std::uint8_t, 4>, 16> const inputs{{
        {0x00, 0x00, 0x00, 0x00}, // forbidden / unbound
        {0x01, 0x00, 0x04, 0x00}, // mouse-button BTN_FORWARD
        {0x09, 0x01, 0x05, 0x00}, // macro slot 5 in mode 1
        {0x02, 0x00, 0x00, 0x00}, // system function 2
        {0x03, 0x00, 0x00, 0x00}, // system function 3
        {0x06, 0x00, 0x00, 0x00}, // system function 6
        {0x08, 0x00, 0x00, 0x00}, // system function 8
        {0x0a, 0x00, 0x00, 0x00}, // system function 10
        {0x0b, 0x00, 0x00, 0x00}, // system function 11
        {0x0d, 0x00, 0x00, 0x00}, // system function 13
        {0x0e, 0x00, 0x00, 0x00}, // system function 14
        {0x12, 0x00, 0x00, 0x00}, // system function 18
        {0x13, 0x00, 0x00, 0x00}, // system function 19
        {0x14, 0x00, 0x00, 0x00}, // system function 20
        {0x16, 0x00, 0x00, 0x00}, // system function 22
        {0xff, 0xff, 0xff, 0xff}, // boundary all-ones
    }};
    std::vector<std::uint8_t> body;
    body.reserve(kKeyMatrixResponseBytes);
    for (auto const& rec : inputs) {
        body.insert(body.end(), rec.begin(), rec.end());
    }
    REQUIRE(body.size() == kKeyMatrixResponseBytes);
    auto const decoded = parseKeyMatrixResponse(body);
    REQUIRE(decoded.has_value());
    for (std::size_t slot = 0; slot < kKeyMatrixSlotCount; ++slot) {
        CAPTURE(slot);
        CHECK(decoded->bindings[slot].bytes == inputs[slot]);
    }
}

// ===========================================================================
// Full round-trip via AjSeriesMouse::readKeyMatrix() - uses the existing
// MockTransport::enqueueRead seam to inject a canned response.
// ===========================================================================

TEST_CASE("AJ-series readKeyMatrix round-trips request + queued response into a MouseKeyMatrix",
          "[aj_series][key-matrix][roundtrip]") {
    auto fx = buildFixture();
    auto* km = dynamic_cast<core::IMouseKeyMatrixReadable*>(fx.device.get());
    REQUIRE(km != nullptr);

    // Queue the synthetic 64-byte response BEFORE the read happens - the
    // MockTransport::read FIFO returns it on the first call.
    fx.transport->enqueueRead(makeSyntheticResponseBody());

    auto const decoded = km->readKeyMatrix();
    REQUIRE(decoded.has_value());

    // The request packet went out via write() and was captured.
    auto const& writes = fx.transport->writes();
    REQUIRE(writes.size() == 1);
    auto const& req = writes.back();
    REQUIRE(req.size() == kReportSize);
    CHECK(req[0] == kReportId);
    CHECK(req[1] == static_cast<std::uint8_t>(FeaCmd::MouseGetKeyMatrix)); // 0xD0
    CHECK(req[2] == 0); // default profile slot
    CHECK(req[kReportSize - 1] == expectedBit7Checksum(req));

    // The decoded matrix mirrors the synthetic body slot-for-slot.
    for (std::size_t slot = 0; slot < kKeyMatrixSlotCount; ++slot) {
        CAPTURE(slot);
        auto const& action = decoded->bindings[slot];
        CHECK(action.bytes[0] == static_cast<std::uint8_t>(slot));
        CHECK(action.bytes[1] == static_cast<std::uint8_t>(slot + 1));
        CHECK(action.bytes[2] == static_cast<std::uint8_t>(slot + 2));
        CHECK(action.bytes[3] == static_cast<std::uint8_t>(slot + 3));
    }
}

TEST_CASE("AJ-series readKeyMatrix returns nullopt when no response is queued (timeout)",
          "[aj_series][key-matrix][roundtrip][safety]") {
    auto fx = buildFixture();
    auto* km = dynamic_cast<core::IMouseKeyMatrixReadable*>(fx.device.get());
    REQUIRE(km != nullptr);

    // Empty read queue - MockTransport::read returns 0 bytes (simulates
    // a HID read timeout / NAK). The backend surfaces this as nullopt
    // rather than throwing.
    auto const decoded = km->readKeyMatrix();
    CHECK_FALSE(decoded.has_value());

    // The request still went out on the wire even though the response
    // never arrived - this is the canonical "request-sent-no-ack" pattern.
    auto const& writes = fx.transport->writes();
    REQUIRE(writes.size() == 1);
    CHECK(writes.back()[1] == static_cast<std::uint8_t>(FeaCmd::MouseGetKeyMatrix));
}

TEST_CASE("AJ-series readKeyMatrix returns nullopt on too-short queued response",
          "[aj_series][key-matrix][roundtrip][safety]") {
    auto fx = buildFixture();
    auto* km = dynamic_cast<core::IMouseKeyMatrixReadable*>(fx.device.get());
    REQUIRE(km != nullptr);

    // Queue a deliberately truncated response - the parser rejects, the
    // backend surfaces as nullopt.
    std::vector<std::uint8_t> const truncated(kKeyMatrixResponseBytes - 8, 0x42u);
    fx.transport->enqueueRead(truncated);

    auto const decoded = km->readKeyMatrix();
    CHECK_FALSE(decoded.has_value());
}
