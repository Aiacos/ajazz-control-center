// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file app_profile_switch_test.cpp
 * @brief RED scaffold for the per-app profile auto-switch matcher (APROF-02,
 *        Phase 34 Wave 0).
 *
 * The matcher — foreground appId -> Profile::applicationHints -> activate that
 * profile; no match -> fall back to the default profile — is WIRED IN PLAN 04.
 * These cases are deliberately RED until then: they are tagged [!shouldfail] so
 * the suite stays green while making the pending behaviour visible in ctest
 * output (NOT silently absent). When Plan 04 lands the matcher, the assertions
 * are filled in and the [!shouldfail] tag is removed.
 *
 * The Wave-0 seam they depend on (synthetic foreground injection via
 * StubActiveWindowWatcher) already exists and is exercised here so the file
 * compiles and links against the real interface.
 *
 * Tags: [app_profile_switch] — select with:
 *   ctest --preset linux-release -R app_profile_switch
 */
#include "ajazz/core/active_window_watcher.hpp"

#include <string>

#include <catch2/catch_test_macros.hpp>

using namespace ajazz::core;

// RED (Plan 04): a foreground appId matching a Profile::applicationHints entry
// must select that profile. The matcher + ProfileController activation wire do
// not exist yet, so this fails by design until Plan 04.
TEST_CASE("app_profile_switch foreground appId matching applicationHints selects that profile",
          "[app_profile_switch][!shouldfail]") {
    StubActiveWindowWatcher stub;
    std::string activatedProfile;
    stub.start([&activatedProfile](ActiveWindowInfo info) {
        // Plan 04 replaces this with the real applicationHints matcher +
        // ProfileController::activateDeviceProfile call. For now there is no
        // matcher, so the activated profile is never set.
        (void)info;
    });

    stub.injectForeground(ActiveWindowInfo{"firefox", ""});

    // Wave 0 RED: matcher implemented in Plan 04.
    REQUIRE(activatedProfile == "browser-profile");
}

// RED (Plan 04): no applicationHints match must fall back to the default profile.
TEST_CASE("app_profile_switch no applicationHints match falls back to the default profile",
          "[app_profile_switch][!shouldfail]") {
    StubActiveWindowWatcher stub;
    std::string activatedProfile;
    stub.start([&activatedProfile](ActiveWindowInfo info) { (void)info; });

    stub.injectForeground(ActiveWindowInfo{"unknown-app-with-no-hint", ""});

    // Wave 0 RED: default-fallback implemented in Plan 04.
    REQUIRE(activatedProfile == "default");
}
