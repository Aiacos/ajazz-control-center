// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file out_of_process_plugin_host.cpp
 * @brief POSIX implementation of @ref OutOfProcessPluginHost.
 *
 * Spawns the child via `fork()` + `execvp()`, plumbs a pair of pipes
 * for line-delimited JSON in both directions, reads with `poll(2)`
 * timeouts, and signals the child for shutdown / forced kill. The
 * IPC is synchronous-request / single-response per op (caller owns
 * serialisation via the mutex).
 *
 * **Wire protocol** (slice 1 — only the ops needed for the safety
 * proof):
 *
 *   host  -> child : `{"op":"list_plugins"}`
 *                    `{"op":"shutdown"}`
 *                    `{"op":"_crash_for_test"}` (signals SIGSEGV inside child)
 *   child -> host  : `{"event":"ready","pid":<int>,"python":"3.x.y"}` (handshake)
 *                    `{"event":"plugins","plugins":[]}`               (list response)
 *                    `{"event":"shutdown_ack"}`                       (clean exit cue)
 *
 * Each line is a single-line JSON object terminated by a newline.
 * Slice 2 adds `add_search_path`, `load_all` and `dispatch`. Until
 * then `plugins()` returns an empty vector — the foundation does
 * NOT load any plugin code; its sole job is to prove the IPC channel
 * works and crashes are isolated.
 *
 * **Why our own JSON instead of nlohmann::json or QJsonDocument**:
 * the plugins library is intentionally Qt-free (matches A2's
 * `Executor` pattern), and pulling a JSON dep just for one-line flat
 * objects is overkill. The encoder below escapes `\\`, `"`, control
 * chars, and the parser is a substring search for the small set of
 * keys we actually read. The child speaks proper JSON via Python's
 * stdlib `json` module so any future host parser upgrade is
 * compatible — only the C++ side is purpose-built.
 */
#ifndef _WIN32

#include "ajazz/plugins/out_of_process_plugin_host.hpp"

#include "ajazz/core/logger.hpp"

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

#include <fcntl.h>
#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>

extern "C" char** environ;

namespace ajazz::plugins {
namespace {

/// Encode a string value into JSON. Output excludes the surrounding
/// quotes; caller wraps. Escapes `"`, `\\`, control chars, and
/// 0x7f-0xff via `\u00XX` (the input is `std::string`, treated as
/// UTF-8 bytes).
std::string jsonEscape(std::string_view value) {
    std::string out;
    out.reserve(value.size() + 2);
    for (char const ch : value) {
        auto const c = static_cast<unsigned char>(ch);
        switch (c) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\b':
            out += "\\b";
            break;
        case '\f':
            out += "\\f";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (c < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned>(c));
                out += buf;
            } else {
                out.push_back(static_cast<char>(c));
            }
        }
    }
    return out;
}

/// Build a flat JSON object `{"op":"<name>"}`, optionally with extra
/// string-valued fields. Each field becomes `,"<key>":"<jsonEscape value>"`
/// in declaration order. Numbers and arrays are not supported because
/// nothing in the slice-2 wire protocol needs them; if that changes,
/// extend here rather than reaching for nlohmann::json.
std::string
buildOp(std::string_view opName,
        std::initializer_list<std::pair<std::string_view, std::string_view>> fields = {}) {
    std::string out{"{\"op\":\""};
    out += jsonEscape(opName);
    out += "\"";
    for (auto const& [key, value] : fields) {
        out += ",\"";
        out += jsonEscape(key);
        out += "\":\"";
        out += jsonEscape(value);
        out += "\"";
    }
    out += "}";
    return out;
}

/// Find `"key":"<value>"` in a JSON line. Returns the decoded value or
/// empty string if not found / value is not a string. Tolerates simple
/// escapes (`\"`, `\\`); does NOT decode `\uXXXX`. Adequate for the
/// keys we actually read on the host side (`event`, `python`).
std::string findStringField(std::string_view line, std::string_view key) {
    std::string needle{"\""};
    needle += key;
    needle += "\":";
    auto const pos = line.find(needle);
    if (pos == std::string_view::npos) {
        return {};
    }
    auto i = pos + needle.size();
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) {
        ++i;
    }
    if (i >= line.size() || line[i] != '"') {
        return {};
    }
    ++i; // past opening quote
    std::string value;
    while (i < line.size() && line[i] != '"') {
        if (line[i] == '\\' && i + 1 < line.size()) {
            char const next = line[i + 1];
            switch (next) {
            case '"':
                value.push_back('"');
                break;
            case '\\':
                value.push_back('\\');
                break;
            case 'n':
                value.push_back('\n');
                break;
            case 'r':
                value.push_back('\r');
                break;
            case 't':
                value.push_back('\t');
                break;
            default:
                value.push_back(next);
                break;
            }
            i += 2;
        } else {
            value.push_back(line[i]);
            ++i;
        }
    }
    return value;
}

/// `read(2)` until newline OR child closes / timeout fires. Returns
/// `{ok=true, line=...}` on a complete line; `{ok=false, eof=true}`
/// when the child closes its stdout (it died); `{ok=false, eof=false}`
/// on timeout. The line excludes the terminating newline.
struct ReadLineResult {
    bool ok{false};
    bool eof{false};
    std::string line;
};

ReadLineResult readLine(int fd, std::string& carry, std::chrono::milliseconds timeout) {
    using clock = std::chrono::steady_clock;
    auto const deadline = clock::now() + timeout;
    while (true) {
        // First, see if `carry` already has a complete line.
        auto const nl = carry.find('\n');
        if (nl != std::string::npos) {
            ReadLineResult r;
            r.ok = true;
            r.line = carry.substr(0, nl);
            carry.erase(0, nl + 1);
            return r;
        }

        auto const now = clock::now();
        if (now >= deadline) {
            return {}; // timeout
        }
        auto const remainingMs =
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();

        struct pollfd pfd {};
        pfd.fd = fd;
        pfd.events = POLLIN;
        int const rc = ::poll(&pfd, 1, static_cast<int>(remainingMs));
        if (rc == 0) {
            return {}; // timeout
        }
        if (rc < 0) {
            if (errno == EINTR) {
                continue;
            }
            return {}; // I/O error treated as timeout-style failure
        }
        if ((pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0 && (pfd.revents & POLLIN) == 0) {
            ReadLineResult r;
            r.eof = true;
            return r;
        }

        char buf[4096];
        ssize_t const n = ::read(fd, buf, sizeof(buf));
        if (n == 0) {
            ReadLineResult r;
            r.eof = true;
            return r;
        }
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return {};
        }
        carry.append(buf, static_cast<std::size_t>(n));
    }
}

} // namespace

struct OutOfProcessPluginHost::Impl {
    OutOfProcessHostConfig config;
    pid_t pid{-1};
    int writeFd{-1};       ///< parent-end of host->child pipe
    int readFd{-1};        ///< parent-end of child->host pipe
    std::string readCarry; ///< buffer for partial lines from the child
    std::string pythonVersion;
    std::mutex mutex;
    bool aliveCached{false};

    void cleanupFds() noexcept {
        if (writeFd >= 0) {
            ::close(writeFd);
            writeFd = -1;
        }
        if (readFd >= 0) {
            ::close(readFd);
            readFd = -1;
        }
    }

    bool waitChildExit(std::chrono::milliseconds timeout) {
        if (pid <= 0) {
            return true;
        }
        using clock = std::chrono::steady_clock;
        auto const deadline = clock::now() + timeout;
        while (clock::now() < deadline) {
            int status = 0;
            pid_t const r = ::waitpid(pid, &status, WNOHANG);
            if (r == pid) {
                pid = -1;
                aliveCached = false;
                return true;
            }
            if (r < 0) {
                if (errno == EINTR) {
                    continue;
                }
                pid = -1;
                aliveCached = false;
                return true; // already reaped
            }
            // Still alive — sleep briefly and retry.
            ::usleep(20 * 1000);
        }
        return false;
    }

    void killChild() noexcept {
        if (pid > 0) {
            ::kill(pid, SIGKILL);
            int status = 0;
            ::waitpid(pid, &status, 0);
            pid = -1;
        }
        aliveCached = false;
    }

    void writeLine(std::string const& line) {
        std::string buf = line;
        buf.push_back('\n');
        std::size_t off = 0;
        while (off < buf.size()) {
            ssize_t const n = ::write(writeFd, buf.data() + off, buf.size() - off);
            if (n < 0) {
                if (errno == EINTR) {
                    continue;
                }
                throw std::runtime_error("plugin-host child stdin write failed");
            }
            off += static_cast<std::size_t>(n);
        }
    }
};

OutOfProcessPluginHost::OutOfProcessPluginHost(OutOfProcessHostConfig config)
    : m_impl(std::make_unique<Impl>()) {
    m_impl->config = std::move(config);

    if (m_impl->config.pythonExecutable.empty()) {
        m_impl->config.pythonExecutable = "python3";
    }
    if (m_impl->config.childScript.empty()) {
        throw std::runtime_error("OutOfProcessPluginHost: childScript must be set in the config");
    }

    int hostToChild[2] = {-1, -1};
    int childToHost[2] = {-1, -1};
    if (::pipe(hostToChild) != 0 || ::pipe(childToHost) != 0) {
        if (hostToChild[0] >= 0) {
            ::close(hostToChild[0]);
        }
        if (hostToChild[1] >= 0) {
            ::close(hostToChild[1]);
        }
        throw std::runtime_error("OutOfProcessPluginHost: pipe() failed");
    }

    pid_t const pid = ::fork();
    if (pid < 0) {
        ::close(hostToChild[0]);
        ::close(hostToChild[1]);
        ::close(childToHost[0]);
        ::close(childToHost[1]);
        throw std::runtime_error("OutOfProcessPluginHost: fork() failed");
    }

    if (pid == 0) {
        // CHILD: rewire stdin <- hostToChild read end, stdout -> childToHost
        // write end, then execvp into python3 with the child script.
        ::dup2(hostToChild[0], STDIN_FILENO);
        ::dup2(childToHost[1], STDOUT_FILENO);
        // Close all four pipe fds on the child side. dup2'd copies remain
        // bound to STDIN/STDOUT.
        ::close(hostToChild[0]);
        ::close(hostToChild[1]);
        ::close(childToHost[0]);
        ::close(childToHost[1]);

        // Build PYTHONPATH for the child so it can `import ajazz_plugins`.
        std::string pythonPath;
        for (auto const& p : m_impl->config.pythonPath) {
            if (!pythonPath.empty()) {
                pythonPath.push_back(':');
            }
            pythonPath += p.string();
        }
        if (!pythonPath.empty()) {
            ::setenv("PYTHONPATH", pythonPath.c_str(), 1);
        }
        // Disable .pyc writing so the child leaves no on-disk crumbs.
        ::setenv("PYTHONDONTWRITEBYTECODE", "1", 1);
        // Line-buffer stdout so our line reads see complete lines on
        // every print() the child makes.
        ::setenv("PYTHONUNBUFFERED", "1", 1);

        std::string const scriptStr = m_impl->config.childScript.string();
        char const* exe = m_impl->config.pythonExecutable.c_str();
        char* args[] = {const_cast<char*>(exe), const_cast<char*>(scriptStr.c_str()), nullptr};
        ::execvp(exe, args);
        // execvp failed; drop a one-line JSON error and exit so the
        // host's read loop sees something specific.
        std::fprintf(stdout,
                     "{\"event\":\"spawn_error\",\"errno\":%d,\"message\":\"execvp failed\"}\n",
                     errno);
        std::fflush(stdout);
        std::_Exit(127);
    }

    // PARENT
    ::close(hostToChild[0]);
    ::close(childToHost[1]);
    m_impl->pid = pid;
    m_impl->writeFd = hostToChild[1];
    m_impl->readFd = childToHost[0];
    m_impl->aliveCached = true;

    // Read the ready handshake.
    auto const result = readLine(m_impl->readFd, m_impl->readCarry, m_impl->config.readyTimeout);
    if (!result.ok) {
        m_impl->killChild();
        m_impl->cleanupFds();
        throw std::runtime_error(result.eof ? "plugin-host child exited before ready handshake"
                                            : "plugin-host child timed out waiting for ready");
    }
    auto const event = findStringField(result.line, "event");
    if (event != "ready") {
        m_impl->killChild();
        m_impl->cleanupFds();
        throw std::runtime_error("plugin-host child sent unexpected first line: " + result.line);
    }
    m_impl->pythonVersion = findStringField(result.line, "python");
    AJAZZ_LOG_INFO("plugin-host",
                   "out-of-process child ready: pid={} python={}",
                   static_cast<long>(m_impl->pid),
                   m_impl->pythonVersion);
}

OutOfProcessPluginHost::~OutOfProcessPluginHost() {
    if (!m_impl) {
        return;
    }
    if (m_impl->aliveCached && m_impl->pid > 0) {
        try {
            std::lock_guard const lock(m_impl->mutex);
            m_impl->writeLine(buildOp("shutdown"));
        } catch (std::exception const&) {
            // Already-dead child or broken pipe — fall through to kill.
        }
        if (!m_impl->waitChildExit(m_impl->config.shutdownTimeout)) {
            AJAZZ_LOG_WARN("plugin-host",
                           "child {} did not exit within {} ms; SIGKILL",
                           static_cast<long>(m_impl->pid),
                           m_impl->config.shutdownTimeout.count());
            m_impl->killChild();
        }
    }
    m_impl->cleanupFds();
}

bool OutOfProcessPluginHost::isAlive() const noexcept {
    if (!m_impl) {
        return false;
    }
    if (!m_impl->aliveCached || m_impl->pid <= 0) {
        return false;
    }
    int status = 0;
    pid_t const r = ::waitpid(m_impl->pid, &status, WNOHANG);
    if (r == 0) {
        return true;
    }
    // Child exited; reap so isAlive becomes idempotent.
    m_impl->aliveCached = false;
    m_impl->pid = -1;
    return false;
}

int OutOfProcessPluginHost::childPid() const noexcept {
    return m_impl ? m_impl->pid : -1;
}

std::vector<PluginInfo> OutOfProcessPluginHost::plugins() {
    if (!m_impl) {
        throw std::runtime_error("OutOfProcessPluginHost: moved-from instance");
    }
    std::lock_guard const lock(m_impl->mutex);
    if (!m_impl->aliveCached) {
        throw std::runtime_error("plugin-host child is not alive");
    }
    m_impl->writeLine(buildOp("list_plugins"));

    // Multi-line response: zero or more `{"event":"plugin",…}` lines
    // followed by exactly one `{"event":"plugins_complete",…}` line.
    // Keeps each line a flat JSON object so the host's mini-parser
    // covers the whole protocol without an array-aware JSON dep.
    std::vector<PluginInfo> out;
    while (true) {
        auto const result =
            readLine(m_impl->readFd, m_impl->readCarry, std::chrono::milliseconds{2000});
        if (!result.ok) {
            m_impl->aliveCached = false;
            throw std::runtime_error(result.eof ? "plugin-host child died during list_plugins"
                                                : "plugin-host child timed out on list_plugins");
        }
        auto const event = findStringField(result.line, "event");
        if (event == "plugin") {
            PluginInfo info;
            info.id = findStringField(result.line, "id");
            info.name = findStringField(result.line, "name");
            info.version = findStringField(result.line, "version");
            info.authors = findStringField(result.line, "authors");
            out.push_back(std::move(info));
            continue;
        }
        if (event == "plugins_complete") {
            return out;
        }
        throw std::runtime_error("plugin-host: unexpected event in response to list_plugins: " +
                                 result.line);
    }
}

void OutOfProcessPluginHost::addSearchPath(std::filesystem::path const& path) {
    if (!m_impl) {
        throw std::runtime_error("OutOfProcessPluginHost: moved-from instance");
    }
    std::lock_guard const lock(m_impl->mutex);
    if (!m_impl->aliveCached) {
        throw std::runtime_error("plugin-host child is not alive");
    }
    m_impl->writeLine(buildOp("add_search_path", {{"path", path.string()}}));
    auto const result =
        readLine(m_impl->readFd, m_impl->readCarry, std::chrono::milliseconds{2000});
    if (!result.ok) {
        m_impl->aliveCached = false;
        throw std::runtime_error(result.eof ? "plugin-host child died during add_search_path"
                                            : "plugin-host child timed out on add_search_path");
    }
    auto const event = findStringField(result.line, "event");
    if (event != "path_added") {
        throw std::runtime_error("plugin-host: unexpected event in response to add_search_path: " +
                                 result.line);
    }
}

std::size_t OutOfProcessPluginHost::loadAll() {
    if (!m_impl) {
        throw std::runtime_error("OutOfProcessPluginHost: moved-from instance");
    }
    std::lock_guard const lock(m_impl->mutex);
    if (!m_impl->aliveCached) {
        throw std::runtime_error("plugin-host child is not alive");
    }
    m_impl->writeLine(buildOp("load_all"));

    // Multi-line response. Per-plugin events:
    //   `plugin_loaded`  — successful import; carry id/name/version.
    //   `plugin_error`   — import failed; we log warning, do not abort.
    // Terminator:
    //   `load_complete`  — count of plugins newly loaded.
    // Loading per plugin can take 100s of ms (especially on a cold
    // CPython start with imports of numpy / opencv etc.); the per-
    // line timeout reflects that — Python's `import` of pandas is
    // the historical 95th percentile.
    std::size_t loadedCount = 0;
    while (true) {
        auto const result =
            readLine(m_impl->readFd, m_impl->readCarry, std::chrono::milliseconds{15000});
        if (!result.ok) {
            m_impl->aliveCached = false;
            throw std::runtime_error(result.eof ? "plugin-host child died during load_all"
                                                : "plugin-host child timed out on load_all");
        }
        auto const event = findStringField(result.line, "event");
        if (event == "plugin_loaded") {
            ++loadedCount;
            continue;
        }
        if (event == "plugin_error") {
            // The child has already logged this on its own stderr
            // (which the host inherits). We surface it as a host log
            // too so plugin authors see the same failure indication
            // whether or not the child's stderr goes anywhere.
            auto const id = findStringField(result.line, "id");
            auto const message = findStringField(result.line, "message");
            AJAZZ_LOG_WARN("plugin-host", "child failed to load {}: {}", id, message);
            continue;
        }
        if (event == "load_complete") {
            return loadedCount;
        }
        throw std::runtime_error("plugin-host: unexpected event in response to load_all: " +
                                 result.line);
    }
}

bool OutOfProcessPluginHost::dispatch(std::string_view pluginId,
                                      std::string_view actionId,
                                      std::string_view settingsJson) {
    if (!m_impl) {
        throw std::runtime_error("OutOfProcessPluginHost: moved-from instance");
    }
    std::lock_guard const lock(m_impl->mutex);
    if (!m_impl->aliveCached) {
        throw std::runtime_error("plugin-host child is not alive");
    }
    m_impl->writeLine(buildOp(
        "dispatch",
        {{"plugin_id", pluginId}, {"action_id", actionId}, {"settings_json", settingsJson}}));
    auto const result =
        readLine(m_impl->readFd, m_impl->readCarry, std::chrono::milliseconds{5000});
    if (!result.ok) {
        m_impl->aliveCached = false;
        throw std::runtime_error(result.eof ? "plugin-host child died during dispatch"
                                            : "plugin-host child timed out on dispatch");
    }
    auto const event = findStringField(result.line, "event");
    if (event == "dispatched") {
        return true;
    }
    if (event == "dispatch_error") {
        // Plugin-side failure (unknown id or handler exception). The
        // child catches the Python exception so dispatch_error is a
        // soft failure: log and report `false` to the caller.
        auto const message = findStringField(result.line, "message");
        AJAZZ_LOG_WARN("plugin-host", "dispatch {}.{} failed: {}", pluginId, actionId, message);
        return false;
    }
    throw std::runtime_error("plugin-host: unexpected event in response to dispatch: " +
                             result.line);
}

bool OutOfProcessPluginHost::crashChildForTest() {
    if (!m_impl || !m_impl->aliveCached) {
        return false;
    }
    {
        std::lock_guard const lock(m_impl->mutex);
        try {
            m_impl->writeLine(buildOp("_crash_for_test"));
        } catch (std::exception const&) {
            // Pipe broke as the child was already going down; that
            // counts as observation that the child is dead.
        }
    }
    // Give the child up to 1 s to die from the signal.
    return m_impl->waitChildExit(std::chrono::milliseconds{1000}) || !isAlive();
}

} // namespace ajazz::plugins

#endif // !_WIN32
