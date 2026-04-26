// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file linux_bwrap_sandbox.hpp
 * @brief `bubblewrap(1)`-based plugin-host sandbox for Linux.
 *
 * Audit finding A4 — slice 3b. Wraps the @ref OutOfProcessPluginHost
 * child invocation in a `bwrap` call configured against a granted
 * permission set. The permission strings come from the
 * @c Ajazz.Permissions enum in
 * @c docs/schemas/plugin_manifest.schema.json.
 *
 * Default profile (empty permissions set):
 *
 *   - `--ro-bind / /` — host filesystem mounted read-only
 *   - `--tmpfs /tmp` — writable scratch
 *   - `--proc /proc` + `--dev /dev` — fresh kernel filesystems
 *   - `--unshare-pid` / `--unshare-ipc` / `--unshare-uts`
 *     / `--unshare-cgroup-try` — namespace isolation
 *   - `--unshare-net` — network unreachable by default
 *   - `--die-with-parent` — child dies if the host crashes
 *   - `--new-session` — detach controlling terminal
 *
 * Permission-driven relaxations:
 *
 *   - `obs-websocket`, `spotify`, `discord-rpc` (any of) ⇒ drop
 *     `--unshare-net` so plugins can reach their network endpoints.
 *   - `notifications`, `media-control`, `system-power` (any of) ⇒
 *     bind-mount the user session DBus socket
 *     (`/run/user/<uid>/bus`) so the child can publish notifications
 *     and listen on `org.freedesktop.Notifications` /
 *     `org.mpris.MediaPlayer2` etc.
 *
 * @par Bwrap availability
 *
 * If `bwrap` is not on `PATH` at construction time, the sandbox
 * falls into a *passthrough* mode equivalent to @ref NoOpSandbox.
 * This keeps the host functional on minimal Docker images and on
 * non-Fedora/Arch distros where bubblewrap isn't pre-installed —
 * the alternative (refusing to spawn) would be a hard regression.
 * @ref hasBwrap returns the actual state for diagnostics / UI.
 *
 * @par What this slice does NOT cover
 *
 * - Per-plugin filesystem write paths (the `Ajazz.Sandbox` block in
 *   the manifest schema is unused by slice 3b — every plugin shares
 *   the same `/tmp` view).
 * - Seccomp filtering (slice 4 will add a basic syscall filter via
 *   `bwrap --seccomp`).
 * - User-namespace remapping to a non-root euid (a future hardening
 *   pass, blocked on `userns_clone` being default-on across distros).
 */
#pragma once

#ifndef _WIN32

#include "ajazz/plugins/sandbox.hpp"

#include <filesystem>
#include <set>
#include <string>

namespace ajazz::plugins {

/**
 * @brief Linux-specific sandbox using `bubblewrap(1)`.
 *
 * Construct once at app boot with the union of permissions the host
 * is willing to grant the child (typically: the union of
 * `Plugin.permissions` declared by every plugin to load), then move
 * the resulting @c std::unique_ptr<Sandbox> into
 * @c OutOfProcessHostConfig::sandbox.
 *
 * The constructor consults `PATH` for `bwrap` immediately — there
 * is no late availability detection. Tests can pass an explicit
 * `bwrapExecutable` to bypass `PATH` lookup.
 */
class LinuxBwrapSandbox final : public Sandbox {
public:
    /**
     * @brief Construct a bwrap sandbox for the given permission set.
     *
     * @param grantedPermissions Strings from the
     *        @c Ajazz.Permissions enum that the host is willing to
     *        grant the child. Unknown strings are silently ignored.
     * @param bwrapExecutable Optional override for the @c bwrap
     *        executable path. Empty (default) means resolve via
     *        @c PATH at construction time. Tests pass an explicit
     *        path so they don't depend on the dev machine layout.
     */
    explicit LinuxBwrapSandbox(std::set<std::string> grantedPermissions,
                               std::string bwrapExecutable = {});

    /// True if `bwrap` was located at construction time. False puts
    /// the sandbox into passthrough mode — `decorate()` then returns
    /// the original spawn unchanged.
    [[nodiscard]] bool hasBwrap() const noexcept { return m_hasBwrap; }

    /// Path the sandbox is using for the bwrap binary. Empty when
    /// `bwrap` was not located.
    [[nodiscard]] std::string const& bwrapExecutable() const noexcept { return m_bwrapExecutable; }

    /// Granted-permission set (read-only access for diagnostics +
    /// tests). The set is an immutable construction-time snapshot.
    [[nodiscard]] std::set<std::string> const& grantedPermissions() const noexcept {
        return m_grantedPermissions;
    }

    [[nodiscard]] DecoratedSpawn decorate(std::string const& pythonExe,
                                          std::filesystem::path const& scriptPath) const override;

private:
    std::set<std::string> m_grantedPermissions;
    std::string m_bwrapExecutable; ///< empty if not found
    bool m_hasBwrap{false};
    std::string m_userBusPath; ///< `/run/user/<uid>/bus`, empty if not bind-able
};

} // namespace ajazz::plugins

#endif // !_WIN32
