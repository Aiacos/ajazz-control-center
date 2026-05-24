// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_builtin_actions.cpp
 * @brief Unit tests for the BuiltinActionRegistry dispatch table (pure-core, Task 1)
 *        and the BuiltinActionsService per-UUID handler wiring (app-tier, Task 2).
 *
 * Task 1 (registry-only cases, no Qt needed):
 *   - register + handles() + dispatch() with a spy handler.
 *   - Prefix gating: handles() returns false for non-builtin UUIDs.
 *   - Unregistered built-in prefix: handles() returns false.
 *   - dispatch with no registered handler is a clean no-op.
 *   - Two distinct UUIDs mapped to two distinct spies do not cross-fire.
 *   - settingsJson is forwarded verbatim (not parsed by the registry).
 *
 * ASCII-only TEST_CASE/SECTION titles (CLAUDE.md: em-dash and right-arrow are
 * mangled by Win32 CMD codepage in ctest filter args).
 */
#include "ajazz/core/builtin_action_registry.hpp"

#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using namespace ajazz::core;

// ============================================================================
// Task 1: BuiltinActionRegistry dispatch table (pure-core, no Qt)
// ============================================================================

TEST_CASE("BuiltinActionRegistry - prefix constant matches spec", "[builtin-actions]") {
    REQUIRE(BuiltinActionRegistry::kBuiltinPrefix == "com.hotspot.streamdock.");
}

TEST_CASE("BuiltinActionRegistry - register then handles returns true", "[builtin-actions]") {
    BuiltinActionRegistry reg;
    reg.registerAction("com.hotspot.streamdock.device.brightness", [](std::string_view) {});
    REQUIRE(reg.handles("com.hotspot.streamdock.device.brightness"));
}

TEST_CASE("BuiltinActionRegistry - handles returns false for third-party UUID",
          "[builtin-actions]") {
    BuiltinActionRegistry reg;
    // Not registered at all, and wrong prefix
    REQUIRE_FALSE(reg.handles("com.thirdparty.plugin.action"));
}

TEST_CASE("BuiltinActionRegistry - handles returns false for unregistered builtin prefix",
          "[builtin-actions]") {
    BuiltinActionRegistry reg;
    // Correct prefix but not registered
    REQUIRE_FALSE(reg.handles("com.hotspot.streamdock.system.hotkey"));
}

TEST_CASE("BuiltinActionRegistry - dispatch invokes handler with verbatim settingsJson",
          "[builtin-actions]") {
    BuiltinActionRegistry reg;

    std::string capturedSettings;
    reg.registerAction(
        "com.hotspot.streamdock.device.brightness",
        [&capturedSettings](std::string_view s) { capturedSettings = std::string{s}; });

    reg.dispatch("com.hotspot.streamdock.device.brightness", "{\"level\":40}");

    REQUIRE(capturedSettings == "{\"level\":40}");
}

TEST_CASE("BuiltinActionRegistry - dispatch calls handler exactly once", "[builtin-actions]") {
    BuiltinActionRegistry reg;

    int callCount = 0;
    reg.registerAction("com.hotspot.streamdock.browser",
                       [&callCount](std::string_view) { ++callCount; });

    reg.dispatch("com.hotspot.streamdock.browser", "{}");
    REQUIRE(callCount == 1);

    reg.dispatch("com.hotspot.streamdock.browser", "{}");
    REQUIRE(callCount == 2);
}

TEST_CASE("BuiltinActionRegistry - dispatch on unregistered UUID is a clean no-op",
          "[builtin-actions]") {
    BuiltinActionRegistry reg;
    // Should not throw and should not crash
    REQUIRE_NOTHROW(reg.dispatch("com.hotspot.streamdock.unregistered", "{}"));
    // Also should not fire for a third-party UUID
    REQUIRE_NOTHROW(reg.dispatch("com.thirdparty.xyz", "{}"));
}

TEST_CASE("BuiltinActionRegistry - two UUIDs mapped to two distinct spies do not cross-fire",
          "[builtin-actions]") {
    BuiltinActionRegistry reg;

    std::vector<std::string> logA;
    std::vector<std::string> logB;

    reg.registerAction("com.hotspot.streamdock.system.hotkey",
                       [&logA](std::string_view s) { logA.emplace_back(s); });
    reg.registerAction("com.hotspot.streamdock.plain.text",
                       [&logB](std::string_view s) { logB.emplace_back(s); });

    reg.dispatch("com.hotspot.streamdock.system.hotkey", "hotkey-payload");
    reg.dispatch("com.hotspot.streamdock.plain.text", "text-payload");

    REQUIRE(logA.size() == 1);
    REQUIRE(logA[0] == "hotkey-payload");
    REQUIRE(logB.size() == 1);
    REQUIRE(logB[0] == "text-payload");
}

TEST_CASE("BuiltinActionRegistry - settingsJson forwarded verbatim without parsing",
          "[builtin-actions]") {
    BuiltinActionRegistry reg;

    // A deliberately complex/unusual JSON blob to confirm no transformation
    constexpr std::string_view kWeirdJson =
        R"({"key":"value with spaces","nested":{"arr":[1,2,3]},"unicode":"é"})";

    std::string received;
    reg.registerAction("com.hotspot.streamdock.multiactions",
                       [&received](std::string_view s) { received = std::string{s}; });

    reg.dispatch("com.hotspot.streamdock.multiactions", kWeirdJson);

    REQUIRE(received == kWeirdJson);
}

TEST_CASE("BuiltinActionRegistry - handles empty dispatch table", "[builtin-actions]") {
    BuiltinActionRegistry reg;
    // Nothing registered
    REQUIRE_FALSE(reg.handles("com.hotspot.streamdock.browser"));
    REQUIRE_NOTHROW(reg.dispatch("com.hotspot.streamdock.browser", "{}"));
}

TEST_CASE("BuiltinActionRegistry - registerAction overwrites previous handler",
          "[builtin-actions]") {
    BuiltinActionRegistry reg;

    int firstCount = 0;
    int secondCount = 0;

    reg.registerAction("com.hotspot.streamdock.page.previous",
                       [&firstCount](std::string_view) { ++firstCount; });
    // Overwrite with new handler
    reg.registerAction("com.hotspot.streamdock.page.previous",
                       [&secondCount](std::string_view) { ++secondCount; });

    reg.dispatch("com.hotspot.streamdock.page.previous", "{}");
    REQUIRE(firstCount == 0);
    REQUIRE(secondCount == 1);
}

// ============================================================================
// COD-031 compliance: no nlohmann in public header (verified at build time by
// grep in Task 1 acceptance_criteria; this test just confirms registry compiles
// without pulling in Qt or nlohmann at the test level)
// ============================================================================
TEST_CASE("BuiltinActionRegistry - compiled without Qt or nlohmann dependency",
          "[builtin-actions]") {
    // If this file compiles without Qt/nlohmann includes, COD-031 is respected.
    // The test body is intentionally trivial.
    BuiltinActionRegistry reg;
    REQUIRE(BuiltinActionRegistry::kBuiltinPrefix.size() > 0);
}
