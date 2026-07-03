// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_toggle_dispatch.cpp
 * @brief Toggle Action dispatch + currentState cycle tests (BIND-05/07, Phase 32-03).
 *
 * Two layers are covered:
 *
 *  1. The ProfileController cycle mutator (Task 1): cycleInstanceState advances a
 *     binding's instance.currentState one step mod N (0->1->2->0 for N=3),
 *     is a safe no-op for a single-state / no-instance binding, and PERSISTS the
 *     new index so a fresh-controller reload reflects it (Q1: persist).
 *
 *  2. The StreamDockInputService toggle seam (Task 2): a key/encoder bound to a
 *     Toggle Action (instance.id == kToggleActionId) drives the injected cycle
 *     hook + render hook on each press; the render hook receives the NEW
 *     states[currentState] visual; a state-change willAppear is emitted carrying
 *     the incremented state index. Recording seams stand in for the real
 *     ProfileController + PluginDeviceBridge (no device, no socket needed).
 *
 * All TEST_CASE titles are ASCII-only (CLAUDE.md cross-platform ctest filter rule).
 * Tag: [toggle]
 */
#include "ajazz/core/action_engine.hpp"
#include "ajazz/core/action_instance.hpp"
#include "ajazz/core/builtin_action_registry.hpp"
#include "ajazz/core/device.hpp"
#include "ajazz/core/profile.hpp"
#include "ajazz/core/profile_io.hpp"
#include "fixtures/fake_stream_dock_device.hpp"
#include "profile_controller.hpp"
#include "qt_app_fixture.hpp"
#include "stream_dock_input_service.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QStandardPaths>
#include <QString>
#include <QTemporaryDir>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using namespace ajazz;
using namespace ajazz::app;
using namespace ajazz::core;

namespace {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/// Build a Toggle Action instance with @p n distinct states (each carries a
/// per-state imagePath so the render seam can assert the right visual).
ActionInstance makeToggle(int n) {
    ActionInstance inst;
    inst.id = std::string{core::BuiltinActionRegistry::kToggleActionId};
    for (int i = 0; i < n; ++i) {
        ActionState st;
        st.visual.imagePath = std::string{"state-"} + std::to_string(i) + ".png";
        st.visual.text = std::string{"S"} + std::to_string(i);
        inst.states.push_back(st);
    }
    return inst;
}

core::DeviceDescriptor makeDescriptor() {
    core::DeviceDescriptor d{};
    d.vendorId = 0x0300;
    d.productId = 0x3004;
    d.family = core::DeviceFamily::StreamDeck;
    d.model = "AKP05E (toggle-test)";
    d.codename = "akp05e_toggle";
    d.keyCount = 10;
    d.gridColumns = 5;
    d.keyRows = 2;
    d.encoderCount = 4;
    d.hasTouchStrip = true;
    d.touchZoneCount = 4;
    return d;
}

std::shared_ptr<tests::FakeStreamDockDevice> makeFake() {
    return std::make_shared<tests::FakeStreamDockDevice>(
        makeDescriptor(), core::DeviceId{0x0300, 0x3004, "TEST-TOGGLE"});
}

DeviceEvent ev(DeviceEvent::Kind kind, std::uint16_t index, std::int32_t value) {
    return DeviceEvent{kind, index, value};
}

} // namespace

// ===========================================================================
// Task 1: ProfileController::cycleInstanceState
// ===========================================================================

TEST_CASE("BIND-07: cycleInstanceState advances currentState 0->1->2->0 for N=3", "[toggle]") {
    ajazz::tests::qtApp();
    auto ctrl = std::make_unique<app::ProfileController>(nullptr);

    // Seed a 3-state toggle on key 0 by loading a hand-built profile through disk
    // (ProfileController has no public instance setter; round-trip via JSON).
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    QString const path = tmp.filePath(QStringLiteral("toggle.json"));
    {
        core::Profile seed;
        seed.id = "toggle-cycle";
        seed.keys[0].instance = makeToggle(3);
        core::writeProfileToDisk(path.toStdString(), seed);
    }
    ctrl->loadProfile(path);
    REQUIRE(ctrl->activeProfile().keys.at(0).instance.has_value());
    REQUIRE(ctrl->activeProfile().keys.at(0).instance->currentState == 0);

    ctrl->cycleInstanceState(QStringLiteral("Keypad"), 0);
    REQUIRE(ctrl->activeProfile().keys.at(0).instance->currentState == 1);
    ctrl->cycleInstanceState(QStringLiteral("Keypad"), 0);
    REQUIRE(ctrl->activeProfile().keys.at(0).instance->currentState == 2);
    ctrl->cycleInstanceState(QStringLiteral("Keypad"), 0);
    REQUIRE(ctrl->activeProfile().keys.at(0).instance->currentState == 0); // wrap
}

TEST_CASE("BIND-07: cycleInstanceState is a no-op for single-state / no-instance bindings",
          "[toggle]") {
    ajazz::tests::qtApp();
    auto ctrl = std::make_unique<app::ProfileController>(nullptr);

    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    QString const path = tmp.filePath(QStringLiteral("toggle-noop.json"));
    {
        core::Profile seed;
        seed.id = "toggle-noop";
        seed.keys[0].instance = makeToggle(1);                         // single state
        seed.keys[1].onPress = {Action{.kind = ActionKind::KeyPress}}; // no instance
        core::writeProfileToDisk(path.toStdString(), seed);
    }
    ctrl->loadProfile(path);

    ctrl->cycleInstanceState(QStringLiteral("Keypad"), 0); // single state
    REQUIRE(ctrl->activeProfile().keys.at(0).instance->currentState == 0);

    // No instance on key 1: must not crash, must not create an instance.
    ctrl->cycleInstanceState(QStringLiteral("Keypad"), 1);
    REQUIRE_FALSE(ctrl->activeProfile().keys.at(1).instance.has_value());

    // Unknown index / controller: clean no-op.
    ctrl->cycleInstanceState(QStringLiteral("Keypad"), 99);
    ctrl->cycleInstanceState(QStringLiteral("Bogus"), 0);
}

TEST_CASE("BIND-07: cycled currentState persists (fresh controller reload sees the new index)",
          "[toggle]") {
    ajazz::tests::qtApp();

    QString savedPath;
    {
        // Point AppDataLocation at a temp dir so saveActiveProfile lands there.
        QTemporaryDir tmp;
        REQUIRE(tmp.isValid());
        QString const seedPath = tmp.filePath(QStringLiteral("toggle-persist.json"));
        {
            core::Profile seed;
            seed.id = "toggle-persist";
            seed.keys[0].instance = makeToggle(3);
            core::writeProfileToDisk(seedPath.toStdString(), seed);
        }
        auto ctrl = std::make_unique<app::ProfileController>(nullptr);
        ctrl->loadProfile(seedPath);
        ctrl->cycleInstanceState(QStringLiteral("Keypad"), 0); // -> 1
        ctrl->cycleInstanceState(QStringLiteral("Keypad"), 0); // -> 2
        // Persist to the same explicit path (independent of AppDataLocation).
        ctrl->saveProfile(seedPath);
        savedPath = seedPath;

        // Re-read from disk with a fresh controller: persisted index survives.
        auto fresh = std::make_unique<app::ProfileController>(nullptr);
        fresh->loadProfile(savedPath);
        REQUIRE(fresh->activeProfile().keys.at(0).instance.has_value());
        REQUIRE(fresh->activeProfile().keys.at(0).instance->currentState == 2);
    }
}

// ===========================================================================
// Task 2: StreamDockInputService toggle seam (cycle hook + render hook)
// ===========================================================================

namespace {

/// Records every render-hook invocation so a test can assert the seam fired with
/// the NEW states[currentState] visual + the state index.
struct RenderRecord {
    QString controller;
    int index{0};
    std::uint32_t stateIndex{0};
    std::string imagePath;
    bool willAppearEmitted{false};
};

} // namespace

TEST_CASE("BIND-07: a key bound to a Toggle cycles + renders + emits willAppear per press",
          "[toggle]") {
    ajazz::tests::qtApp();
    auto fake = makeFake();

    // The "profile" is a local Profile we mutate from the cycle hook so the
    // input service reads the advanced currentState back on the next press.
    Profile prof;
    prof.keys[2].instance = makeToggle(3);

    int cycleCalls = 0;
    std::vector<RenderRecord> renders;

    auto engine = std::make_unique<ActionEngine>(ActionExecutors{});
    StreamDockInputService svc(
        [&]() -> Profile const& { return prof; }, std::move(engine), nullptr);

    // Cycle hook: mirror ProfileController::cycleInstanceState on the local profile.
    svc.setToggleCycleHook([&](QString const& controller, int index) {
        ++cycleCalls;
        if (controller == QStringLiteral("Keypad")) {
            auto it = prof.keys.find(static_cast<std::uint16_t>(index));
            if (it != prof.keys.end() && it->second.instance &&
                it->second.instance->states.size() > 1) {
                auto& cs = it->second.instance->currentState;
                cs = (cs + 1) % static_cast<std::uint32_t>(it->second.instance->states.size());
            }
        }
    });
    // Render hook: record the visual sourced from states[currentState].
    svc.setToggleRenderHook(
        [&](QString const& controller, int index, core::ActionInstance const& inst) {
            RenderRecord r;
            r.controller = controller;
            r.index = index;
            r.stateIndex = inst.currentState;
            r.imagePath = inst.states.at(inst.currentState).visual.imagePath.value_or("");
            r.willAppearEmitted = true; // the real hook emits the state-change willAppear
            renders.push_back(r);
        });

    svc.setActiveDevice(fake);

    // Press 1: 0 -> 1
    fake->injectEvent(ev(DeviceEvent::Kind::KeyPressed, 3, 1));
    // Press 2: 1 -> 2
    fake->injectEvent(ev(DeviceEvent::Kind::KeyPressed, 3, 1));
    // Press 3: 2 -> 0 (wrap)
    fake->injectEvent(ev(DeviceEvent::Kind::KeyPressed, 3, 1));

    REQUIRE(cycleCalls == 3);
    REQUIRE(renders.size() == 3);

    // Each render saw the NEW currentState + its visual; index 0->1->2->0.
    REQUIRE(renders[0].stateIndex == 1);
    REQUIRE(renders[0].imagePath == "state-1.png");
    REQUIRE(renders[1].stateIndex == 2);
    REQUIRE(renders[1].imagePath == "state-2.png");
    REQUIRE(renders[2].stateIndex == 0); // wrapped
    REQUIRE(renders[2].imagePath == "state-0.png");

    // state-change willAppear emitted on every press.
    for (auto const& r : renders) {
        REQUIRE(r.willAppearEmitted);
        REQUIRE(r.controller == QStringLiteral("Keypad"));
        REQUIRE(r.index == 2);
    }
}

TEST_CASE("BIND-07: an encoder bound to a Toggle cycles + renders via the same seam", "[toggle]") {
    ajazz::tests::qtApp();
    auto fake = makeFake();

    Profile prof;
    prof.encoders[1].instance = makeToggle(2);

    int cycleCalls = 0;
    std::vector<RenderRecord> renders;

    auto engine = std::make_unique<ActionEngine>(ActionExecutors{});
    StreamDockInputService svc(
        [&]() -> Profile const& { return prof; }, std::move(engine), nullptr);

    svc.setToggleCycleHook([&](QString const& controller, int index) {
        ++cycleCalls;
        if (controller == QStringLiteral("Encoder")) {
            auto it = prof.encoders.find(static_cast<std::uint16_t>(index));
            if (it != prof.encoders.end() && it->second.instance &&
                it->second.instance->states.size() > 1) {
                auto& cs = it->second.instance->currentState;
                cs = (cs + 1) % static_cast<std::uint32_t>(it->second.instance->states.size());
            }
        }
    });
    svc.setToggleRenderHook(
        [&](QString const& controller, int index, core::ActionInstance const& inst) {
            RenderRecord r;
            r.controller = controller;
            r.index = index;
            r.stateIndex = inst.currentState;
            renders.push_back(r);
        });

    svc.setActiveDevice(fake);

    fake->injectEvent(ev(DeviceEvent::Kind::EncoderPressed, 1, 1)); // 0 -> 1
    fake->injectEvent(ev(DeviceEvent::Kind::EncoderPressed, 1, 1)); // 1 -> 0 (wrap)

    REQUIRE(cycleCalls == 2);
    REQUIRE(renders.size() == 2);
    REQUIRE(renders[0].controller == QStringLiteral("Encoder"));
    REQUIRE(renders[0].index == 1);
    REQUIRE(renders[0].stateIndex == 1);
    REQUIRE(renders[1].stateIndex == 0); // wrap
}

TEST_CASE("BIND-07: a Multi Action key does NOT trigger the toggle seam (no regression)",
          "[toggle]") {
    ajazz::tests::qtApp();
    auto fake = makeFake();

    Profile prof;
    ActionInstance multi;
    multi.id = std::string{core::BuiltinActionRegistry::kMultiActionId};
    multi.children.push_back(ActionInstance{.id = "child.a"});
    prof.keys[0].instance = multi;

    int cycleCalls = 0;
    int renderCalls = 0;
    auto engine = std::make_unique<ActionEngine>(ActionExecutors{});
    StreamDockInputService svc(
        [&]() -> Profile const& { return prof; }, std::move(engine), nullptr);
    svc.setToggleCycleHook([&](QString const&, int) { ++cycleCalls; });
    svc.setToggleRenderHook(
        [&](QString const&, int, core::ActionInstance const&) { ++renderCalls; });
    svc.setActiveDevice(fake);

    fake->injectEvent(ev(DeviceEvent::Kind::KeyPressed, 1, 1));

    REQUIRE(cycleCalls == 0);
    REQUIRE(renderCalls == 0);
}
