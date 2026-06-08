// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file app_lifecycle_events_test.cpp
 * @brief RED scaffold for applicationDidLaunch / applicationDidTerminate host
 *        event fan-out (APROF-04, Phase 34 Wave 0).
 *
 * The fan-out — the watcher / OS source emits launch/terminate, the Application
 * seam delivers each event ONLY to subscribed/registered plugins via
 * SdPluginServer::sendEvent (no broadcast; V4 access control) — is WIRED IN
 * PLAN 04. These cases are deliberately RED until then: tagged [!shouldfail] so
 * the suite stays green while the pending behaviour is visible in ctest output
 * (NOT silently absent). When Plan 04 lands the fan-out, the assertions are
 * filled in against a fake SdPluginServer recording sink and [!shouldfail] is
 * removed.
 *
 * Tags: [app_lifecycle_events] — select with:
 *   ctest --preset linux-release -R app_lifecycle_events
 */
#include "ajazz/core/active_window_watcher.hpp"

#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using namespace ajazz::core;

// RED (Plan 04): applicationDidLaunch must be delivered to subscribed plugins.
TEST_CASE("app_lifecycle_events applicationDidLaunch is delivered to subscribed plugins",
          "[app_lifecycle_events][!shouldfail]") {
    // Plan 04 introduces the Application seam that maps a foreground/launch event
    // onto SdPluginServer::sendEvent("applicationDidLaunch", {application}) for
    // each registered plugin. No such fan-out exists yet.
    std::vector<std::string> deliveredEvents; // would be filled by the fake server sink

    // Wave 0 RED: fan-out implemented in Plan 04.
    REQUIRE(deliveredEvents.size() == 1);
    REQUIRE(deliveredEvents[0] == "applicationDidLaunch");
}

// RED (Plan 04): applicationDidTerminate must be delivered to subscribed plugins.
TEST_CASE("app_lifecycle_events applicationDidTerminate is delivered to subscribed plugins",
          "[app_lifecycle_events][!shouldfail]") {
    std::vector<std::string> deliveredEvents;

    // Wave 0 RED: fan-out implemented in Plan 04.
    REQUIRE(deliveredEvents.size() == 1);
    REQUIRE(deliveredEvents[0] == "applicationDidTerminate");
}

// RED (Plan 04): lifecycle events must NOT broadcast to unregistered plugins
// (V4 access control — foreground app-name disclosure mitigation).
TEST_CASE("app_lifecycle_events not delivered to unregistered plugins",
          "[app_lifecycle_events][!shouldfail]") {
    int deliveriesToUnregistered = -1; // sentinel: matcher not wired

    // Wave 0 RED: registered-only delivery implemented in Plan 04.
    REQUIRE(deliveriesToUnregistered == 0);
}
