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
 *
 * A2 (threading): Sleep steps and non-zero `delayMs` post-step waits are
 * deferred via the injected @ref Executor. The engine never calls
 * `std::this_thread::sleep_for` itself; the legacy in-place sleep lives
 * inside @ref BlockingExecutor and only happens when no async executor is
 * injected.
 */
#include "ajazz/core/action_engine.hpp"

#include "ajazz/core/logger.hpp"

#include <chrono>
#include <memory>
#include <utility>

namespace ajazz::core {

ActionEngine::ActionEngine(ActionExecutors executors, std::shared_ptr<Executor> executor) noexcept
    : executors_{std::move(executors)},
      executor_{executor ? std::move(executor) : defaultExecutor()} {}

void ActionEngine::setProfile(Profile profile) {
    profile_ = std::move(profile);
    nav_.pageStack.clear();
    nav_.pageStack.emplace_back("root");
}

std::string ActionEngine::currentPageId() const {
    return nav_.pageStack.empty() ? std::string{"root"} : nav_.pageStack.back();
}

void ActionEngine::run(ActionChain const& chain) {
    if (chain.empty()) {
        return;
    }
    // Capture the chain in a shared_ptr so the continuation that the
    // executor runs on a worker thread cannot outlive the chain storage.
    auto owned = std::make_shared<ActionChain const>(chain);
    runFrom(owned, 0);
}

void ActionEngine::runFrom(std::shared_ptr<ActionChain const> const& chain, std::size_t index) {
    auto const& steps = *chain;
    for (std::size_t i = index; i < steps.size(); ++i) {
        auto const& step = steps[i];

        switch (step.kind) {
        case ActionKind::Plugin:
            if (executors_.plugin) {
                executors_.plugin(step.id, step.settingsJson);
            }
            break;
        case ActionKind::Sleep: {
            // Telemetry / test hook — the *real* delay happens in the executor.
            if (executors_.sleep) {
                executors_.sleep(std::chrono::milliseconds{step.delayMs});
            }
            // Defer the rest of the chain through the executor so the
            // calling thread (HID poll / Qt main) is not blocked.
            std::shared_ptr<ActionChain const> chainCopy = chain;
            std::size_t const next = i + 1;
            executor_->scheduleAfter(std::chrono::milliseconds{step.delayMs},
                                     [this, chainCopy, next]() { runFrom(chainCopy, next); });
            return;
        }
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

        if (step.delayMs > 0 && step.kind != ActionKind::Sleep) {
            // Post-step delay: same deferral story as Sleep so the calling
            // thread is never blocked.
            if (executors_.sleep) {
                executors_.sleep(std::chrono::milliseconds{step.delayMs});
            }
            std::shared_ptr<ActionChain const> chainCopy = chain;
            std::size_t const next = i + 1;
            executor_->scheduleAfter(std::chrono::milliseconds{step.delayMs},
                                     [this, chainCopy, next]() { runFrom(chainCopy, next); });
            return;
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
