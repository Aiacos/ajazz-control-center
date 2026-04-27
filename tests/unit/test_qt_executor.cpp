// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_qt_executor.cpp
 * @brief Unit tests for @ref ajazz::app::QtExecutor (audit finding A2).
 *
 * Pins the contract that the audit-finding-A2 refactor relies on: the
 * Qt-backed `Executor` defers work to the event loop instead of
 * blocking the caller, runs the task on the parent QObject's thread,
 * silently drops null callables, and drops queued tasks (no
 * use-after-free) if the parent QObject is destroyed before the timer
 * fires. The legacy `BlockingExecutor` semantics stay covered by
 * `test_action_engine.cpp`.
 */
#include "qt_executor.hpp"

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QElapsedTimer>
#include <QObject>
#include <QThread>

#include <atomic>
#include <chrono>

#include <catch2/catch_test_macros.hpp>

using ajazz::app::QtExecutor;

namespace {

/// Defensive against the streamdock / opendeck tests sharing the
/// suite — only the first qtApp() call constructs.
QCoreApplication& qtApp() {
    if (QCoreApplication::instance() == nullptr) {
        static int argc = 0;
        static char* argv[] = {nullptr};
        static QCoreApplication app{argc, argv};
    }
    QCoreApplication::setApplicationName(QStringLiteral("ajazz-control-center-tests"));
    QCoreApplication::setOrganizationName(QStringLiteral("Aiacos"));
    return *QCoreApplication::instance();
}

/// Pump the event loop until @p predicate is true OR @p budget elapses.
/// Returns the predicate's final value. Beats fixed `sleep` calls — the
/// event loop only runs as long as needed and we don't slow down green
/// tests just to be safe on slow runners.
template <typename Predicate>
bool pumpUntil(Predicate&& predicate, std::chrono::milliseconds budget) {
    QDeadlineTimer deadline(budget);
    while (!predicate() && !deadline.hasExpired()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents,
                                        static_cast<int>(deadline.remainingTime()));
    }
    return predicate();
}

} // namespace

TEST_CASE("QtExecutor: scheduled task fires after the requested delay", "[qt-executor]") {
    qtApp();
    QtExecutor exec;
    std::atomic<bool> fired{false};

    QElapsedTimer timer;
    timer.start();
    exec.scheduleAfter(std::chrono::milliseconds{20}, [&fired] { fired = true; });

    // Caller must NOT have been blocked synchronously. If the executor
    // ran the task inline, fired would already be true here.
    REQUIRE_FALSE(fired.load());

    REQUIRE(pumpUntil([&] { return fired.load(); }, std::chrono::milliseconds{500}));
    // Lower bound: the task must wait at least the requested delay.
    // Upper bound: 500 ms is generous for a 20 ms delay even on slow CI.
    REQUIRE(timer.elapsed() >= 15); // QTimer is documented as "at least"
}

TEST_CASE("QtExecutor: zero / negative delay still schedules asynchronously", "[qt-executor]") {
    qtApp();
    QtExecutor exec;
    std::atomic<bool> fired{false};

    exec.scheduleAfter(std::chrono::milliseconds{0}, [&fired] { fired = true; });
    // Even with a zero delay the task must be queued, not run inline.
    REQUIRE_FALSE(fired.load());
    REQUIRE(pumpUntil([&] { return fired.load(); }, std::chrono::milliseconds{500}));

    fired = false;
    exec.scheduleAfter(std::chrono::milliseconds{-50}, [&fired] { fired = true; });
    REQUIRE_FALSE(fired.load());
    REQUIRE(pumpUntil([&] { return fired.load(); }, std::chrono::milliseconds{500}));
}

TEST_CASE("QtExecutor: null task is silently ignored", "[qt-executor]") {
    qtApp();
    QtExecutor exec;
    // No assertion — the test simply must not crash. A previous bug
    // would have invoked `task()` on a null `std::function`, which
    // throws `std::bad_function_call` (a `noexcept`-marked method on
    // an underlying virtual function would `std::terminate`).
    REQUIRE_NOTHROW(exec.scheduleAfter(std::chrono::milliseconds{0}, {}));
    // Pump the loop briefly to make sure no deferred crash sneaks in
    // a follow-up event tick.
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
}

TEST_CASE("QtExecutor: tasks queued before destruction are silently dropped", "[qt-executor]") {
    qtApp();
    std::atomic<bool> fired{false};

    {
        QtExecutor exec;
        // Queue a task with a delay long enough that destruction
        // happens before the timer fires.
        exec.scheduleAfter(std::chrono::milliseconds{200}, [&fired] { fired = true; });
        REQUIRE_FALSE(fired.load());
        // exec goes out of scope here — Qt's `singleShot` with a
        // context object cancels the timer when the context dies.
    }

    // Pump well past the original delay; the task must NOT run.
    QCoreApplication::processEvents(QEventLoop::AllEvents, 400);
    REQUIRE_FALSE(fired.load());
}

TEST_CASE("QtExecutor: task runs on the parent QObject's thread", "[qt-executor]") {
    qtApp();
    QtExecutor exec;
    auto const callerThread = QThread::currentThread();
    std::atomic<QThread*> taskThread{nullptr};

    exec.scheduleAfter(std::chrono::milliseconds{0},
                       [&taskThread] { taskThread = QThread::currentThread(); });

    REQUIRE(
        pumpUntil([&] { return taskThread.load() != nullptr; }, std::chrono::milliseconds{500}));
    // The QtExecutor uses `this` as the context for QTimer::singleShot,
    // so the task fires on the executor's owning thread — which is the
    // QCoreApplication's main thread for our test setup. This pins the
    // A2 contract: HID poll thread can call scheduleAfter and the
    // task runs on the GUI thread, never inline on the HID thread.
    REQUIRE(taskThread.load() == callerThread);
}
