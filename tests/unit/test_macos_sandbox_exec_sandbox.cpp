// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_macos_sandbox_exec_sandbox.cpp
 * @brief Unit tests for @ref ajazz::plugins::MacosSandboxExecSandbox.
 *
 * Symmetric to test_linux_bwrap_sandbox.cpp: the decoration and
 * profile-generation logic are pure, given a permissions set + a
 * python exe + a script path produce a profile string + an argv.
 * We pin the rules in the audit-finding-A4 slice 3c spec:
 *
 *   1. **Default profile is most-restrictive** — `(deny default)` is
 *      present, network is unmentioned (so denied), `mach-lookup`
 *      is allowed only for the bootstrap-server lookup CoreFoundation
 *      requires to start.
 *   2. **Network permissions add (allow network*)** — granting any of
 *      `obs-websocket`, `spotify`, `discord-rpc` produces the rule.
 *   3. **DBus-equivalent permissions add (allow mach-lookup)** —
 *      granting any of `notifications`, `media-control`, `system-power`
 *      adds the broad lookup allowance.
 *   4. **Argv terminates with python + script** after `-p <profile>`.
 *   5. **No sandbox-exec on the box falls back to passthrough** —
 *      no decoration, just `(python3, [python3, script])`.
 *
 * The tests do NOT actually invoke `sandbox-exec`; they exercise the
 * pure policy generator + argv builder. The tests run on Linux too
 * because the class itself is portable POSIX C++, with the
 * sandbox-exec lookup overridden by a `/bin/sh` shim in tests.
 *
 * Slice 3c does not E2E test the sandbox on macOS — that requires a
 * macOS runner and is gated on a future CI matrix expansion. The
 * shape is identical to the Linux-side bwrap E2E test
 * (`spawn through LinuxBwrapSandbox round-trips end-to-end`).
 */
#ifndef _WIN32

#include "ajazz/plugins/macos_sandbox_exec_sandbox.hpp"

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

bool argvContains(std::vector<std::string> const& argv, std::string_view needle) {
    return std::any_of(argv.begin(), argv.end(), [&](std::string const& s) { return s == needle; });
}

bool profileContains(std::string const& profile, std::string_view rule) {
    return profile.find(rule) != std::string::npos;
}

/// Build a sandbox using a forced sandbox-exec path so the test is
/// independent of the dev machine's filesystem layout. `/bin/sh` is
/// universally executable and lets us drive the
/// "hasSandboxExec == true" branch on Linux runners without needing
/// to actually invoke macOS-only `sandbox-exec`.
ajazz::plugins::MacosSandboxExecSandbox makeSandbox(std::set<std::string> permissions) {
    return ajazz::plugins::MacosSandboxExecSandbox{std::move(permissions), {}, "/bin/sh"};
}

/// As @ref makeSandbox but with an explicit read-only allowlist.
ajazz::plugins::MacosSandboxExecSandbox
makeSandboxWithReadable(std::set<std::string> permissions,
                        std::vector<std::filesystem::path> readablePaths) {
    return ajazz::plugins::MacosSandboxExecSandbox{
        std::move(permissions), std::move(readablePaths), "/bin/sh"};
}

} // namespace

TEST_CASE("MacosSandboxExecSandbox: passthrough when sandbox-exec absent",
          "[plugins][sandbox][macos]") {
    // Forced-empty path with a non-executable target → passthrough,
    // regardless of whether the host machine is macOS or Linux.
    ajazz::plugins::MacosSandboxExecSandbox sandbox{{}, {}, "/nonexistent/sandbox-exec"};
    REQUIRE_FALSE(sandbox.hasSandboxExec());
    auto const spawn = sandbox.decorate("python3", "/tmp/host_child.py");
    REQUIRE(spawn.executable == "python3");
    REQUIRE(spawn.args == std::vector<std::string>{"python3", "/tmp/host_child.py"});
}

TEST_CASE("MacosSandboxExecSandbox: default profile is most-restrictive",
          "[plugins][sandbox][macos]") {
    auto const sandbox = makeSandbox({});
    REQUIRE(sandbox.hasSandboxExec());

    auto const& profile = sandbox.profile();
    REQUIRE(profileContains(profile, "(version 1)"));
    REQUIRE(profileContains(profile, "(deny default)"));
    REQUIRE(profileContains(profile, "(allow process-fork)"));
    REQUIRE(profileContains(profile, "(allow process-exec*)"));
    REQUIRE(profileContains(profile, "(allow signal (target self))"));
    // file-read* is SCOPED to a system allowlist, NOT the former
    // blanket `(allow file-read*)` (which let plugins read $HOME —
    // CWE-200). The blanket form (the rule terminated immediately by a
    // newline) must be gone; the scoped subpath form must be present.
    REQUIRE_FALSE(profileContains(profile, "(allow file-read*)\n"));
    REQUIRE(profileContains(profile, "(allow file-read* (subpath \"/usr\")"));
    REQUIRE(profileContains(profile, "(subpath \"/System\")"));
    REQUIRE(profileContains(profile, "(subpath \"/Library\")"));
    REQUIRE(profileContains(profile, "(allow file-write* (subpath \"/private/var/folders\")"));
    // `(allow network*)` must NOT be present in the default profile —
    // its absence is what enforces "no network without permission".
    REQUIRE_FALSE(profileContains(profile, "(allow network*)"));
    // The broad mach-lookup rule must NOT be present either; only the
    // narrow bootstrap-server rule for CoreFoundation init.
    REQUIRE_FALSE(profileContains(profile, "(allow mach-lookup)\n"));
    REQUIRE(profileContains(profile, "(allow mach-lookup (global-name "));

    // Argv shape: sandbox-exec -p <profile> python3 <script>. The
    // profile string lives between the `-p` flag and the inner
    // python3 invocation.
    auto const spawn = sandbox.decorate("python3", "/tmp/host_child.py");
    REQUIRE(spawn.executable == "/bin/sh");
    REQUIRE(spawn.args.front() == "/bin/sh");
    REQUIRE(argvContains(spawn.args, "-p"));
    REQUIRE(spawn.args.at(spawn.args.size() - 2) == "python3");
    REQUIRE(spawn.args.back() == "/tmp/host_child.py");
}

TEST_CASE("MacosSandboxExecSandbox: readable allowlist scopes file-read to supplied paths",
          "[plugins][sandbox][macos]") {
    auto const sandbox =
        makeSandboxWithReadable({}, {"/opt/ajazz/python", "/var/lib/ajazz/plugins"});

    // The cached profile carries the readable subpaths but NOT the
    // per-spawn script parent.
    auto const& profile = sandbox.profile();
    REQUIRE(profileContains(profile, "(subpath \"/opt/ajazz/python\")"));
    REQUIRE(profileContains(profile, "(subpath \"/var/lib/ajazz/plugins\")"));
    REQUIRE_FALSE(profileContains(profile, "(allow file-read*)\n"));

    // decorate() appends a file-read* rule for the script's parent dir.
    auto const spawn = sandbox.decorate("python3", "/opt/ajazz/host/_host_child.py");
    // The profile string lives at args[2] (after argv[0] and "-p").
    REQUIRE(spawn.args.size() >= 3);
    std::string const& spawnProfile = spawn.args.at(2);
    REQUIRE(spawnProfile.find("(allow file-read* (subpath \"/opt/ajazz/host\"))") !=
            std::string::npos);
    // $HOME is never granted.
    REQUIRE(spawnProfile.find("(subpath \"/home") == std::string::npos);
}

TEST_CASE("MacosSandboxExecSandbox: network permissions enable (allow network*)",
          "[plugins][sandbox][macos]") {
    SECTION("obs-websocket") {
        auto const sandbox = makeSandbox({"obs-websocket"});
        REQUIRE(profileContains(sandbox.profile(), "(allow network*)"));
    }
    SECTION("spotify") {
        auto const sandbox = makeSandbox({"spotify"});
        REQUIRE(profileContains(sandbox.profile(), "(allow network*)"));
    }
    SECTION("discord-rpc") {
        auto const sandbox = makeSandbox({"discord-rpc"});
        REQUIRE(profileContains(sandbox.profile(), "(allow network*)"));
    }
    SECTION("non-network permission keeps network denied") {
        auto const sandbox = makeSandbox({"clipboard-read", "shell-exec"});
        REQUIRE_FALSE(profileContains(sandbox.profile(), "(allow network*)"));
    }
}

TEST_CASE("MacosSandboxExecSandbox: DBus-equivalent permissions enable (allow mach-lookup)",
          "[plugins][sandbox][macos]") {
    SECTION("notifications") {
        auto const sandbox = makeSandbox({"notifications"});
        REQUIRE(profileContains(sandbox.profile(), "(allow mach-lookup)"));
    }
    SECTION("media-control") {
        auto const sandbox = makeSandbox({"media-control"});
        REQUIRE(profileContains(sandbox.profile(), "(allow mach-lookup)"));
    }
    SECTION("system-power") {
        auto const sandbox = makeSandbox({"system-power"});
        REQUIRE(profileContains(sandbox.profile(), "(allow mach-lookup)"));
    }
    SECTION("non-DBus permission keeps mach lookups narrow") {
        auto const sandbox = makeSandbox({"shell-exec"});
        // Bootstrap-server rule still present; broad rule absent.
        REQUIRE(profileContains(sandbox.profile(), "(allow mach-lookup (global-name "));
        REQUIRE_FALSE(profileContains(sandbox.profile(), "(allow mach-lookup)\n"));
    }
}

TEST_CASE("MacosSandboxExecSandbox: unknown permission strings are silently ignored",
          "[plugins][sandbox][macos]") {
    auto const sandbox = makeSandbox({"not-a-real-permission", "neither-is-this"});
    auto const& profile = sandbox.profile();
    // Default-restrictive profile — no relaxations should leak in.
    REQUIRE_FALSE(profileContains(profile, "(allow network*)"));
    REQUIRE_FALSE(profileContains(profile, "(allow mach-lookup)\n"));
}

TEST_CASE("MacosSandboxExecSandbox: profile is stable across decorate() calls",
          "[plugins][sandbox][macos]") {
    // The profile is computed once at construction and cached. Two
    // decorate() calls with the same args should produce byte-identical
    // argv. Catches a regression where someone moves profile generation
    // back into decorate() and accidentally introduces nondeterminism.
    auto const sandbox = makeSandbox({"obs-websocket", "notifications"});
    auto const a = sandbox.decorate("python3", "/tmp/host_child.py");
    auto const b = sandbox.decorate("python3", "/tmp/host_child.py");
    REQUIRE(a.executable == b.executable);
    REQUIRE(a.args == b.args);
}

#endif // !_WIN32
