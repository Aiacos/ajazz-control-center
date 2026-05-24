// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file plugin_crash_tracker.cpp
 * @brief PluginCrashTracker implementation — pure 3-in-30s crash-window logic.
 *
 * Source reference: akp_plugin_sdk.md §3 crash policy ("3 crashes within ~30s -> disable").
 * Pattern: 18-RESEARCH.md Pattern 3 (crash window counter — per-plugin ring of recent
 * timestamps; drop entries older than 30 s; disable at count >= 3).
 *
 * No nlohmann::json (COD-031). No real clock (clock injected via nowMs parameter).
 * Phase: 18-plugin-manifest-discovery-lifecycle-spawn / Plan 18-04 (PLUGIN-07)
 */
#include "plugin_crash_tracker.hpp"

namespace ajazz::app {

void PluginCrashTracker::recordCrash(QString const& uuid, qint64 const nowMs) {
    m_crashes[uuid].append(nowMs);
}

bool PluginCrashTracker::shouldDisable(QString const& uuid, qint64 const nowMs) {
    auto it = m_crashes.find(uuid);
    if (it == m_crashes.end()) {
        return false;
    }

    qint64 const windowStart = nowMs - kWindowMs;

    // Prune entries older than the window to keep the list bounded.
    QList<qint64>& timestamps = it.value();
    timestamps.erase(std::remove_if(timestamps.begin(),
                                    timestamps.end(),
                                    [windowStart](qint64 const t) { return t < windowStart; }),
                     timestamps.end());

    return timestamps.size() >= kDisableThreshold;
}

} // namespace ajazz::app
