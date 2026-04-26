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
#include <string>
#include <string_view>

#include <unistd.h>

namespace ajazz::plugins {
namespace {

/// Resolve `bwrap` against `PATH`. Returns empty string if not found.
/// The lookup is `access(X_OK)` on each `PATH` entry — same semantics
/// `execvp` would use, just done eagerly so we know at construction
/// time whether bwrap is usable.
std::string findOnPath(std::string_view name) {
    char const* path = std::getenv("PATH");
    if (path == nullptr || *path == '\0') {
        return {};
    }
    std::string buf;
    std::string_view const sv{path};
    std::size_t start = 0;
    while (start <= sv.size()) {
        auto const colon = sv.find(':', start);
        auto const end = (colon == std::string_view::npos) ? sv.size() : colon;
        std::string_view const dir = sv.substr(start, end - start);
        if (!dir.empty()) {
            buf.assign(dir);
            buf.push_back('/');
            buf.append(name);
            if (::access(buf.c_str(), X_OK) == 0) {
                return buf;
            }
        }
        if (colon == std::string_view::npos) {
            break;
        }
        start = end + 1;
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
                                     std::string bwrapExecutable)
    : m_grantedPermissions(std::move(grantedPermissions)),
      m_bwrapExecutable(std::move(bwrapExecutable)) {
    if (m_bwrapExecutable.empty()) {
        m_bwrapExecutable = findOnPath("bwrap");
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

    // Filesystem layout: read-only host root, fresh /proc + /dev,
    // writable tmpfs at /tmp. The host's PYTHONPATH still resolves
    // because the project sources are reachable through `/`.
    argv.emplace_back("--ro-bind");
    argv.emplace_back("/");
    argv.emplace_back("/");
    argv.emplace_back("--proc");
    argv.emplace_back("/proc");
    argv.emplace_back("--dev");
    argv.emplace_back("/dev");
    argv.emplace_back("--tmpfs");
    argv.emplace_back("/tmp");

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
