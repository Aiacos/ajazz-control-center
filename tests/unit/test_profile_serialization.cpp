// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_profile_serialization.cpp
 * @brief Unit tests for Profile JSON serialisation (profileToJson +
 *        profileFromJson round-trip).
 *
 * Verifies that profileToJson() encodes required fields and that
 * profileFromJson() restores them byte-equivalent.
 */
#include "ajazz/core/profile.hpp"

#include <catch2/catch_test_macros.hpp>

/// profileToJson() must include name, device codename, action id, and application hint strings.
TEST_CASE("profile round-trips required fields", "[profile]") {
    using namespace ajazz::core;

    Profile p{};
    p.id = "4c3a5b84-feed-4c0c-a1b9-deadbeef0001";
    p.name = "OBS Scenes";
    p.deviceCodename = "akp153";

    Binding b{};
    b.onPress.push_back(
        Action{.id = "obs.switch", .settingsJson = R"({"scene":"cam1"})", .label = "Cam 1"});
    p.keys[3] = b;
    p.applicationHints = {"obs", "obs-studio"};

    auto const json = profileToJson(p);
    REQUIRE(json.find("OBS Scenes") != std::string::npos);
    REQUIRE(json.find("\"device\":\"akp153\"") != std::string::npos);
    REQUIRE(json.find("obs.switch") != std::string::npos);
    REQUIRE(json.find("obs-studio") != std::string::npos);
}

/// profileFromJson() must reverse profileToJson() for keys, encoders, and hints.
TEST_CASE("profile JSON reader round-trips writer output", "[profile][roundtrip]") {
    using namespace ajazz::core;

    Profile p{};
    p.id = "round-trip-uuid-0001";
    p.name = "Round Trip";
    p.deviceCodename = "akp05";

    Binding b{};
    b.onPress.push_back(
        Action{.kind = ActionKind::Plugin, .id = "obs.switch", .label = "Cam 1", .delayMs = 0});
    b.onRelease.push_back(
        Action{.kind = ActionKind::Sleep, .id = "", .label = "Wait 100ms", .delayMs = 100});
    b.onLongPress.push_back(Action{.kind = ActionKind::RunCommand,
                                   .id = "",
                                   .settingsJson = R"({"argv0":"echo","args":["hi"]})",
                                   .label = "Echo hi",
                                   .delayMs = 0});
    p.keys[3] = b;
    p.keys[5] = Binding{}; // empty binding round-trips too

    EncoderBinding eb{};
    eb.onCw.push_back(Action{.kind = ActionKind::OpenUrl,
                             .id = "",
                             .settingsJson = R"({"url":"https://example.com"})",
                             .label = "Open URL",
                             .delayMs = 0});
    eb.onCcw.push_back(
        Action{.kind = ActionKind::BackToParent, .id = "", .label = "Back", .delayMs = 0});
    p.encoders[0] = eb;

    p.applicationHints = {"obs", "obs-studio", "OBS"};

    auto const json = profileToJson(p);
    auto const restored = profileFromJson(json);

    REQUIRE(restored.id == p.id);
    REQUIRE(restored.name == p.name);
    REQUIRE(restored.deviceCodename == p.deviceCodename);
    REQUIRE(restored.applicationHints == p.applicationHints);
    REQUIRE(restored.keys.size() == 2);
    REQUIRE(restored.keys.at(3).onPress.size() == 1);
    REQUIRE(restored.keys.at(3).onPress.front().id == "obs.switch");
    REQUIRE(restored.keys.at(3).onPress.front().kind == ActionKind::Plugin);
    REQUIRE(restored.keys.at(3).onRelease.size() == 1);
    REQUIRE(restored.keys.at(3).onRelease.front().kind == ActionKind::Sleep);
    REQUIRE(restored.keys.at(3).onRelease.front().delayMs == 100);
    REQUIRE(restored.keys.at(3).onLongPress.size() == 1);
    REQUIRE(restored.keys.at(3).onLongPress.front().kind == ActionKind::RunCommand);
    REQUIRE(restored.keys.at(3).onLongPress.front().settingsJson ==
            R"({"argv0":"echo","args":["hi"]})");
    REQUIRE(restored.keys.at(5).onPress.empty());
    REQUIRE(restored.encoders.size() == 1);
    REQUIRE(restored.encoders.at(0).onCw.size() == 1);
    REQUIRE(restored.encoders.at(0).onCw.front().kind == ActionKind::OpenUrl);
    REQUIRE(restored.encoders.at(0).onCcw.front().kind == ActionKind::BackToParent);
}

/// profileFromJson() must tolerate extra whitespace and unknown keys.
TEST_CASE("profile reader skips unknown keys and tolerates whitespace",
          "[profile][forward-compat]") {
    using namespace ajazz::core;
    constexpr char const* kJson = R"({
        "id" : "x" ,
        "name": "Y",
        "device": "akp03",
        "futureField": {"nested": [1,2,3]},
        "keys": {},
        "applicationHints": ["a","b"]
    })";
    auto const p = profileFromJson(kJson);
    REQUIRE(p.id == "x");
    REQUIRE(p.name == "Y");
    REQUIRE(p.deviceCodename == "akp03");
    REQUIRE(p.applicationHints.size() == 2);
}

/// profileFromJson() must report a byte offset on malformed input.
TEST_CASE("profile reader fails with byte offset on malformed input", "[profile][error]") {
    using namespace ajazz::core;
    REQUIRE_THROWS_AS(profileFromJson(""), std::runtime_error);
    REQUIRE_THROWS_AS(profileFromJson("not an object"), std::runtime_error);
    REQUIRE_THROWS_AS(profileFromJson("{\"id\":"), std::runtime_error);
    REQUIRE_THROWS_AS(profileFromJson("{\"id\":\"x\""), std::runtime_error); // missing close
}
