// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_multiaction_dispatch.cpp
 * @brief Phase 32 (BIND-04/BIND-05) coverage for instanceChildrenToChain --
 *        the ActionInstance.children -> core::ActionChain adapter that feeds the
 *        existing ActionEngine sequential walk for Multi Action dispatch.
 *
 * Proves the adapter: (1) preserves child order, (2) maps each field
 * (id -> Action.id, settings -> Action.settingsJson, delayMs -> Action.delayMs,
 * kind == Plugin), (3) yields an empty chain for empty children, and (4)
 * flattens nested children depth-first while truncating a hostile deep nest at
 * the depth cap (T-32-01 / CR-01 DoS mitigation) without crashing.
 *
 * Pure-core: includes only the core headers + Catch2; no Qt, no event loop.
 * All TEST_CASE titles are ASCII-only (use "-" and "->", never em-dash or
 * arrows) per the CLAUDE.md cross-platform ctest-filter rule.
 */
#include "ajazz/core/action_chain_adapter.hpp"
#include "ajazz/core/action_instance.hpp"
#include "ajazz/core/profile.hpp"

#include <string>

#include <catch2/catch_test_macros.hpp>

/// Order + count: a parent with children [A,B,C] flattens to a 3-step chain in
/// the same order A,B,C.
TEST_CASE("multiaction adapter preserves child order", "[multiaction]") {
    using namespace ajazz::core;

    ActionInstance parent{};
    for (auto const* id : {"A", "B", "C"}) {
        ActionInstance child{};
        child.id = id;
        parent.children.push_back(child);
    }

    ActionChain const chain = instanceChildrenToChain(parent);
    REQUIRE(chain.size() == 3);
    REQUIRE(chain[0].id == "A");
    REQUIRE(chain[1].id == "B");
    REQUIRE(chain[2].id == "C");
}

/// Field mapping: each step copies id -> Action.id, settings ->
/// Action.settingsJson, delayMs -> Action.delayMs, and kind == Plugin.
TEST_CASE("multiaction adapter maps each child field onto its Action step", "[multiaction]") {
    using namespace ajazz::core;

    ActionInstance parent{};
    ActionInstance child{};
    child.id = "com.test.step";
    child.settings = R"({"k":1})";
    child.delayMs = 250;
    parent.children.push_back(child);

    ActionChain const chain = instanceChildrenToChain(parent);
    REQUIRE(chain.size() == 1);
    REQUIRE(chain[0].kind == ActionKind::Plugin);
    REQUIRE(chain[0].id == "com.test.step");
    REQUIRE(chain[0].settingsJson == R"({"k":1})");
    REQUIRE(chain[0].delayMs == 250);
}

/// Empty children -> empty chain (no spurious steps).
TEST_CASE("multiaction adapter yields an empty chain for no children", "[multiaction]") {
    using namespace ajazz::core;

    ActionInstance parent{};
    parent.id = "com.test.empty";

    ActionChain const chain = instanceChildrenToChain(parent);
    REQUIRE(chain.empty());
}

/// Depth-first flatten: a parent [A, B[B1,B2], C] flattens to A,B,B1,B2,C --
/// each nested child's own children recurse inline immediately after it.
TEST_CASE("multiaction adapter flattens nested children depth-first", "[multiaction]") {
    using namespace ajazz::core;

    ActionInstance a{};
    a.id = "A";
    ActionInstance b{};
    b.id = "B";
    ActionInstance b1{};
    b1.id = "B1";
    ActionInstance b2{};
    b2.id = "B2";
    b.children.push_back(b1);
    b.children.push_back(b2);
    ActionInstance c{};
    c.id = "C";

    ActionInstance parent{};
    parent.children.push_back(a);
    parent.children.push_back(b);
    parent.children.push_back(c);

    ActionChain const chain = instanceChildrenToChain(parent);
    REQUIRE(chain.size() == 5);
    REQUIRE(chain[0].id == "A");
    REQUIRE(chain[1].id == "B");
    REQUIRE(chain[2].id == "B1");
    REQUIRE(chain[3].id == "B2");
    REQUIRE(chain[4].id == "C");
}

/// T-32-01 DoS mitigation: a pathologically deep nest (far beyond the depth
/// cap) is truncated rather than overflowing the C++ stack. The call must
/// return (no crash) and the produced chain must be bounded by the depth cap.
TEST_CASE("multiaction adapter truncates a nest beyond the depth cap without crashing",
          "[multiaction]") {
    using namespace ajazz::core;

    // Build a single chain of nested children 4096 levels deep -- well past the
    // kMaxDepth=64 cap mirrored from the profile reader's DepthGuard (CR-01).
    ActionInstance deep{};
    deep.id = "leaf";
    for (int i = 0; i < 4096; ++i) {
        ActionInstance wrap{};
        wrap.id = "n" + std::to_string(i);
        wrap.children.push_back(deep);
        deep = wrap;
    }

    ActionChain const chain = instanceChildrenToChain(deep);
    // Must not overflow: the produced chain is bounded by the depth cap, far
    // below the 4096 levels nested.
    REQUIRE(chain.size() <= 64);
    REQUIRE_FALSE(chain.empty());
}
