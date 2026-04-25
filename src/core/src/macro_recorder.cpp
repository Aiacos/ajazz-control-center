// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file macro_recorder.cpp
 * @brief Stub @ref ajazz::core::MacroRecorder implementation.
 *
 * The shipped implementation does NOT grab real OS events; it exists so the
 * UI workflow (toolbar button → start, keystrokes appear, stop → chain
 * appears in the inspector) can be driven end-to-end.
 *
 * The real evdev / CGEventTap / SetWindowsHookExW back-ends are gated
 * behind the build-time option `AJAZZ_FEATURE_MACRO_RECORDER` and live
 * in dedicated translation units (Linux/macOS/Windows) added later.
 *
 * Closes #30 (scaffold).
 */
#include "ajazz/core/macro_recorder.hpp"

#include "ajazz/core/logger.hpp"

#include <chrono>
#include <utility>

namespace ajazz::core {

namespace {

/**
 * @brief Concrete stub recorder used as the default platform implementation.
 *
 * Records the fact that start() / stop() were called, but never produces
 * real events. The resulting chain is always empty.
 */
class StubMacroRecorder final : public MacroRecorder {
public:
    bool start(EventCallback onEvent) override {
        events_.clear();
        onEvent_ = std::move(onEvent);
        startedAt_ = std::chrono::steady_clock::now();
        running_ = true;
        AJAZZ_LOG_INFO("macro_recorder", "stub recorder started");
        return true;
    }

    [[nodiscard]] ActionChain stop() override {
        if (!running_) {
            return {};
        }
        running_ = false;
        ActionChain chain;
        chain.reserve(events_.size());
        for (auto const& e : events_) {
            if (e.kind == MacroEvent::Kind::KeyDown) {
                Action step;
                step.kind = ActionKind::KeyPress;
                step.label = e.label;
                step.delayMs = static_cast<std::uint32_t>(e.timestamp.count());
                chain.push_back(std::move(step));
            }
        }
        AJAZZ_LOG_INFO("macro_recorder", "stub recorder stopped");
        return chain;
    }

    [[nodiscard]] std::vector<MacroEvent> const& rawEvents() const noexcept override {
        return events_;
    }

private:
    std::vector<MacroEvent> events_;
    EventCallback onEvent_;
    std::chrono::steady_clock::time_point startedAt_{};
    bool running_{false};
};

} // namespace

std::unique_ptr<MacroRecorder> makeDefaultMacroRecorder() {
    return std::make_unique<StubMacroRecorder>();
}

} // namespace ajazz::core
