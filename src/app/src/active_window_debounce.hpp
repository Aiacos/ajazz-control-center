// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file active_window_debounce.hpp
 * @brief Trailing-edge QTimer debounce shared by every native
 *        @ref ajazz::core::IActiveWindowWatcher backend (Phase 34 APROF-01).
 *
 * Rapid foreground thrash (alt-tab storms, transient focus on layer-shell
 * surfaces) would otherwise drive one profile auto-switch per intermediate
 * window — a self-inflicted DoS (threat T-34-03-01) and a device-repaint storm.
 * This coalesces a burst of @ref submit calls within @c kDebounceMs to a single
 * trailing-edge emit carrying the LAST submitted @ref ActiveWindowInfo, and
 * additionally drops a no-op emit when the settled app id equals the one already
 * delivered (idempotent-switch guard, mirrors CR WR-01).
 *
 * Mirrors the @c HotplugDebouncer trailing-edge QTimer idiom (300ms there; the
 * watcher uses ~180ms — within the 150-250ms target). Lives app-tier (uses
 * QTimer); the core interface stays Qt-free (COD-031).
 *
 * Single-thread (GUI thread): native backends submit from the Qt event loop on
 * the GUI thread, where they were constructed (Pitfall 5).
 */
#pragma once

#if defined(AJAZZ_FEATURE_ACTIVE_WINDOW)

#include "ajazz/core/active_window_watcher.hpp"

#include <QObject>
#include <QTimer>

#include <functional>
#include <string>
#include <utility>

namespace ajazz::core {

/// Trailing-edge debounce window. 180ms sits in the 150-250ms target band.
inline constexpr int kActiveWindowDebounceMs = 180;

/**
 * @brief Coalesce a burst of foreground changes to one trailing-edge emit.
 *
 * Not a QObject itself (it owns one privately) so backends can hold it by value /
 * unique_ptr without MOC. @ref submit restarts the single-shot timer; on timeout
 * it fires the stored callback with the last-submitted info, unless that info's
 * appId equals the last one already emitted (idempotent guard).
 */
class ActiveWindowDebouncer {
public:
    ActiveWindowDebouncer() {
        timer_.setSingleShot(true);
        timer_.setInterval(kActiveWindowDebounceMs);
        QObject::connect(&timer_, &QTimer::timeout, &timer_, [this]() { fire(); });
    }

    /// Register / replace the trailing-edge emit callback. Null disables emits.
    void setCallback(std::function<void(ActiveWindowInfo)> onChange) {
        onChange_ = std::move(onChange);
    }

    /// Submit a foreground change; restarts the trailing-edge window.
    void submit(ActiveWindowInfo info) {
        pending_ = std::move(info);
        hasPending_ = true;
        timer_.start(); // trailing-edge restart
    }

private:
    void fire() {
        if (!hasPending_) {
            return;
        }
        hasPending_ = false;
        // Idempotent-switch guard: do not re-emit the app already delivered
        // (avoids a redundant downstream profile switch / repaint — CR WR-01).
        if (hasLast_ && pending_.appId == lastAppId_) {
            return;
        }
        hasLast_ = true;
        lastAppId_ = pending_.appId;
        if (onChange_) {
            onChange_(pending_);
        }
    }

    QTimer timer_;
    std::function<void(ActiveWindowInfo)> onChange_;
    ActiveWindowInfo pending_;
    std::string lastAppId_;
    bool hasPending_{false};
    bool hasLast_{false};
};

} // namespace ajazz::core

#endif // defined(AJAZZ_FEATURE_ACTIVE_WINDOW)
