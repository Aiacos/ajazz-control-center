// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_stream_dock_input_service.cpp
 * @brief Hardware-free dispatch assertions for StreamDockInputService.
 *
 * Gating proof for INPUT-03 (key press/release dispatch), INPUT-04 (encoder
 * CW/CCW/press + rotation coalescer + synthetic release), and INPUT-05
 * (touch tap zone routing + swipe page-nav intent).
 *
 * experiment/mirajazz: migrated off MockTransport::enqueueRead + the C++ AKP05
 * decode (akp05::parseInputReport, removed) onto the in-process
 * FakeStreamDockDevice. Input now arrives the way the sidecar delivers it — via
 * the device's onEvent callback — so tests drive `fake->injectEvent(DeviceEvent)`
 * directly. The wire-frame decode coverage moved to the streamdock-host sidecar;
 * dispatch() maps DeviceEvent::index straight to the profile binding, so we
 * inject the same indices the profile binds.
 *
 * Test name convention: ASCII-only titles; tag [stream-dock-input].
 */
#include "ajazz/core/action_engine.hpp"
#include "ajazz/core/device.hpp"
#include "ajazz/core/profile.hpp"
#include "fixtures/fake_stream_dock_device.hpp"
#include "qt_app_fixture.hpp"
#include "qt_executor.hpp"
#include "stream_dock_input_service.hpp"

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QEventLoop>
#include <QObject>
#include <QSignalSpy>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>

#include <catch2/catch_test_macros.hpp>

using namespace ajazz;
using namespace ajazz::app;
using namespace ajazz::core;

namespace {

core::DeviceDescriptor makeDescriptor() {
    core::DeviceDescriptor d{};
    d.vendorId = 0x0300;
    d.productId = 0x3004;
    d.family = core::DeviceFamily::StreamDeck;
    d.model = "AKP05E (test)";
    d.codename = "akp05e_test";
    d.encoderCount = 4;
    d.hasTouchStrip = true;
    d.touchZoneCount = 4;
    return d;
}

core::DeviceId makeDeviceId() {
    return core::DeviceId{0x0300, 0x3004, "TEST-INPUT-SERVICE"};
}

std::shared_ptr<tests::FakeStreamDockDevice> makeFake() {
    return std::make_shared<tests::FakeStreamDockDevice>(makeDescriptor(), makeDeviceId());
}

DeviceEvent ev(DeviceEvent::Kind kind, std::uint16_t index, std::int32_t value) {
    return DeviceEvent{kind, index, value};
}

/// Spin the Qt event loop until `pred()` returns true, or `budget` elapses.
template <typename Pred>
bool pumpUntil(Pred&& pred, std::chrono::milliseconds budget) {
    QDeadlineTimer deadline(budget);
    while (!pred() && !deadline.hasExpired()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents,
                                        static_cast<int>(deadline.remainingTime()));
    }
    return pred();
}

} // namespace

TEST_CASE("INPUT-03: key press fires onPress chain via ActionEngine", "[stream-dock-input]") {
    ajazz::tests::qtApp();
    auto fake = makeFake();

    int keyPressCount = 0;
    int runCommandCount = 0;
    ActionExecutors spies;
    spies.keyPress = [&](std::string_view) { ++keyPressCount; };
    spies.runCommand = [&](std::string_view) { ++runCommandCount; };
    auto engine = std::make_unique<ActionEngine>(std::move(spies));

    Profile prof;
    prof.keys[3].onPress = {Action{.kind = ActionKind::KeyPress}};
    prof.keys[3].onRelease = {Action{.kind = ActionKind::RunCommand}};

    StreamDockInputService svc(
        [&]() -> Profile const& { return prof; }, std::move(engine), nullptr);
    svc.setActiveDevice(fake);

    SECTION("press fires onPress (keyPress), not onRelease") {
        fake->injectEvent(ev(DeviceEvent::Kind::KeyPressed, 3, 1));
        REQUIRE(keyPressCount == 1);
        REQUIRE(runCommandCount == 0);
    }
    SECTION("release fires onRelease (runCommand), not onPress") {
        fake->injectEvent(ev(DeviceEvent::Kind::KeyReleased, 3, 0));
        REQUIRE(runCommandCount == 1);
        REQUIRE(keyPressCount == 0);
    }
}

TEST_CASE("INPUT-04a: encoder CW fires onCw, CCW fires onCcw (distinct)", "[stream-dock-input]") {
    ajazz::tests::qtApp();
    auto fake = makeFake();

    int cwCount = 0;
    int urlCount = 0;
    ActionExecutors spies;
    spies.keyPress = [&](std::string_view) { ++cwCount; };
    spies.openUrl = [&](std::string_view) { ++urlCount; };
    auto engine = std::make_unique<ActionEngine>(std::move(spies));

    Profile prof;
    prof.encoders[0].onCw = {Action{.kind = ActionKind::KeyPress}};
    prof.encoders[0].onCcw = {Action{.kind = ActionKind::OpenUrl}};

    StreamDockInputService svc(
        [&]() -> Profile const& { return prof; }, std::move(engine), nullptr);
    svc.setActiveDevice(fake);

    SECTION("CW fires onCw after coalescer drains") {
        fake->injectEvent(ev(DeviceEvent::Kind::EncoderTurned, 0, +1));
        REQUIRE(pumpUntil([&] { return cwCount > 0; }, std::chrono::milliseconds{200}));
        REQUIRE(cwCount == 1);
        REQUIRE(urlCount == 0);
    }
    SECTION("CCW fires onCcw after coalescer drains") {
        fake->injectEvent(ev(DeviceEvent::Kind::EncoderTurned, 0, -1));
        REQUIRE(pumpUntil([&] { return urlCount > 0; }, std::chrono::milliseconds{200}));
        REQUIRE(urlCount == 1);
        REQUIRE(cwCount == 0);
    }
}

TEST_CASE("INPUT-04c: 5 rapid CW ticks coalesce to ONE onCw dispatch", "[stream-dock-input]") {
    ajazz::tests::qtApp();
    auto fake = makeFake();

    int cwCount = 0;
    ActionExecutors spies;
    spies.keyPress = [&](std::string_view) { ++cwCount; };
    auto engine = std::make_unique<ActionEngine>(std::move(spies));

    Profile prof;
    prof.encoders[0].onCw = {Action{.kind = ActionKind::KeyPress}};

    StreamDockInputService svc(
        [&]() -> Profile const& { return prof; }, std::move(engine), nullptr);
    svc.setActiveDevice(fake);

    for (int i = 0; i < 5; ++i) {
        fake->injectEvent(ev(DeviceEvent::Kind::EncoderTurned, 0, +1));
    }
    REQUIRE(pumpUntil([&] { return cwCount > 0; }, std::chrono::milliseconds{300}));
    REQUIRE(cwCount == 1); // 16 ms coalescer collapses the burst to one dispatch
}

TEST_CASE("INPUT-04b: encoder press fires onPress and synthesises paired release",
          "[stream-dock-input]") {
    ajazz::tests::qtApp();
    auto fake = makeFake();

    int onPressCount = 0;
    int syntheticRelCount = 0;
    ActionExecutors spies;
    spies.keyPress = [&](std::string_view) { ++onPressCount; };
    auto engine = std::make_unique<ActionEngine>(std::move(spies));

    Profile prof;
    prof.encoders[1].onPress = {Action{.kind = ActionKind::KeyPress}};

    StreamDockInputService svc(
        [&]() -> Profile const& { return prof; }, std::move(engine), nullptr);
    QObject::connect(
        &svc, &StreamDockInputService::encoderReleaseSynthesised, [&](std::uint16_t idx) {
            if (idx == 1) {
                ++syntheticRelCount;
            }
        });
    svc.setActiveDevice(fake);

    fake->injectEvent(ev(DeviceEvent::Kind::EncoderPressed, 1, 1));

    REQUIRE(onPressCount == 1);
    REQUIRE(syntheticRelCount == 1); // host synthesises release without a wire frame
}

TEST_CASE("INPUT-05a: touch tap at X=140 routes to encoder 2 onPress (zone 140*4/256==2)",
          "[stream-dock-input]") {
    ajazz::tests::qtApp();
    auto fake = makeFake();

    int openUrlCount = 0;
    ActionExecutors spies;
    spies.openUrl = [&](std::string_view) { ++openUrlCount; };
    auto engine = std::make_unique<ActionEngine>(std::move(spies));

    Profile prof;
    prof.encoders[2].onPress = {Action{.kind = ActionKind::OpenUrl}};

    StreamDockInputService svc(
        [&]() -> Profile const& { return prof; }, std::move(engine), nullptr);
    svc.setActiveDevice(fake);

    // A tap is a down->up pair with a small X delta; X=140 -> zone 2.
    fake->injectEvent(ev(DeviceEvent::Kind::TouchDown, 0, 140));
    fake->injectEvent(ev(DeviceEvent::Kind::TouchUp, 0, 140));

    REQUIRE(openUrlCount == 1);
}

TEST_CASE("INPUT-05a: zoneForX helper maps correctly and is marked PROVISIONAL",
          "[stream-dock-input]") {
    REQUIRE(StreamDockInputService::zoneForX(0) == 0);
    REQUIRE(StreamDockInputService::zoneForX(63) == 0);
    REQUIRE(StreamDockInputService::zoneForX(64) == 1);
    REQUIRE(StreamDockInputService::zoneForX(140) == 2);
    REQUIRE(StreamDockInputService::zoneForX(192) == 3);
    REQUIRE(StreamDockInputService::zoneForX(255) == 3); // clamp to EncoderCount-1
}

TEST_CASE("INPUT-05b: a down->up X delta swipe emits pageNavRequested(-1/+1)",
          "[stream-dock-input]") {
    ajazz::tests::qtApp();
    auto fake = makeFake();

    ActionExecutors spies;
    auto engine = std::make_unique<ActionEngine>(std::move(spies));
    Profile prof;

    StreamDockInputService svc(
        [&]() -> Profile const& { return prof; }, std::move(engine), nullptr);
    svc.setActiveDevice(fake);

    QSignalSpy spy(&svc, &StreamDockInputService::pageNavRequested);

    SECTION("decreasing X (down 200 -> up 150) emits pageNavRequested(-1)") {
        fake->injectEvent(ev(DeviceEvent::Kind::TouchDown, 0, 200));
        fake->injectEvent(ev(DeviceEvent::Kind::TouchUp, 0, 150));
        REQUIRE(spy.count() == 1);
        REQUIRE(spy.at(0).at(0).toInt() == -1);
    }
    SECTION("increasing X (down 100 -> up 160) emits pageNavRequested(+1)") {
        fake->injectEvent(ev(DeviceEvent::Kind::TouchDown, 0, 100));
        fake->injectEvent(ev(DeviceEvent::Kind::TouchUp, 0, 160));
        REQUIRE(spy.count() == 1);
        REQUIRE(spy.at(0).at(0).toInt() == +1);
    }
}

TEST_CASE("Sleep non-block: chain with leading Sleep defers KeyPress via QtExecutor",
          "[stream-dock-input]") {
    ajazz::tests::qtApp();
    auto fake = makeFake();

    std::atomic<int> keyPressCount{0};
    ActionExecutors spies;
    spies.keyPress = [&](std::string_view) { ++keyPressCount; };
    auto qtExec = std::make_shared<QtExecutor>();
    auto engine = std::make_unique<ActionEngine>(std::move(spies), qtExec);

    Profile prof;
    prof.keys[1].onPress = {
        Action{.kind = ActionKind::Sleep, .delayMs = 50},
        Action{.kind = ActionKind::KeyPress},
    };

    StreamDockInputService svc(
        [&]() -> Profile const& { return prof; }, std::move(engine), nullptr);
    svc.setActiveDevice(fake);

    fake->injectEvent(ev(DeviceEvent::Kind::KeyPressed, 1, 1));

    // The leading Sleep defers the KeyPress via QtExecutor -- it has not fired yet.
    REQUIRE(keyPressCount.load() == 0);
    REQUIRE(pumpUntil([&] { return keyPressCount.load() > 0; }, std::chrono::milliseconds{500}));
    REQUIRE(keyPressCount.load() == 1);
}

TEST_CASE("Held handle: the input service neither opens nor closes the device",
          "[stream-dock-input]") {
    ajazz::tests::qtApp();
    auto fake = makeFake();
    fake->open(); // the control service owns open/close; pre-open here

    ActionExecutors spies;
    auto engine = std::make_unique<ActionEngine>(std::move(spies));
    Profile prof;

    StreamDockInputService svc(
        [&]() -> Profile const& { return prof; }, std::move(engine), nullptr);
    svc.setActiveDevice(fake);

    svc.pump();
    svc.pump();

    // ARCH-03 single-handle invariant: the input service must not (re)open or
    // close the device — that is StreamDockControlService's responsibility.
    CHECK(fake->openCount == 1);
    CHECK(fake->closeCount == 0);
    CHECK(fake->isOpen());
}

TEST_CASE("IN-01: setActiveDevice(nullptr) stops dispatch; the input service does not close",
          "[stream-dock-input]") {
    ajazz::tests::qtApp();
    auto fake = makeFake();
    fake->open();

    int keyPressCount = 0;
    ActionExecutors spies;
    spies.keyPress = [&](std::string_view) { ++keyPressCount; };
    auto engine = std::make_unique<ActionEngine>(std::move(spies));

    Profile prof;
    prof.keys[1].onPress = {Action{.kind = ActionKind::KeyPress}};

    StreamDockInputService svc(
        [&]() -> Profile const& { return prof; }, std::move(engine), nullptr);

    SECTION("injected event dispatches while device is active") {
        svc.setActiveDevice(fake);
        fake->injectEvent(ev(DeviceEvent::Kind::KeyPressed, 1, 1));
        REQUIRE(keyPressCount == 1);
    }

    SECTION("after setActiveDevice(nullptr) the callback is cleared and nothing dispatches") {
        svc.setActiveDevice(fake);
        fake->injectEvent(ev(DeviceEvent::Kind::KeyPressed, 1, 1));
        REQUIRE(keyPressCount == 1);

        svc.setActiveDevice(nullptr); // device removal

        fake->injectEvent(ev(DeviceEvent::Kind::KeyPressed, 1, 1));
        REQUIRE(keyPressCount == 1); // still 1 — callback was deregistered
    }

    SECTION("setActiveDevice(nullptr) does not close the device (control service owns that)") {
        svc.setActiveDevice(fake);
        svc.setActiveDevice(nullptr);
        CHECK(fake->closeCount == 0);
        CHECK(fake->isOpen());
    }
}
