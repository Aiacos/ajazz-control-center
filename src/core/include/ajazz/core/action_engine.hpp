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
 *   - `Kind::Sleep`         — defers the rest of the chain via @ref Executor.
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
 * Sleep handling (A2): when the engine hits a `Sleep` step it delegates to
 * the injected @ref Executor — the rest of the chain executes inside the
 * scheduled continuation, so the calling thread (typically the HID poll
 * thread or the Qt main thread) is **not** blocked. Headless and test
 * callers get a @ref BlockingExecutor by default, which preserves the
 * legacy in-place sleep semantics.
 *
 * Closes #25 (multi-action engine), #28 (folder navigation), #29 (encoder
 * dispatch contract).
 */
#pragma once

#include "ajazz/core/executor.hpp"
#include "ajazz/core/profile.hpp"

#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>
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
 *
 * @note The optional `sleep` callback predates the @ref Executor injection
 *       point. When provided, it is invoked for *recording* / inspection
 *       purposes (see the test suite); actual deferral of the chain
 *       continuation is delegated to the @ref Executor passed to
 *       @ref ActionEngine. Setting `sleep` does **not** make the engine
 *       sleep on the calling thread.
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
    /// Optional: invoked once per encountered Sleep step for telemetry / test
    /// inspection. The actual delay is provided by the @ref Executor.
    std::function<void(std::chrono::milliseconds duration)> sleep;
};

/**
 * @brief Multi-action engine for chained @ref Action vectors with folder
 *        navigation support.
 *
 * The engine runs non-Sleep steps inline on the calling thread and defers
 * the rest of the chain through an injected @ref Executor whenever it
 * encounters a `Sleep` step. Without an injected executor, a process-wide
 * @ref BlockingExecutor is used so the legacy semantics still hold for
 * tests and headless callers.
 */
class ActionEngine {
public:
    /**
     * @brief Construct with a set of pluggable executors.
     *
     * @param executors  Callbacks for each non-plugin action kind. May be
     *                   default-constructed; missing callbacks are no-ops.
     * @param executor   Continuation scheduler. Defaults to the shared
     *                   @ref BlockingExecutor returned by @ref defaultExecutor.
     *                   The pointer must outlive the engine if non-default.
     */
    explicit ActionEngine(ActionExecutors executors = {},
                          std::shared_ptr<Executor> executor = nullptr) noexcept;

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
     * Sleep steps and non-zero `delayMs` post-step waits are deferred via
     * the injected @ref Executor; in that case @ref run returns to its
     * caller before the chain finishes, and the remainder runs inside the
     * executor's worker thread / event loop. Non-Sleep steps still run
     * inline, so chains without delays complete synchronously.
     *
     * @param chain Chain to execute. Empty chains are a no-op.
     */
    void run(ActionChain const& chain);

    /// Push a child page onto the navigation stack (called by OpenFolder).
    void pushPage(std::string_view pageId);

    /// Pop one page off the navigation stack (called by BackToParent).
    void popPage();

private:
    /**
     * @brief Internal helper: dispatch chain steps starting at @p index.
     *
     * Either runs to completion (no Sleep / delay encountered) or schedules
     * a continuation through the executor and returns.
     */
    void runFrom(std::shared_ptr<ActionChain const> const& chain, std::size_t index);

    ActionExecutors executors_;
    std::shared_ptr<Executor> executor_;
    Profile profile_;
    NavigationContext nav_;
};

} // namespace ajazz::core
