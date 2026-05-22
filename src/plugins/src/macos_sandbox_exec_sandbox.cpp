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
#include <set>
#include <string>
#include <string_view>
#include <vector>

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

/// Escape a path so it is a safe S-expression string literal. The
/// TinyScheme reader treats `"` as the string terminator and `\` as
/// an escape introducer, so both must be backslash-escaped. Other
/// bytes (including spaces and UTF-8) pass through verbatim — they are
/// legal inside a quoted literal.
std::string escapeSexpr(std::string const& raw) {
    std::string out;
    out.reserve(raw.size() + 2);
    for (char const c : raw) {
        if (c == '\\' || c == '"') {
            out.push_back('\\');
        }
        out.push_back(c);
    }
    return out;
}

/// Emit a `(subpath "...")` clause for a single path, skipping empties
/// and duplicates (tracked via @p seen). Appends to @p out.
void appendSubpath(std::string& out, std::set<std::string>& seen, std::filesystem::path const& p) {
    if (p.empty()) {
        return;
    }
    std::string const s = p.string();
    if (!seen.insert(s).second) {
        return;
    }
    out += " (subpath \"";
    out += escapeSexpr(s);
    out += "\")";
}

/// Build the inline S-expression sandbox profile from the granted
/// permission set + the extra readable paths. The result is what we
/// hand to `sandbox-exec -p <profile> ...` at decoration time.
std::string buildProfile(std::set<std::string> const& granted,
                         std::vector<std::filesystem::path> const& readablePaths) {
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
    // is SCOPED to a minimal allowlist instead of the former blanket
    // `(allow file-read*)`, which (mirroring Linux's old `--ro-bind / /`)
    // let a plugin read `~/.ssh`, browser credentials and `~/.config`
    // secrets (CWE-200). We expose only the system trees CPython + the
    // dynamic loader need, plus each caller-supplied readable path (the
    // python package dir, the user-plugins dir). The script's parent is
    // appended per-spawn in decorate(). `$HOME` is deliberately absent.
    out += "(allow sysctl-read)\n";
    out += "(allow file-read*";
    out += " (subpath \"/usr\")";
    out += " (subpath \"/System\")";
    out += " (subpath \"/Library\")";
    out += " (subpath \"/private/etc\")";
    out += " (subpath \"/etc\")";
    {
        std::set<std::string> seen;
        for (auto const& p : readablePaths) {
            appendSubpath(out, seen, p);
        }
    }
    out += ")\n";

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
                                                 std::vector<std::filesystem::path> readablePaths,
                                                 std::string sandboxExecExecutable)
    : m_grantedPermissions(std::move(grantedPermissions)),
      m_readablePaths(std::move(readablePaths)),
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
    m_profile = buildProfile(m_grantedPermissions, m_readablePaths);
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
    // The cached profile already carries the system + readablePaths
    // file-read* rules. Append one more `(allow file-read* ...)` form
    // scoped to the script's parent directory — only known per-spawn —
    // so the child can actually read the host child script it execs.
    std::string profile = m_profile;
    auto const scriptParent = scriptPath.parent_path();
    if (!scriptParent.empty()) {
        profile += "(allow file-read* (subpath \"";
        profile += escapeSexpr(scriptParent.string());
        profile += "\"))\n";
    }

    argv.emplace_back("-p");
    argv.push_back(std::move(profile));
    argv.push_back(pythonExe);
    argv.push_back(scriptPath.string());
    return out;
}

} // namespace ajazz::plugins

#endif // !_WIN32
