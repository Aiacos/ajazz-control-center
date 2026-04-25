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
#include "ajazz/core/executor.hpp"

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <utility>
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

namespace {

/// Test double: queues continuations instead of running them, so the test
/// can assert that `run()` returned to its caller before the chain finished
/// (i.e. the calling thread was *not* blocked by Sleep).
class FakeAsyncExecutor final : public ajazz::core::Executor {
public:
    struct Pending {
        std::chrono::milliseconds delay;
        std::function<void()> task;
    };

    void scheduleAfter(std::chrono::milliseconds delay,
                       std::function<void()> task) noexcept override {
        pending.push_back({delay, std::move(task)});
    }

    /// Drain queued continuations. Continuations may schedule further
    /// continuations, hence the loop.
    void drain() {
        while (!pending.empty()) {
            auto next = std::move(pending.front());
            pending.erase(pending.begin());
            if (next.task) {
                next.task();
            }
        }
    }

    std::vector<Pending> pending;
};

} // namespace

TEST_CASE("ActionEngine BlockingExecutor runs continuation on caller",
          "[action_engine][executor]") {
    RecordingExecutors rec;
    auto executor = std::make_shared<ajazz::core::BlockingExecutor>();
    ActionEngine engine(rec.make(), executor);
    engine.setProfile(Profile{});

    ActionChain chain{
        Action{.kind = ActionKind::Plugin, .id = "before"},
        Action{.kind = ActionKind::Sleep, .delayMs = 1},
        Action{.kind = ActionKind::Plugin, .id = "after"},
    };
    engine.run(chain);

    REQUIRE(rec.log.size() == 3);
    REQUIRE(rec.log[0] == "plugin:before:");
    REQUIRE(rec.log[1] == "sleep:1");
    REQUIRE(rec.log[2] == "plugin:after:");
}

TEST_CASE("ActionEngine async executor defers continuation off caller",
          "[action_engine][executor]") {
    RecordingExecutors rec;
    auto executor = std::make_shared<FakeAsyncExecutor>();
    ActionEngine engine(rec.make(), executor);
    engine.setProfile(Profile{});

    ActionChain chain{
        Action{.kind = ActionKind::Plugin, .id = "before"},
        Action{.kind = ActionKind::Sleep, .delayMs = 250},
        Action{.kind = ActionKind::Plugin, .id = "after"},
    };
    engine.run(chain);

    // run() must return without blocking; only the pre-Sleep step has
    // executed, the rest is queued on the executor.
    REQUIRE(rec.log.size() == 2);
    REQUIRE(rec.log[0] == "plugin:before:");
    REQUIRE(rec.log[1] == "sleep:250");
    REQUIRE(executor->pending.size() == 1);
    REQUIRE(executor->pending[0].delay == std::chrono::milliseconds{250});

    executor->drain();

    REQUIRE(rec.log.size() == 3);
    REQUIRE(rec.log[2] == "plugin:after:");
    REQUIRE(executor->pending.empty());
}

TEST_CASE("ActionEngine post-step delayMs also defers via executor", "[action_engine][executor]") {
    RecordingExecutors rec;
    auto executor = std::make_shared<FakeAsyncExecutor>();
    ActionEngine engine(rec.make(), executor);
    engine.setProfile(Profile{});

    // KeyPress with a non-zero post-step delayMs: must not block, must
    // schedule the continuation on the executor.
    ActionChain chain{
        Action{.kind = ActionKind::KeyPress, .settingsJson = "F1", .delayMs = 50},
        Action{.kind = ActionKind::Plugin, .id = "after"},
    };
    engine.run(chain);

    REQUIRE(rec.log.size() == 2);
    REQUIRE(rec.log[0] == "key:F1");
    REQUIRE(rec.log[1] == "sleep:50");
    REQUIRE(executor->pending.size() == 1);

    executor->drain();

    REQUIRE(rec.log.size() == 3);
    REQUIRE(rec.log[2] == "plugin:after:");
}
