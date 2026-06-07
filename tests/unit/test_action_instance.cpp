// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_action_instance.cpp
 * @brief BIND-02 verification for the Phase 31 ActionInstance / ActionState
 *        core model and its hand-rolled Profile schema-v2 serialiser.
 *
 * These tests prove that the 31-01 serializer round-trips ActionInstance
 * losslessly across all required variants (0/1/3-state + 2-children),
 * migrates a legacy singular "state" into a states[] array of one (lazy
 * v1->v2 fold), returns std::nullopt when a binding carries no "instance"
 * key (on both Binding and EncoderBinding), and defensively clamps an
 * out-of-range currentState to 0 on read.
 *
 * Round-trip cases use the profileFromJson(profileToJson(p)) idiom from
 * test_profile_serialization.cpp; the migration / nullopt / clamp cases feed
 * literal JSON across the reader boundary directly so the legacy singular
 * "state", the missing "instance" key, and the out-of-range index are
 * exercised on the parser itself.
 *
 * All TEST_CASE / SECTION titles are ASCII-only (use "-" and "->", never
 * em-dash or arrows) per the CLAUDE.md cross-platform ctest-filter rule.
 */
#include "ajazz/core/action_instance.hpp"
#include "ajazz/core/profile.hpp"

#include <optional>
#include <string>

#include <catch2/catch_test_macros.hpp>

/// 0-state instance: empty states[] -> restored has_value, states empty,
/// currentState clamped to 0; the serialised JSON carries an empty states array.
TEST_CASE("action_instance round-trips a 0-state instance", "[action_instance][roundtrip]") {
    using namespace ajazz::core;

    Profile p{};
    p.id = "ai-0state";
    p.name = "AI";
    p.deviceCodename = "akp05e";

    Binding b{};
    b.instance = ActionInstance{}; // no states, no children
    p.keys[0] = b;

    auto const json = profileToJson(p);
    REQUIRE(json.find("\"states\":[]") != std::string::npos);

    auto const restored = profileFromJson(json);
    REQUIRE(restored.keys.at(0).instance.has_value());
    auto const& ri = *restored.keys.at(0).instance;
    REQUIRE(ri.states.empty());
    REQUIRE(ri.currentState == 0);
    REQUIRE(ri.children.empty());
}

/// 1-state instance: a single ActionState with a set visual.text round-trips
/// to states.size() == 1 with the text preserved.
TEST_CASE("action_instance round-trips a 1-state instance", "[action_instance][roundtrip]") {
    using namespace ajazz::core;

    Profile p{};
    p.id = "ai-1state";
    p.name = "AI";
    p.deviceCodename = "akp05e";

    Binding b{};
    ActionInstance inst{};
    inst.id = "com.test.single";
    ActionState s{};
    s.visual.text = "On";
    inst.states.push_back(s);
    b.instance = inst;
    p.keys[0] = b;

    auto const restored = profileFromJson(profileToJson(p));
    REQUIRE(restored.keys.at(0).instance.has_value());
    auto const& ri = *restored.keys.at(0).instance;
    REQUIRE(ri.id == "com.test.single");
    REQUIRE(ri.states.size() == 1);
    REQUIRE(ri.states[0].visual.text.has_value());
    REQUIRE(ri.states[0].visual.text.value() == "On");
    REQUIRE(ri.currentState == 0);
}

/// 3-state instance: three states, currentState == 2, settings is an escaped
/// JSON string that round-trips byte-equal, and an inner state text survives.
TEST_CASE("action_instance round-trips a 3-state instance with currentState and settings",
          "[action_instance][roundtrip]") {
    using namespace ajazz::core;

    Profile p{};
    p.id = "ai-3state";
    p.name = "AI";
    p.deviceCodename = "akp05e";

    Binding b{};
    ActionInstance inst{};
    inst.id = "com.test.toggle";
    inst.settings = R"({"x":1})";
    for (int i = 0; i < 3; ++i) {
        ActionState s{};
        s.visual.text = "S" + std::to_string(i);
        inst.states.push_back(s);
    }
    inst.currentState = 2;
    b.instance = inst;
    p.keys[0] = b;

    auto const restored = profileFromJson(profileToJson(p));
    REQUIRE(restored.keys.at(0).instance.has_value());
    auto const& ri = *restored.keys.at(0).instance;
    REQUIRE(ri.states.size() == 3);
    REQUIRE(ri.currentState == 2);
    REQUIRE(ri.settings == R"({"x":1})");
    REQUIRE(ri.states[1].visual.text.value() == "S1");
}

/// 2-children instance: a parent with two child ActionInstances (each with its
/// own state) round-trips, proving the recursive children path.
TEST_CASE("action_instance round-trips a 2-children Multi Action instance",
          "[action_instance][roundtrip]") {
    using namespace ajazz::core;

    Profile p{};
    p.id = "ai-children";
    p.name = "AI";
    p.deviceCodename = "akp05e";

    Binding b{};
    ActionInstance parent{};
    parent.id = "com.test.multi";

    ActionInstance child0{};
    child0.id = "com.test.child0";
    {
        ActionState cs{};
        cs.visual.text = "C0";
        child0.states.push_back(cs);
    }
    ActionInstance child1{};
    child1.id = "com.test.child1";
    {
        ActionState cs{};
        cs.visual.text = "C1";
        child1.states.push_back(cs);
    }
    parent.children.push_back(child0);
    parent.children.push_back(child1);
    b.instance = parent;
    p.keys[0] = b;

    auto const restored = profileFromJson(profileToJson(p));
    REQUIRE(restored.keys.at(0).instance.has_value());
    auto const& ri = *restored.keys.at(0).instance;
    REQUIRE(ri.children.size() == 2);
    REQUIRE(ri.children[0].id == "com.test.child0");
    REQUIRE(ri.children[0].states.size() == 1);
    REQUIRE(ri.children[0].states[0].visual.text.value() == "C0");
    REQUIRE(ri.children[1].id == "com.test.child1");
    REQUIRE(ri.children[1].states[0].visual.text.value() == "C1");
}

/// v1->v2 migration: a legacy profile whose binding instance carries a singular
/// "state" object (not "states") folds to a states[] array of one on read, and
/// re-serialising emits the array form "states":[ ... ].
TEST_CASE("action_instance folds a legacy singular state into a states array of one",
          "[action_instance][migration]") {
    using namespace ajazz::core;

    // Literal legacy v2-ish profile: the instance carries "state" (singular),
    // never "states". The reader must fold it into a one-element states vector.
    constexpr char const* kLegacyJson =
        R"({"id":"legacy","name":"L","device":"akp05e",)"
        R"("keys":{"0":{"onPress":[],"onRelease":[],"onLongPress":[],)"
        R"("instance":{"id":"com.test.legacy","state":{"text":"Legacy"},"currentState":0}}},)"
        R"("encoders":{}})";

    Profile const p = profileFromJson(kLegacyJson);
    REQUIRE(p.keys.at(0).instance.has_value());
    auto const& inst = *p.keys.at(0).instance;
    REQUIRE(inst.states.size() == 1);
    REQUIRE(inst.states[0].visual.text.has_value());
    REQUIRE(inst.states[0].visual.text.value() == "Legacy");

    // Re-serialise the restored profile: the writer always emits the array form.
    auto const json = profileToJson(p);
    REQUIRE(json.find("\"states\":[") != std::string::npos);
}

/// no instance -> nullopt: a binding (key) and an encoder with no "instance"
/// key parse to instance == std::nullopt on both Binding and EncoderBinding.
TEST_CASE(
    "action_instance is nullopt when no instance key is present on a Binding or EncoderBinding",
    "[action_instance][nullopt]") {
    using namespace ajazz::core;

    constexpr char const* kNoInstanceJson =
        R"({"id":"noinst","name":"N","device":"akp05e",)"
        R"("keys":{"0":{"onPress":[],"onRelease":[],"onLongPress":[]}},)"
        R"("encoders":{"0":{"onCw":[],"onCcw":[],"onPress":[]}}})";

    Profile const p = profileFromJson(kNoInstanceJson);
    REQUIRE(p.keys.at(0).instance == std::nullopt);
    REQUIRE_FALSE(p.keys.at(0).instance.has_value());
    REQUIRE(p.encoders.at(0).instance == std::nullopt);
    REQUIRE_FALSE(p.encoders.at(0).instance.has_value());
}

/// currentState clamp: an instance literal with "currentState":99 and 2 states
/// restores with currentState == 0 (lossless defensive read, never index OOB).
TEST_CASE("action_instance clamps an out-of-range currentState to 0 on read",
          "[action_instance][clamp]") {
    using namespace ajazz::core;

    constexpr char const* kClampJson =
        R"({"id":"clamp","name":"C","device":"akp05e",)"
        R"("keys":{"0":{"onPress":[],"onRelease":[],"onLongPress":[],)"
        R"("instance":{"states":[{"text":"A"},{"text":"B"}],"currentState":99}}},)"
        R"("encoders":{}})";

    Profile const p = profileFromJson(kClampJson);
    REQUIRE(p.keys.at(0).instance.has_value());
    auto const& inst = *p.keys.at(0).instance;
    REQUIRE(inst.states.size() == 2);
    REQUIRE(inst.currentState == 0);
}
