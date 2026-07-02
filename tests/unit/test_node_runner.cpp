// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_node_runner.cpp
 * @brief NodeRunnerTest: pins the PLUGIN-08 node argv contract and injectable node detection.
 *
 * All cases inject a fake NodeProbe — no live node process is launched.
 * ASCII-only TEST_CASE titles (CLAUDE.md / Pitfall 6: ctest Win32 codepage).
 *
 * Phase 18 Plan 18-02 (PLUGIN-08).
 */
#include "node_runner.hpp"

#include <catch2/catch_test_macros.hpp>

using ajazz::app::buildNodeArgv;
using ajazz::app::NodeProbe;
using ajazz::app::resolveNode20Plus;

// ---------------------------------------------------------------------------
// buildNodeArgv: exact §3 token list
// ---------------------------------------------------------------------------

TEST_CASE("NodeRunnerTest buildsCorrectArgv", "[node-runner]") {
    // Ref: akp_plugin_sdk.md §3 step 3.
    // QProcess::start(nodeExe, buildNodeArgv(...)) — nodeExe resolved separately.
    // argv[0] is codePath, NOT the node binary (Pitfall 3).
    QStringList const argv = buildNodeArgv("/p/index.js", 51234, "com.x.y", "{\"application\":{}}");

    REQUIRE(argv.size() == 9);
    CHECK(argv.at(0) == "/p/index.js");
    CHECK(argv.at(1) == "-port");
    CHECK(argv.at(2) == "51234");
    CHECK(argv.at(3) == "-pluginUUID");
    CHECK(argv.at(4) == "com.x.y");
    CHECK(argv.at(5) == "-registerEvent");
    CHECK(argv.at(6) == "registerPlugin");
    CHECK(argv.at(7) == "-info");
    CHECK(argv.at(8) == "{\"application\":{}}");
}

// ---------------------------------------------------------------------------
// resolveNode20Plus: injectable probe — present / absent / too old
// ---------------------------------------------------------------------------

TEST_CASE("NodeRunnerTest detects node 20 plus via injected probe", "[node-runner]") {
    NodeProbe probe;
    probe.findNode = [] { return QString("/usr/bin/node"); };
    probe.queryVersion = [](QString const&) { return QString("v26.0.0\n"); };

    auto result = resolveNode20Plus(probe);
    REQUIRE(result.has_value());
    CHECK(*result == "/usr/bin/node");
}

TEST_CASE("NodeRunnerTest rejects absent node", "[node-runner]") {
    NodeProbe probe;
    probe.findNode = [] { return QString(); };
    probe.queryVersion = [](QString const&) { return QString("v26.0.0\n"); };
    // queryVersion should never be called when findNode returns empty.

    auto result = resolveNode20Plus(probe);
    CHECK_FALSE(result.has_value());
}

TEST_CASE("NodeRunnerTest rejects node below 20", "[node-runner]") {
    NodeProbe probe;
    probe.findNode = [] { return QString("/usr/bin/node"); };
    probe.queryVersion = [](QString const&) { return QString("v18.20.0"); };

    auto result = resolveNode20Plus(probe);
    CHECK_FALSE(result.has_value());
}

TEST_CASE("NodeRunnerTest version strip tolerates leading v and trailing newline",
          "[node-runner]") {
    // Verify that "v20.0.0\n" is accepted (major == 20, the minimum allowed).
    NodeProbe probe;
    probe.findNode = [] { return QString("/usr/local/bin/node"); };
    probe.queryVersion = [](QString const&) { return QString("v20.0.0\n"); };

    auto result = resolveNode20Plus(probe);
    REQUIRE(result.has_value());
    CHECK(*result == "/usr/local/bin/node");
}

TEST_CASE("NodeRunnerTest rejects node 19 boundary", "[node-runner]") {
    // Edge: node 19 is just below the threshold.
    NodeProbe probe;
    probe.findNode = [] { return QString("/usr/bin/node"); };
    probe.queryVersion = [](QString const&) { return QString("v19.9.9"); };

    auto result = resolveNode20Plus(probe);
    CHECK_FALSE(result.has_value());
}
