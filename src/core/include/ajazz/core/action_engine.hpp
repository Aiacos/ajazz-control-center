// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file action_engine.hpp
 * @brief Sequenced execution of @ref ActionChain steps with built-in kinds.
 *
 * The action engine owns a small interpreter loop that walks an
 * @ref ajazz::core::Action vector ("chain") and dispatches each step to the
 * appropriate executor depending on @ref ActionKind:
 *
 *   - `Kind::Plugin`        — forwarded to the plugin host (via callback).
 *   - `Kind::Sleep`         — sleeps `delayMs` milliseconds.
 *   - `Kind::KeyPress`      — synthesises an OS key press (delegated callback).
 *   - `Kind::RunCommand`    — launches a shell command (delegated callback).
 *   - `Kind::OpenUrl`       — opens a URL (delegated callback).
 *   - `Kind::OpenFolder`    — pushes a child @ref ProfilePage on the navigation
 *                              stack so the device renders its keys.
 *   - `Kind::BackToParent`  — pops one page off the navigation stack.
 *
 * The engine is intentionally callback-driven so the core library has zero
 * Qt / OS dependencies. The app layer provides the executor lambdas at
 * construction time.
 *
 * Closes #25 (multi-action engine), #28 (folder navigation), #29 (encoder
 * dispatch contract).
 */
#pragma once

#include "ajazz/core/profile.hpp"

#include <chrono>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace ajazz::core {

/// Convenience alias for an ordered chain of @ref Action steps.
using ActionChain = std::vector<Action>;

/**
 * @brief Lightweight context describing the navigation stack at the time
 *        a chain is invoked.
 *
 * Pages are referenced by id (matching @ref ProfilePage::id). The stack is
 * owned by the engine; callers receive it read-only via @ref currentPageId.
 */
struct NavigationContext {
    std::vector<std::string> pageStack; ///< Front = root, back = active page.
};

/**
 * @brief Pluggable executors for each non-plugin @ref ActionKind.
 *
 * All callbacks are optional; a missing callback turns the corresponding
 * action into a silent no-op (useful in unit tests). Callbacks must be
 * thread-safe when used by a multi-threaded engine.
 */
struct ActionExecutors {
    /// Run a plugin-defined step. `id` is a dotted "<plugin>.<action>" string.
    std::function<void(std::string_view id, std::string_view settingsJson)> plugin;
    /// Synthesise an OS-level key press described by `settingsJson`.
    std::function<void(std::string_view settingsJson)> keyPress;
    /// Run a shell command described by `settingsJson`.
    std::function<void(std::string_view settingsJson)> runCommand;
    /// Open a URL in the default browser.
    std::function<void(std::string_view url)> openUrl;
    /// Block the current chain for `duration` (default: std::this_thread::sleep_for).
    std::function<void(std::chrono::milliseconds duration)> sleep;
};

/**
 * @brief Multi-action engine for chained @ref Action vectors with folder
 *        navigation support.
 *
 * The engine is single-threaded by default; long chains run on the calling
 * thread. Embed in a worker thread / Qt thread pool to avoid blocking the UI.
 */
class ActionEngine {
public:
    /**
     * @brief Construct with a set of pluggable executors.
     *
     * @param executors  Callbacks for each non-plugin action kind. May be
     *                   default-constructed; missing callbacks are no-ops.
     */
    explicit ActionEngine(ActionExecutors executors = {}) noexcept;

    /**
     * @brief Replace the active profile.
     *
     * Resets the navigation stack to the profile's "root" page.
     */
    void setProfile(Profile profile);

    /// @return Const view of the currently-loaded profile.
    [[nodiscard]] Profile const& profile() const noexcept { return profile_; }

    /// @return Const view of the navigation context.
    [[nodiscard]] NavigationContext const& navigation() const noexcept { return nav_; }

    /// @return Id of the page currently shown on the device.
    [[nodiscard]] std::string currentPageId() const;

    /**
     * @brief Walk a chain step-by-step, dispatching each step.
     *
     * Honors `Action::delayMs` (post-step delay) and short-circuits on
     * @ref ActionKind::OpenFolder / @ref ActionKind::BackToParent so
     * subsequent steps execute on the new page.
     *
     * @param chain Chain to execute. Empty chains are a no-op.
     */
    void run(ActionChain const& chain);

    /// Push a child page onto the navigation stack (called by OpenFolder).
    void pushPage(std::string_view pageId);

    /// Pop one page off the navigation stack (called by BackToParent).
    void popPage();

private:
    ActionExecutors executors_;
    Profile profile_;
    NavigationContext nav_;
};

} // namespace ajazz::core
