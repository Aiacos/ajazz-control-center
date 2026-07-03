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
#include <stdexcept>
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

    // WR-04: serialise-parse-serialize must be byte-stable, not just
    // structurally equal. A writer regression the lenient reader tolerates
    // would slip past a struct-only assertion; comparing the two emitted
    // strings catches it for the multi-state path.
    auto const j1 = profileToJson(restored);
    auto const j2 = profileToJson(profileFromJson(j1));
    REQUIRE(j1 == j2);
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

    // WR-04: byte-stable double-serialise for the recursive children path -- a
    // missing separator on nested-children emit that the reader tolerates would
    // pass a struct-only check but break this string equality.
    auto const j1 = profileToJson(restored);
    auto const j2 = profileToJson(profileFromJson(j1));
    REQUIRE(j1 == j2);
}

/// delayMs round-trip (Phase 32, BIND-04): an ActionInstance carrying
/// delayMs=250 round-trips (write -> read) preserving the value. delayMs is the
/// additive optional per-step delay mirroring Action::delayMs.
TEST_CASE("action_instance round-trips a non-zero delayMs", "[action_instance][delay]") {
    using namespace ajazz::core;

    Profile p{};
    p.id = "ai-delay";
    p.name = "AI";
    p.deviceCodename = "akp05e";

    Binding b{};
    ActionInstance inst{};
    inst.id = "com.test.delay";
    inst.delayMs = 250;
    b.instance = inst;
    p.keys[0] = b;

    auto const restored = profileFromJson(profileToJson(p));
    REQUIRE(restored.keys.at(0).instance.has_value());
    auto const& ri = *restored.keys.at(0).instance;
    REQUIRE(ri.delayMs == 250);
}

/// delayMs reader-tolerance (Phase 32): a legacy instance with NO "delayMs" key
/// reads back delayMs=0. The field is additive; absence is the default.
TEST_CASE("action_instance defaults a missing delayMs to zero", "[action_instance][delay]") {
    using namespace ajazz::core;

    constexpr char const* kNoDelayJson =
        R"({"id":"nodelay","name":"N","device":"akp05e",)"
        R"("keys":{"0":{"onPress":[],"onRelease":[],"onLongPress":[],)"
        R"("instance":{"id":"com.test.nodelay","states":[{"text":"A"}],"currentState":0}}},)"
        R"("encoders":{}})";

    Profile const p = profileFromJson(kNoDelayJson);
    REQUIRE(p.keys.at(0).instance.has_value());
    REQUIRE(p.keys.at(0).instance->delayMs == 0);
}

/// delayMs survives nested children (Phase 32): a child with delayMs=100
/// round-trips inside parent.children. This is the per-step delay the Multi
/// Action adapter copies onto each generated ActionChain step.
TEST_CASE("action_instance round-trips delayMs inside a nested child", "[action_instance][delay]") {
    using namespace ajazz::core;

    Profile p{};
    p.id = "ai-childdelay";
    p.name = "AI";
    p.deviceCodename = "akp05e";

    Binding b{};
    ActionInstance parent{};
    parent.id = "com.test.multi";

    ActionInstance child{};
    child.id = "com.test.child";
    child.delayMs = 100;
    parent.children.push_back(child);
    b.instance = parent;
    p.keys[0] = b;

    auto const restored = profileFromJson(profileToJson(p));
    REQUIRE(restored.keys.at(0).instance.has_value());
    auto const& ri = *restored.keys.at(0).instance;
    REQUIRE(ri.children.size() == 1);
    REQUIRE(ri.children[0].delayMs == 100);

    // WR-04: byte-stable double-serialise so a missing delayMs emit on the
    // nested-children path is caught by string equality, not just struct compare.
    auto const j1 = profileToJson(restored);
    auto const j2 = profileToJson(profileFromJson(j1));
    REQUIRE(j1 == j2);
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

namespace {

/// Build a profile JSON whose key-0 instance nests `depth` levels of
/// `children`. Used to drive the CR-01 recursion-depth guard from both sides
/// (a legitimate shallow nesting that must parse, and a pathological deep one
/// that must be rejected rather than overflow the stack).
std::string makeNestedProfile(int depth) {
    std::string inner = R"({"id":"leaf"})";
    for (int i = 0; i < depth; ++i) {
        inner = R"({"id":"n","children":[)" + inner + R"(]})";
    }
    return std::string{R"({"id":"deep","name":"D","device":"akp05e",)"} +
           R"("keys":{"0":{"onPress":[],"onRelease":[],"onLongPress":[],)" + R"("instance":)" +
           inner + R"(}},"encoders":{}})";
}

} // namespace

/// CR-01 positive: a reasonably-deep but legitimate children nesting (a few
/// levels) still parses fine -- the depth guard must not reject normal Multi
/// Action nesting.
TEST_CASE("action_instance parses a reasonably-deep children nesting within the limit",
          "[action_instance][depth]") {
    using namespace ajazz::core;

    Profile const p = profileFromJson(makeNestedProfile(8));
    REQUIRE(p.keys.at(0).instance.has_value());

    // Walk down the 8 nested children to confirm the structure survived.
    ActionInstance const* cur = &*p.keys.at(0).instance;
    for (int i = 0; i < 8; ++i) {
        REQUIRE(cur->children.size() == 1);
        cur = &cur->children[0];
    }
    REQUIRE(cur->id == "leaf");
    REQUIRE(cur->children.empty());
}

/// CR-01 negative: children nested beyond the reader's depth cap must be
/// rejected with std::runtime_error (the parser's fail() path) rather than
/// overflowing the C++ stack with a SIGSEGV. 4096 levels is far past the
/// kMaxDepth=64 cap.
TEST_CASE("action_instance rejects children nested beyond the depth limit",
          "[action_instance][depth]") {
    using namespace ajazz::core;

    REQUIRE_THROWS_AS(profileFromJson(makeNestedProfile(4096)), std::runtime_error);
}

/// CR-01 negative via skipValue: deep nesting hidden under an UNKNOWN key routes
/// through skipValue's recursive descent, which must also be depth-bounded.
TEST_CASE("action_instance rejects deep nesting hidden under an unknown key",
          "[action_instance][depth]") {
    using namespace ajazz::core;

    // Build a deeply-nested array of arrays under an unknown "_x" key inside the
    // instance object; the reader skips unknown keys via skipValue().
    std::string nested = "0";
    for (int i = 0; i < 4096; ++i) {
        nested = "[" + nested + "]";
    }
    std::string const json = std::string{R"({"id":"deepskip","name":"D","device":"akp05e",)"} +
                             R"("keys":{"0":{"onPress":[],"onRelease":[],"onLongPress":[],)" +
                             R"("instance":{"id":"x","_x":)" + nested + R"(}}},"encoders":{}})";

    REQUIRE_THROWS_AS(profileFromJson(json), std::runtime_error);
}

/// WR-02 precedence: an instance carrying BOTH a populated `states` array AND a
/// legacy singular `state` must keep the v2 `states` array regardless of key
/// order. states-then-state must NOT wipe states[] down to the legacy single.
TEST_CASE("action_instance keeps the states array when a legacy state key also appears",
          "[action_instance][migration]") {
    using namespace ajazz::core;

    // states[] first, then a legacy "state": the v2 array must win (2 states),
    // the legacy form is discarded.
    constexpr char const* kStatesThenState =
        R"({"id":"both","name":"B","device":"akp05e",)"
        R"("keys":{"0":{"onPress":[],"onRelease":[],"onLongPress":[],)"
        R"("instance":{"states":[{"text":"A"},{"text":"B"}],"state":{"text":"Legacy"}}}},)"
        R"("encoders":{}})";

    Profile const p1 = profileFromJson(kStatesThenState);
    REQUIRE(p1.keys.at(0).instance.has_value());
    auto const& i1 = *p1.keys.at(0).instance;
    REQUIRE(i1.states.size() == 2);
    REQUIRE(i1.states[0].visual.text.value() == "A");
    REQUIRE(i1.states[1].visual.text.value() == "B");

    // state first, then states[]: the v2 array must still win (2 states), the
    // earlier legacy state must not survive as a leading element.
    constexpr char const* kStateThenStates =
        R"({"id":"both2","name":"B","device":"akp05e",)"
        R"("keys":{"0":{"onPress":[],"onRelease":[],"onLongPress":[],)"
        R"("instance":{"state":{"text":"Legacy"},"states":[{"text":"A"},{"text":"B"}]}}},)"
        R"("encoders":{}})";

    Profile const p2 = profileFromJson(kStateThenStates);
    REQUIRE(p2.keys.at(0).instance.has_value());
    auto const& i2 = *p2.keys.at(0).instance;
    REQUIRE(i2.states.size() == 2);
    REQUIRE(i2.states[0].visual.text.value() == "A");
    REQUIRE(i2.states[1].visual.text.value() == "B");
}

/// WR-01: control characters in a string field (here ActionState text) must be
/// emitted as \u00XX escapes so the output is valid JSON, and must round-trip
/// back to the original bytes through the reader.
TEST_CASE("action_instance escapes control characters in state text as unicode escapes",
          "[action_instance][escape]") {
    using namespace ajazz::core;

    Profile p{};
    p.id = "ctrl";
    p.name = "C";
    p.deviceCodename = "akp05e";

    Binding b{};
    ActionInstance inst{};
    inst.id = "com.test.ctrl";
    ActionState s{};
    // Embed NUL (0x00), bell (0x07), and unit-separator (0x1F). Build the bytes
    // explicitly to avoid C++ greedy-hex-escape pitfalls (e.g. "\x07c" would
    // parse as a single 0x7C byte because the trailing 'c' is a hex digit).
    std::string const ctrl =
        std::string(1, '\0') + std::string(1, char(0x07)) + std::string(1, char(0x1F));
    s.visual.text = ctrl;
    inst.states.push_back(s);
    b.instance = inst;
    p.keys[0] = b;

    auto const json = profileToJson(p);
    // The raw control bytes must NOT appear; their \u escapes must.
    REQUIRE(json.find('\0') == std::string::npos);
    REQUIRE(json.find("\\u0000") != std::string::npos);
    REQUIRE(json.find("\\u0007") != std::string::npos);
    REQUIRE(json.find("\\u001f") != std::string::npos);

    // And the value must round-trip byte-for-byte.
    auto const restored = profileFromJson(json);
    REQUIRE(restored.keys.at(0).instance.has_value());
    auto const& ri = *restored.keys.at(0).instance;
    REQUIRE(ri.states.size() == 1);
    REQUIRE(ri.states[0].visual.text.has_value());
    REQUIRE(ri.states[0].visual.text.value() == ctrl);
}
