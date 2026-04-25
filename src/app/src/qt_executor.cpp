// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file qt_executor.cpp
 * @brief Implementation of @ref ajazz::app::QtExecutor.
 */
#include "qt_executor.hpp"

#include <QTimer>

#include <utility>

namespace ajazz::app {

QtExecutor::QtExecutor(QObject* parent) : QObject(parent) {}

void QtExecutor::scheduleAfter(std::chrono::milliseconds delay,
                               std::function<void()> task) noexcept {
    if (!task) {
        return;
    }
    // QTimer::singleShot with a context object posts the lambda back to the
    // context's thread via a queued connection. Using `this` as the context
    // ensures the task is dropped if QtExecutor is destroyed before the
    // timer fires (no use-after-free), and that the task runs on this
    // QObject's owning thread (typically the Qt main thread).
    int const ms = (delay.count() > 0) ? static_cast<int>(delay.count()) : 0;
    QTimer::singleShot(ms, this, [task = std::move(task)]() { task(); });
}

} // namespace ajazz::app
