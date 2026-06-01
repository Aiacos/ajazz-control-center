// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_stream_dock_controls.cpp
 * @brief StreamDockControlService brightness/clear behaviour (DISPLAY-09).
 *
 * experiment/mirajazz: migrated off the MockTransport + makeAkp05WithTransport
 * wire fixture onto the in-process FakeStreamDockDevice. The C++ AKP05 wire
 * layer is gone, so the LIG/CLE byte format + brightness clamping are now the
 * sidecar's concern (covered by streamdock-host cargo tests). These tests now
 * assert the SERVICE behaviour that remains: setBrightness/clearAll forward to
 * the active device's IDisplayCapable, and a missing device is a safe no-op.
 *
 * Test tag: [stream-dock-controls]
 */
#include "ajazz/core/capabilities.hpp"
#include "ajazz/core/device.hpp"
#include "fixtures/fake_stream_dock_device.hpp"
#include "qt_app_fixture.hpp"
#include "stream_dock_control_service.hpp"

#include <QCoreApplication>
#include <QString>

#include <memory>

#include <catch2/catch_test_macros.hpp>

using namespace ajazz;

namespace {

core::DeviceDescriptor makeDescriptor() {
    core::DeviceDescriptor d{};
    d.vendorId = 0x0300;
    d.productId = 0x3004;
    d.family = core::DeviceFamily::StreamDeck;
    d.model = "AJAZZ AKP05E (test)";
    d.codename = "akp05e";
    d.keyCount = 10;
    d.gridColumns = 5;
    d.encoderCount = 4;
    d.hasTouchStrip = true;
    d.keyRows = 2;
    return d;
}

core::DeviceId makeDeviceId() {
    return core::DeviceId{0x0300, 0x3004, "TEST-16-01"};
}

void drainQueue() {
    QCoreApplication::processEvents();
    QCoreApplication::processEvents();
}

std::shared_ptr<tests::FakeStreamDockDevice> makeFake() {
    return std::make_shared<tests::FakeStreamDockDevice>(makeDescriptor(), makeDeviceId());
}

} // namespace

TEST_CASE("StreamDockControlService: setBrightness forwards to the active device (DISPLAY-09)",
          "[stream-dock-controls][DISPLAY-09]") {
    ajazz::tests::qtApp();
    auto fake = makeFake();

    app::StreamDockControlService svc(
        [fake](QString const&) -> std::shared_ptr<core::IDevice> { return fake; }, nullptr);

    svc.setActiveDevice(QStringLiteral("akp05e"));
    drainQueue();
    svc.setBrightness(QStringLiteral("akp05e"), 42);
    drainQueue();

    REQUIRE_FALSE(fake->brightnessCalls.empty());
    CHECK(fake->brightnessCalls.back() == 42);
}

TEST_CASE("StreamDockControlService: clearAll clears every key on the active device (DISPLAY-09)",
          "[stream-dock-controls][DISPLAY-09]") {
    ajazz::tests::qtApp();
    auto fake = makeFake();

    app::StreamDockControlService svc(
        [fake](QString const&) -> std::shared_ptr<core::IDevice> { return fake; }, nullptr);

    svc.setActiveDevice(QStringLiteral("akp05e"));
    drainQueue();
    svc.clearAll(QStringLiteral("akp05e"));
    drainQueue();

    // clearAll maps to a broadcast clear (key index 0xFF) on IDisplayCapable.
    REQUIRE_FALSE(fake->clearedKeys.empty());
    CHECK(fake->clearedKeys.back() == 0xFF);
}

TEST_CASE("StreamDockControlService: missing device makes brightness/clear safe no-ops",
          "[stream-dock-controls][DISPLAY-09]") {
    ajazz::tests::qtApp();

    app::StreamDockControlService svc(
        [](QString const&) -> std::shared_ptr<core::IDevice> { return nullptr; }, nullptr);

    CHECK_NOTHROW(svc.setBrightness(QStringLiteral("unknown"), 50));
    drainQueue();
    CHECK_NOTHROW(svc.clearAll(QStringLiteral("unknown")));
    drainQueue();

    svc.setActiveDevice(QStringLiteral("akp05e")); // lookup still returns nullptr
    drainQueue();
    CHECK_NOTHROW(svc.setBrightness(QStringLiteral("akp05e"), 50));
    CHECK_NOTHROW(svc.clearAll(QStringLiteral("akp05e")));
    drainQueue();
}
