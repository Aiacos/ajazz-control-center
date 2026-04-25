// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_action_engine.cpp
 * @brief Unit tests for the ActionEngine multi-action interpreter.
 *
 * Verifies the contract advertised in action_engine.hpp: chains run in
 * order, plugin/keyPress/runCommand/openUrl callbacks fire with the
 * expected payload, Sleep honours `delayMs`, and OpenFolder/BackToParent
 * mutate the navigation stack as documented.
 *
 * Closes #25, #28, #29 (acceptance tests).
 */
#include "ajazz/core/action_engine.hpp"

#include <chrono>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using namespace ajazz::core;

namespace {

/// Build an executor pack that records every dispatched call into a string log.
struct RecordingExecutors {
    std::vector<std::string> log;

    [[nodiscard]] ActionExecutors make() {
        return ActionExecutors{
            .plugin =
                [this](std::string_view id, std::string_view settings) {
                    log.emplace_back(std::string{"plugin:"} + std::string{id} + ":" +
                                     std::string{settings});
                },
            .keyPress = [this](std::string_view s) { log.emplace_back("key:" + std::string{s}); },
            .runCommand = [this](std::string_view s) { log.emplace_back("cmd:" + std::string{s}); },
            .openUrl = [this](std::string_view s) { log.emplace_back("url:" + std::string{s}); },
            .sleep =
                [this](std::chrono::milliseconds d) {
                    log.emplace_back("sleep:" + std::to_string(d.count()));
                },
        };
    }
};

} // namespace

TEST_CASE("ActionEngine fires chains in order", "[action_engine]") {
    RecordingExecutors rec;
    ActionEngine engine(rec.make());

    Profile p{};
    p.id = "p1";
    p.deviceCodename = "akp153";
    engine.setProfile(std::move(p));

    ActionChain chain{
        Action{.kind = ActionKind::Plugin, .id = "obs.switch", .settingsJson = "scn"},
        Action{.kind = ActionKind::KeyPress, .settingsJson = "F1"},
        Action{.kind = ActionKind::OpenUrl, .id = "https://example.com"},
    };
    engine.run(chain);

    REQUIRE(rec.log.size() == 3);
    REQUIRE(rec.log[0] == "plugin:obs.switch:scn");
    REQUIRE(rec.log[1] == "key:F1");
    REQUIRE(rec.log[2] == "url:https://example.com");
}

TEST_CASE("ActionEngine honours Sleep steps", "[action_engine]") {
    RecordingExecutors rec;
    ActionEngine engine(rec.make());
    engine.setProfile(Profile{});

    ActionChain chain{
        Action{.kind = ActionKind::Sleep, .delayMs = 42},
        Action{.kind = ActionKind::Plugin, .id = "after"},
    };
    engine.run(chain);

    REQUIRE(rec.log.size() == 2);
    REQUIRE(rec.log[0] == "sleep:42");
    REQUIRE(rec.log[1] == "plugin:after:");
}

TEST_CASE("ActionEngine pushes / pops navigation pages", "[action_engine][folders]") {
    ActionEngine engine;
    engine.setProfile(Profile{});

    REQUIRE(engine.currentPageId() == "root");

    engine.run(ActionChain{Action{.kind = ActionKind::OpenFolder, .id = "music"}});
    REQUIRE(engine.currentPageId() == "music");

    engine.run(ActionChain{Action{.kind = ActionKind::OpenFolder, .id = "playlists"}});
    REQUIRE(engine.currentPageId() == "playlists");

    engine.run(ActionChain{Action{.kind = ActionKind::BackToParent}});
    REQUIRE(engine.currentPageId() == "music");

    engine.run(ActionChain{Action{.kind = ActionKind::BackToParent}});
    REQUIRE(engine.currentPageId() == "root");

    // BackToParent at root must not pop past it.
    engine.run(ActionChain{Action{.kind = ActionKind::BackToParent}});
    REQUIRE(engine.currentPageId() == "root");
}
