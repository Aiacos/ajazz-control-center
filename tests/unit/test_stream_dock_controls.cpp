// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_stream_dock_controls.cpp
 * @brief MockTransport wire-level assertions for StreamDockControlService
 *        DISPLAY-09 extension: setBrightness (-> LIG) + clearAll (-> CLE).
 *
 * Phase 16 Plan 16-01 gating proof — hardware-free (MockTransport + makeAkp05WithTransport).
 * Live brightness/clear hardware witness is Phase 25.
 *
 * Test tag: [stream-dock-controls]
 *
 * Byte conventions (akp05_protocol.hpp + akp_common_protocol.hpp):
 *   LIG  (brightness): byte[5] == 'L' (0x4C), byte[6]=='I', byte[7]=='G', brightness at byte[10].
 *   CLE  (clear key):  byte[5] == 'C' (0x43), byte[6]=='L', byte[7]=='E'.
 */
#include "ajazz/core/capabilities.hpp"
#include "ajazz/core/device.hpp"
#include "ajazz/core/profile.hpp"
#include "ajazz/streamdeck/streamdeck.hpp"
#include "fixtures/mock_transport.hpp"
#include "qt_app_fixture.hpp"
#include "stream_dock_control_service.hpp"

#include <QCoreApplication>
#include <QString>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using namespace ajazz;

namespace {

// ---------------------------------------------------------------------------
// Helper: build an AKP05E descriptor + device id for tests.
// ---------------------------------------------------------------------------
core::DeviceDescriptor makeDescriptor() {
    core::DeviceDescriptor d{};
    d.vendorId = 0x0300;
    d.productId = 0x3004;
    d.family = core::DeviceFamily::StreamDeck;
    d.model = "AJAZZ AKP05E (test-16)";
    d.codename = "akp05e";
    d.keyCount = 10;
    d.encoderCount = 4;
    d.hasTouchStrip = true;
    d.hasClock = false;
    return d;
}

core::DeviceId makeDeviceId() {
    core::DeviceId id{};
    id.vendorId = 0x0300;
    id.productId = 0x3004;
    id.serial = "TEST-16-01";
    return id;
}

// ---------------------------------------------------------------------------
// Fixture: MockTransport-backed Akp05Device with VER feature response queued.
// ---------------------------------------------------------------------------
struct Fixture {
    core::DevicePtr device;
    tests::MockTransport* transport; ///< Non-owning observer ptr.
};

Fixture makeFixture() {
    auto owned = std::make_unique<tests::MockTransport>();
    auto* obs = owned.get();
    // Seed the GET_FEATURE response that probeFirmwareVersion() consumes.
    std::vector<std::uint8_t> verResponse(20, 0);
    verResponse[0] = 0x01; // report id
    std::string const vstr = "V3.AKP05E.01.007";
    for (std::size_t i = 0; i < vstr.size() && i + 1 < verResponse.size(); ++i) {
        verResponse[i + 1] = static_cast<std::uint8_t>(vstr[i]);
    }
    obs->enqueueReadFeature(std::move(verResponse));
    auto dev =
        streamdeck::makeAkp05WithTransport(makeDescriptor(), makeDeviceId(), std::move(owned));
    return Fixture{std::move(dev), obs};
}

/// Drain the QTimer-based write queue (two passes handle any deferred timers).
void drainQueue() {
    QCoreApplication::processEvents();
    QCoreApplication::processEvents();
}

/// Find the first packet (from startFrom) whose byte[5..7] == a, b, c.
/// Returns writes.size() on miss.
std::size_t findPacketByCmd(std::vector<std::vector<std::uint8_t>> const& writes,
                            std::uint8_t a,
                            std::uint8_t b,
                            std::uint8_t c,
                            std::size_t startFrom = 0) {
    for (std::size_t i = startFrom; i < writes.size(); ++i) {
        if (writes[i].size() > 7 && writes[i][5] == a && writes[i][6] == b && writes[i][7] == c) {
            return i;
        }
    }
    return writes.size();
}

} // namespace

// ===========================================================================
// DISPLAY-09: setBrightness(codename, 42) produces LIG write with brightness 42
// ===========================================================================

TEST_CASE("StreamDockControlService: setBrightness emits LIG with correct brightness (DISPLAY-09)",
          "[stream-dock-controls][DISPLAY-09]") {
    ajazz::tests::qtApp();

    auto fx = makeFixture();
    auto devPtr = fx.device;
    auto* obs = fx.transport;

    app::StreamDockControlService svc(
        [devPtr](QString const&) -> std::shared_ptr<core::IDevice> { return devPtr; }, nullptr);

    svc.setActiveDevice(QStringLiteral("akp05e"));
    drainQueue();
    auto const writeCountAfterOpen = obs->writeCount();

    svc.setBrightness(QStringLiteral("akp05e"), 42);
    drainQueue();

    auto const& writes = obs->writes();
    // Must have at least one new write after setBrightness.
    REQUIRE(writes.size() > writeCountAfterOpen);

    // Find LIG packet produced by setBrightness call.
    auto const ligIdx =
        findPacketByCmd(writes, 0x4C, 0x49, 0x47, writeCountAfterOpen); // 'L','I','G'
    REQUIRE(ligIdx < writes.size());
    auto const& ligPkt = writes[ligIdx];
    REQUIRE(ligPkt.size() >= 11);
    CHECK(ligPkt[5] == 0x4C); // 'L'
    CHECK(ligPkt[6] == 0x49); // 'I'
    CHECK(ligPkt[7] == 0x47); // 'G'
    // Brightness byte is at position 10 (per akp05_protocol.hpp).
    CHECK(ligPkt[10] == 42);
}

// ===========================================================================
// DISPLAY-09: setBrightness clamps 150 -> 100 (no byte > 100 in LIG)
// ===========================================================================

TEST_CASE("StreamDockControlService: setBrightness clamps out-of-range value to 100",
          "[stream-dock-controls][DISPLAY-09]") {
    ajazz::tests::qtApp();

    auto fx = makeFixture();
    auto devPtr = fx.device;
    auto* obs = fx.transport;

    app::StreamDockControlService svc(
        [devPtr](QString const&) -> std::shared_ptr<core::IDevice> { return devPtr; }, nullptr);

    svc.setActiveDevice(QStringLiteral("akp05e"));
    drainQueue();
    auto const writeCountAfterOpen = obs->writeCount();

    svc.setBrightness(QStringLiteral("akp05e"), 150); // out of range
    drainQueue();

    auto const& writes = obs->writes();
    REQUIRE(writes.size() > writeCountAfterOpen);

    auto const ligIdx = findPacketByCmd(writes, 0x4C, 0x49, 0x47, writeCountAfterOpen);
    REQUIRE(ligIdx < writes.size());
    auto const& ligPkt = writes[ligIdx];
    REQUIRE(ligPkt.size() >= 11);
    // Brightness must be clamped to 100.
    CHECK(ligPkt[10] == 100);
}

// ===========================================================================
// DISPLAY-09: setBrightness clamps -50 -> 0 (lower-bound symmetric of above)
// ===========================================================================

TEST_CASE("StreamDockControlService: setBrightness clamps negative value to 0",
          "[stream-dock-controls][DISPLAY-09]") {
    ajazz::tests::qtApp();

    auto fx = makeFixture();
    auto devPtr = fx.device;
    auto* obs = fx.transport;

    app::StreamDockControlService svc(
        [devPtr](QString const&) -> std::shared_ptr<core::IDevice> { return devPtr; }, nullptr);

    svc.setActiveDevice(QStringLiteral("akp05e"));
    drainQueue();
    auto const writeCountAfterOpen = obs->writeCount();

    svc.setBrightness(QStringLiteral("akp05e"), -50); // negative -- must clamp to 0
    drainQueue();

    auto const& writes = obs->writes();
    REQUIRE(writes.size() > writeCountAfterOpen);

    auto const ligIdx = findPacketByCmd(writes, 0x4C, 0x49, 0x47, writeCountAfterOpen);
    REQUIRE(ligIdx < writes.size());
    auto const& ligPkt = writes[ligIdx];
    REQUIRE(ligPkt.size() >= 11);
    // Brightness must be clamped to 0.
    CHECK(ligPkt[10] == 0);
}

// ===========================================================================
// DISPLAY-09: clearAll(codename) produces CLE write
// ===========================================================================

TEST_CASE("StreamDockControlService: clearAll emits CLE write (DISPLAY-09)",
          "[stream-dock-controls][DISPLAY-09]") {
    ajazz::tests::qtApp();

    auto fx = makeFixture();
    auto devPtr = fx.device;
    auto* obs = fx.transport;

    app::StreamDockControlService svc(
        [devPtr](QString const&) -> std::shared_ptr<core::IDevice> { return devPtr; }, nullptr);

    svc.setActiveDevice(QStringLiteral("akp05e"));
    drainQueue();
    auto const writeCountAfterOpen = obs->writeCount();

    svc.clearAll(QStringLiteral("akp05e"));
    drainQueue();

    auto const& writes = obs->writes();
    REQUIRE(writes.size() > writeCountAfterOpen);

    // CLE packet: bytes[5..7] == 'C','L','E'
    auto const cleIdx =
        findPacketByCmd(writes, 0x43, 0x4C, 0x45, writeCountAfterOpen); // 'C','L','E'
    REQUIRE(cleIdx < writes.size());
    auto const& clePkt = writes[cleIdx];
    REQUIRE(clePkt.size() >= 8);
    CHECK(clePkt[5] == 0x43); // 'C'
    CHECK(clePkt[6] == 0x4C); // 'L'
    CHECK(clePkt[7] == 0x45); // 'E'
}

// ===========================================================================
// DISPLAY-09: non-IDisplayCapable device no-ops both calls (no crash, no write)
// ===========================================================================

TEST_CASE("StreamDockControlService: non-display device no-ops setBrightness and clearAll",
          "[stream-dock-controls][DISPLAY-09]") {
    ajazz::tests::qtApp();

    // Construct a MockTransport-backed device that is NOT IDisplayCapable.
    // We achieve this by using a null DeviceLookup result (device not connected).
    app::StreamDockControlService svc(
        [](QString const&) -> std::shared_ptr<core::IDevice> {
            return nullptr; // device not found
        },
        nullptr);

    // No active device set -- setBrightness and clearAll must not crash.
    CHECK_NOTHROW(svc.setBrightness(QStringLiteral("unknown"), 50));
    drainQueue();
    CHECK_NOTHROW(svc.clearAll(QStringLiteral("unknown")));
    drainQueue();

    // With active device set to nullptr-returning lookup, setBrightness with a
    // real codename still must not crash.
    svc.setActiveDevice(QStringLiteral("akp05e")); // lookup returns nullptr
    drainQueue();
    CHECK_NOTHROW(svc.setBrightness(QStringLiteral("akp05e"), 50));
    drainQueue();
    CHECK_NOTHROW(svc.clearAll(QStringLiteral("akp05e")));
    drainQueue();
}
