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
#include "ajazz/core/profile_bundle.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

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
    // WR-05: the new onRelease chain must round-trip like the other encoder chains.
    eb.onRelease.push_back(
        Action{.kind = ActionKind::RunCommand, .id = "", .label = "Release", .delayMs = 0});
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
    REQUIRE(restored.encoders.at(0).onRelease.size() == 1);
    REQUIRE(restored.encoders.at(0).onRelease.front().kind == ActionKind::RunCommand);
}

TEST_CASE("profile round-trips mouseButtons (string-keyed Bindings)",
          "[profile][roundtrip][mouse]") {
    using namespace ajazz::core;

    Profile p{};
    p.id = "mouse-rt-uuid";
    p.name = "Mouse Profile";
    p.deviceCodename = "aj_series";

    Binding side{};
    side.onPress.push_back(
        Action{.kind = ActionKind::Plugin, .id = "media.next", .label = "Next", .delayMs = 0});
    side.onLongPress.push_back(
        Action{.kind = ActionKind::Sleep, .id = "", .label = "Hold", .delayMs = 250});
    p.mouseButtons["side1"] = side;
    // A button name needing JSON escaping exercises escape()/readString()
    // symmetry on the string key (the keys/encoders maps are uint16-keyed
    // and never hit this path); empty binding must round-trip too.
    p.mouseButtons[R"(dpi"shift)"] = Binding{};

    auto const restored = profileFromJson(profileToJson(p));

    REQUIRE(restored.mouseButtons.size() == 2);
    REQUIRE(restored.mouseButtons.at("side1").onPress.size() == 1);
    REQUIRE(restored.mouseButtons.at("side1").onPress.front().id == "media.next");
    REQUIRE(restored.mouseButtons.at("side1").onPress.front().kind == ActionKind::Plugin);
    REQUIRE(restored.mouseButtons.at("side1").onLongPress.front().kind == ActionKind::Sleep);
    REQUIRE(restored.mouseButtons.at("side1").onLongPress.front().delayMs == 250);
    REQUIRE(restored.mouseButtons.at(R"(dpi"shift)").onPress.empty());
}

TEST_CASE("profile round-trips Binding::state (KeyState visuals)",
          "[profile][roundtrip][keystate]") {
    using namespace ajazz::core;

    Profile p{};
    p.id = "ks-uuid";
    p.name = "KeyState Profile";
    p.deviceCodename = "akp153";

    // Key 1: full visual state (icon + label + both colors + custom font).
    Binding b1{};
    b1.state.imagePath = "/home/user/icons/mic.png";
    b1.state.text = "Mute";
    b1.state.background = Rgb{10, 20, 30};
    b1.state.foreground = Rgb{255, 255, 255};
    b1.state.fontSize = 22;
    p.keys[1] = b1;

    // Key 2: default state (no visuals) — must NOT emit a state block and
    // must restore to a default-constructed KeyState.
    p.keys[2] = Binding{};

    // Encoder 0: partial state (label only) on the encoder's KeyState.
    EncoderBinding eb{};
    eb.state.text = "Vol";
    p.encoders[0] = eb;

    auto const json = profileToJson(p);
    auto const restored = profileFromJson(json);

    auto const& s1 = restored.keys.at(1).state;
    REQUIRE(s1.imagePath.has_value());
    REQUIRE(s1.imagePath.value() == "/home/user/icons/mic.png");
    REQUIRE(s1.text.value() == "Mute");
    REQUIRE(s1.background.has_value());
    REQUIRE(s1.background->r == 10);
    REQUIRE(s1.background->g == 20);
    REQUIRE(s1.background->b == 30);
    REQUIRE(s1.foreground->r == 255);
    REQUIRE(s1.fontSize == 22);

    // Default state round-trips as default; the writer omits it from the wire.
    REQUIRE_FALSE(restored.keys.at(2).state.imagePath.has_value());
    REQUIRE(restored.keys.at(2).state.fontSize == KeyState{}.fontSize);
    REQUIRE(json.find("\"state\"") != std::string::npos); // key 1 emitted it
    REQUIRE(restored.encoders.at(0).state.text.value() == "Vol");
    REQUIRE_FALSE(restored.encoders.at(0).state.background.has_value());
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

/// D-12: a v1 profile (no _schemaVersion key) loads with empty touchZones;
/// re-serialising upgrades it to schema v2 with a touchZones key.
TEST_CASE("v1 profile migrates touchZones to empty on load", "[profile][migration]") {
    using namespace ajazz::core;

    // Construct a minimal v1 profile JSON: no _schemaVersion, no touchZones.
    // The device, keys, encoders fields are present to exercise the existing
    // reader paths; the absence of _schemaVersion signals a v1 document.
    constexpr char const* kV1Json =
        R"({"id":"abc","name":"x","device":"akp05e","keys":{},"encoders":{}})";

    Profile const p = profileFromJson(kV1Json);

    // D-12: v1 profile loads with empty touchZones map.
    REQUIRE(p.touchZones.empty());
    REQUIRE(p.id == "abc");
    REQUIRE(p.deviceCodename == "akp05e");

    // Re-serialise: the writer always emits schema v2 + touchZones key.
    auto const json = profileToJson(p);
    REQUIRE(json.find("\"_schemaVersion\":2") != std::string::npos);
    REQUIRE(json.find("\"touchZones\":") != std::string::npos);
}

/// D-12: a Profile with non-empty touchZones serialises to v2 JSON and
/// round-trips back to the same touchZones (size, keys, chain, state.text).
TEST_CASE("v2 profile touchZones round-trip", "[profile][touchzone]") {
    using namespace ajazz::core;

    Profile p{};
    p.id = "tz-rt-uuid";
    p.name = "Touch Zone Profile";
    p.deviceCodename = "akp05e";

    // Zone 0: tap chain with one action + label on the state.
    TouchZoneBinding tz0{};
    tz0.onTap.push_back(
        Action{.kind = ActionKind::Plugin, .id = "media.play", .label = "Play", .delayMs = 0});
    tz0.state.text = "Z0";
    p.touchZones[0] = tz0;

    // Zone 3: empty tap chain + custom state text only (exercises sparse map).
    TouchZoneBinding tz3{};
    tz3.state.text = "Z3";
    p.touchZones[3] = tz3;

    auto const json = profileToJson(p);
    // Schema v2 markers must be present.
    REQUIRE(json.find("\"_schemaVersion\":2") != std::string::npos);
    REQUIRE(json.find("\"touchZones\":") != std::string::npos);

    // Round-trip the JSON back to a Profile.
    Profile const restored = profileFromJson(json);

    REQUIRE(restored.touchZones.size() == 2);
    REQUIRE(restored.touchZones.count(0) == 1);
    REQUIRE(restored.touchZones.count(3) == 1);

    auto const& r0 = restored.touchZones.at(0);
    REQUIRE(r0.onTap.size() == 1);
    REQUIRE(r0.onTap.front().id == "media.play");
    REQUIRE(r0.onTap.front().kind == ActionKind::Plugin);
    REQUIRE(r0.state.text.has_value());
    REQUIRE(r0.state.text.value() == "Z0");

    auto const& r3 = restored.touchZones.at(3);
    REQUIRE(r3.onTap.empty());
    REQUIRE(r3.state.text.has_value());
    REQUIRE(r3.state.text.value() == "Z3");
}

/// US3 (002) read-compat: a legacy profile carrying BOTH an encoder binding and
/// an independent touchZone at the same index must load losslessly. Under the
/// dial-owns-segment model the dial (encoder) wins, but the legacy touchZone is
/// retained in-model (no crash, no data loss) so older readers still see it. This
/// is the backward-compatibility guarantee for the touch-zone -> dial migration.
TEST_CASE("encoder and legacy touchZone at same index coexist losslessly",
          "[profile][touchzone][readcompat]") {
    using namespace ajazz::core;

    Profile p{};
    p.id = "coexist-uuid";
    p.name = "Coexist";
    p.deviceCodename = "akp05e";

    // Dial 0: a plugin encoder binding (the dial that now owns segment 0).
    EncoderBinding eb0{};
    eb0.onCw.push_back(
        Action{.kind = ActionKind::Plugin, .id = "vol.up", .label = "Vol+", .delayMs = 0});
    eb0.state.text = "Dial0";
    p.encoders[0] = eb0;

    // Legacy independent touchZone 0 (authored before the dial-owns-segment
    // model). It must survive the round-trip even though the dial now owns it.
    TouchZoneBinding tz0{};
    tz0.onTap.push_back(
        Action{.kind = ActionKind::Plugin, .id = "media.play", .label = "Play", .delayMs = 0});
    tz0.state.text = "Zone0";
    p.touchZones[0] = tz0;

    auto const json = profileToJson(p);
    Profile const restored = profileFromJson(json);

    // The encoder (dial) survives intact — it is the live control for index 0.
    REQUIRE(restored.encoders.count(0) == 1);
    REQUIRE(restored.encoders.at(0).onCw.size() == 1);
    REQUIRE(restored.encoders.at(0).onCw.front().id == "vol.up");
    REQUIRE(restored.encoders.at(0).state.text.value_or("") == "Dial0");

    // The legacy touchZone is retained losslessly (read-compat; no data loss).
    REQUIRE(restored.touchZones.count(0) == 1);
    REQUIRE(restored.touchZones.at(0).onTap.size() == 1);
    REQUIRE(restored.touchZones.at(0).onTap.front().id == "media.play");
    REQUIRE(restored.touchZones.at(0).state.text.value_or("") == "Zone0");
}

/// CR-01 / WR-06 regression: touchZones BEFORE _schemaVersion must not be discarded.
///
/// RFC 8259 does not specify JSON key order, so a compliant serialiser or
/// hand-edited file may emit "touchZones" before "_schemaVersion". Before the
/// CR-01 fix, schemaVersion was still 1 at the point "touchZones" was parsed,
/// causing the branch to fall through to r.skipValue() and silently discard
/// all touch-zone data. After the fix the branch is unconditional.
TEST_CASE("v2 profile with touchZones key before _schemaVersion round-trips",
          "[profile][migration][ordering]") {
    using namespace ajazz::core;

    // Keys are in reversed order relative to the writer output: touchZones comes
    // BEFORE _schemaVersion. This is valid JSON per RFC 8259.
    constexpr char const* kReorderedJson =
        R"({)"
        R"("id":"x",)"
        R"("touchZones":{"0":{"onTap":[{"id":"media.play","label":"Play"}],"state":{}}},)"
        R"("_schemaVersion":2,)"
        R"("name":"Y",)"
        R"("device":"akp05e",)"
        R"("keys":{},)"
        R"("encoders":{})"
        R"(})";

    Profile const p = profileFromJson(kReorderedJson);

    // Must have exactly 1 touch zone; if it has 0, the ordering bug is still live.
    REQUIRE(p.touchZones.size() == 1);
    REQUIRE(p.touchZones.count(0) == 1);
    auto const& tz = p.touchZones.at(0);
    REQUIRE(tz.onTap.size() == 1);
    REQUIRE(tz.onTap.front().id == "media.play");
    REQUIRE(tz.onTap.front().label == "Play");
    REQUIRE(p.id == "x");
    REQUIRE(p.deviceCodename == "akp05e");
}

/// profileFromJson() must report a byte offset on malformed input.
TEST_CASE("profile reader fails with byte offset on malformed input", "[profile][error]") {
    using namespace ajazz::core;
    REQUIRE_THROWS_AS(profileFromJson(""), std::runtime_error);
    REQUIRE_THROWS_AS(profileFromJson("not an object"), std::runtime_error);
    REQUIRE_THROWS_AS(profileFromJson("{\"id\":"), std::runtime_error);
    REQUIRE_THROWS_AS(profileFromJson("{\"id\":\"x\""), std::runtime_error); // missing close
    // A negative value in an unsigned field (delayMs) must be rejected, not
    // silently wrapped to a huge uint32 (WR-04).
    REQUIRE_THROWS_AS(
        profileFromJson(
            R"({"id":"x","name":"Y","device":"d","keys":{"0":{"onPress":[{"id":"a","delayMs":-1}]}}})"),
        std::runtime_error);
}

// Regression: out-of-range integers must be rejected, not truncated. On LP64
// std::stoul is 64-bit, so values in (type-max, ULONG_MAX] parsed without
// throwing and then truncated on the cast — e.g. delayMs 4294967296 -> 0 or
// key index 65536 -> 0 (silently overwriting binding 0). See audit 2026-06-05.
TEST_CASE("profileFromJson rejects out-of-range integers instead of truncating",
          "[profile][hardening]") {
    using namespace ajazz::core;

    // delayMs above UINT32_MAX.
    REQUIRE_THROWS_AS(
        profileFromJson(
            R"({"id":"x","name":"Y","device":"d","keys":{"0":{"onPress":[{"id":"a","delayMs":4294967296}]}}})"),
        std::runtime_error);

    // keys map index above UINT16_MAX (would fold to a low index and clobber it).
    REQUIRE_THROWS_AS(profileFromJson(R"({"id":"x","name":"Y","device":"d","keys":{"70000":{}}})"),
                      std::runtime_error);

    // touchZones map index above UINT8_MAX (the old `& 0xFFu` mask folded 256 -> 0).
    REQUIRE_THROWS_AS(
        profileFromJson(R"({"id":"x","name":"Y","device":"d","keys":{},"touchZones":{"256":{}}})"),
        std::runtime_error);
}

// Regression: the bundle manifest's free-form `author` field must be JSON-escaped.
// Unescaped, an author containing a quote/backslash produced malformed bundle
// JSON. See audit 2026-06-05.
TEST_CASE("exportProfileBundle escapes the author field", "[profile_bundle]") {
    using namespace ajazz::core;

    Profile p{};
    p.id = "bundle-uuid";
    p.name = "Bundle";
    p.deviceCodename = "akp05";

    auto const dir = std::filesystem::temp_directory_path() / "ajazz_bundle_escape_test";
    std::filesystem::create_directories(dir);
    auto const path = dir / "out.bundle.json";

    // Author with a double-quote and a backslash — unescaped, these break JSON.
    exportProfileBundle(path, p, std::string(R"(He said "hi"\done)"));

    std::ifstream in(path, std::ios::binary);
    std::ostringstream buf;
    buf << in.rdbuf();
    auto const contents = buf.str();

    // The manifest must carry the escaped author, never the raw bytes.
    REQUIRE(contents.find(R"("author":"He said \"hi\"\\done")") != std::string::npos);

    // Close the reader before removing: on Windows std::filesystem::remove_all
    // throws "file being used by another process" while the ifstream still holds
    // the bundle open, unlike POSIX where unlink-while-open succeeds.
    in.close();
    std::filesystem::remove_all(dir);
}
