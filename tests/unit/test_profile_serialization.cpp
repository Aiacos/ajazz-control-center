// SPDX-License-Identifier: GPL-3.0-or-later
#include "ajazz/core/profile.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("profile round-trips required fields", "[profile]") {
    using namespace ajazz::core;

    Profile p{};
    p.id             = "4c3a5b84-feed-4c0c-a1b9-deadbeef0001";
    p.name           = "OBS Scenes";
    p.deviceCodename = "akp153";

    Binding b{};
    b.onPress.push_back(Action{ .id = "obs.switch", .settingsJson = R"({"scene":"cam1"})",
                                .label = "Cam 1" });
    p.keys[3] = b;
    p.applicationHints = { "obs", "obs-studio" };

    auto const json = profileToJson(p);
    REQUIRE(json.find("OBS Scenes")             != std::string::npos);
    REQUIRE(json.find("\"device\":\"akp153\"")  != std::string::npos);
    REQUIRE(json.find("obs.switch")             != std::string::npos);
    REQUIRE(json.find("obs-studio")             != std::string::npos);
}
