// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_stream_dock_control_service.cpp
 * @brief StreamDockControlService behaviour (DISPLAY-06/07/08/10, DOCK-01/02).
 *
 * experiment/mirajazz: migrated off the MockTransport + makeAkp05WithTransport
 * wire fixture onto the in-process FakeStreamDockDevice. The C++ AKP05 wire
 * layer (BAT/MAI/DRA/ULEND byte framing) is gone — that format coverage now
 * lives in the streamdock-host sidecar (cargo). These tests assert the SERVICE
 * behaviour that remains: it forwards each surface to the right capability
 * (setBrightness / setKeyImage / setMainImage / setEncoderImage /
 * setTouchStripImage), honours range guards, repaints the right bindings, holds
 * one handle, and surfaces the firmware string.
 *
 * Tag: [stream-dock-control]
 */
#include "ajazz/core/capabilities.hpp"
#include "ajazz/core/device.hpp"
#include "ajazz/core/profile.hpp"
#include "fixtures/fake_stream_dock_device.hpp"
#include "qt_app_fixture.hpp"
#include "stream_dock_control_service.hpp"

#include <QCoreApplication>
#include <QImage>
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
    d.touchZoneCount = 4;
    return d;
}

core::DeviceId makeDeviceId() {
    return core::DeviceId{0x0300, 0x3004, "TEST-14-01"};
}

std::shared_ptr<tests::FakeStreamDockDevice> makeFake() {
    auto fake = std::make_shared<tests::FakeStreamDockDevice>(makeDescriptor(), makeDeviceId());
    fake->setFirmwareVersion("V3.AKP05E.01.007");
    return fake;
}

void drainQueue() {
    QCoreApplication::processEvents();
    QCoreApplication::processEvents();
}

QImage solid(int w, int h) {
    QImage img(w, h, QImage::Format_RGBA8888);
    img.fill(qRgba(255, 0, 0, 255));
    return img;
}

} // namespace

TEST_CASE("StreamDockControlService: setActiveDevice issues brightness at open (DISPLAY-06)",
          "[stream-dock-control][DISPLAY-06]") {
    ajazz::tests::qtApp();
    auto fake = makeFake();
    app::StreamDockControlService svc(
        [fake](QString const&) -> std::shared_ptr<core::IDevice> { return fake; }, nullptr);

    svc.setActiveDevice(QStringLiteral("akp05e"));
    drainQueue();

    REQUIRE(fake->openCount == 1);
    REQUIRE_FALSE(fake->brightnessCalls.empty());
    CHECK(fake->brightnessCalls.front() > 0); // panel must light up
}

TEST_CASE("StreamDockControlService: firmwareVersionFor returns the device firmware (DOCK-01)",
          "[stream-dock-control][DOCK-01]") {
    ajazz::tests::qtApp();
    auto fake = makeFake();
    app::StreamDockControlService svc(
        [fake](QString const&) -> std::shared_ptr<core::IDevice> { return fake; }, nullptr);

    svc.setActiveDevice(QStringLiteral("akp05e"));
    CHECK(svc.firmwareVersionFor(QStringLiteral("akp05e")) == QStringLiteral("V3.AKP05E.01.007"));
}

TEST_CASE("StreamDockControlService: assignKeyImage paints the key (DISPLAY-07+DOCK-02)",
          "[stream-dock-control][DISPLAY-07][DOCK-02]") {
    ajazz::tests::qtApp();
    auto fake = makeFake();
    app::StreamDockControlService svc(
        [fake](QString const&) -> std::shared_ptr<core::IDevice> { return fake; }, nullptr);

    svc.setActiveDevice(QStringLiteral("akp05e"));
    drainQueue();
    auto const baseline = fake->keyImages.size();

    svc.assignKeyImage(1, solid(85, 85)); // 1-based key index
    drainQueue();

    REQUIRE(fake->keyImages.size() == baseline + 1);
    CHECK(fake->keyImages.back().index == 1);
}

TEST_CASE("StreamDockControlService: lastKeyImage caches the assigned image per key",
          "[stream-dock-control][state]") {
    ajazz::tests::qtApp();
    auto fake = makeFake();
    app::StreamDockControlService svc(
        [fake](QString const&) -> std::shared_ptr<core::IDevice> { return fake; }, nullptr);

    svc.setActiveDevice(QStringLiteral("akp05e"));

    // No image assigned yet -> null.
    CHECK(svc.lastKeyImage(1).isNull());

    // Assigning caches the source image (synchronously, no drain needed).
    QImage const src = solid(85, 85);
    svc.assignKeyImage(1, src);
    QImage const cached = svc.lastKeyImage(1);
    REQUIRE_FALSE(cached.isNull());
    CHECK(cached.size() == src.size());

    // A different key stays empty.
    CHECK(svc.lastKeyImage(2).isNull());

    // Switching to a DIFFERENT device invalidates the cache (surfaces belong to
    // the previous device).
    svc.setActiveDevice(QStringLiteral("akp03"));
    CHECK(svc.lastKeyImage(1).isNull());
}

TEST_CASE("StreamDockControlService: repaintFromProfile repaints all bound keys (DISPLAY-08)",
          "[stream-dock-control][DISPLAY-08]") {
    ajazz::tests::qtApp();
    auto fake = makeFake();

    core::Profile prof;
    prof.id = "test-profile";
    prof.deviceCodename = "akp05e";
    {
        core::Binding b;
        b.state.background = core::Rgb{255, 0, 0};
        prof.keys[0] = std::move(b);
    }
    {
        core::Binding b;
        b.state.background = core::Rgb{0, 0, 255};
        prof.keys[1] = std::move(b);
    }

    app::StreamDockControlService svc(
        [fake](QString const&) -> std::shared_ptr<core::IDevice> { return fake; },
        [&prof]() -> core::Profile const& { return prof; },
        nullptr);
    svc.setActiveDevice(QStringLiteral("akp05e"));
    drainQueue();
    auto const baseline = fake->keyImages.size();

    svc.repaintFromProfile();
    drainQueue();

    CHECK(fake->keyImages.size() - baseline == 2); // 2 bound keys
}

TEST_CASE("StreamDockControlService: assignMainImage paints the main strip (DISPLAY-10)",
          "[stream-dock-control][DISPLAY-10][aux-surface]") {
    ajazz::tests::qtApp();
    auto fake = makeFake();
    app::StreamDockControlService svc(
        [fake](QString const&) -> std::shared_ptr<core::IDevice> { return fake; }, nullptr);

    svc.setActiveDevice(QStringLiteral("akp05e"));
    drainQueue();
    auto const baseline = fake->mainImages.size();

    svc.assignMainImage(solid(100, 50));
    drainQueue();

    CHECK(fake->mainImages.size() == baseline + 1);
}

TEST_CASE("StreamDockControlService: assignEncoderImage paints the encoder zone (DISPLAY-10)",
          "[stream-dock-control][DISPLAY-10][aux-surface]") {
    ajazz::tests::qtApp();
    auto fake = makeFake();
    app::StreamDockControlService svc(
        [fake](QString const&) -> std::shared_ptr<core::IDevice> { return fake; }, nullptr);

    svc.setActiveDevice(QStringLiteral("akp05e"));
    drainQueue();
    auto const baseline = fake->encoderImages.size();

    svc.assignEncoderImage(2, solid(100, 100)); // 0-based encoder index
    drainQueue();

    REQUIRE(fake->encoderImages.size() == baseline + 1);
    CHECK(fake->encoderImages.back().index == 2);
}

TEST_CASE("StreamDockControlService: assignTouchStripZone paints the zone (DISPLAY-10)",
          "[stream-dock-control][DISPLAY-10][aux-surface]") {
    ajazz::tests::qtApp();
    auto fake = makeFake();
    app::StreamDockControlService svc(
        [fake](QString const&) -> std::shared_ptr<core::IDevice> { return fake; }, nullptr);

    svc.setActiveDevice(QStringLiteral("akp05e"));
    drainQueue();
    auto const baseline = fake->touchStripImages.size();

    svc.assignTouchStripZone(1, solid(100, 100));
    drainQueue();

    REQUIRE(fake->touchStripImages.size() == baseline + 1);
    CHECK(fake->touchStripImages.back().index == 1); // location == zone
}

TEST_CASE("StreamDockControlService: assignEncoderImage index>=4 is a no-op (range guard)",
          "[stream-dock-control][DISPLAY-10][range-guard]") {
    ajazz::tests::qtApp();
    auto fake = makeFake();
    app::StreamDockControlService svc(
        [fake](QString const&) -> std::shared_ptr<core::IDevice> { return fake; }, nullptr);

    svc.setActiveDevice(QStringLiteral("akp05e"));
    drainQueue();
    auto const baseline = fake->encoderImages.size();

    svc.assignEncoderImage(4, solid(100, 100)); // out of range (0..3)
    drainQueue();

    CHECK(fake->encoderImages.size() == baseline);
}

TEST_CASE("StreamDockControlService: assignTouchStripZone zone>=4 is a no-op (range guard)",
          "[stream-dock-control][DISPLAY-10][range-guard]") {
    ajazz::tests::qtApp();
    auto fake = makeFake();
    app::StreamDockControlService svc(
        [fake](QString const&) -> std::shared_ptr<core::IDevice> { return fake; }, nullptr);

    svc.setActiveDevice(QStringLiteral("akp05e"));
    drainQueue();
    auto const baseline = fake->touchStripImages.size();

    svc.assignTouchStripZone(4, solid(100, 100)); // out of range (0..3)
    drainQueue();

    CHECK(fake->touchStripImages.size() == baseline);
}

TEST_CASE("StreamDockControlService: repaintEncodersFromProfile paints a bound encoder zone",
          "[stream-dock-control][DISPLAY-10][repaint-encoders]") {
    ajazz::tests::qtApp();
    auto fake = makeFake();

    core::Profile prof;
    prof.id = "enc-repaint-test";
    prof.deviceCodename = "akp05e";
    {
        core::EncoderBinding eb;
        eb.state.background = core::Rgb{0, 128, 255};
        prof.encoders[1] = std::move(eb);
    }

    app::StreamDockControlService svc(
        [fake](QString const&) -> std::shared_ptr<core::IDevice> { return fake; },
        [&prof]() -> core::Profile const& { return prof; },
        nullptr);
    svc.setActiveDevice(QStringLiteral("akp05e"));
    drainQueue();
    auto const baseline = fake->touchStripImages.size();

    svc.repaintEncodersFromProfile();
    drainQueue();

    // Encoder bindings repaint to the touch-strip zone aligned to the encoder.
    REQUIRE(fake->touchStripImages.size() == baseline + 1);
    CHECK(fake->touchStripImages.back().index == 1);
}

TEST_CASE("StreamDockControlService: repaintEncodersFromProfile paints two bound encoder zones",
          "[stream-dock-control][DISPLAY-10][repaint-encoders]") {
    ajazz::tests::qtApp();
    auto fake = makeFake();

    core::Profile prof;
    prof.id = "enc-two-repaint-test";
    prof.deviceCodename = "akp05e";
    {
        core::EncoderBinding eb0;
        eb0.state.background = core::Rgb{255, 0, 0};
        prof.encoders[0] = std::move(eb0);
        core::EncoderBinding eb2;
        eb2.state.background = core::Rgb{0, 255, 0};
        prof.encoders[2] = std::move(eb2);
    }

    app::StreamDockControlService svc(
        [fake](QString const&) -> std::shared_ptr<core::IDevice> { return fake; },
        [&prof]() -> core::Profile const& { return prof; },
        nullptr);
    svc.setActiveDevice(QStringLiteral("akp05e"));
    drainQueue();
    auto const baseline = fake->touchStripImages.size();

    svc.repaintEncodersFromProfile();
    drainQueue();

    CHECK(fake->touchStripImages.size() - baseline == 2);
}

TEST_CASE("StreamDockControlService: repaintEncodersFromProfile skips an unbound encoder",
          "[stream-dock-control][DISPLAY-10][repaint-encoders]") {
    ajazz::tests::qtApp();
    auto fake = makeFake();

    core::Profile prof;
    prof.id = "enc-unbound-test";
    prof.deviceCodename = "akp05e";
    {
        core::EncoderBinding eb; // neither imagePath nor background -> unbound
        prof.encoders[0] = std::move(eb);
    }

    app::StreamDockControlService svc(
        [fake](QString const&) -> std::shared_ptr<core::IDevice> { return fake; },
        [&prof]() -> core::Profile const& { return prof; },
        nullptr);
    svc.setActiveDevice(QStringLiteral("akp05e"));
    drainQueue();
    auto const baseline = fake->touchStripImages.size();

    svc.repaintEncodersFromProfile();
    drainQueue();

    CHECK(fake->touchStripImages.size() == baseline);
}

TEST_CASE("StreamDockControlService: holds one handle across multiple assigns (pitfall 2)",
          "[stream-dock-control][pitfall-2]") {
    ajazz::tests::qtApp();
    auto fake = makeFake();
    app::StreamDockControlService svc(
        [fake](QString const&) -> std::shared_ptr<core::IDevice> { return fake; }, nullptr);

    svc.setActiveDevice(QStringLiteral("akp05e"));
    drainQueue();
    svc.assignKeyImage(1, solid(85, 85));
    drainQueue();
    svc.assignKeyImage(2, solid(85, 85));
    drainQueue();

    // The device was opened exactly once, then held across both assigns.
    CHECK(fake->openCount == 1);
    CHECK(svc.firmwareVersionFor(QStringLiteral("akp05e")) == QStringLiteral("V3.AKP05E.01.007"));
}
