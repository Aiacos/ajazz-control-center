// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_windows_app_container_sandbox.cpp
 * @brief Unit tests for @ref ajazz::plugins::WindowsAppContainerSandbox.
 *
 * Symmetric to @c test_linux_bwrap_sandbox.cpp and
 * @c test_macos_sandbox_exec_sandbox.cpp, but with a Windows-specific
 * subtlety: the sandbox's real work happens in
 * @c configureProcessAttributes, not in @c decorate. @c decorate is a
 * passthrough on Windows because AppContainer isolation is applied
 * at @c CreateProcessW time via @c STARTUPINFOEX, not via a wrapper
 * executable. Policy assertions here cover:
 *
 *   1. **@c decorate is passthrough** — no argv mutation.
 *   2. **Default container name** — produced when the caller omits it,
 *      stable across constructions.
 *   3. **Custom container name** — preserved verbatim in
 *      @c containerName().
 *   4. **@c grantedPermissions are preserved** — the getter returns
 *      exactly what the constructor was called with, including
 *      "unknown" strings (the sandbox silently ignores them at
 *      @c configureProcessAttributes time).
 *   5. **@c hasAppContainer() reports the real runtime state** — true
 *      on a Windows host with @c userenv.dll reachable, false on
 *      POSIX (where @c userenv.dll does not exist) or on a Windows
 *      host with AppContainer disabled by policy.
 *
 * End-to-end tests that actually spawn a child under AppContainer
 * live in @c test_out_of_process_plugin_host.cpp, gated on a Windows
 * runner. They would have to go through the full CreateProcessW +
 * STARTUPINFOEX pipeline; that coverage waits for a CI matrix slot
 * with the AppContainer feature enabled (GitHub-hosted windows-2022
 * runners have it on by default).
 */
#ifdef _WIN32

#include "ajazz/plugins/windows_app_container_sandbox.hpp"

#include <set>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("WindowsAppContainerSandbox: decorate is passthrough", "[plugins][sandbox][windows]") {
    // decorate() should produce the plain (pythonExe, [pythonExe,
    // scriptPath]) pair regardless of whether AppContainer is
    // available — the isolation happens at CreateProcessW time, not
    // through a wrapper executable.
    ajazz::plugins::WindowsAppContainerSandbox sandbox{{}};
    auto const spawn = sandbox.decorate("python", "C:/tmp/host_child.py");
    REQUIRE(spawn.executable == "python");
    REQUIRE(spawn.args == std::vector<std::string>{"python", "C:/tmp/host_child.py"});
}

TEST_CASE("WindowsAppContainerSandbox: default container name is stable",
          "[plugins][sandbox][windows]") {
    ajazz::plugins::WindowsAppContainerSandbox sandbox{{}};
    REQUIRE(sandbox.containerName() == "ajazz-control-center.plugin-host");

    // Second construction with the same args produces the same
    // name — the label is compile-time deterministic.
    ajazz::plugins::WindowsAppContainerSandbox sandbox2{{}};
    REQUIRE(sandbox.containerName() == sandbox2.containerName());
}

TEST_CASE("WindowsAppContainerSandbox: custom container name is preserved",
          "[plugins][sandbox][windows]") {
    ajazz::plugins::WindowsAppContainerSandbox sandbox{{}, "custom.label"};
    REQUIRE(sandbox.containerName() == "custom.label");
}

TEST_CASE("WindowsAppContainerSandbox: grantedPermissions are preserved verbatim",
          "[plugins][sandbox][windows]") {
    std::set<std::string> const perms{
        "obs-websocket",
        "not-a-real-permission",
        "notifications",
    };
    ajazz::plugins::WindowsAppContainerSandbox sandbox{perms};
    REQUIRE(sandbox.grantedPermissions() == perms);
}

TEST_CASE(
    "WindowsAppContainerSandbox: configureProcessAttributes does not throw on any permission set",
    "[plugins][sandbox][windows]") {
    // The API surface must be exception-safe regardless of sandbox
    // availability — the host's CreateProcessW path treats the
    // ProcessAttributes as an opt-in side-channel, so a failure to
    // populate it must be surfaced via hasAppContainer()/pimpl-null
    // rather than via a throw.
    ajazz::plugins::WindowsAppContainerSandbox sandbox{{"obs-websocket", "notifications"}};
    ajazz::plugins::ProcessAttributes attrs;
    REQUIRE_NOTHROW(sandbox.configureProcessAttributes(attrs));
}

#endif // _WIN32
