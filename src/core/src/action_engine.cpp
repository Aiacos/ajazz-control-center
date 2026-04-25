// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file action_engine.cpp
 * @brief Implementation of @ref ajazz::core::ActionEngine.
 *
 * Closes:
 *   - #25 multi-action engine — sequenced chain execution with delay support.
 *   - #28 folders — push/pop ProfilePage on navigation stack.
 *   - #29 encoder dispatch — engine consumes EncoderBinding chains the same
 *     way it consumes Binding chains; `run()` is shape-agnostic.
 */
#include "ajazz/core/action_engine.hpp"

#include "ajazz/core/logger.hpp"

#include <chrono>
#include <thread>
#include <utility>

namespace ajazz::core {

namespace {

/// Fallback sleep helper used when no executor is supplied.
void defaultSleep(std::chrono::milliseconds duration) {
    if (duration.count() > 0) {
        std::this_thread::sleep_for(duration);
    }
}

} // namespace

ActionEngine::ActionEngine(ActionExecutors executors) noexcept : executors_{std::move(executors)} {
    if (!executors_.sleep) {
        executors_.sleep = &defaultSleep;
    }
}

void ActionEngine::setProfile(Profile profile) {
    profile_ = std::move(profile);
    nav_.pageStack.clear();
    nav_.pageStack.emplace_back("root");
}

std::string ActionEngine::currentPageId() const {
    return nav_.pageStack.empty() ? std::string{"root"} : nav_.pageStack.back();
}

void ActionEngine::run(ActionChain const& chain) {
    for (auto const& step : chain) {
        switch (step.kind) {
        case ActionKind::Plugin:
            if (executors_.plugin) {
                executors_.plugin(step.id, step.settingsJson);
            }
            break;
        case ActionKind::Sleep:
            executors_.sleep(std::chrono::milliseconds{step.delayMs});
            // Sleep already consumed the delay — don't double-sleep below.
            continue;
        case ActionKind::KeyPress:
            if (executors_.keyPress) {
                executors_.keyPress(step.settingsJson);
            }
            break;
        case ActionKind::RunCommand:
            if (executors_.runCommand) {
                executors_.runCommand(step.settingsJson);
            }
            break;
        case ActionKind::OpenUrl:
            if (executors_.openUrl) {
                executors_.openUrl(step.id.empty() ? step.settingsJson : step.id);
            }
            break;
        case ActionKind::OpenFolder:
            // Settings JSON is expected to be a tiny object {"target":"<id>"};
            // we intentionally avoid pulling in a JSON parser here — callers
            // can pre-stash the page id in `Action::id` for convenience.
            pushPage(step.id);
            return; // Subsequent steps fire on the new page next event.
        case ActionKind::BackToParent:
            popPage();
            return;
        }

        if (step.delayMs > 0) {
            executors_.sleep(std::chrono::milliseconds{step.delayMs});
        }
    }
}

void ActionEngine::pushPage(std::string_view pageId) {
    if (pageId.empty()) {
        AJAZZ_LOG_WARN("action_engine", "OpenFolder with empty target page id");
        return;
    }
    nav_.pageStack.emplace_back(pageId);
}

void ActionEngine::popPage() {
    if (nav_.pageStack.size() > 1) {
        nav_.pageStack.pop_back();
    }
}

} // namespace ajazz::core
