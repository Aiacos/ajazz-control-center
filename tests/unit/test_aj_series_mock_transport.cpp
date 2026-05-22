// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_aj_series_mock_transport.cpp
 * @brief CAPTURE-04 (Phase 09, Plan 09-04) smoke test: wire `MockTransport`
 *        into `AjSeriesMouse` via the new `makeAjSeriesWithTransport` public
 *        factory and assert the exact 64-byte feature-report envelope that
 *        `setActiveDpiStage(0)` writes to the wire.
 *
 * Locks the `MockTransport` + COD-026 DI seam in place BEFORE Phase 10/11/12
 * implementation work depends on it. The byte-level assertion doubles as a
 * regression guard on `aj_series.cpp`'s envelope shape — `kReportSize == 64`,
 * report-id `0x05` at byte 0, command `0x21` (kCmdDpi) at byte 1, sub-cmd
 * `0x01` at byte 2, payload-length `0x01` at byte 3, stage index at byte 4,
 * 58 zero bytes of padding, and a checksum byte at byte 63 equal to
 * `(0x21 + 0x01 + 0x01 + 0x00) & 0xff = 0x23`.
 *
 * See docs/protocols/mouse/aj_series.md for the full byte-level wire format
 * and checksum derivation, and `tests/unit/fixtures/mock_transport.hpp` for
 * the inspection API used here.
 */
#include "ajazz/core/capabilities.hpp"
#include "ajazz/core/device.hpp"
#include "ajazz/core/transport.hpp"
#include "ajazz/mouse/mouse.hpp"
#include "fixtures/mock_transport.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
#include <utility>

#include <catch2/catch_test_macros.hpp>

using namespace ajazz;

namespace {

/// Minimal `DeviceDescriptor` for the ajazz_24g_8k (VID 0x3151 / PID 0x5007)
/// model. AjSeriesMouse::setActiveDpiStage() does not read these fields; we
/// populate just enough to keep `descriptor()` returning a meaningful object.
core::DeviceDescriptor makeDescriptor() {
    core::DeviceDescriptor d{};
    d.vendorId = 0x3151;
    d.productId = 0x5007;
    d.family = core::DeviceFamily::Mouse;
    d.model = "AJAZZ 2.4G 8K (test)";
    d.codename = "ajazz_24g_8k";
    d.dpiStageCount = 6;
    return d;
}

core::DeviceId makeId() {
    core::DeviceId id{};
    id.vendorId = 0x3151;
    id.productId = 0x5007;
    id.serial = "TEST";
    return id;
}

/// Minimal ITransport whose write() always throws, simulating a mouse
/// physically yanked mid-transaction (makeHidTransport's write() throws on
/// a failed HID write). MockTransport is `final`, so this is a separate stub.
class ThrowingTransport final : public core::ITransport {
public:
    void open() override {}
    void close() override {}
    [[nodiscard]] bool isOpen() const noexcept override { return true; }
    std::size_t write(std::span<std::uint8_t const>) override {
        throw std::runtime_error("simulated device-yank: HID write failed");
    }
    std::size_t read(std::span<std::uint8_t>, std::chrono::milliseconds) override { return 0; }
    std::size_t writeFeature(std::span<std::uint8_t const>) override {
        throw std::runtime_error("simulated device-yank: HID writeFeature failed");
    }
    std::size_t readFeature(std::span<std::uint8_t>) override { return 0; }
    [[nodiscard]] core::TransportStats stats() const noexcept override { return {}; }
};

} // namespace

TEST_CASE("MockTransport captures setActiveDpiStage envelope on AjSeriesMouse",
          "[unit][aj_series][mock_transport][CAPTURE-04]") {
    auto transport = std::make_unique<tests::MockTransport>();
    auto* observer = transport.get(); // observer ptr; ownership transfers to backend
    transport->open();

    auto device =
        mouse::makeAjSeriesWithTransport(makeDescriptor(), makeId(), std::move(transport));
    REQUIRE(device != nullptr);

    auto* dpi = dynamic_cast<core::IMouseCapable*>(device.get());
    REQUIRE(dpi != nullptr);

    dpi->setActiveDpiStage(0);

    // P3.12.2: AjSeriesMouse migrated to vendor-correct wire format.
    // setActiveDpiStage now re-uploads the FULL 8-stage DPI table atomically
    // via opcode 0x54 (FEA_CMD_MOUSE_SET_OPTIONPARAM1, vendor pattern). The
    // packet is a 65-byte HID OUTPUT REPORT (writeFeature → write), checksum
    // is BIT7 (& 0x7F), 8 stages of DPI/colour.
    REQUIRE(observer->writeCount() == 1);        // OUTPUT report, NOT feature
    REQUIRE(observer->writeFeatureCount() == 0); // confirms transport correction
    REQUIRE(observer->writes().size() == 1);
    auto const& pkt = observer->writes().at(0);
    REQUIRE(pkt.size() == 65); // 1 ReportId + 64 vendor envelope bytes

    CHECK(pkt[0] == 0x05); // ReportId
    CHECK(pkt[1] == 0x54); // FeaCmd::MouseSetOption1 (was 0x21, wrong)
    CHECK(pkt[2] == 0x00); // active DPI stage = 0
    CHECK(pkt[3] == 0x00); // stage count = 0 (all DPI values are 0 by default)
    // bytes 4..8 reserved zero, then DPI table (LE uint16) at pkt[9..24]
    for (std::size_t i = 4; i < 41; ++i) {
        CAPTURE(i);
        CHECK(pkt[i] == 0x00);
    }
    // BIT7 checksum at pkt[64] — sum of pkt[1..63] = 0x54 (opcode only) & 0x7F
    // = 0x54. Other bytes are zero so the checksum is just the opcode value.
    CHECK(pkt[64] == 0x54);
}

TEST_CASE("AjSeriesMouse batteryPercent polls 0xF7 then reads the Windows frame (byte 3)",
          "[unit][aj_series][mock_transport][battery]") {
    // The basetta only fills the charge after the 0xF7 status poll. The backend
    // SET_FEATUREEs that poll (report-id 0x00, opcode 0xF7), then GET_FEATUREEs the
    // status report. On Windows hidapi keeps the report-id byte at index 0, so
    // the captured frame is `05 00 00 64 01 01 01 02` (charge 0x64=100% at byte 3).
    auto transport = std::make_unique<tests::MockTransport>();
    auto* observer = transport.get();
    transport->open();
    observer->enqueueReadFeature({0x05, 0x00, 0x00, 0x64, 0x01, 0x01, 0x01, 0x02});

    auto device =
        mouse::makeAjSeriesWithTransport(makeDescriptor(), makeId(), std::move(transport));
    auto* battery = dynamic_cast<core::IBatteryCapable*>(device.get());
    REQUIRE(battery != nullptr);

    auto const charge = battery->batteryPercent();
    REQUIRE(charge.has_value());
    CHECK(charge.value() == 100);
    // Exactly one SET_FEATURE — the 0xF7 status poll on report-id 0x00.
    REQUIRE(observer->writeFeatureCount() == 1);
    REQUIRE(observer->writes().size() == 1);
    auto const& poll = observer->writes().at(0);
    REQUIRE(poll.size() == 65);
    CHECK(poll[0] == 0x00); // status-poll report id
    CHECK(poll[1] == 0xF7); // status-poll opcode
}

TEST_CASE("AjSeriesMouse batteryPercent reads the Linux status frame (byte 2)",
          "[unit][aj_series][mock_transport][battery]") {
    // On Linux hidraw the unnumbered frame has no report-id prefix, so the same
    // status report reads back as `00 00 64 …` (charge at byte 2). The shared
    // parseBatteryCharge() auto-detects the offset from the leading byte.
    auto transport = std::make_unique<tests::MockTransport>();
    auto* observer = transport.get();
    transport->open();
    observer->enqueueReadFeature({0x00, 0x00, 0x64, 0x01, 0x01, 0x01, 0x02});

    auto device =
        mouse::makeAjSeriesWithTransport(makeDescriptor(), makeId(), std::move(transport));
    auto* battery = dynamic_cast<core::IBatteryCapable*>(device.get());
    REQUIRE(battery != nullptr);

    auto const charge = battery->batteryPercent();
    REQUIRE(charge.has_value());
    CHECK(charge.value() == 100);
}

TEST_CASE("AjSeriesMouse batteryPercent treats an all-zero frame as unknown",
          "[unit][aj_series][mock_transport][battery]") {
    // Charge byte 0 = link up but the dongle has not reported yet (asleep) →
    // nullopt (grey), never a wrong 0%.
    auto transport = std::make_unique<tests::MockTransport>();
    auto* observer = transport.get();
    transport->open();
    observer->enqueueReadFeature({0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});

    auto device =
        mouse::makeAjSeriesWithTransport(makeDescriptor(), makeId(), std::move(transport));
    auto* battery = dynamic_cast<core::IBatteryCapable*>(device.get());
    REQUIRE(battery != nullptr);
    CHECK_FALSE(battery->batteryPercent().has_value());
}

TEST_CASE("AjSeriesMouse batteryPercent rejects a transient garbage frame",
          "[unit][aj_series][mock_transport][battery]") {
    // A non-zero byte 1 marks the garbage GET_FEATURE can return right after a
    // wireless reconnect (e.g. `00 ad 04 ...`) → rejected.
    auto transport = std::make_unique<tests::MockTransport>();
    auto* observer = transport.get();
    transport->open();
    observer->enqueueReadFeature({0x00, 0xAD, 0x04, 0x00, 0x00, 0x00, 0x00});

    auto device =
        mouse::makeAjSeriesWithTransport(makeDescriptor(), makeId(), std::move(transport));
    auto* battery = dynamic_cast<core::IBatteryCapable*>(device.get());
    REQUIRE(battery != nullptr);
    CHECK_FALSE(battery->batteryPercent().has_value());
}

TEST_CASE("MockTransport reset() clears captured writes", "[unit][mock_transport][CAPTURE-04]") {
    tests::MockTransport mt;
    std::array<std::uint8_t, 4> sample{0x01, 0x02, 0x03, 0x04};
    mt.open();
    mt.write(sample);
    REQUIRE(mt.writes().size() == 1);
    REQUIRE(mt.writeCount() == 1);
    REQUIRE(mt.isOpen());

    mt.reset();
    REQUIRE(mt.writes().empty());
    REQUIRE(mt.writeCount() == 0);
    REQUIRE(mt.writeFeatureCount() == 0);
    REQUIRE_FALSE(mt.isOpen());
}

TEST_CASE("MockTransport differentiates write and writeFeature in counts",
          "[unit][mock_transport][CAPTURE-04]") {
    tests::MockTransport mt;
    std::array<std::uint8_t, 2> out_report{0x00, 0xAA};
    std::array<std::uint8_t, 3> feature_report{0x05, 0x21, 0x00};
    mt.write(out_report);
    mt.writeFeature(feature_report);
    mt.write(out_report);

    REQUIRE(mt.writeCount() == 3);
    REQUIRE(mt.writeFeatureCount() == 1);
    // Order is preserved across the two interleaved write paths.
    REQUIRE(mt.writes().at(0).size() == 2);
    REQUIRE(mt.writes().at(1).size() == 3);
    REQUIRE(mt.writes().at(2).size() == 2);
    CHECK(mt.writes().at(1)[1] == 0x21);
}

TEST_CASE("AjSeriesMouse void setters swallow a transport throw on device-yank",
          "[mouse][aj_series][regression]") {
    // Regression for Phase 11 CR-01: the void IMouseCapable/IRgbCapable
    // setters must not let a transport write() exception (physical yank)
    // escape into fire-and-forget QML callers. Covers all four patched
    // sites: two direct writes plus uploadDpiTableAtomic (via
    // setActiveDpiStage) and emitLedPacket (via setRgbBrightness).
    auto device = mouse::makeAjSeriesWithTransport(
        makeDescriptor(), makeId(), std::make_unique<ThrowingTransport>());
    auto* m = dynamic_cast<core::IMouseCapable*>(device.get());
    auto* rgb = dynamic_cast<core::IRgbCapable*>(device.get());
    REQUIRE(m != nullptr);
    REQUIRE(rgb != nullptr);

    CHECK_NOTHROW(m->setLiftOffDistanceMm(2.0F));   // direct write
    CHECK_NOTHROW(m->setButtonBinding(1, 0x1234U)); // direct write
    CHECK_NOTHROW(m->setActiveDpiStage(0));         // -> uploadDpiTableAtomic
    CHECK_NOTHROW(rgb->setRgbBrightness(50));       // -> emitLedPacket
}

TEST_CASE("AjSeriesMouse setRgbBrightness clamps percent to the 0..5 vendor scale",
          "[unit][aj_series][mock_transport][rgb]") {
    // WR-04: `percent` is a uint8 (0..255). Without the clamp, 200 maps to
    // (200*5)/100 = 10 — outside the documented 0..5 LED brightness range.
    auto transport = std::make_unique<tests::MockTransport>();
    auto* observer = transport.get();
    transport->open();
    auto device =
        mouse::makeAjSeriesWithTransport(makeDescriptor(), makeId(), std::move(transport));
    auto* rgb = dynamic_cast<core::IRgbCapable*>(device.get());
    REQUIRE(rgb != nullptr);

    rgb->setRgbBrightness(200); // over 100%
    REQUIRE(observer->writes().size() == 1);
    auto const& pkt = observer->writes().at(0); // 0x07 SetLedParam packet
    REQUIRE(pkt.size() >= 5);
    REQUIRE(pkt[1] == 0x07);
    CHECK(pkt[4] == 5); // clamped to the vendor max, not 10
}
