// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_profile_pages.cpp
 * @brief MockTransport wire-level assertions for Phase 16-03 PROFILE-02:
 *        repaintPage(pageId) page-scoped repaint and the carousel page-nav owner.
 *
 * Plan 16-03 deliverables:
 *   Task 1 - repaintPage("root")  paints root keys (identical to repaintFromProfile).
 *   Task 1 - repaintPage("folderA") paints the CHILD page bindings, NOT root keys (Pitfall 1).
 *   Task 1 - repaintPage("doesNotExist") no-ops cleanly (no throw, no write).
 *   Task 2 - pageNavRequested(+1) on a 2-page profile advances to the next page + repaints.
 *   Task 2 - pageNavRequested(-1) on a 2-page profile goes to the previous page.
 *   Task 2 - single-root profile makes pageNavRequested a no-op (Decision 2).
 *
 * Tag: [profile-pages]
 *
 * Byte conventions (same as test_stream_dock_control_service.cpp):
 *   BAT  (key image):  bytes[5..7] == 'B','A','T'.
 *   ULEND:             bytes[5..9] == 'U','L','E','N','D'.
 */
#include "ajazz/core/capabilities.hpp"
#include "ajazz/core/device.hpp"
#include "ajazz/core/profile.hpp"
#include "ajazz/streamdeck/streamdeck.hpp"
#include "fixtures/mock_transport.hpp"
#include "qt_app_fixture.hpp"
#include "stream_dock_control_service.hpp"
#include "stream_dock_input_service.hpp"

#include <QCoreApplication>
#include <QImage>
#include <QSignalSpy>
#include <QString>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using namespace ajazz;

namespace {

// ---------------------------------------------------------------------------
// Helpers -- identical descriptor/device-id shape as other control-service tests.
// ---------------------------------------------------------------------------

core::DeviceDescriptor makeDesc() {
    core::DeviceDescriptor d{};
    d.vendorId = 0x0300;
    d.productId = 0x3004;
    d.family = core::DeviceFamily::StreamDeck;
    d.model = "AJAZZ AKP05E (test-16-03)";
    d.codename = "akp05e";
    d.keyCount = 10;
    d.encoderCount = 4;
    d.hasTouchStrip = true;
    d.hasClock = false;
    return d;
}

core::DeviceId makeDevId(char const* serial) {
    core::DeviceId id{};
    id.vendorId = 0x0300;
    id.productId = 0x3004;
    id.serial = serial;
    return id;
}

struct Fixture {
    core::DevicePtr device;
    tests::MockTransport* transport; // non-owning observer
};

/// Build a MockTransport-backed AKP05E with a VER feature response queued.
Fixture makeFixture(char const* serial = "TEST-16-03") {
    auto owned = std::make_unique<tests::MockTransport>();
    auto* obs = owned.get();
    std::vector<std::uint8_t> verResponse(20, 0);
    verResponse[0] = 0x01; // report id
    std::string const vstr = "V3.AKP05E.01.007";
    for (std::size_t i = 0; i < vstr.size() && i + 1 < verResponse.size(); ++i) {
        verResponse[i + 1] = static_cast<std::uint8_t>(vstr[i]);
    }
    obs->enqueueReadFeature(std::move(verResponse));
    auto dev = streamdeck::makeAkp05WithTransport(makeDesc(), makeDevId(serial), std::move(owned));
    return Fixture{std::move(dev), obs};
}

/// Drain the QTimer-based write queue.
void drainQueue() {
    QCoreApplication::processEvents();
    QCoreApplication::processEvents();
}

/// Count BAT headers in writes[start..end).
std::size_t
countBat(std::vector<std::vector<std::uint8_t>> const& writes, std::size_t start, std::size_t end) {
    std::size_t n = 0;
    for (std::size_t i = start; i < end && i < writes.size(); ++i) {
        auto const& p = writes[i];
        if (p.size() >= 8 && p[5] == 0x42 && p[6] == 0x41 && p[7] == 0x54) {
            ++n;
        }
    }
    return n;
}

/// Count ULEND commits in writes[start..end).
std::size_t countUlend(std::vector<std::vector<std::uint8_t>> const& writes,
                       std::size_t start,
                       std::size_t end) {
    std::size_t n = 0;
    for (std::size_t i = start; i < end && i < writes.size(); ++i) {
        auto const& p = writes[i];
        if (p.size() >= 10 && p[5] == 0x55 && p[6] == 0x4c && p[7] == 0x45 && p[8] == 0x4e &&
            p[9] == 0x44) {
            ++n;
        }
    }
    return n;
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

    // root keys
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

    // folderA page - uses keys 2 and 3 (distinct from root)
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

    // folderB page - uses key 4
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

// ===========================================================================
// Task 1 — repaintPage("root") paints root keys (same as repaintFromProfile)
// ===========================================================================

TEST_CASE("StreamDockControlService: repaintPage root paints root bindings (PROFILE-02)",
          "[profile-pages][PROFILE-02]") {
    ajazz::tests::qtApp();

    auto fx = makeFixture("TEST-16-03-A");
    auto devPtr = fx.device;
    auto* obs = fx.transport;

    auto prof = makeMultiPageProfile();
    app::StreamDockControlService svc(
        [devPtr](QString const&) -> std::shared_ptr<core::IDevice> { return devPtr; },
        [&prof]() -> core::Profile const& { return prof; },
        nullptr);
    svc.setActiveDevice(QStringLiteral("akp05e"));
    drainQueue();
    auto const baseline = obs->writeCount();

    svc.repaintPage(QStringLiteral("root"));
    drainQueue();

    auto const end = obs->writeCount();
    auto const bat = countBat(obs->writes(), baseline, end);
    auto const uld = countUlend(obs->writes(), baseline, end);

    // 2 root keys -> >= 2 BAT headers + >= 2 ULEND commits
    CHECK(bat >= 2);
    CHECK(uld >= 2);
}

// ===========================================================================
// Task 1 — repaintPage("folderA") paints CHILD keys, NOT root keys (Pitfall 1)
// ===========================================================================

TEST_CASE(
    "StreamDockControlService: repaintPage child paints child bindings, not root (PROFILE-02)",
    "[profile-pages][PROFILE-02]") {
    ajazz::tests::qtApp();

    auto fx = makeFixture("TEST-16-03-B");
    auto devPtr = fx.device;
    auto* obs = fx.transport;

    auto prof = makeMultiPageProfile();
    app::StreamDockControlService svc(
        [devPtr](QString const&) -> std::shared_ptr<core::IDevice> { return devPtr; },
        [&prof]() -> core::Profile const& { return prof; },
        nullptr);
    svc.setActiveDevice(QStringLiteral("akp05e"));
    drainQueue();
    auto const baseline = obs->writeCount();

    svc.repaintPage(QStringLiteral("folderA"));
    drainQueue();

    auto const end = obs->writeCount();
    auto const bat = countBat(obs->writes(), baseline, end);
    auto const uld = countUlend(obs->writes(), baseline, end);

    // folderA has 2 keys (indices 2 and 3) -> >= 2 BAT + >= 2 ULEND
    CHECK(bat >= 2);
    CHECK(uld >= 2);

    // Critical (Pitfall 1): root has 2 keys (0,1), folderA has 2 keys (2,3).
    // Total key paints must be exactly 2 (the child page's keys) — if root were
    // also painted we'd see 4. Counts should match folderA's key count.
    // We know folderA has exactly 2 keys; assert <= 2 distinct BAT bursts.
    CHECK(bat == 2);
}

// ===========================================================================
// Task 1 — repaintPage("doesNotExist") no-ops cleanly (T-16c-01)
// ===========================================================================

TEST_CASE("StreamDockControlService: repaintPage missing page no-ops cleanly (PROFILE-02)",
          "[profile-pages][PROFILE-02]") {
    ajazz::tests::qtApp();

    auto fx = makeFixture("TEST-16-03-C");
    auto devPtr = fx.device;
    auto* obs = fx.transport;

    auto prof = makeMultiPageProfile();
    app::StreamDockControlService svc(
        [devPtr](QString const&) -> std::shared_ptr<core::IDevice> { return devPtr; },
        [&prof]() -> core::Profile const& { return prof; },
        nullptr);
    svc.setActiveDevice(QStringLiteral("akp05e"));
    drainQueue();
    auto const baseline = obs->writeCount();

    // Must not throw, must not write
    REQUIRE_NOTHROW(svc.repaintPage(QStringLiteral("doesNotExist")));
    drainQueue();

    // No new writes produced
    CHECK(obs->writeCount() == baseline);
}

// ===========================================================================
// Task 1 — repaintFromProfile() delegates to repaintPage("root") (single path)
// ===========================================================================

TEST_CASE("StreamDockControlService: repaintFromProfile delegates to repaintPage root",
          "[profile-pages][PROFILE-02]") {
    ajazz::tests::qtApp();

    auto fx = makeFixture("TEST-16-03-D");
    auto devPtr = fx.device;
    auto* obs = fx.transport;

    auto prof = makeMultiPageProfile();
    app::StreamDockControlService svc(
        [devPtr](QString const&) -> std::shared_ptr<core::IDevice> { return devPtr; },
        [&prof]() -> core::Profile const& { return prof; },
        nullptr);
    svc.setActiveDevice(QStringLiteral("akp05e"));
    drainQueue();
    auto const baseline = obs->writeCount();

    svc.repaintFromProfile();
    drainQueue();

    // repaintFromProfile == repaintPage("root"): 2 root keys -> >= 2 BAT + >= 2 ULEND
    auto const end = obs->writeCount();
    auto const bat = countBat(obs->writes(), baseline, end);
    auto const uld = countUlend(obs->writes(), baseline, end);
    CHECK(bat >= 2);
    CHECK(uld >= 2);
}

// ===========================================================================
// Task 2 — pageNavRequested(+1) advances to next page + repaints its bindings
// ===========================================================================

TEST_CASE(
    "StreamDockControlService: pageNavRequested +1 advances carousel and repaints (PROFILE-02)",
    "[profile-pages][PROFILE-02]") {
    ajazz::tests::qtApp();

    auto fx = makeFixture("TEST-16-03-E");
    auto devPtr = fx.device;
    auto* obs = fx.transport;

    auto prof = makeMultiPageProfile();
    app::StreamDockControlService svc(
        [devPtr](QString const&) -> std::shared_ptr<core::IDevice> { return devPtr; },
        [&prof]() -> core::Profile const& { return prof; },
        nullptr);
    svc.setActiveDevice(QStringLiteral("akp05e"));
    drainQueue();

    // Build the input service whose pageNavRequested is the source
    auto engine = std::make_unique<core::ActionEngine>();
    engine->setProfile(prof);
    app::StreamDockInputService inputSvc(
        [&prof]() -> core::Profile const& { return prof; }, std::move(engine), nullptr);

    // Wire pageNavRequested -> navigatePage on the control service
    QObject::connect(&inputSvc,
                     &app::StreamDockInputService::pageNavRequested,
                     &svc,
                     &app::StreamDockControlService::navigatePage);

    // Baseline after initial open + any repaint-on-load
    drainQueue();
    auto const baseline = obs->writeCount();

    // Emit +1 (next page) -- profile has "root" plus at least one other page
    Q_EMIT inputSvc.pageNavRequested(+1);
    drainQueue();

    auto const end = obs->writeCount();
    // Must have produced writes for the new page (at least 1 BAT + ULEND)
    CHECK(obs->writeCount() > baseline);
    // At least 1 key painted
    CHECK(countBat(obs->writes(), baseline, end) >= 1);
    CHECK(countUlend(obs->writes(), baseline, end) >= 1);
}

// ===========================================================================
// Task 2 — pageNavRequested(-1) with current=page[1] returns to page[0]
// ===========================================================================

TEST_CASE("StreamDockControlService: pageNavRequested -1 goes to previous page (PROFILE-02)",
          "[profile-pages][PROFILE-02]") {
    ajazz::tests::qtApp();

    auto fx = makeFixture("TEST-16-03-F");
    auto devPtr = fx.device;
    auto* obs = fx.transport;

    auto prof = makeMultiPageProfile();
    app::StreamDockControlService svc(
        [devPtr](QString const&) -> std::shared_ptr<core::IDevice> { return devPtr; },
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

    // Advance to page 1 first
    Q_EMIT inputSvc.pageNavRequested(+1);
    drainQueue();
    auto const afterAdvance = obs->writeCount();

    // Now go back to page 0
    Q_EMIT inputSvc.pageNavRequested(-1);
    drainQueue();

    auto const end = obs->writeCount();
    // Going back must also produce writes (repaints root page)
    CHECK(obs->writeCount() > afterAdvance);
    CHECK(countBat(obs->writes(), afterAdvance, end) >= 1);
    CHECK(countUlend(obs->writes(), afterAdvance, end) >= 1);
}

// ===========================================================================
// Task 2 — single-root profile: pageNavRequested is a no-op (Decision 2)
// ===========================================================================

TEST_CASE(
    "StreamDockControlService: single-root profile makes pageNavRequested a no-op (PROFILE-02)",
    "[profile-pages][PROFILE-02]") {
    ajazz::tests::qtApp();

    auto fx = makeFixture("TEST-16-03-G");
    auto devPtr = fx.device;
    auto* obs = fx.transport;

    // A profile with only root keys (no pages map entries)
    core::Profile singleRoot;
    singleRoot.id = "single-root";
    singleRoot.deviceCodename = "akp05e";
    {
        core::Binding b;
        b.state.background = core::Rgb{128, 0, 128};
        singleRoot.keys[0] = b;
    }

    app::StreamDockControlService svc(
        [devPtr](QString const&) -> std::shared_ptr<core::IDevice> { return devPtr; },
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

    // Capture write count after any initial paints triggered by wiring
    auto const baseline = obs->writeCount();

    // Both +1 and -1 must be no-ops on a single-root profile
    REQUIRE_NOTHROW(Q_EMIT inputSvc.pageNavRequested(+1));
    drainQueue();
    REQUIRE_NOTHROW(Q_EMIT inputSvc.pageNavRequested(-1));
    drainQueue();

    // No new writes after the no-op swipes
    CHECK(obs->writeCount() == baseline);
}
