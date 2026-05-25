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
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <sys/wait.h>
#include <unistd.h>

namespace {

bool contains(std::vector<std::string> const& argv, std::string_view needle) {
    return std::any_of(argv.begin(), argv.end(), [&](std::string const& s) { return s == needle; });
}

/// Build a sandbox using a forced bwrap path so the test is
/// independent of the dev machine's `PATH`. `/bin/sh` is universally
/// executable and lets us drive the "hasBwrap == true" branch without
/// actually invoking bwrap during decoration.
ajazz::plugins::LinuxBwrapSandbox makeSandbox(std::set<std::string> permissions) {
    return ajazz::plugins::LinuxBwrapSandbox{std::move(permissions), {}, "/bin/sh"};
}

/// As @ref makeSandbox but with an explicit read-only allowlist.
ajazz::plugins::LinuxBwrapSandbox
makeSandboxWithReadable(std::set<std::string> permissions,
                        std::vector<std::filesystem::path> readablePaths) {
    return ajazz::plugins::LinuxBwrapSandbox{
        std::move(permissions), std::move(readablePaths), "/bin/sh"};
}

/// True if argv contains the consecutive triple `flag src dst`
/// anywhere (e.g. `--ro-bind /usr /usr`).
bool containsBind(std::vector<std::string> const& argv,
                  std::string_view flag,
                  std::string_view src,
                  std::string_view dst) {
    for (std::size_t i = 0; i + 2 < argv.size(); ++i) {
        if (argv[i] == flag && argv[i + 1] == src && argv[i + 2] == dst) {
            return true;
        }
    }
    return false;
}

} // namespace

TEST_CASE("LinuxBwrapSandbox: passthrough when bwrap absent", "[plugins][sandbox]") {
    // Forced-empty bwrapExecutable + non-executable path → falls into
    // passthrough mode regardless of what's actually on PATH.
    ajazz::plugins::LinuxBwrapSandbox sandbox{{}, {}, "/nonexistent/bwrap"};
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

    // Filesystem layout: a MINIMAL read-only allowlist, NOT the host
    // root. The blanket `--ro-bind / /` (which let plugins read
    // `~/.ssh`, browser creds, `~/.config` secrets — CWE-200) must be
    // gone. The system baseline binds `/usr` and `-try`s the legacy
    // split-usr dirs + `/etc`.
    REQUIRE_FALSE(containsBind(spawn.args, "--ro-bind", "/", "/"));
    REQUIRE(containsBind(spawn.args, "--ro-bind", "/usr", "/usr"));
    REQUIRE(containsBind(spawn.args, "--ro-bind-try", "/lib", "/lib"));
    REQUIRE(containsBind(spawn.args, "--ro-bind-try", "/lib64", "/lib64"));
    REQUIRE(containsBind(spawn.args, "--ro-bind-try", "/bin", "/bin"));
    REQUIRE(containsBind(spawn.args, "--ro-bind-try", "/sbin", "/sbin"));
    REQUIRE(containsBind(spawn.args, "--ro-bind-try", "/etc", "/etc"));
    // $HOME / sensitive trees must NOT be bound.
    REQUIRE_FALSE(containsBind(spawn.args, "--ro-bind", "/home", "/home"));
    REQUIRE_FALSE(containsBind(spawn.args, "--ro-bind", "/root", "/root"));
    REQUIRE(contains(spawn.args, "--proc"));
    REQUIRE(contains(spawn.args, "--dev"));
    REQUIRE(contains(spawn.args, "--tmpfs"));

    // Inner command terminator + Python exec.
    REQUIRE(contains(spawn.args, "--"));
    REQUIRE(spawn.args.at(spawn.args.size() - 2) == "python3");
    REQUIRE(spawn.args.back() == "/tmp/host_child.py");
}

TEST_CASE("LinuxBwrapSandbox: readable allowlist binds each path plus script parent",
          "[plugins][sandbox]") {
    auto const sandbox = makeSandboxWithReadable(
        {}, {"/opt/ajazz/python", "/opt/ajazz/python", "/var/lib/ajazz/plugins"});
    auto const spawn = sandbox.decorate("python3", "/opt/ajazz/host/_host_child.py");

    // Each supplied readable path is bound read-only.
    REQUIRE(containsBind(spawn.args, "--ro-bind", "/opt/ajazz/python", "/opt/ajazz/python"));
    REQUIRE(
        containsBind(spawn.args, "--ro-bind", "/var/lib/ajazz/plugins", "/var/lib/ajazz/plugins"));
    // The script's parent directory is bound read-only too.
    REQUIRE(containsBind(spawn.args, "--ro-bind", "/opt/ajazz/host", "/opt/ajazz/host"));

    // Deduplication: `/opt/ajazz/python` was passed twice but must be
    // bound exactly once.
    std::size_t pyBindCount = 0;
    for (std::size_t i = 0; i + 2 < spawn.args.size(); ++i) {
        if (spawn.args[i] == "--ro-bind" && spawn.args[i + 1] == "/opt/ajazz/python" &&
            spawn.args[i + 2] == "/opt/ajazz/python") {
            ++pyBindCount;
        }
    }
    REQUIRE(pyBindCount == 1);

    // The system baseline is still present and the root bind is gone.
    REQUIRE(containsBind(spawn.args, "--ro-bind", "/usr", "/usr"));
    REQUIRE_FALSE(containsBind(spawn.args, "--ro-bind", "/", "/"));
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

    // The bus-bind always uses the same path on src and dst. Assert on
    // ITS presence specifically rather than a total `--ro-bind` count
    // (the baseline now also binds `/usr` and the script's parent dir).
    // Guard the null: XDG_RUNTIME_DIR is unset on the macOS CI runner, and
    // `std::string{nullptr}` is UB (SIGSEGV). When it's unset there is no
    // bus path to bind, so sawBusBind is correctly false.
    bool sawBusBind = false;
    if (xdg != nullptr) {
        std::string const expected = std::string{xdg} + "/bus";
        sawBusBind = containsBind(spawn.args, "--ro-bind", expected, expected);
    }
    if (busExists) {
        REQUIRE(sawBusBind);
    } else {
        REQUIRE_FALSE(sawBusBind);
    }
    // Either way the system baseline is present.
    REQUIRE(containsBind(spawn.args, "--ro-bind", "/usr", "/usr"));
}

TEST_CASE("LinuxBwrapSandbox: unknown permission strings are silently ignored",
          "[plugins][sandbox]") {
    auto const sandbox = makeSandbox({"not-a-real-permission", "neither-is-this"});
    auto const spawn = sandbox.decorate("python3", "/tmp/host_child.py");
    // No relaxation should happen — the profile is still most-restrictive,
    // no DBus bus bind, and the root-bind escape hatch stays gone.
    REQUIRE(contains(spawn.args, "--unshare-net"));
    REQUIRE_FALSE(containsBind(spawn.args, "--ro-bind", "/", "/"));
    REQUIRE(containsBind(spawn.args, "--ro-bind", "/usr", "/usr"));
    char const* xdg = std::getenv("XDG_RUNTIME_DIR");
    if (xdg != nullptr && *xdg != '\0') {
        REQUIRE_FALSE(containsBind(
            spawn.args, "--ro-bind", std::string{xdg} + "/bus", std::string{xdg} + "/bus"));
    }
}

namespace {

/// Resolve a program against PATH (access(X_OK)), returning empty if
/// not found. Mirrors the sandbox's own lookup so the E2E test uses
/// the same notion of "available".
std::string locateOnPath(std::string_view name) {
    char const* path = std::getenv("PATH");
    if (path == nullptr || *path == '\0') {
        return {};
    }
    std::string_view const sv{path};
    std::size_t start = 0;
    while (start <= sv.size()) {
        auto const colon = sv.find(':', start);
        auto const end = (colon == std::string_view::npos) ? sv.size() : colon;
        std::string_view const dir = sv.substr(start, end - start);
        if (!dir.empty()) {
            std::filesystem::path cand{dir};
            cand /= name;
            if (::access(cand.c_str(), X_OK) == 0) {
                return cand.string();
            }
        }
        if (colon == std::string_view::npos) {
            break;
        }
        start = end + 1;
    }
    return {};
}

/// fork()+execv() a decorated spawn and return the child's exit status
/// (WEXITSTATUS), or -1 if it did not exit normally.
int runSpawn(ajazz::plugins::DecoratedSpawn const& spawn) {
    std::vector<char*> cargv;
    cargv.reserve(spawn.args.size() + 1);
    for (auto const& a : spawn.args) {
        cargv.push_back(
            const_cast<char*>(a.c_str())); // NOLINT(cppcoreguidelines-pro-type-const-cast)
    }
    cargv.push_back(nullptr);

    pid_t const pid = ::fork();
    REQUIRE(pid >= 0);
    if (pid == 0) {
        ::execv(spawn.executable.c_str(), cargv.data());
        ::_exit(127); // execv only returns on failure
    }
    int status = 0;
    REQUIRE(::waitpid(pid, &status, 0) == pid);
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    return -1;
}

} // namespace

TEST_CASE("LinuxBwrapSandbox: real bwrap spawn allows readable paths but hides HOME",
          "[plugins][sandbox][bwrap-e2e]") {
    std::string const bwrap = locateOnPath("bwrap");
    // Prefer a system-resident interpreter: it lives under the `/usr`
    // baseline bind, so the test does not depend on adding some
    // out-of-tree prefix (e.g. a linuxbrew/conda python under $HOME,
    // which would itself be hidden by the very isolation we test).
    std::string python =
        (::access("/usr/bin/python3", X_OK) == 0) ? "/usr/bin/python3" : locateOnPath("python3");
    if (bwrap.empty() || python.empty()) {
        // No bwrap and/or python3 on this box (e.g. a stripped CI
        // runner). The pure-argv tests above still cover the bind set;
        // the isolation guarantee can only be proven where bwrap can
        // actually launch. Skip cleanly so the suite stays green.
        SUCCEED("bwrap or python3 unavailable: skipping real-launch E2E");
        return;
    }

    namespace fs = std::filesystem;

    // A private sandbox tree we DO grant: a readable file the child
    // should be able to open. Placed under the system temp dir.
    auto const work =
        fs::temp_directory_path() / fs::path{"ajazz_bwrap_e2e_" + std::to_string(::getpid())};
    fs::create_directories(work);
    auto const readableFile = work / "allowed.txt";
    {
        std::ofstream ofs{readableFile};
        ofs << "readable-by-plugin\n";
    }

    // A sentinel under the REAL $HOME — a path the sandbox never binds.
    // This is the actual isolation guarantee: a plugin must not be able
    // to read the user's home (where ~/.ssh, browser creds, ~/.config
    // secrets live). We write a uniquely-named file so we never touch a
    // real dotfile, and remove it afterwards.
    char const* home = std::getenv("HOME");
    if (home == nullptr || *home == '\0') {
        std::error_code rmEc;
        fs::remove_all(work, rmEc);
        SUCCEED("HOME unset: cannot place the isolation sentinel; skipping");
        return;
    }
    auto const secret =
        fs::path{home} / fs::path{"ajazz_bwrap_e2e_secret_" + std::to_string(::getpid()) + ".txt"};
    {
        std::ofstream ofs{secret};
        ofs << "TOP-SECRET-CREDENTIAL\n";
    }
    // Sanity: the sentinel is readable OUTSIDE the sandbox.
    REQUIRE(fs::exists(secret));

    // The interpreter's install prefix must be readable for python to
    // start (linuxbrew/conda pythons live outside /usr). Resolve the
    // real binary and grant its <prefix> (parent of .../bin/python3).
    // For a /usr-resident python this is /usr (already in the baseline).
    fs::path const pyReal = fs::canonical(python);
    fs::path const pyPrefix = pyReal.parent_path().parent_path();

    std::vector<fs::path> readablePaths{work, pyPrefix};

    ajazz::plugins::LinuxBwrapSandbox sandbox{{}, readablePaths, bwrap};
    REQUIRE(sandbox.hasBwrap());

    SECTION("POSITIVE: a granted readable file opens (interpreter + binds work)") {
        std::string const code = "import sys; "
                                 "f=open(sys.argv[1]); "
                                 "sys.exit(0 if 'readable-by-plugin' in f.read() else 3)";
        // Decorate, then append the python -c argv. decorate() ends the
        // argv with `python <script>`; we instead want `python -c <code>
        // <arg>`, so build the inner command ourselves on top of the
        // wrapper flags by decorating with the python exe + a throwaway
        // script path, then replacing the trailing two entries.
        auto spawn = sandbox.decorate(python, work / "unused.py");
        // Drop the trailing `python <script>` and re-push the real cmd.
        spawn.args.resize(spawn.args.size() - 2);
        spawn.args.push_back(python);
        spawn.args.emplace_back("-c");
        spawn.args.push_back(code);
        spawn.args.push_back(readableFile.string());
        REQUIRE(runSpawn(spawn) == 0);
    }

    SECTION("NEGATIVE: the $HOME sentinel is NOT readable (isolation holds)") {
        // exit 0 ONLY if opening the secret raised FileNotFoundError
        // (the path is hidden). Any other outcome (open succeeded, or a
        // different error) fails the test.
        std::string const code = "import sys\n"
                                 "try:\n"
                                 "    open(sys.argv[1])\n"
                                 "    sys.exit(5)\n"
                                 "except FileNotFoundError:\n"
                                 "    sys.exit(0)\n"
                                 "except Exception:\n"
                                 "    sys.exit(6)\n";
        auto spawn = sandbox.decorate(python, work / "unused.py");
        spawn.args.resize(spawn.args.size() - 2);
        spawn.args.push_back(python);
        spawn.args.emplace_back("-c");
        spawn.args.push_back(code);
        spawn.args.push_back(secret.string());
        REQUIRE(runSpawn(spawn) == 0);
    }

    std::error_code ec;
    fs::remove_all(work, ec);
    fs::remove(secret, ec);
}

#endif // !_WIN32
