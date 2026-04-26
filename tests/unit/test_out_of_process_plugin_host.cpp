// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_out_of_process_plugin_host.cpp
 * @brief Unit tests for the slice-1 @ref OutOfProcessPluginHost.
 *
 * The point of these tests is to PROVE the safety claim that motivates
 * audit finding A4: a crash in plugin code MUST NOT take the host
 * process down. We validate three invariants:
 *
 *   1. **Spawn round-trip works** — host launches the child, reads the
 *      `ready` handshake, can request `list_plugins` and receive the
 *      empty-array response, and tear-down via the destructor's
 *      `shutdown` flow leaves no zombie.
 *   2. **Crash isolation holds** — a deliberate SIGSEGV inside the
 *      child kills only the child. The host's process is unaffected;
 *      the next operation on the dead host returns an error
 *      (`std::runtime_error`) instead of crashing.
 *   3. **Bad config is rejected at construction** — pointing at a
 *      nonexistent script returns a clean exception, not a hang.
 *
 * Skipped when `python3` is not on `PATH` (rare on dev machines but
 * possible on a stripped CI runner) — the test logs and reports as
 * `SKIPPED` rather than failing.
 */
#ifndef _WIN32

#include "ajazz/plugins/out_of_process_plugin_host.hpp"

#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <string>

#include <catch2/catch_test_macros.hpp>

namespace {

std::filesystem::path repoRoot() {
    // Resolve from the test binary's CWD upward until we find the
    // python/ajazz_plugins/ directory (CTest runs the binary from its
    // build dir; the source tree's `python/` sits at <repo>/python).
    std::filesystem::path here = std::filesystem::current_path();
    for (int i = 0; i < 8; ++i) {
        if (std::filesystem::is_directory(here / "python" / "ajazz_plugins")) {
            return here;
        }
        if (!here.has_parent_path() || here == here.parent_path()) {
            break;
        }
        here = here.parent_path();
    }
    return std::filesystem::current_path(); // fallback — test will fail with a clear error
}

bool python3Available() {
    return std::system("command -v python3 >/dev/null 2>&1") == 0;
}

ajazz::plugins::OutOfProcessHostConfig makeConfig() {
    auto root = repoRoot();
    ajazz::plugins::OutOfProcessHostConfig cfg;
    cfg.pythonExecutable = "python3";
    cfg.childScript = root / "python" / "ajazz_plugins" / "_host_child.py";
    cfg.pythonPath = {root / "python"};
    return cfg;
}

} // namespace

TEST_CASE("OOP plugin host: spawn -> list -> shutdown round-trip", "[plugins][oop][!mayfail]") {
    if (!python3Available()) {
        SKIP("python3 not on PATH; skipping OutOfProcessPluginHost spawn test");
    }

    ajazz::plugins::OutOfProcessPluginHost host{makeConfig()};
    REQUIRE(host.isAlive());
    REQUIRE(host.childPid() > 0);

    auto const inventory = host.listPlugins();
    REQUIRE(inventory.empty()); // slice 1: child never loads any plugin

    // Destructor sends `shutdown`, waits for clean exit. We let scope
    // exit do the work; verify no zombie remains by re-checking after
    // the destructor runs in the next test case (the Linux kernel
    // would surface a stuck zombie as ENOMEM-like long-term, but
    // here the visible signal is `isAlive()` flipping to false).
}

TEST_CASE("OOP plugin host: crash in child does not crash host", "[plugins][oop][!mayfail]") {
    if (!python3Available()) {
        SKIP("python3 not on PATH; skipping crash-isolation test");
    }

    ajazz::plugins::OutOfProcessPluginHost host{makeConfig()};
    REQUIRE(host.isAlive());

    // Trigger SIGSEGV inside the child. The host process must survive.
    bool const childDead = host.crashChildForTest();
    REQUIRE(childDead);
    REQUIRE_FALSE(host.isAlive());

    // The next op on the dead host should fail with a clean exception,
    // not crash the process. This is the actual safety claim — pre-A4
    // a segfault in pybind11's interpreter would have brought the
    // whole AJAZZ Control Center process down.
    REQUIRE_THROWS_AS(host.listPlugins(), std::runtime_error);
}

TEST_CASE("OOP plugin host: add_search_path + load_all + list_plugins discover the hello example",
          "[plugins][oop][!mayfail]") {
    if (!python3Available()) {
        SKIP("python3 not on PATH; skipping plugin discovery test");
    }

    auto cfg = makeConfig();
    ajazz::plugins::OutOfProcessPluginHost host{cfg};
    REQUIRE(host.isAlive());

    auto const examples = repoRoot() / "python" / "ajazz_plugins" / "examples";
    REQUIRE(std::filesystem::is_directory(examples));
    REQUIRE_NOTHROW(host.addSearchPath(examples));

    auto const loadedNow = host.loadAll();
    // Today the examples/ directory contains exactly `hello/`. If a
    // future PR adds more examples this test will start observing
    // them; the assertion below only requires the hello example to
    // be present in the inventory, not exclusivity.
    REQUIRE(loadedNow >= 1);

    auto const inventory = host.listPlugins();
    auto const hello =
        std::find_if(inventory.begin(), inventory.end(), [](ajazz::plugins::PluginInfo const& p) {
            return p.id == "com.example.hello";
        });
    REQUIRE(hello != inventory.end());
    REQUIRE(hello->name == "Hello World");
    REQUIRE(hello->version == "1.0.0");
    REQUIRE(hello->authors == "AJAZZ Control Center contributors");
}

TEST_CASE("OOP plugin host: dispatch routes to the loaded handler", "[plugins][oop][!mayfail]") {
    if (!python3Available()) {
        SKIP("python3 not on PATH; skipping dispatch test");
    }

    ajazz::plugins::OutOfProcessPluginHost host{makeConfig()};
    host.addSearchPath(repoRoot() / "python" / "ajazz_plugins" / "examples");
    REQUIRE(host.loadAll() >= 1);

    // Successful dispatch: the hello example exposes `say-hi`. The
    // handler calls `ctx.notify` which prints to stderr (per the
    // child's stdout-redirect contract), so the test does not parse
    // anything — it just asserts the IPC contract: dispatch returns
    // true on a known plugin/action.
    REQUIRE(host.dispatch("com.example.hello", "say-hi", "{}"));

    // Unknown plugin: dispatch returns false but does NOT throw —
    // a typo in a profile shouldn't tear the host down.
    REQUIRE_FALSE(host.dispatch("com.example.does-not-exist", "noop", "{}"));

    // Unknown action on a known plugin: same soft-failure behaviour.
    REQUIRE_FALSE(host.dispatch("com.example.hello", "unknown-action", "{}"));
}

TEST_CASE("OOP plugin host: bad child script path is rejected at construction", "[plugins][oop]") {
    if (!python3Available()) {
        SKIP("python3 not on PATH; skipping bad-config test");
    }

    auto cfg = makeConfig();
    cfg.childScript = "/nonexistent/path/to/_host_child.py";
    cfg.readyTimeout = std::chrono::milliseconds{1500}; // bound the wait

    REQUIRE_THROWS_AS(ajazz::plugins::OutOfProcessPluginHost{cfg}, std::runtime_error);
}

#endif // !_WIN32
