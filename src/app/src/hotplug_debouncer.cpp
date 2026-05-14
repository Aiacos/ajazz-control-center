// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file hotplug_debouncer.cpp
 * @brief HotplugDebouncer implementation — per-key trailing-edge QTimer fan-in.
 */
#include "hotplug_debouncer.hpp"

#include "ajazz/core/logger.hpp"

#include <QMetaObject>

namespace ajazz::app {

HotplugDebouncer::HotplugDebouncer(QObject* parent) : QObject(parent) {}

void HotplugDebouncer::observe(core::HotplugEvent const& ev) {
    // Marshal onto the debouncer's owning thread (always the GUI thread
    // per the Application wiring) so QTimer manipulation is single-thread.
    // observe() may be called from the HotplugMonitor worker thread.
    QMetaObject::invokeMethod(
        this,
        [this, ev]() {
            HotplugKey const key{ev.vid, ev.pid, ev.serial};
            // Always overwrite the pending entry — the latest event for
            // a key is what consumers care about. (Burst of A/R/A/R for
            // the same key collapses to the final state.)
            m_pending.insert(key, ev);

            auto const timerIt = m_timers.find(key);
            if (timerIt != m_timers.end()) {
                // Trailing-edge restart — reset the 300ms window.
                timerIt.value()->start(kDebounceMs);
                return;
            }

            // First event for this key — create the timer.
            auto* timer = new QTimer(this);
            timer->setSingleShot(true);
            timer->setInterval(kDebounceMs);
            QObject::connect(timer, &QTimer::timeout, this, [this, key]() {
                // Fetch + remove the pending entry atomically (single-thread).
                core::HotplugEvent const pending = m_pending.value(key);
                m_pending.remove(key);

                // Fetch + remove the timer; deleteLater so the
                // timeout slot can return cleanly before the QObject dies.
                if (auto it = m_timers.find(key); it != m_timers.end()) {
                    it.value()->deleteLater();
                    m_timers.erase(it);
                }

                AJAZZ_LOG_INFO("debouncer",
                               "coalesced hot-plug {}: {:04x}:{:04x}",
                               pending.action == core::HotplugAction::Arrived ? "+" : "-",
                               static_cast<int>(pending.vid),
                               static_cast<int>(pending.pid));

                emit coalesced(pending);
            });
            m_timers.insert(key, timer);
            timer->start();
        },
        Qt::QueuedConnection);
}

} // namespace ajazz::app
