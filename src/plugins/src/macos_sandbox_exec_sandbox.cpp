// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file macos_sandbox_exec_sandbox.cpp
 * @brief Implementation of @ref MacosSandboxExecSandbox.
 *
 * The interesting bit is the S-expression policy generator. We build
 * the profile once at construction time and cache it, so each
 * @ref decorate call is a constant-time string concat into the argv.
 * Building it eagerly also lets unit tests pin the *exact* policy
 * text via the @ref profile accessor without having to peek at argv.
 *
 * @par Profile-language gotchas pinned by the implementation
 *
 *   - `(version 1)` MUST be the first form. Any preceding whitespace
 *     or comment causes `sandbox-exec` to refuse to launch.
 *   - `(deny default)` is the safe starting posture; macOS's stock
 *     sandbox profile in `/usr/share/sandbox/` defaults to allow.
 *     We do NOT include the stock profile.
 *   - String literals MUST be double-quoted. The profile is generated
 *     entirely by us; if a future rule needs to embed a path with
 *     a literal `"`, escape it via `\\"` (TinyScheme accepts the same
 *     backslash escapes as JSON for the quote character).
 *   - `sandbox-exec -p "<profile>"` reads the profile from argv;
 *     there is no shell quoting to worry about because we hand the
 *     argv to `execvp` directly, not to a shell.
 */
#ifndef _WIN32

#include "ajazz/plugins/macos_sandbox_exec_sandbox.hpp"

#include <array>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>

#include <unistd.h>

namespace ajazz::plugins {
namespace {

/// Default location of `sandbox-exec` on macOS. Hard-coded because
/// macOS does not relocate this binary (unlike `bwrap` which can be
/// in `/usr/bin` or `/usr/local/bin` depending on distro), and the
/// only environment where the file would be missing is non-macOS,
/// in which case we do not want to silently pick up some random
/// executable named `sandbox-exec` from the user's `PATH`.
constexpr char const* kSandboxExecPath = "/usr/bin/sandbox-exec";

/// True if the granted set requests any permission that implies
/// outbound network access. Mirrors @ref LinuxBwrapSandbox's logic
/// so the two backends have parallel semantics.
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

/// True if the granted set requests any permission backed by a mach
/// service on macOS — desktop notifications go through
/// `com.apple.usernotificationsd`, media keys through MediaRemote,
/// system-power through IOKit / IOPower. Slice 3c grants
/// `(allow mach-lookup)` broadly when any of these is requested;
/// slice 4 will narrow to specific `(global-name "...")` clauses
/// once we measure exactly which services each permission contacts.
bool grantsMachLookup(std::set<std::string> const& granted) {
    static constexpr std::array<std::string_view, 3> kMachPerms{
        "notifications",
        "media-control",
        "system-power",
    };
    for (auto const& perm : kMachPerms) {
        if (granted.count(std::string{perm}) != 0) {
            return true;
        }
    }
    return false;
}

/// Build the inline S-expression sandbox profile from the granted
/// permission set. The result is what we hand to
/// `sandbox-exec -p <profile> ...` at decoration time.
std::string buildProfile(std::set<std::string> const& granted) {
    std::string out;
    // Most-restrictive baseline. The order matters: `(version 1)`
    // MUST be first; `(deny default)` MUST come before any allow rule
    // so the deny is the fall-through, not the allow.
    out += "(version 1)\n";
    out += "(deny default)\n";

    // Process bookkeeping the python3 interpreter needs to start.
    // process-fork covers `os.fork()` (rare, but the threading bootstrap
    // touches it); process-exec* covers any subprocess.run()-style
    // call from within the plugin (further restricted by file-read*).
    out += "(allow process-fork)\n";
    out += "(allow process-exec*)\n";
    out += "(allow signal (target self))\n";

    // sysctl-read is needed for libc / CoreFoundation init. file-read*
    // is broad on purpose — it mirrors `--ro-bind / /` on Linux. Future
    // hardening can scope it to (allow file-read* (subpath "/usr")
    // (subpath "/System") (subpath "/Library")) once we measure what
    // CPython actually opens.
    out += "(allow sysctl-read)\n";
    out += "(allow file-read*)\n";

    // Writable scratch under the user's $TMPDIR (macOS's per-user
    // temp dir; Apple discourages /tmp). The runtime expansion
    // happens via `(param "TMP")` if the host sets that env via
    // `sandbox-exec -D TMP=...`; for slice 3c we keep it simple and
    // allow file-write* under /private/var/folders (where macOS
    // mounts $TMPDIR) plus /tmp for compatibility.
    out += "(allow file-write* (subpath \"/private/var/folders\") (subpath \"/tmp\"))\n";

    // Network: default-deny. `(allow network*)` opens all four
    // subclasses (network-bind, network-inbound, network-outbound,
    // network-listen). We rely on the host process trusting the
    // permission grant — once macOS plugins want a finer-grained
    // story (e.g. UDP vs TCP) we can split this.
    if (grantsNetwork(granted)) {
        out += "(allow network*)\n";
    }

    // Mach IPC: a default `(deny default)` blocks every mach-lookup,
    // so even reading time-of-day or talking to the runtime would
    // fail. We always allow the bootstrap server lookup (otherwise
    // CoreFoundation can't initialise) and broaden to general
    // mach-lookup when a DBus-equivalent permission is granted.
    out += "(allow mach-lookup (global-name \"com.apple.system.opendirectoryd.libinfo\"))\n";
    if (grantsMachLookup(granted)) {
        out += "(allow mach-lookup)\n";
    }
    return out;
}

} // namespace

MacosSandboxExecSandbox::MacosSandboxExecSandbox(std::set<std::string> grantedPermissions,
                                                 std::string sandboxExecExecutable)
    : m_grantedPermissions(std::move(grantedPermissions)),
      m_sandboxExecExecutable(std::move(sandboxExecExecutable)) {
    if (m_sandboxExecExecutable.empty()) {
        // Default lookup: the canonical macOS path. We deliberately
        // do not search `PATH` — non-macOS boxes that happen to have
        // `sandbox-exec` shimmed somewhere in PATH should fall into
        // passthrough mode, not silently pick up the shim.
        if (::access(kSandboxExecPath, X_OK) == 0) {
            m_sandboxExecExecutable = kSandboxExecPath;
        }
    } else {
        // Caller-provided override — verify executability for honest
        // hasSandboxExec() reporting. Tests use this code path with
        // /bin/sh as the fake binary.
        if (::access(m_sandboxExecExecutable.c_str(), X_OK) != 0) {
            m_sandboxExecExecutable.clear();
        }
    }
    m_hasSandboxExec = !m_sandboxExecExecutable.empty();
    m_profile = buildProfile(m_grantedPermissions);
}

DecoratedSpawn MacosSandboxExecSandbox::decorate(std::string const& pythonExe,
                                                 std::filesystem::path const& scriptPath) const {
    if (!m_hasSandboxExec) {
        // Passthrough — same shape as NoOpSandbox. The caller can
        // still observe hasSandboxExec() == false to surface a
        // "sandbox unavailable" UI hint or refuse high-risk plugins.
        DecoratedSpawn out;
        out.executable = pythonExe;
        out.args = {pythonExe, scriptPath.string()};
        return out;
    }

    DecoratedSpawn out;
    out.executable = m_sandboxExecExecutable;
    auto& argv = out.args;
    argv.push_back(m_sandboxExecExecutable);

    // -p <profile-string> is the inline-profile form. Avoids needing
    // a temp file, which would have its own lifecycle / cleanup
    // concerns post-fork. The profile is precomputed at construction
    // time so this argv assembly is cheap and allocation-free
    // beyond the trivial vector growth.
    argv.emplace_back("-p");
    argv.push_back(m_profile);
    argv.push_back(pythonExe);
    argv.push_back(scriptPath.string());
    return out;
}

} // namespace ajazz::plugins

#endif // !_WIN32
