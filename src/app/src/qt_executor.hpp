// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file qt_executor.hpp
 * @brief Qt-backed @ref ajazz::core::Executor used by the GUI app.
 */
#pragma once

#include "ajazz/core/executor.hpp"

#include <QObject>

#include <chrono>
#include <functional>

namespace ajazz::app {

/**
 * @class QtExecutor
 * @brief Non-blocking @ref ajazz::core::Executor backed by `QTimer::singleShot`.
 *
 * `scheduleAfter` posts a single-shot timer that fires on the QObject
 * parent's thread (typically the Qt main thread). The task runs in the Qt
 * event loop, so the calling thread of the original `run()` invocation —
 * the HID poll thread, in particular — is freed immediately. This fulfils
 * audit finding A2: Sleep actions never block HID polling and never freeze
 * the UI even if a chain inserts a multi-second delay.
 *
 * Lifetime: must outlive every @ref ajazz::core::ActionEngine that holds a
 * reference to it. Owned by `Application` in this codebase.
 *
 * @note If the QObject parent is destroyed before a queued task fires, the
 *       single-shot timer is cleaned up by Qt and the task is dropped — no
 *       use-after-free. Callers should not assume tasks always execute.
 */
class QtExecutor final : public QObject, public core::Executor {
    Q_OBJECT
public:
    explicit QtExecutor(QObject* parent = nullptr);

    /// @copydoc ajazz::core::Executor::scheduleAfter
    void scheduleAfter(std::chrono::milliseconds delay,
                       std::function<void()> task) noexcept override;
};

} // namespace ajazz::app
