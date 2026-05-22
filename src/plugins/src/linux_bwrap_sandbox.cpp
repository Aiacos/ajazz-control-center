// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file linux_bwrap_sandbox.cpp
 * @brief Implementation of @ref LinuxBwrapSandbox.
 *
 * The interesting bit is how the granted permission set maps to
 * `bwrap` flags. We do this in @ref decorate (called once per spawn)
 * rather than in the constructor: keeping the per-spawn argv
 * computation visible at the call site makes it easy to read what
 * the sandbox is granting, and keeps construction cheap.
 *
 * The granted permission set is a @c std::set<std::string> at
 * construction time and immutable afterwards, so flag generation is
 * a pure function of state — easy to unit-test.
 */
#ifndef _WIN32

#include "ajazz/plugins/linux_bwrap_sandbox.hpp"

#include <array>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include <unistd.h>

namespace ajazz::plugins {
namespace {

/// Resolve a system program (here: `bwrap`) to an absolute path against
/// a vetted set of system directories — NEVER via `$PATH` (CWE-426). The
/// sandbox wrapper is a security boundary: a PATH-hijack that prepends a
/// writable dir with a fake `bwrap` would silently DISABLE isolation
/// while still reporting hasBwrap()==true. `bwrap` is always installed as
/// a system binary, so a fixed allowlist is both correct and safe (this
/// mirrors the macOS backend, which hard-codes `/usr/bin/sandbox-exec`).
/// Returns an empty string if not found in any vetted directory.
std::string findVettedExecutable(std::string_view name) {
    static constexpr std::array<std::string_view, 3> kVettedDirs{
        "/usr/bin",
        "/usr/local/bin",
        "/bin",
    };
    for (auto const& dir : kVettedDirs) {
        std::string buf{dir};
        buf.push_back('/');
        buf.append(name);
        if (::access(buf.c_str(), X_OK) == 0) {
            return buf;
        }
    }
    return {};
}

/// True if the granted set requests any permission that implies
/// outbound network access. Mirrors the schema's `Ajazz.Permissions`
/// enum entries that talk over the public internet — when the
/// sandbox grants any of these we drop `--unshare-net`.
bool grantsNetwork(std::set<std::string> const& granted) {
    static constexpr std::array<std::string_view, 3> kNetworkPerms{
        "obs-websocket",
        "spotify",
        "discord-rpc",
    };
    for (auto const& perm : kNetworkPerms) {
        if (granted.count(std::string{perm}) != 0) {
            return true;
        }
    }
    return false;
}

/// True if the granted set requests any DBus-using permission. The
/// session bus is what `org.freedesktop.Notifications` and the MPRIS
/// media-control interface are published on, so granting any of these
/// requires bind-mounting `/run/user/<uid>/bus` into the sandbox.
bool grantsDbus(std::set<std::string> const& granted) {
    static constexpr std::array<std::string_view, 3> kDbusPerms{
        "notifications",
        "media-control",
        "system-power",
    };
    for (auto const& perm : kDbusPerms) {
        if (granted.count(std::string{perm}) != 0) {
            return true;
        }
    }
    return false;
}

/// Compute the path to the user's session DBus socket. Returns an
/// empty string if `XDG_RUNTIME_DIR` is unset (rare — typically only
/// in headless service contexts) or the socket is missing. We do not
/// fall back to the system bus: plugin code that wants to talk to
/// system services has to declare a more specific permission and we
/// will route it explicitly in a future slice.
std::string discoverUserBus() {
    char const* xdg = std::getenv("XDG_RUNTIME_DIR");
    if (xdg == nullptr || *xdg == '\0') {
        return {};
    }
    std::string candidate{xdg};
    candidate += "/bus";
    std::error_code ec;
    if (std::filesystem::exists(candidate, ec)) {
        return candidate;
    }
    return {};
}

} // namespace

LinuxBwrapSandbox::LinuxBwrapSandbox(std::set<std::string> grantedPermissions,
                                     std::vector<std::filesystem::path> readablePaths,
                                     std::string bwrapExecutable)
    : m_grantedPermissions(std::move(grantedPermissions)),
      m_readablePaths(std::move(readablePaths)), m_bwrapExecutable(std::move(bwrapExecutable)) {
    if (m_bwrapExecutable.empty()) {
        m_bwrapExecutable = findVettedExecutable("bwrap");
    } else {
        // Caller-provided path: still verify it's executable so the
        // hasBwrap() invariant stays honest.
        if (::access(m_bwrapExecutable.c_str(), X_OK) != 0) {
            m_bwrapExecutable.clear();
        }
    }
    m_hasBwrap = !m_bwrapExecutable.empty();
    if (m_hasBwrap && grantsDbus(m_grantedPermissions)) {
        m_userBusPath = discoverUserBus();
    }
}

DecoratedSpawn LinuxBwrapSandbox::decorate(std::string const& pythonExe,
                                           std::filesystem::path const& scriptPath) const {
    if (!m_hasBwrap) {
        // Passthrough: bwrap is not available, fall back to the same
        // shape NoOpSandbox would emit. The caller can still observe
        // hasBwrap() == false to surface a "sandbox unavailable" UI
        // hint or refuse to load high-risk plugins.
        DecoratedSpawn out;
        out.executable = pythonExe;
        out.args = {pythonExe, scriptPath.string()};
        return out;
    }

    DecoratedSpawn out;
    out.executable = m_bwrapExecutable;
    auto& argv = out.args;
    argv.push_back(m_bwrapExecutable);

    // Filesystem layout: a MINIMAL read-only allowlist instead of
    // binding the host root. Binding `/` read-only (the pre-CWE-200
    // posture) still let a plugin read `~/.ssh`, browser credentials,
    // and `~/.config` secrets — the filesystem was readable, only
    // un-writable. We now expose only what the python interpreter and
    // the plugin code actually need:
    //
    //   - the system tree (`/usr`, and `-try` for the legacy split-/usr
    //     dirs + `/etc` for ld.so.cache, SSL certs, resolv.conf);
    //   - each caller-supplied readable path (the python package dir,
    //     the user-plugins dir);
    //   - the child script's parent directory.
    //
    // `$HOME`, `/root`, `/mnt`, `/media`, and `/run/user/*` (bar the
    // explicit DBus socket bound below) are deliberately NEVER bound.
    argv.emplace_back("--ro-bind");
    argv.emplace_back("/usr");
    argv.emplace_back("/usr");
    for (char const* dir : {"/lib", "/lib64", "/bin", "/sbin", "/etc"}) {
        // `--ro-bind-try` does not fail when the source is absent — on
        // merged-/usr distros `/lib` etc. are symlinks into /usr (still
        // resolvable) while non-merged layouts have them as real dirs.
        argv.emplace_back("--ro-bind-try");
        argv.emplace_back(dir);
        argv.emplace_back(dir);
    }

    argv.emplace_back("--proc");
    argv.emplace_back("/proc");
    argv.emplace_back("--dev");
    argv.emplace_back("/dev");
    argv.emplace_back("--tmpfs");
    argv.emplace_back("/tmp");

    // Caller-supplied readable paths + the script's parent directory.
    // Deduplicate so a path that equals the script parent (or appears
    // twice in the config) is bound once. Empty paths are skipped: an
    // empty source would make bwrap reject the whole launch.
    //
    // These binds come AFTER `--tmpfs /tmp`: bwrap applies mounts in
    // argv order, so a readable path located under `/tmp` (or any
    // earlier mount) must be bound last or the later overlay would
    // shadow it.
    {
        std::set<std::string> seen;
        auto bindReadable = [&](std::filesystem::path const& p) {
            if (p.empty()) {
                return;
            }
            std::string const s = p.string();
            if (!seen.insert(s).second) {
                return;
            }
            argv.emplace_back("--ro-bind");
            argv.push_back(s);
            argv.push_back(s);
        };
        for (auto const& p : m_readablePaths) {
            bindReadable(p);
        }
        bindReadable(scriptPath.parent_path());
    }

    // Lifecycle: detach controlling terminal, die when host dies.
    // --die-with-parent is the kill-switch the host relies on if it
    // crashes mid-IPC: the kernel reaps the child instead of leaving
    // a daemon-style ghost behind.
    argv.emplace_back("--new-session");
    argv.emplace_back("--die-with-parent");

    // Namespace isolation. --unshare-cgroup-try (rather than
    // --unshare-cgroup) tolerates kernels without cgroup-namespace
    // support — a hard error here would refuse to launch on older
    // distros even though everything else in the profile would work.
    argv.emplace_back("--unshare-pid");
    argv.emplace_back("--unshare-ipc");
    argv.emplace_back("--unshare-uts");
    argv.emplace_back("--unshare-cgroup-try");

    // Network: default-deny. Unshare unless the granted set mentions
    // a network-using permission, in which case the child sees the
    // host's network namespace verbatim.
    if (!grantsNetwork(m_grantedPermissions)) {
        argv.emplace_back("--unshare-net");
    }

    // DBus session bus: bind-mount only when one of the DBus-using
    // permissions is granted AND we found a real socket at the
    // expected path. Skipping when the socket is absent keeps bwrap
    // from refusing to start with a "no such file" error in headless
    // contexts.
    if (!m_userBusPath.empty()) {
        argv.emplace_back("--ro-bind");
        argv.push_back(m_userBusPath);
        argv.push_back(m_userBusPath);
    }

    // Argv terminator + actual command. `bwrap` requires `--` between
    // its flags and the inner command's argv; without it the inner
    // python invocation would be parsed as more bwrap flags.
    argv.emplace_back("--");
    argv.push_back(pythonExe);
    argv.push_back(scriptPath.string());
    return out;
}

} // namespace ajazz::plugins

#endif // !_WIN32
