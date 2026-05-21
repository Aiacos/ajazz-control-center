// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_aj_series_tft_clock.cpp
 * @brief Byte-level test of the AJ-series mouse OLED basetta firmware-RTC
 *        clock (opcode 0x28).
 *
 * HARDWARE-CONFIRMED 2026-05-21: `IClockCapable::setTime()` on `AjSeriesMouse`
 * sends a single structured RTC packet (opcode 0x28) via writeFeature(), NOT a
 * host-rendered RGB565 bitmap. The Frida capture of the vendor iot_driver
 * (scripts/aj_mouse_frida_capture.py) pinned the wire format:
 *   00 28 00*6 d7 <yrHi yrLo> M D h m s  (report id 0x00, fixed 0xD7 marker,
 *   year big-endian, NO checksum).
 *
 * This test wires `MockTransport` through `makeAjSeriesWithTransport`, drives
 * `setTime()`, and asserts the exact 0x28 envelope. The render-pipeline tests
 * below still exercise renderClockDpiFace/encodeRgb565Chunks (kept for a future
 * custom-image-on-basetta feature via opcode 0x25).
 */
#include "aj_series_protocol.hpp"
#include "aj_series_tft_pipeline.hpp"
#include "ajazz/core/capabilities.hpp"
#include "ajazz/core/device.hpp"
#include "ajazz/mouse/mouse.hpp"
#include "fixtures/mock_transport.hpp"
#include "qt_app_fixture.hpp"

#include <QImage>

#include <chrono>
#include <cstdint>
#include <memory>
#include <utility>

#include <catch2/catch_test_macros.hpp>

using namespace ajazz;
using namespace ajazz::mouse::aj_series;

namespace {

core::DeviceDescriptor makeDescriptor() {
    core::DeviceDescriptor d{};
    d.vendorId = 0x3151;
    d.productId = 0x5007;
    d.family = core::DeviceFamily::Mouse;
    d.model = "AJAZZ 2.4G 8K (test)";
    d.codename = "ajazz_24g_8k";
    d.dpiStageCount = 8;
    d.hasClock = true;
    return d;
}

core::DeviceId makeId() {
    core::DeviceId id{};
    id.vendorId = 0x3151;
    id.productId = 0x5007;
    id.serial = "TEST";
    return id;
}

std::chrono::system_clock::time_point referenceTime() {
    // 2026-05-18 16:34:00 local — deterministic for layout/render
    // assertions. The renderer doesn't surface the seconds in the
    // current face design so we keep them at 0.
    std::tm local{};
    local.tm_year = 2026 - 1900;
    local.tm_mon = 4; // May
    local.tm_mday = 18;
    local.tm_hour = 16;
    local.tm_min = 34;
    local.tm_sec = 0;
    local.tm_isdst = -1;
    return std::chrono::system_clock::from_time_t(std::mktime(&local));
}

} // namespace

TEST_CASE("AjSeriesMouse setTime emits the 0x28 OLED-clock RTC packet",
          "[mouse][aj_series][tft][clock][vendor-re]") {
    auto transport = std::make_unique<tests::MockTransport>();
    auto* observer = transport.get();
    transport->open();

    auto device =
        mouse::makeAjSeriesWithTransport(makeDescriptor(), makeId(), std::move(transport));
    REQUIRE(device != nullptr);

    auto* clk = dynamic_cast<core::IClockCapable*>(device.get());
    REQUIRE(clk != nullptr);

    auto const result = clk->setTime(referenceTime());
    REQUIRE(result == core::TimeSyncResult::Ok);

    // Exactly one feature report — the structured RTC packet, not a chunked
    // bitmap upload.
    auto const& writes = observer->writes();
    REQUIRE(writes.size() == 1u);
    REQUIRE(observer->writeFeatureCount() == 1u);

    auto const& pkt = writes.front();
    REQUIRE(pkt.size() == kReportSize); // 65 bytes
    REQUIRE(pkt[0] == 0x00);            // HID report id 0x00 (NOT kReportId 0x05)
    REQUIRE(pkt[1] == 0x28);            // FEA_CMD_SET_OLEDCLOCK
    REQUIRE(pkt[8] == 0xd7);            // REQUIRED fixed marker
    // referenceTime() = 2026-05-18 16:34:00 local.
    REQUIRE(pkt[9] == 0x07);  // year hi (2026 = 0x07ea, big-endian)
    REQUIRE(pkt[10] == 0xea); // year lo
    REQUIRE(pkt[11] == 5);    // month
    REQUIRE(pkt[12] == 18);   // day
    REQUIRE(pkt[13] == 16);   // hour
    REQUIRE(pkt[14] == 34);   // minute
    REQUIRE(pkt[15] == 0);    // second
    // No checksum: the vendor leaves the tail zero.
    REQUIRE(pkt[kReportSize - 1] == 0x00);
}

TEST_CASE("renderClockDpiFace produces a Format_RGB16 image of the requested size",
          "[mouse][aj_series][tft][render]") {
    tests::qtGuiApp();
    QSize const panel(128, 128);
    QImage const face = mouse::renderClockDpiFace(panel, referenceTime(), /*activeDpi*/ 1600);
    REQUIRE_FALSE(face.isNull());
    REQUIRE(face.size() == panel);
    REQUIRE(face.format() == QImage::Format_RGB16);
}

TEST_CASE("encodeRgb565Chunks slices the framebuffer into <= 55-byte payloads",
          "[mouse][aj_series][tft][render]") {
    tests::qtGuiApp();
    QImage const face =
        mouse::renderClockDpiFace(QSize(64, 64), referenceTime(), /*activeDpi*/ 800);
    auto const chunks = mouse::encodeRgb565Chunks(face);
    REQUIRE_FALSE(chunks.empty());
    // Total bytes must equal width*height*2 (RGB565).
    std::size_t total = 0;
    for (auto const& c : chunks) {
        REQUIRE(c.size() <= kTftChunkPayloadBytes);
        total += c.size();
    }
    REQUIRE(total == 64u * 64u * 2u);
}

TEST_CASE("AjSeriesMouse setTime is independent of DPI state",
          "[mouse][aj_series][tft][robustness]") {
    // The 0x28 RTC packet carries no DPI field, so setTime must work without
    // any setDpiStages call (unlike the old bitmap face that rendered the DPI).
    auto transport = std::make_unique<tests::MockTransport>();
    auto* observer = transport.get();
    transport->open();
    auto device =
        mouse::makeAjSeriesWithTransport(makeDescriptor(), makeId(), std::move(transport));
    auto* clk = dynamic_cast<core::IClockCapable*>(device.get());
    REQUIRE(clk != nullptr);

    REQUIRE(clk->setTime(referenceTime()) == core::TimeSyncResult::Ok);
    REQUIRE(observer->writes().size() == 1u);
    REQUIRE(observer->writes().front()[1] == 0x28);
}
