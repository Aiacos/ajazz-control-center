// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file plugin_crash_tracker.hpp
 * @brief PluginCrashTracker — pure 3-in-30s crash-window logic (PLUGIN-07).
 *
 * Tracks recent crash timestamps per plugin UUID. When a plugin crashes:
 *   1. Call `recordCrash(uuid, nowMs)` to register the crash.
 *   2. Call `shouldDisable(uuid, nowMs)` to decide restart-vs-disable.
 *
 * The "30 second window" rule (akp_plugin_sdk.md §3):
 *   - If 3 or more recorded crashes for a given UUID fall within the trailing
 *     30 000 ms window ending at `nowMs`, the plugin should be disabled.
 *   - Fewer or slower crashes should trigger a restart.
 *
 * **Clock injection:** `nowMs` is an explicit parameter on both methods.
 * The class NEVER reads a real clock internally — this keeps it fully
 * deterministic and unit-testable with synthetic timestamps (Pattern 3 from
 * 18-RESEARCH.md).
 *
 * **UUID isolation:** crash histories are stored per-UUID. Crashes for one
 * plugin do not influence the disable decision for another.
 *
 * COD-031: Qt-Core only, no nlohmann::json.
 * Phase: 18-plugin-manifest-discovery-lifecycle-spawn / Plan 18-04 (PLUGIN-07)
 */
#pragma once

#include <QHash>
#include <QList>
#include <QString>

namespace ajazz::app {

/**
 * @brief Pure, clock-injected 3-in-30s crash-window tracker.
 *
 * Thread safety: not thread-safe. Must be used from a single thread (the
 * application main thread where QProcess signals are dispatched).
 */
class PluginCrashTracker {
public:
    PluginCrashTracker() = default;

    /**
     * @brief Record a crash event for the given plugin UUID.
     *
     * Appends @p nowMs to the timestamp list for @p uuid. Older entries
     * are pruned lazily (in `shouldDisable`) so the structure does not grow
     * unbounded.
     *
     * @param uuid   Plugin UUID (reverse-DNS).
     * @param nowMs  Current time in milliseconds since epoch (injected; no
     *               real clock is read inside this method).
     */
    void recordCrash(QString const& uuid, qint64 nowMs);

    /**
     * @brief Decide whether @p uuid should be disabled rather than restarted.
     *
     * Returns @c true iff at least 3 recorded crashes for @p uuid fall within
     * the trailing 30 000 ms window `[nowMs - 30000, nowMs]`.
     *
     * Side effect: entries older than the window are pruned so the per-UUID
     * list does not grow unbounded.
     *
     * @param uuid   Plugin UUID to evaluate.
     * @param nowMs  Current time in milliseconds since epoch (injected).
     * @return       @c true -> disable; @c false -> restart is safe.
     */
    [[nodiscard]] bool shouldDisable(QString const& uuid, qint64 nowMs);

private:
    /// Per-UUID list of crash timestamps in milliseconds since epoch.
    QHash<QString, QList<qint64>> m_crashes;

    /// Window width in milliseconds (30 seconds per akp_plugin_sdk.md §3).
    static constexpr qint64 kWindowMs = 30'000;

    /// Crash count that triggers disable within the window.
    static constexpr int kDisableThreshold = 3;
};

} // namespace ajazz::app
