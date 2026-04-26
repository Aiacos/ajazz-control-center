// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file macos_sandbox_exec_sandbox.hpp
 * @brief `sandbox-exec(1)`-based plugin-host sandbox for macOS.
 *
 * Audit finding A4 — slice 3c. macOS counterpart of
 * @ref LinuxBwrapSandbox. Wraps the @ref OutOfProcessPluginHost child
 * invocation in a `sandbox-exec` call configured against a granted
 * permission set, where the granted set comes from the
 * @c Ajazz.Permissions enum in
 * @c docs/schemas/plugin_manifest.schema.json.
 *
 * `sandbox-exec(1)` is part of every shipping macOS (it is the
 * underlying mechanism behind macOS app sandboxing and the
 * `seatbelt` framework). It accepts an inline TinyScheme-derived
 * profile via @c -p and forks-execs the child under the resulting
 * sandbox.
 *
 * Default profile (empty permissions set):
 *
 *   - `(version 1)` — required first form.
 *   - `(deny default)` — start from a closed posture.
 *   - Allow the bare minimum the python3 interpreter needs to start:
 *     process exec/fork, signal-self, sysctl-read, and unrestricted
 *     file-read* (so /usr/bin/python3 + the standard library are
 *     reachable). Matches @ref LinuxBwrapSandbox's `--ro-bind / /`
 *     posture.
 *   - Allow file-write* only under the user's @c $TMPDIR, mirroring
 *     bwrap's `--tmpfs /tmp`.
 *
 * Permission-driven relaxations:
 *
 *   - `obs-websocket`, `spotify`, `discord-rpc` (any of) ⇒ append
 *     `(allow network*)` so plugins can reach their network endpoints.
 *   - `notifications`, `media-control`, `system-power` (any of) ⇒
 *     append `(allow mach-lookup)` so the child can talk to the
 *     UserNotifications center (`com.apple.usernotificationsd`),
 *     MediaRemote, and IOPower-management mach services. Slice 3c
 *     keeps the broad rule; slice 4 narrows to specific
 *     `(global-name "...")` clauses once we have measurements of
 *     which services each permission actually contacts.
 *
 * @par sandbox-exec availability
 *
 * If `/usr/bin/sandbox-exec` is missing at construction time the
 * sandbox falls into passthrough mode (equivalent to @ref NoOpSandbox),
 * matching the bwrap-fallback contract on Linux. This keeps the
 * library functional on stripped systems and on non-macOS POSIX
 * environments where the file is naturally absent — a safety net,
 * not a blanket "macOS-only" build gate.
 *
 * @par What this slice does NOT cover
 *
 * - TCC consent (camera / microphone / screen-capture). macOS
 *   surfaces those through a separate user prompt; the sandbox
 *   profile cannot grant TCC permissions.
 * - Per-plugin custom profiles. Every plugin loaded into one host
 *   shares the same `sandbox-exec` policy — same constraint as the
 *   `LinuxBwrapSandbox`. Per-plugin spawn-on-demand is slice 4.
 * - Hardened-runtime / notarisation. Those affect the host binary
 *   itself, not the sandbox the host applies to its child.
 */
#pragma once

#ifndef _WIN32

#include "ajazz/plugins/sandbox.hpp"

#include <filesystem>
#include <set>
#include <string>

namespace ajazz::plugins {

/**
 * @brief macOS-specific sandbox using `sandbox-exec(1)`.
 *
 * Construct once at app boot with the union of permissions the host
 * is willing to grant the child, then move the resulting
 * @c std::unique_ptr<Sandbox> into @c OutOfProcessHostConfig::sandbox.
 *
 * The class itself is portable POSIX C++ — it builds on Linux too
 * (where `/usr/bin/sandbox-exec` is absent and the sandbox falls into
 * passthrough). That keeps slice-3c unit tests runnable on the Linux
 * dev machine: the policy-string generator is pure logic and is
 * exercised by overriding the executable path in the constructor.
 */
class MacosSandboxExecSandbox final : public Sandbox {
public:
    /**
     * @brief Construct a sandbox-exec sandbox for the given permission set.
     *
     * @param grantedPermissions Strings from the
     *        @c Ajazz.Permissions enum that the host is willing to
     *        grant the child. Unknown strings are silently ignored.
     * @param sandboxExecExecutable Optional override for the
     *        @c sandbox-exec binary path. Empty (default) checks
     *        @c /usr/bin/sandbox-exec at construction time. Tests
     *        pass an explicit path so they don't depend on macOS
     *        being present.
     */
    explicit MacosSandboxExecSandbox(std::set<std::string> grantedPermissions,
                                     std::string sandboxExecExecutable = {});

    /// True if `sandbox-exec` was located at construction time.
    /// False puts the sandbox into passthrough mode — `decorate()`
    /// then returns the original spawn unchanged.
    [[nodiscard]] bool hasSandboxExec() const noexcept { return m_hasSandboxExec; }

    /// Path to the sandbox-exec binary. Empty when not located.
    [[nodiscard]] std::string const& sandboxExecExecutable() const noexcept {
        return m_sandboxExecExecutable;
    }

    /// Granted-permission set (read-only access for diagnostics +
    /// tests). The set is an immutable construction-time snapshot.
    [[nodiscard]] std::set<std::string> const& grantedPermissions() const noexcept {
        return m_grantedPermissions;
    }

    /// The S-expression profile string this sandbox would pass to
    /// `sandbox-exec -p`. Exposed for unit tests so they can pin the
    /// policy text without having to round-trip through `decorate()`.
    /// The string is computed once at construction and cached.
    [[nodiscard]] std::string const& profile() const noexcept { return m_profile; }

    [[nodiscard]] DecoratedSpawn decorate(std::string const& pythonExe,
                                          std::filesystem::path const& scriptPath) const override;

private:
    std::set<std::string> m_grantedPermissions;
    std::string m_sandboxExecExecutable; ///< empty if not found
    std::string m_profile;               ///< cached S-expression profile
    bool m_hasSandboxExec{false};
};

} // namespace ajazz::plugins

#endif // !_WIN32
