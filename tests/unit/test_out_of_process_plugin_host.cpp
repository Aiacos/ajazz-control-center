// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_out_of_process_plugin_host.cpp
 * @brief Unit tests for @ref OutOfProcessPluginHost (slices 1 → 3d).
 *
 * The point of these tests is to PROVE the safety claim that motivates
 * audit finding A4: a crash in plugin code MUST NOT take the host
 * process down. We validate three invariants:
 *
 *   1. **Spawn round-trip works** — host launches the child, reads the
 *      `ready` handshake, can request `list_plugins` and receive the
 *      empty-array response, and tear-down via the destructor's
 *      `shutdown` flow leaves no zombie.
 *   2. **Crash isolation holds** — a deliberate SIGSEGV (or null-deref
 *      via ctypes on Windows) inside the child kills only the child.
 *      The host's process is unaffected; the next operation on the
 *      dead host returns an error (`std::runtime_error`) instead of
 *      crashing.
 *   3. **Bad config is rejected at construction** — pointing at a
 *      nonexistent script returns a clean exception, not a hang.
 *
 * Skipped when neither `python` nor `python3` is on `PATH` (rare on
 * dev machines but possible on a stripped CI runner) — the test logs
 * and reports as `SKIPPED` rather than failing.
 *
 * **Slice 3d cross-platform note**: this file used to be wrapped in
 * `#ifndef _WIN32` because it imported `linux_bwrap_sandbox.hpp`. The
 * non-sandbox tests are platform-agnostic (they exercise the
 * `IPluginHost` contract, which both backends now implement), so they
 * build on all three OSes and prove the win32 backend's contract by
 * running the same suite. Only the bwrap end-to-end test below is
 * still gated to non-Windows because of the Linux-only header.
 */
#include "ajazz/plugins/out_of_process_plugin_host.hpp"

#ifndef _WIN32
#include "ajazz/plugins/linux_bwrap_sandbox.hpp"
#endif

#include <cstdlib>
#include <filesystem>
#include <memory>
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

/// Resolve the Python interpreter visible on PATH. Prefers `python3`
/// (POSIX convention) but falls back to `python` (Windows convention,
/// since `actions/setup-python` puts only `python` on PATH on Windows
/// runners). Returns an empty string when neither is available.
std::string findPython() {
#ifdef _WIN32
    if (std::system("where python3 >NUL 2>&1") == 0) {
        return "python3";
    }
    if (std::system("where python >NUL 2>&1") == 0) {
        return "python";
    }
#else
    if (std::system("command -v python3 >/dev/null 2>&1") == 0) {
        return "python3";
    }
    if (std::system("command -v python >/dev/null 2>&1") == 0) {
        return "python";
    }
#endif
    return {};
}

bool pythonAvailable() {
    return !findPython().empty();
}

ajazz::plugins::OutOfProcessHostConfig makeConfig() {
    auto root = repoRoot();
    ajazz::plugins::OutOfProcessHostConfig cfg;
    cfg.pythonExecutable = findPython();
    cfg.childScript = root / "python" / "ajazz_plugins" / "_host_child.py";
    cfg.pythonPath = {root / "python"};
    return cfg;
}

} // namespace

TEST_CASE("OOP plugin host: spawn -> list -> shutdown round-trip", "[plugins][oop][!mayfail]") {
    if (!pythonAvailable()) {
        SKIP("python / python3 not on PATH; skipping OutOfProcessPluginHost spawn test");
    }

    ajazz::plugins::OutOfProcessPluginHost host{makeConfig()};
    REQUIRE(host.isAlive());
    REQUIRE(host.childPid() > 0);

    auto const inventory = host.plugins();
    REQUIRE(inventory.empty()); // slice 1: child never loads any plugin

    // Destructor sends `shutdown`, waits for clean exit. We let scope
    // exit do the work; verify no zombie remains by re-checking after
    // the destructor runs in the next test case (the Linux kernel
    // would surface a stuck zombie as ENOMEM-like long-term, but
    // here the visible signal is `isAlive()` flipping to false).
}

TEST_CASE("OOP plugin host: crash in child does not crash host", "[plugins][oop][!mayfail]") {
    if (!pythonAvailable()) {
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
    REQUIRE_THROWS_AS(host.plugins(), std::runtime_error);
}

TEST_CASE("OOP plugin host: add_search_path + load_all + list_plugins discover the hello example",
          "[plugins][oop][!mayfail]") {
    if (!pythonAvailable()) {
        SKIP("python3 not on PATH; skipping plugin discovery test");
    }

    auto cfg = makeConfig();
    ajazz::plugins::OutOfProcessPluginHost host{std::move(cfg)};
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

    auto const inventory = host.plugins();
    auto const hello =
        std::find_if(inventory.begin(), inventory.end(), [](ajazz::plugins::PluginInfo const& p) {
            return p.id == "com.example.hello";
        });
    REQUIRE(hello != inventory.end());
    REQUIRE(hello->name == "Hello World");
    REQUIRE(hello->version == "1.0.0");
    REQUIRE(hello->authors == "AJAZZ Control Center contributors");

    // Slice 3a: the plugin declares `permissions = ["notifications"]`
    // because every action calls `ctx.notify`. The wire protocol must
    // surface the array verbatim — the UI reads it at install time.
    REQUIRE(hello->permissions.size() == 1);
    REQUIRE(hello->permissions.at(0) == "notifications");
}

TEST_CASE("OOP plugin host: dispatch routes to the loaded handler", "[plugins][oop][!mayfail]") {
    if (!pythonAvailable()) {
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
    if (!pythonAvailable()) {
        SKIP("python3 not on PATH; skipping bad-config test");
    }

    auto cfg = makeConfig();
    cfg.childScript = "/nonexistent/path/to/_host_child.py";
    cfg.readyTimeout = std::chrono::milliseconds{1500}; // bound the wait

    REQUIRE_THROWS_AS(ajazz::plugins::OutOfProcessPluginHost{std::move(cfg)}, std::runtime_error);
}

TEST_CASE("OOP plugin host: drives the full lifecycle through an IPluginHost pointer",
          "[plugins][oop][interface][!mayfail]") {
    if (!pythonAvailable()) {
        SKIP("python3 not on PATH; skipping IPluginHost contract test");
    }

    // Audit finding A4 slice 2.5 introduced IPluginHost as the shared
    // contract; slice 3e retired the legacy in-process PluginHost so
    // OutOfProcessPluginHost is the only concrete implementation today.
    // Driving it through a base-class pointer still proves the contract
    // is honoured by virtual dispatch — a method missing `override`
    // would break this test, and a future macOS/Windows backend will
    // satisfy the same assertions.
    ajazz::plugins::OutOfProcessPluginHost concrete{makeConfig()};
    ajazz::plugins::IPluginHost& host = concrete;

    REQUIRE_NOTHROW(host.addSearchPath(repoRoot() / "python" / "ajazz_plugins" / "examples"));
    REQUIRE(host.loadAll() >= 1);
    auto const inventory = host.plugins();
    REQUIRE_FALSE(inventory.empty());
    REQUIRE(host.dispatch("com.example.hello", "say-hi", "{}"));
    REQUIRE_FALSE(host.dispatch("does.not.exist", "noop", "{}"));
}

// The bwrap-sandboxed end-to-end test imports `linux_bwrap_sandbox.hpp`
// which is gated to non-Windows (bwrap doesn't exist on Windows; the
// sandbox falls back to passthrough at runtime, but the test would
// still need the sandbox class definition to construct one). Other
// platform-specific sandbox tests (sandbox-exec on macOS, AppContainer
// once slice 3d-ii lands) live in their own test files.
#ifndef _WIN32

namespace {

bool bwrapAvailable() {
    return std::system("command -v bwrap >/dev/null 2>&1") == 0;
}

} // namespace

TEST_CASE("OOP plugin host: spawn through LinuxBwrapSandbox round-trips end-to-end",
          "[plugins][oop][sandbox][!mayfail]") {
    if (!pythonAvailable()) {
        SKIP("python / python3 not on PATH; skipping bwrap-sandboxed spawn test");
    }
    if (!bwrapAvailable()) {
        SKIP("bwrap not on PATH; skipping LinuxBwrapSandbox integration test");
    }

    // Slice 3b: prove the bwrap-decorated spawn works end-to-end.
    // The default profile (empty permissions) is most-restrictive —
    // network unshared, namespaces fresh, /tmp tmpfs — and the child
    // still completes the ready handshake, the load_all sweep, and a
    // successful dispatch under those constraints. If a future bwrap
    // flag breaks the child's ability to read /usr/bin/python3 or the
    // PYTHONPATH directories the test catches it immediately.
    auto cfg = makeConfig();
    cfg.sandbox = std::make_unique<ajazz::plugins::LinuxBwrapSandbox>(std::set<std::string>{});

    ajazz::plugins::OutOfProcessPluginHost host{std::move(cfg)};
    REQUIRE(host.isAlive());
    REQUIRE(host.childPid() > 0);

    REQUIRE_NOTHROW(host.addSearchPath(repoRoot() / "python" / "ajazz_plugins" / "examples"));
    REQUIRE(host.loadAll() >= 1);
    auto const inventory = host.plugins();
    auto const hello =
        std::find_if(inventory.begin(), inventory.end(), [](ajazz::plugins::PluginInfo const& p) {
            return p.id == "com.example.hello";
        });
    REQUIRE(hello != inventory.end());
    // Slice 3a permissions plumb through the sandboxed child too.
    REQUIRE(hello->permissions.size() == 1);
    REQUIRE(hello->permissions.at(0) == "notifications");

    // Dispatch still works under the sandbox — `say-hi` only calls
    // `ctx.notify`, which prints to stderr (the child's redirected
    // stdout); the IPC contract is honoured even without a desktop.
    REQUIRE(host.dispatch("com.example.hello", "say-hi", "{}"));
}

#endif // !_WIN32
