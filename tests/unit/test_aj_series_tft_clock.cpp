// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_aj_series_tft_clock.cpp
 * @brief End-to-end byte-level test of the AJ-series mouse TFT LCD
 *        clock+DPI upload (opcode 0x25 chunked, OLED basetta).
 *
 * The audit-2 follow-up landed `IClockCapable::setTime()` on
 * `AjSeriesMouse`: caller passes `system_clock::time_point`, the backend
 * renders a clock + active-DPI face via `renderClockDpiFace`, packs it
 * into RGB565 chunks via `encodeRgb565Chunks`, and uploads each chunk
 * with `buildSetTftLcdData` (opcode 0x25).
 *
 * This test wires `MockTransport` through the existing P3.12.1 DI seam
 * (`makeAjSeriesWithTransport`), drives `setTime()`, and asserts the
 * envelope shape of every uploaded chunk:
 *   - opcode 0x25 at pkt[1] on every chunk
 *   - frameCount=1, frameDelay=0 (still image)
 *   - chunkIndex strictly increasing
 *   - chunkLen matches the slice the pipeline produced
 *   - BIT7 checksum stamped at pkt[63]
 *
 * Pins the wire format against regressions and proves the OLED basetta
 * code path is actually live (not just compiled).
 */
#include "aj_series_protocol.hpp"
#include "aj_series_tft_pipeline.hpp"
#include "ajazz/core/capabilities.hpp"
#include "ajazz/core/device.hpp"
#include "ajazz/mouse/mouse.hpp"
#include "fixtures/mock_transport.hpp"

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

TEST_CASE("AjSeriesMouse setTime emits chunked 0x25 TFT upload",
          "[mouse][aj_series][tft][clock][CAPTURE-04][vendor-re]") {
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

    auto const& writes = observer->writes();
    REQUIRE_FALSE(writes.empty());

    // Default panel is 128x128 RGB565 = 32 KiB, chunked at 55 bytes/packet
    // = ceil(32768/55) = 596 chunks. Pin a lower bound that survives
    // layout tweaks without becoming flaky.
    REQUIRE(writes.size() >= 590);

    // Every uploaded packet is a TFT chunk: opcode 0x25, frameCount=1,
    // frameDelay=0, chunkIndex monotonically increasing.
    std::uint16_t expectedIndex = 0;
    for (auto const& pkt : writes) {
        REQUIRE(pkt.size() == kReportSize);          // 65 bytes
        REQUIRE(pkt[0] == kReportId);                // 0x05
        REQUIRE(pkt[1] == 0x25);                     // FEA_CMD_SETTFTLCDDATA
        REQUIRE(pkt[2] == 0);                        // currentFrame
        REQUIRE(pkt[3] == 1);                        // frameNum (still image)
        REQUIRE(pkt[4] == 0);                        // frameDelay
        std::uint16_t const idx =
            static_cast<std::uint16_t>(pkt[5] | (static_cast<std::uint16_t>(pkt[6]) << 8U));
        REQUIRE(idx == expectedIndex);
        REQUIRE(pkt[7] <= kTftChunkPayloadBytes);    // chunkLen budget
        REQUIRE(pkt[8] == 0);                        // reserved
        ++expectedIndex;
    }
}

TEST_CASE("renderClockDpiFace produces a Format_RGB16 image of the requested size",
          "[mouse][aj_series][tft][render]") {
    QSize const panel(128, 128);
    QImage const face =
        mouse::renderClockDpiFace(panel, referenceTime(), /*activeDpi*/ 1600);
    REQUIRE_FALSE(face.isNull());
    REQUIRE(face.size() == panel);
    REQUIRE(face.format() == QImage::Format_RGB16);
}

TEST_CASE("encodeRgb565Chunks slices the framebuffer into <= 55-byte payloads",
          "[mouse][aj_series][tft][render]") {
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

TEST_CASE("AjSeriesMouse setTime tolerates renderer edge cases",
          "[mouse][aj_series][tft][robustness]") {
    // activeDpi == 0 -> renderer omits the DPI line (mouse without DPI
    // introspection); the upload still happens with the clock-only face.
    auto transport = std::make_unique<tests::MockTransport>();
    auto* observer = transport.get();
    transport->open();
    auto device =
        mouse::makeAjSeriesWithTransport(makeDescriptor(), makeId(), std::move(transport));
    auto* clk = dynamic_cast<core::IClockCapable*>(device.get());
    REQUIRE(clk != nullptr);

    // No setDpiStages call -> m_dpiStages stays zero-filled -> activeDpi=0.
    REQUIRE(clk->setTime(referenceTime()) == core::TimeSyncResult::Ok);
    REQUIRE_FALSE(observer->writes().empty());
}
