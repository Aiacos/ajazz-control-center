// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_linux_bwrap_sandbox.cpp
 * @brief Unit tests for @ref ajazz::plugins::LinuxBwrapSandbox.
 *
 * The decoration logic is pure: given a permissions set + a python
 * exe + a script path, produce an argv. We pin the rules in the
 * audit-finding-A4 slice 3b spec:
 *
 *   1. **Default profile is most-restrictive** — every namespace
 *      except user is unshared, and `--unshare-net` is present.
 *   2. **Network permissions drop --unshare-net** — granting any of
 *      `obs-websocket`, `spotify`, `discord-rpc` removes the flag.
 *   3. **DBus permissions add session-bus bind-mount** — only when
 *      `XDG_RUNTIME_DIR/bus` exists.
 *   4. **Argv terminates with python + script** — bwrap's `--`
 *      separator is present and the inner command is unchanged.
 *   5. **No bwrap on PATH falls back to passthrough** — no decoration,
 *      just `(python3, [python3, script])`.
 *
 * The tests do NOT spawn anything — the decorate() call is offline.
 * The end-to-end "spawn through bwrap" test lives in
 * test_out_of_process_plugin_host.cpp where the host machinery is
 * already in scope.
 */
#ifndef _WIN32

#include "ajazz/plugins/linux_bwrap_sandbox.hpp"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace {

bool contains(std::vector<std::string> const& argv, std::string_view needle) {
    return std::any_of(argv.begin(), argv.end(), [&](std::string const& s) { return s == needle; });
}

/// Locate `flag` in argv and return the immediately-following arg, or
/// empty if the flag is absent. Used to assert e.g.
/// `--ro-bind / /` carries the right source/target.
std::pair<std::string, std::string> argAfter(std::vector<std::string> const& argv,
                                             std::string_view flag) {
    for (std::size_t i = 0; i + 2 < argv.size(); ++i) {
        if (argv[i] == flag) {
            return {argv[i + 1], argv[i + 2]};
        }
    }
    return {};
}

/// Build a sandbox using a forced bwrap path so the test is
/// independent of the dev machine's `PATH`. `/bin/sh` is universally
/// executable and lets us drive the "hasBwrap == true" branch without
/// actually invoking bwrap during decoration.
ajazz::plugins::LinuxBwrapSandbox makeSandbox(std::set<std::string> permissions) {
    return ajazz::plugins::LinuxBwrapSandbox{std::move(permissions), "/bin/sh"};
}

} // namespace

TEST_CASE("LinuxBwrapSandbox: passthrough when bwrap absent", "[plugins][sandbox]") {
    // Forced-empty bwrapExecutable + non-executable path → falls into
    // passthrough mode regardless of what's actually on PATH.
    ajazz::plugins::LinuxBwrapSandbox sandbox{{}, "/nonexistent/bwrap"};
    REQUIRE_FALSE(sandbox.hasBwrap());
    auto const spawn = sandbox.decorate("python3", "/tmp/host_child.py");
    REQUIRE(spawn.executable == "python3");
    REQUIRE(spawn.args == std::vector<std::string>{"python3", "/tmp/host_child.py"});
}

TEST_CASE("LinuxBwrapSandbox: default profile is most-restrictive", "[plugins][sandbox]") {
    auto const sandbox = makeSandbox({});
    REQUIRE(sandbox.hasBwrap());

    auto const spawn = sandbox.decorate("python3", "/tmp/host_child.py");
    REQUIRE(spawn.executable == "/bin/sh");
    REQUIRE(spawn.args.front() == "/bin/sh");

    // The most-restrictive profile MUST include these flags. Any of
    // them missing is a regression that reduces isolation.
    REQUIRE(contains(spawn.args, "--unshare-pid"));
    REQUIRE(contains(spawn.args, "--unshare-ipc"));
    REQUIRE(contains(spawn.args, "--unshare-uts"));
    REQUIRE(contains(spawn.args, "--unshare-cgroup-try"));
    REQUIRE(contains(spawn.args, "--unshare-net"));
    REQUIRE(contains(spawn.args, "--die-with-parent"));
    REQUIRE(contains(spawn.args, "--new-session"));

    // Filesystem layout: read-only host, fresh /proc + /dev, tmpfs /tmp.
    auto const [roSrc, roDst] = argAfter(spawn.args, "--ro-bind");
    REQUIRE(roSrc == "/");
    REQUIRE(roDst == "/");
    REQUIRE(contains(spawn.args, "--proc"));
    REQUIRE(contains(spawn.args, "--dev"));
    REQUIRE(contains(spawn.args, "--tmpfs"));

    // Inner command terminator + Python exec.
    REQUIRE(contains(spawn.args, "--"));
    REQUIRE(spawn.args.at(spawn.args.size() - 2) == "python3");
    REQUIRE(spawn.args.back() == "/tmp/host_child.py");
}

TEST_CASE("LinuxBwrapSandbox: network permission drops --unshare-net", "[plugins][sandbox]") {
    SECTION("obs-websocket") {
        auto const sandbox = makeSandbox({"obs-websocket"});
        auto const spawn = sandbox.decorate("python3", "/tmp/host_child.py");
        REQUIRE_FALSE(contains(spawn.args, "--unshare-net"));
    }
    SECTION("spotify") {
        auto const sandbox = makeSandbox({"spotify"});
        auto const spawn = sandbox.decorate("python3", "/tmp/host_child.py");
        REQUIRE_FALSE(contains(spawn.args, "--unshare-net"));
    }
    SECTION("discord-rpc") {
        auto const sandbox = makeSandbox({"discord-rpc"});
        auto const spawn = sandbox.decorate("python3", "/tmp/host_child.py");
        REQUIRE_FALSE(contains(spawn.args, "--unshare-net"));
    }
    SECTION("non-network permission keeps --unshare-net") {
        auto const sandbox = makeSandbox({"clipboard-read", "shell-exec"});
        auto const spawn = sandbox.decorate("python3", "/tmp/host_child.py");
        REQUIRE(contains(spawn.args, "--unshare-net"));
    }
}

TEST_CASE("LinuxBwrapSandbox: DBus permissions can bind the session bus when present",
          "[plugins][sandbox]") {
    // The session-bus bind-mount only fires when XDG_RUNTIME_DIR/bus
    // actually exists. We can't fake the file in CI without writing
    // root-owned paths, so we observe the live behaviour: if the
    // socket exists locally (typical dev machine), DBus permissions
    // produce a second `--ro-bind <bus> <bus>` pair; if the socket is
    // absent (CI runner), they don't. Both branches are valid — the
    // assertion is about consistency.
    char const* xdg = std::getenv("XDG_RUNTIME_DIR");
    bool const busExists = xdg != nullptr && std::filesystem::exists(std::string{xdg} + "/bus");

    auto const sandbox = makeSandbox({"notifications"});
    auto const spawn = sandbox.decorate("python3", "/tmp/host_child.py");

    // Count the `--ro-bind` occurrences: 1 for the root mount,
    // optionally +1 for the bus mount.
    auto const roBindCount =
        std::count(spawn.args.begin(), spawn.args.end(), std::string{"--ro-bind"});
    if (busExists) {
        REQUIRE(roBindCount == 2);
        // The bus-bind always uses the same path on src and dst.
        std::string const expected = std::string{xdg} + "/bus";
        bool sawBusBind = false;
        for (std::size_t i = 0; i + 2 < spawn.args.size(); ++i) {
            if (spawn.args[i] == "--ro-bind" && spawn.args[i + 1] == expected &&
                spawn.args[i + 2] == expected) {
                sawBusBind = true;
                break;
            }
        }
        REQUIRE(sawBusBind);
    } else {
        REQUIRE(roBindCount == 1);
    }
}

TEST_CASE("LinuxBwrapSandbox: unknown permission strings are silently ignored",
          "[plugins][sandbox]") {
    auto const sandbox = makeSandbox({"not-a-real-permission", "neither-is-this"});
    auto const spawn = sandbox.decorate("python3", "/tmp/host_child.py");
    // No relaxation should happen — the profile is still most-restrictive.
    REQUIRE(contains(spawn.args, "--unshare-net"));
    auto const roBindCount =
        std::count(spawn.args.begin(), spawn.args.end(), std::string{"--ro-bind"});
    REQUIRE(roBindCount == 1);
}

#endif // !_WIN32
