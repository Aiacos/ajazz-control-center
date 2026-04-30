// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file sandbox.hpp
 * @brief OS-agnostic sandbox abstraction for the out-of-process plugin host.
 *
 * Audit finding A4 — slice 3b. The @ref OutOfProcessPluginHost spawns
 * a Python child via `fork()` + `execvp()`. Without sandboxing the
 * child runs with the host's full ambient authority — same uid, same
 * environment, same network, same filesystem. A malicious or buggy
 * plugin could read `~/.ssh/`, exfiltrate data over the network, or
 * spawn arbitrary children.
 *
 * The @ref Sandbox interface lets the host decorate the spawn so the
 * child runs inside an OS-level container. Implementations:
 *
 *   - @c NoOpSandbox — passes the spawn through unchanged. Default
 *     when no sandbox is configured (preserves the slice-1/2/2.5/3a
 *     behaviour callers see today).
 *   - @c LinuxBwrapSandbox — wraps the spawn in
 *     `bwrap(1)` (bubblewrap) with a default-restrictive profile:
 *     fresh PID/IPC/UTS/cgroup namespaces, network unshared by default,
 *     read-only access to the host filesystem, writable tmpfs at /tmp.
 *     Permissions in the granted set selectively relax the profile —
 *     e.g. `obs-websocket` / `spotify` / `discord-rpc` keep network
 *     enabled, `notifications` / `media-control` / `system-power`
 *     bind-mount the user session DBus socket.
 *
 * Slice 3b shipped @c LinuxBwrapSandbox, slice 3c added
 * @c MacosSandboxExecSandbox. Slice 3d-ii ships
 * @c WindowsAppContainerSandbox — AppContainer + a restricted token
 * configured at `CreateProcessW` time via `STARTUPINFOEX`. Windows
 * does not use a wrapper executable (unlike `bwrap` / `sandbox-exec`),
 * so the Sandbox interface gained a second virtual method,
 * @ref Sandbox::configureProcessAttributes, that is a no-op on POSIX
 * and the active path on Windows.
 *
 * @par Permission set semantics
 *
 * The sandbox is configured at @b host-construction time with the
 * union of permissions the host is willing to grant the child — not
 * per-plugin. The child loads N plugins into a single process, so a
 * static profile is the only thing the kernel can enforce. Per-plugin
 * runtime gating (a la Wayland portals) is deferred to slice 4.
 *
 * Permission strings come from the @c Ajazz.Permissions enum in
 * @c docs/schemas/plugin_manifest.schema.json. Unknown strings are
 * silently ignored — the schema validator already rejects them at
 * manifest-validation time, and we don't want a typo on the host
 * side to weaken the sandbox by accident.
 */
#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace ajazz::plugins {

/**
 * @brief Opaque side-channel for per-spawn process attributes.
 *
 * Most sandboxes (Linux @c bwrap, macOS @c sandbox-exec) implement
 * isolation by prepending a wrapper executable to the argv; for
 * those, @ref Sandbox::decorate does all the work and this struct
 * stays empty. Windows AppContainer is different: the sandbox is
 * configured at @c CreateProcessW time via
 * `STARTUPINFOEX::lpAttributeList`, so @ref WindowsAppContainerSandbox
 * populates platform-specific state here via
 * @ref Sandbox::configureProcessAttributes, and the win32 host backend
 * consumes it when it builds the `STARTUPINFOEX`.
 *
 * The struct is defined as an opaque pimpl so the header can be
 * included from POSIX translation units without dragging in
 * `<windows.h>`. POSIX backends leave @c impl null; the win32 host
 * consumes the pimpl through a helper function exported from
 * @c windows_app_container_sandbox.cpp.
 *
 * Thread-safety: an instance is owned by the stack frame of the
 * host constructor; it is not shared across threads.
 */
struct ProcessAttributes {
    /// Opaque per-OS state. Null means "no special attributes"; the
    /// host backend treats that identically to a default spawn.
    struct Impl;
    std::unique_ptr<Impl> impl;

    ProcessAttributes();
    ~ProcessAttributes();
    ProcessAttributes(ProcessAttributes const&) = delete;
    ProcessAttributes& operator=(ProcessAttributes const&) = delete;
    ProcessAttributes(ProcessAttributes&&) noexcept;
    ProcessAttributes& operator=(ProcessAttributes&&) noexcept;
};

/**
 * @brief Decorated spawn produced by @ref Sandbox::decorate.
 *
 * Replaces the plain `(pythonExe, [pythonExe, scriptPath])` pair the
 * host would otherwise pass to `execvp()`. The sandbox may prepend
 * its own runtime (e.g. `bwrap`) and inject arguments before the
 * Python invocation.
 *
 * Environment variables are intentionally NOT part of this struct —
 * the host sets `PYTHONPATH` / `PYTHONDONTWRITEBYTECODE` /
 * `PYTHONUNBUFFERED` via `setenv()` before exec, and `bwrap` (and
 * macOS `sandbox-exec`) inherit the parent's env by default. If a
 * future sandbox needs to mutate env, add a field then rather than
 * a hidden behaviour.
 */
struct DecoratedSpawn {
    /// Executable to pass to `execvp()`. For NoOp this is the
    /// caller's `pythonExe`; for bwrap this is `"bwrap"` (the
    /// child process exec()s bwrap, which exec()s python).
    std::string executable;

    /// argv for `execvp()`. Includes @c executable as `argv[0]`.
    /// The trailing entries are always the python interpreter +
    /// child script, in that order.
    std::vector<std::string> args;
};

/**
 * @brief Abstract sandbox decorator for child-process spawning.
 *
 * Implementations are owned by @c OutOfProcessHostConfig and consulted
 * once at spawn time. They MUST be safe to call from a
 * post-`fork()`-pre-`exec()` context — the parent has already forked,
 * so the implementation cannot allocate via the host's allocator
 * (the child's heap state is undefined post-fork in a multithreaded
 * parent). The slice-3b implementations comply by building all of
 * their state at construction time and by `decorate()` returning a
 * pre-built struct that the host consumes by copy in the child.
 *
 * Thread-safety: @ref decorate is called once per spawn; instances
 * do not need to be reentrant. The lifetime is at least as long as
 * the host using them.
 */
class Sandbox {
public:
    Sandbox() = default;
    virtual ~Sandbox() = default;

    Sandbox(Sandbox const&) = delete;
    Sandbox& operator=(Sandbox const&) = delete;
    Sandbox(Sandbox&&) = delete;
    Sandbox& operator=(Sandbox&&) = delete;

    /**
     * @brief Decorate a Python child spawn.
     *
     * @param pythonExe  Path to (or name of) the Python interpreter
     *                   the host wants to invoke. May be a bare name
     *                   (e.g. @c "python3") for `execvp` to resolve
     *                   via @c PATH.
     * @param scriptPath Absolute path to the child script
     *                   (`_host_child.py`).
     * @return The decorated spawn — the executable + argv the host
     *         should hand to `execvp()`. NoOp returns the original
     *         pair; bwrap returns @c "bwrap" plus a long argv.
     */
    [[nodiscard]] virtual DecoratedSpawn
    decorate(std::string const& pythonExe, std::filesystem::path const& scriptPath) const = 0;

    /**
     * @brief Configure per-spawn process attributes (Windows-only path).
     *
     * On Linux and macOS this is a no-op: @ref decorate already
     * produced the full argv including the sandbox-wrapper executable,
     * and there is nothing more to do at spawn time.
     *
     * On Windows, AppContainer isolation and restricted-token
     * configuration happen at @c CreateProcessW via
     * `STARTUPINFOEX::lpAttributeList` — there is no wrapper
     * executable. @ref WindowsAppContainerSandbox populates the
     * @p attributes pimpl with the AppContainer SID, capability SIDs
     * and restricted-token handle; the win32 host backend consumes
     * the pimpl when it builds the `STARTUPINFOEX`.
     *
     * @param attributes Out-parameter to populate. The default
     *        implementation leaves it untouched.
     */
    virtual void configureProcessAttributes(ProcessAttributes& attributes) const {
        (void)attributes;
    }
};

/**
 * @brief Pass-through sandbox — no isolation.
 *
 * Used as the default when no sandbox is configured. Preserves the
 * pre-slice-3b behaviour exactly: the host invokes
 * `python3 _host_child.py` with the parent's full ambient authority.
 */
class NoOpSandbox final : public Sandbox {
public:
    [[nodiscard]] DecoratedSpawn decorate(std::string const& pythonExe,
                                          std::filesystem::path const& scriptPath) const override {
        DecoratedSpawn out;
        out.executable = pythonExe;
        out.args = {pythonExe, scriptPath.string()};
        return out;
    }
};

} // namespace ajazz::plugins
