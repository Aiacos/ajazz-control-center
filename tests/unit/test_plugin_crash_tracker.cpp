// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_plugin_crash_tracker.cpp
 * @brief Unit tests for PluginCrashTracker — 3-in-30s crash window with injected clock.
 *
 * All cases inject synthetic timestamps; no real clock or QProcess is started.
 * Tag: [plugin-crash] (ctest -R "plugin-crash" to run this suite).
 *
 * Phase: 18-plugin-manifest-discovery-lifecycle-spawn / Plan 18-04 (PLUGIN-07)
 */
#include "plugin_crash_tracker.hpp"

#include <catch2/catch_test_macros.hpp>

using ajazz::app::PluginCrashTracker;

TEST_CASE("PluginCrashTracker disables after 3 crashes in 30s", "[plugin-crash]") {
    PluginCrashTracker tracker;
    QString const uuid = QStringLiteral("com.example.plugin");

    tracker.recordCrash(uuid, 0);
    tracker.recordCrash(uuid, 1000);
    tracker.recordCrash(uuid, 2000);

    REQUIRE(tracker.shouldDisable(uuid, 2000));
}

TEST_CASE("PluginCrashTracker restarts on 2 crashes", "[plugin-crash]") {
    PluginCrashTracker tracker;
    QString const uuid = QStringLiteral("com.example.plugin");

    tracker.recordCrash(uuid, 0);
    tracker.recordCrash(uuid, 1000);

    REQUIRE_FALSE(tracker.shouldDisable(uuid, 1000));
}

TEST_CASE("PluginCrashTracker ignores crashes outside the 30s window", "[plugin-crash]") {
    PluginCrashTracker tracker;
    QString const uuid = QStringLiteral("com.example.plugin");

    // t=0 is outside the 30s window at t=31000 (window is [31000-30000, 31000] = [1000, 31000])
    tracker.recordCrash(uuid, 0);
    tracker.recordCrash(uuid, 16000);
    tracker.recordCrash(uuid, 31000);

    // Only the pair at 16000 and 31000 are inside the trailing 30s window at t=31000.
    // That is only 2 crashes -> should NOT disable.
    REQUIRE_FALSE(tracker.shouldDisable(uuid, 31000));
}

TEST_CASE("PluginCrashTracker isolates uuids", "[plugin-crash]") {
    PluginCrashTracker tracker;
    QString const uuidA = QStringLiteral("com.example.pluginA");
    QString const uuidB = QStringLiteral("com.example.pluginB");

    // 3 crashes on uuid A
    tracker.recordCrash(uuidA, 0);
    tracker.recordCrash(uuidA, 1000);
    tracker.recordCrash(uuidA, 2000);

    REQUIRE(tracker.shouldDisable(uuidA, 2000));
    // uuid B has no crashes -> should NOT disable
    REQUIRE_FALSE(tracker.shouldDisable(uuidB, 2000));
}
