// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_profile_pages.cpp
 * @brief StreamDockControlService page-scoped repaint + carousel nav (PROFILE-02).
 *
 * experiment/mirajazz: migrated off the MockTransport wire fixture onto the
 * in-process FakeStreamDockDevice. "Keys painted" is now counted as
 * setKeyImage calls on the fake instead of BAT/ULEND byte bursts (that wire
 * coverage moved to the streamdock-host sidecar). Behaviour under test is
 * unchanged: repaintPage paints exactly the target page's bindings, missing
 * pages no-op, repaintFromProfile == repaintPage("root"), and the carousel
 * page-nav advances/retreats (or no-ops on a single-root profile).
 *
 * Tag: [profile-pages]
 */
#include "ajazz/core/action_engine.hpp"
#include "ajazz/core/capabilities.hpp"
#include "ajazz/core/device.hpp"
#include "ajazz/core/profile.hpp"
#include "fixtures/fake_stream_dock_device.hpp"
#include "qt_app_fixture.hpp"
#include "stream_dock_control_service.hpp"
#include "stream_dock_input_service.hpp"

#include <QCoreApplication>
#include <QObject>
#include <QString>

#include <memory>

#include <catch2/catch_test_macros.hpp>

using namespace ajazz;

namespace {

core::DeviceDescriptor makeDesc() {
    core::DeviceDescriptor d{};
    d.vendorId = 0x0300;
    d.productId = 0x3004;
    d.family = core::DeviceFamily::StreamDeck;
    d.model = "AJAZZ AKP05E (test-16-03)";
    d.codename = "akp05e";
    d.keyCount = 10;
    d.gridColumns = 5;
    d.encoderCount = 4;
    d.hasTouchStrip = true;
    d.keyRows = 2;
    return d;
}

core::DeviceId makeDevId(char const* serial) {
    return core::DeviceId{0x0300, 0x3004, serial};
}

std::shared_ptr<tests::FakeStreamDockDevice> makeFake(char const* serial = "TEST-16-03") {
    return std::make_shared<tests::FakeStreamDockDevice>(makeDesc(), makeDevId(serial));
}

void drainQueue() {
    QCoreApplication::processEvents();
    QCoreApplication::processEvents();
}

/// Build a Profile with:
///   - root: keys[0] red, keys[1] blue
///   - pages["folderA"]: keys[2] green, keys[3] yellow
///   - pages["folderB"]: keys[4] cyan
core::Profile makeMultiPageProfile() {
    core::Profile p;
    p.id = "mp-profile";
    p.name = "Multi-page";
    p.deviceCodename = "akp05e";

    {
        core::Binding b;
        b.state.background = core::Rgb{255, 0, 0};
        p.keys[0] = b;
    }
    {
        core::Binding b;
        b.state.background = core::Rgb{0, 0, 255};
        p.keys[1] = b;
    }
    {
        core::ProfilePage pg;
        pg.id = "folderA";
        pg.name = "Folder A";
        {
            core::Binding b;
            b.state.background = core::Rgb{0, 255, 0};
            pg.keys[2] = b;
        }
        {
            core::Binding b;
            b.state.background = core::Rgb{255, 255, 0};
            pg.keys[3] = b;
        }
        p.pages["folderA"] = std::move(pg);
    }
    {
        core::ProfilePage pg;
        pg.id = "folderB";
        pg.name = "Folder B";
        {
            core::Binding b;
            b.state.background = core::Rgb{0, 255, 255};
            pg.keys[4] = b;
        }
        p.pages["folderB"] = std::move(pg);
    }
    return p;
}

} // namespace

TEST_CASE("StreamDockControlService: repaintPage root paints root bindings (PROFILE-02)",
          "[profile-pages][PROFILE-02]") {
    ajazz::tests::qtApp();
    auto fake = makeFake("TEST-16-03-A");
    auto prof = makeMultiPageProfile();
    app::StreamDockControlService svc(
        [fake](QString const&) -> std::shared_ptr<core::IDevice> { return fake; },
        [&prof]() -> core::Profile const& { return prof; },
        nullptr);
    svc.setActiveDevice(QStringLiteral("akp05e"));
    drainQueue();
    auto const baseline = fake->keyImages.size();

    svc.repaintPage(QStringLiteral("root"));
    drainQueue();

    // 2 root keys -> 2 setKeyImage calls.
    CHECK(fake->keyImages.size() - baseline == 2);
}

TEST_CASE(
    "StreamDockControlService: repaintPage child paints child bindings, not root (PROFILE-02)",
    "[profile-pages][PROFILE-02]") {
    ajazz::tests::qtApp();
    auto fake = makeFake("TEST-16-03-B");
    auto prof = makeMultiPageProfile();
    app::StreamDockControlService svc(
        [fake](QString const&) -> std::shared_ptr<core::IDevice> { return fake; },
        [&prof]() -> core::Profile const& { return prof; },
        nullptr);
    svc.setActiveDevice(QStringLiteral("akp05e"));
    drainQueue();
    auto const baseline = fake->keyImages.size();

    svc.repaintPage(QStringLiteral("folderA"));
    drainQueue();

    // Pitfall 1: folderA has exactly 2 keys (2,3). If root were also painted
    // we'd see 4. Must be exactly the child page's key count.
    CHECK(fake->keyImages.size() - baseline == 2);
}

TEST_CASE("StreamDockControlService: repaintPage missing page no-ops cleanly (PROFILE-02)",
          "[profile-pages][PROFILE-02]") {
    ajazz::tests::qtApp();
    auto fake = makeFake("TEST-16-03-C");
    auto prof = makeMultiPageProfile();
    app::StreamDockControlService svc(
        [fake](QString const&) -> std::shared_ptr<core::IDevice> { return fake; },
        [&prof]() -> core::Profile const& { return prof; },
        nullptr);
    svc.setActiveDevice(QStringLiteral("akp05e"));
    drainQueue();
    auto const baseline = fake->keyImages.size();

    REQUIRE_NOTHROW(svc.repaintPage(QStringLiteral("doesNotExist")));
    drainQueue();

    CHECK(fake->keyImages.size() == baseline);
}

TEST_CASE("StreamDockControlService: repaintFromProfile delegates to repaintPage root",
          "[profile-pages][PROFILE-02]") {
    ajazz::tests::qtApp();
    auto fake = makeFake("TEST-16-03-D");
    auto prof = makeMultiPageProfile();
    app::StreamDockControlService svc(
        [fake](QString const&) -> std::shared_ptr<core::IDevice> { return fake; },
        [&prof]() -> core::Profile const& { return prof; },
        nullptr);
    svc.setActiveDevice(QStringLiteral("akp05e"));
    drainQueue();
    auto const baseline = fake->keyImages.size();

    svc.repaintFromProfile();
    drainQueue();

    // repaintFromProfile == repaintPage("root"): 2 root keys.
    CHECK(fake->keyImages.size() - baseline == 2);
}

TEST_CASE(
    "StreamDockControlService: pageNavRequested +1 advances carousel and repaints (PROFILE-02)",
    "[profile-pages][PROFILE-02]") {
    ajazz::tests::qtApp();
    auto fake = makeFake("TEST-16-03-E");
    auto prof = makeMultiPageProfile();
    app::StreamDockControlService svc(
        [fake](QString const&) -> std::shared_ptr<core::IDevice> { return fake; },
        [&prof]() -> core::Profile const& { return prof; },
        nullptr);
    svc.setActiveDevice(QStringLiteral("akp05e"));
    drainQueue();

    auto engine = std::make_unique<core::ActionEngine>();
    engine->setProfile(prof);
    app::StreamDockInputService inputSvc(
        [&prof]() -> core::Profile const& { return prof; }, std::move(engine), nullptr);
    QObject::connect(&inputSvc,
                     &app::StreamDockInputService::pageNavRequested,
                     &svc,
                     &app::StreamDockControlService::navigatePage);
    drainQueue();
    auto const baseline = fake->keyImages.size();

    Q_EMIT inputSvc.pageNavRequested(+1);
    drainQueue();

    // Advancing to the next page paints at least one key.
    CHECK(fake->keyImages.size() > baseline);
}

TEST_CASE("StreamDockControlService: pageNavRequested -1 goes to previous page (PROFILE-02)",
          "[profile-pages][PROFILE-02]") {
    ajazz::tests::qtApp();
    auto fake = makeFake("TEST-16-03-F");
    auto prof = makeMultiPageProfile();
    app::StreamDockControlService svc(
        [fake](QString const&) -> std::shared_ptr<core::IDevice> { return fake; },
        [&prof]() -> core::Profile const& { return prof; },
        nullptr);
    svc.setActiveDevice(QStringLiteral("akp05e"));
    drainQueue();

    auto engine = std::make_unique<core::ActionEngine>();
    engine->setProfile(prof);
    app::StreamDockInputService inputSvc(
        [&prof]() -> core::Profile const& { return prof; }, std::move(engine), nullptr);
    QObject::connect(&inputSvc,
                     &app::StreamDockInputService::pageNavRequested,
                     &svc,
                     &app::StreamDockControlService::navigatePage);
    drainQueue();

    Q_EMIT inputSvc.pageNavRequested(+1);
    drainQueue();
    auto const afterAdvance = fake->keyImages.size();

    Q_EMIT inputSvc.pageNavRequested(-1);
    drainQueue();

    // Going back repaints the previous page.
    CHECK(fake->keyImages.size() > afterAdvance);
}

TEST_CASE(
    "StreamDockControlService: single-root profile makes pageNavRequested a no-op (PROFILE-02)",
    "[profile-pages][PROFILE-02]") {
    ajazz::tests::qtApp();
    auto fake = makeFake("TEST-16-03-G");

    core::Profile singleRoot;
    singleRoot.id = "single-root";
    singleRoot.deviceCodename = "akp05e";
    {
        core::Binding b;
        b.state.background = core::Rgb{128, 0, 128};
        singleRoot.keys[0] = b;
    }

    app::StreamDockControlService svc(
        [fake](QString const&) -> std::shared_ptr<core::IDevice> { return fake; },
        [&singleRoot]() -> core::Profile const& { return singleRoot; },
        nullptr);
    svc.setActiveDevice(QStringLiteral("akp05e"));
    drainQueue();

    auto engine = std::make_unique<core::ActionEngine>();
    engine->setProfile(singleRoot);
    app::StreamDockInputService inputSvc(
        [&singleRoot]() -> core::Profile const& { return singleRoot; }, std::move(engine), nullptr);
    QObject::connect(&inputSvc,
                     &app::StreamDockInputService::pageNavRequested,
                     &svc,
                     &app::StreamDockControlService::navigatePage);
    drainQueue();
    auto const baseline = fake->keyImages.size();

    REQUIRE_NOTHROW(Q_EMIT inputSvc.pageNavRequested(+1));
    drainQueue();
    REQUIRE_NOTHROW(Q_EMIT inputSvc.pageNavRequested(-1));
    drainQueue();

    // Single-root profile: nav is a no-op, no extra paints.
    CHECK(fake->keyImages.size() == baseline);
}
