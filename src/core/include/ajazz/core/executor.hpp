// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file executor.hpp
 * @brief Pluggable scheduling abstraction for delayed @ref ActionEngine work.
 *
 * The action engine used to call `std::this_thread::sleep_for` directly when
 * it hit a `Sleep` action step, blocking whatever thread `run()` was invoked
 * on (typically the HID poll thread or the Qt main thread). The
 * @ref Executor interface lets the app layer inject a Qt-backed executor
 * (`QtExecutor`, see `src/app/`) that hands the continuation off to a
 * `QTimer::singleShot` posted to a worker so the caller's thread is freed.
 *
 * Core stays Qt-free — the abstraction is plain C++ — and headless / test
 * callers can keep the legacy "sleep on the calling thread" semantics by
 * using @ref BlockingExecutor (the default if no executor is injected).
 */
#pragma once

#include <chrono>
#include <functional>
#include <memory>

namespace ajazz::core {

/**
 * @brief Schedules deferred work without blocking the calling thread.
 *
 * Implementations must guarantee that @ref scheduleAfter does not invoke
 * `task` synchronously on the caller — it must hand the task off to a
 * worker (timer, thread pool, event loop) and return immediately. The only
 * exception is @ref BlockingExecutor, kept for backward compatibility with
 * callers (and tests) that explicitly want the legacy in-place sleep.
 *
 * Threading: implementations are responsible for documenting which thread
 * `task` runs on; callers must capture state by value or by shared
 * ownership and not assume any particular execution context.
 */
class Executor {
public:
    virtual ~Executor() = default;

    /**
     * @brief Schedule @p task to run after @p delay.
     *
     * @param delay Lower bound on how long the executor waits before
     *              invoking @p task. A zero (or negative) delay means
     *              "as soon as the worker can run it".
     * @param task  Callable to execute. Must not throw; implementations
     *              may swallow or terminate on exceptions.
     */
    virtual void scheduleAfter(std::chrono::milliseconds delay,
                               std::function<void()> task) noexcept = 0;
};

/**
 * @brief Trivial executor that runs @p task on the calling thread after a
 *        synchronous `std::this_thread::sleep_for(delay)`.
 *
 * This preserves the historical behaviour of @ref ActionEngine (block the
 * calling thread for the requested duration, then continue inline). It is
 * the default when no executor is injected, so existing call sites and
 * unit tests keep their semantics.
 *
 * @warning Do **not** use this from the HID poll thread or the Qt main
 *          thread in production — the whole point of A2 was to stop doing
 *          that. Inject a non-blocking executor (e.g. `QtExecutor`) at
 *          construction time instead.
 */
class BlockingExecutor final : public Executor {
public:
    /// @copydoc Executor::scheduleAfter
    void scheduleAfter(std::chrono::milliseconds delay,
                       std::function<void()> task) noexcept override;
};

/**
 * @brief Convenience factory: a process-wide shared @ref BlockingExecutor.
 *
 * Returned by-value as a `shared_ptr` so callers can stash it without
 * worrying about lifetime. Reused across @ref ActionEngine instances that
 * don't supply their own executor.
 */
[[nodiscard]] std::shared_ptr<Executor> defaultExecutor();

} // namespace ajazz::core
