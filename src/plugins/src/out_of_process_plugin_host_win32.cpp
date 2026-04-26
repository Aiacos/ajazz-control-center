// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file out_of_process_plugin_host_win32.cpp
 * @brief Windows implementation of @ref OutOfProcessPluginHost
 *        (audit finding A4 — slice 3d).
 *
 * Mirrors @ref out_of_process_plugin_host.cpp method-for-method, but
 * substitutes the POSIX `fork()` + `pipe(2)` + `poll(2)` machinery
 * with Windows CRT equivalents:
 *
 *   - `_pipe` from @c <io.h> creates anonymous pipes whose ends are
 *     CRT file descriptors. We inherit a redirected stdin/stdout
 *     across the spawn just like the POSIX path.
 *   - `_spawnvp(_P_NOWAIT, ...)` from @c <process.h> spawns the
 *     child without blocking; the return value is a HANDLE-as-intptr
 *     that we use for `WaitForSingleObject`, `TerminateProcess`,
 *     and `GetExitCodeProcess`.
 *   - `PeekNamedPipe` (from @c <windows.h>, applied to the HANDLE
 *     obtained via `_get_osfhandle(fd)`) gives us the timeout-read
 *     semantics that `poll(2)` provides on POSIX. We don't use
 *     overlapped I/O — pipe-bytes-available polling with a small
 *     sleep is adequate for line-delimited JSON IPC and is far
 *     simpler to reason about than overlapped completion.
 *
 * @par Testing posture
 *
 * **This file ships untested on Windows.** The dev environment that
 * produced slice 3d is Linux; CI does not yet run Windows jobs that
 * exercise this code. Linux + macOS continue to use the POSIX file
 * (`out_of_process_plugin_host.cpp`); Windows builds compile this
 * file via the CMake gate but the runtime path has only been
 * smoke-tested through code review. The class signatures mirror the
 * POSIX backend exactly, so the unit tests in
 * `tests/unit/test_out_of_process_plugin_host.cpp` and
 * `test_linux_bwrap_sandbox.cpp` will be runnable on a Windows CI
 * runner once one is provisioned — at which point any divergences
 * surface immediately.
 *
 * Sandbox status: Windows AppContainer + restricted token are not
 * yet wired in. The `sandbox` field in `OutOfProcessHostConfig` is
 * still consulted (callers can pass a NoOpSandbox or a custom
 * sandbox), but `LinuxBwrapSandbox` and `MacosSandboxExecSandbox`
 * report `hasBwrap()` / `hasSandboxExec()` == false on Windows
 * (their tools don't exist there) and degrade to passthrough.
 * A future slice 3d-ii will introduce `WindowsAppContainerSandbox`
 * with a different shape — AppContainer is configured at
 * `CreateProcessW` time via `STARTUPINFOEX`, not via a wrapper
 * executable, so the current `Sandbox::decorate(argv)` interface
 * will need a side-channel.
 */
#ifdef _WIN32

#include "ajazz/core/logger.hpp"
#include "ajazz/plugins/out_of_process_plugin_host.hpp"
#include "ajazz/plugins/sandbox.hpp"
#include "wire_protocol.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

// Windows headers: <windows.h> brings in HANDLE, PeekNamedPipe, etc.
// <io.h> + <process.h> + <fcntl.h> for the CRT pipe/spawn surface.
// NOMINMAX prevents the legacy macros from clobbering std::min/max.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <fcntl.h>
#include <io.h>
#include <process.h>
#include <windows.h>

namespace ajazz::plugins {
namespace {

using wire::buildOp;
using wire::findStringArrayField;
using wire::findStringField;

/// Read-line result, identical shape to the POSIX side. The Windows
/// timeout source is `PeekNamedPipe` polling rather than `poll(2)`,
/// but the contract callers see is the same.
struct ReadLineResult {
    bool ok{false};
    bool eof{false};
    std::string line;
};

/// Polling resolution for `readLine` — how long we sleep between
/// `PeekNamedPipe` checks. Smaller = lower latency, higher CPU; 5 ms
/// is a good middle ground (sub-frame for any plausible IPC, well
/// under any user-perceptible threshold).
constexpr std::chrono::milliseconds kReadPollInterval{5};

/// Read until newline OR child closes / timeout fires. Mirrors the
/// POSIX `readLine` contract: `{ok=true,line=...}` on a complete
/// line, `{ok=false,eof=true}` when the child closes its stdout,
/// `{ok=false,eof=false}` on timeout. The line excludes the
/// terminating newline.
///
/// Polls `PeekNamedPipe` rather than using overlapped I/O. The
/// sleep loop costs at most `kReadPollInterval` of latency on each
/// new byte; for line-delimited JSON ops that take milliseconds to
/// generate this is invisible.
ReadLineResult readLine(int fd, std::string& carry, std::chrono::milliseconds timeout) {
    using clock = std::chrono::steady_clock;
    auto const deadline = clock::now() + timeout;

    // Convert the CRT fd to a Win32 HANDLE for PeekNamedPipe. The
    // HANDLE is owned by the CRT — do NOT CloseHandle it; we only
    // borrow it for the lifetime of the read.
    HANDLE const h = reinterpret_cast<HANDLE>(_get_osfhandle(fd));
    if (h == INVALID_HANDLE_VALUE) {
        return {};
    }

    while (true) {
        // 1. If `carry` already has a complete line, return it.
        auto const nl = carry.find('\n');
        if (nl != std::string::npos) {
            ReadLineResult r;
            r.ok = true;
            r.line = carry.substr(0, nl);
            carry.erase(0, nl + 1);
            return r;
        }

        // 2. Check timeout.
        if (clock::now() >= deadline) {
            return {};
        }

        // 3. Probe with PeekNamedPipe. If the child has closed its
        //    end of the pipe, the call fails with ERROR_BROKEN_PIPE
        //    — that's our EOF signal.
        DWORD bytesAvail = 0;
        BOOL const peekOk = PeekNamedPipe(h, nullptr, 0, nullptr, &bytesAvail, nullptr);
        if (peekOk == 0) {
            DWORD const err = GetLastError();
            if (err == ERROR_BROKEN_PIPE || err == ERROR_HANDLE_EOF) {
                ReadLineResult r;
                r.eof = true;
                return r;
            }
            // Other errors are treated as timeout-equivalent so the
            // caller can decide whether to mark the host dead.
            return {};
        }

        if (bytesAvail == 0) {
            // Nothing yet — sleep briefly and retry.
            std::this_thread::sleep_for(kReadPollInterval);
            continue;
        }

        // 4. Read whatever's available, up to our buffer size. _read
        //    blocks on the CRT side, but we just confirmed bytes are
        //    pending so the call returns immediately.
        char buf[4096];
        DWORD const wantRead = bytesAvail < sizeof(buf) ? bytesAvail : sizeof(buf);
        int const n = _read(fd, buf, wantRead);
        if (n == 0) {
            // _read returns 0 on EOF (even though PeekNamedPipe said
            // bytes available — race window if the child closed
            // between peek and read).
            ReadLineResult r;
            r.eof = true;
            return r;
        }
        if (n < 0) {
            return {};
        }
        carry.append(buf, static_cast<std::size_t>(n));
    }
}

} // namespace

struct OutOfProcessPluginHost::Impl {
    OutOfProcessHostConfig config;
    intptr_t processHandle{-1}; ///< _spawnvp return value, used as HANDLE
    int writeFd{-1};            ///< parent-end of host->child pipe
    int readFd{-1};             ///< parent-end of child->host pipe
    std::string readCarry;
    std::string pythonVersion;
    std::mutex mutex;
    bool aliveCached{false};

    void cleanupFds() noexcept {
        if (writeFd >= 0) {
            _close(writeFd);
            writeFd = -1;
        }
        if (readFd >= 0) {
            _close(readFd);
            readFd = -1;
        }
    }

    /// Wait up to @p timeout for the child to exit cleanly.
    /// Returns true if the child has exited (and was reaped), false
    /// on timeout. The Windows analogue of the POSIX `waitpid`
    /// loop in the sibling file.
    bool waitChildExit(std::chrono::milliseconds timeout) {
        if (processHandle == -1 || processHandle == 0) {
            return true;
        }
        auto const ms = static_cast<DWORD>(timeout.count());
        DWORD const rc = WaitForSingleObject(reinterpret_cast<HANDLE>(processHandle), ms);
        if (rc == WAIT_OBJECT_0) {
            CloseHandle(reinterpret_cast<HANDLE>(processHandle));
            processHandle = -1;
            aliveCached = false;
            return true;
        }
        // WAIT_TIMEOUT or WAIT_FAILED — caller decides escalation.
        return false;
    }

    /// SIGKILL equivalent: terminate the child unconditionally and
    /// release the process handle. Idempotent.
    void killChild() noexcept {
        if (processHandle != -1 && processHandle != 0) {
            HANDLE const h = reinterpret_cast<HANDLE>(processHandle);
            TerminateProcess(h, 1);
            // Drain the exit status so the kernel can release the
            // process record. WAIT_OBJECT_0 is the only success.
            WaitForSingleObject(h, INFINITE);
            CloseHandle(h);
            processHandle = -1;
        }
        aliveCached = false;
    }

    /// `write(2)` equivalent: keep retrying until every byte plus the
    /// trailing `\n` lands on the pipe, or the pipe breaks. Throws
    /// `std::runtime_error` on a hard write failure so the caller
    /// can mark the host dead.
    void writeLine(std::string const& line) {
        std::string buf = line;
        buf.push_back('\n');
        std::size_t off = 0;
        while (off < buf.size()) {
            int const n =
                _write(writeFd, buf.data() + off, static_cast<unsigned>(buf.size() - off));
            if (n < 0) {
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
        m_impl->config.pythonExecutable = "python";
    }
    if (m_impl->config.childScript.empty()) {
        throw std::runtime_error("OutOfProcessPluginHost: childScript must be set in the config");
    }

    // Compute the decorated spawn pre-fork-equivalent. Same pattern
    // as the POSIX side: a null sandbox in the config means "no
    // isolation" — synthesise NoOpSandbox so the spawn path is
    // uniform.
    NoOpSandbox const noOp;
    Sandbox const& sandbox = m_impl->config.sandbox
                                 ? static_cast<Sandbox const&>(*m_impl->config.sandbox)
                                 : static_cast<Sandbox const&>(noOp);
    DecoratedSpawn const spawn =
        sandbox.decorate(m_impl->config.pythonExecutable, m_impl->config.childScript);

    // Two pipes: hostToChild for stdin redirect, childToHost for
    // stdout. _O_BINARY avoids \r\n translation on stdout — the
    // wire protocol is one JSON-line-per-`\n` and any `\r` injection
    // would break framing.
    int hostToChild[2] = {-1, -1};
    int childToHost[2] = {-1, -1};
    if (_pipe(hostToChild, 65536, _O_BINARY) != 0 || _pipe(childToHost, 65536, _O_BINARY) != 0) {
        if (hostToChild[0] >= 0) {
            _close(hostToChild[0]);
        }
        if (hostToChild[1] >= 0) {
            _close(hostToChild[1]);
        }
        throw std::runtime_error("OutOfProcessPluginHost: _pipe() failed");
    }

    // Build PYTHONPATH for the child. Windows uses ';' as the path
    // separator (different from POSIX's ':'). The PYTHONPATH env
    // var is read by the child python interpreter at startup.
    std::string pythonPath;
    for (auto const& p : m_impl->config.pythonPath) {
        if (!pythonPath.empty()) {
            pythonPath.push_back(';');
        }
        pythonPath += p.string();
    }
    if (!pythonPath.empty()) {
        _putenv_s("PYTHONPATH", pythonPath.c_str());
    }
    _putenv_s("PYTHONDONTWRITEBYTECODE", "1");
    _putenv_s("PYTHONUNBUFFERED", "1");

    // Redirect stdin/stdout temporarily so the spawned child
    // inherits the right ends of the pipes. The parent's stdin/
    // stdout are restored immediately after _spawnvp returns. This
    // is racy if the parent is multithreaded, but the host ctor is
    // documented as not-thread-safe with the dtor (typical RAII
    // contract) and other threads don't run during construction.
    int const savedStdin = _dup(_fileno(stdin));
    int const savedStdout = _dup(_fileno(stdout));
    if (savedStdin < 0 || savedStdout < 0) {
        _close(hostToChild[0]);
        _close(hostToChild[1]);
        _close(childToHost[0]);
        _close(childToHost[1]);
        throw std::runtime_error("OutOfProcessPluginHost: _dup of stdio failed");
    }
    _dup2(hostToChild[0], _fileno(stdin));
    _dup2(childToHost[1], _fileno(stdout));

    // Build the argv. _spawnvp accepts an array of `const char*`
    // terminated by NULL.
    std::vector<char const*> argv;
    argv.reserve(spawn.args.size() + 1);
    for (auto const& arg : spawn.args) {
        argv.push_back(arg.c_str());
    }
    argv.push_back(nullptr);

    // _P_NOWAIT spawns asynchronously and returns the process
    // HANDLE-as-intptr. _P_DETACH is wrong here because we want a
    // wait-able handle.
    intptr_t const handle =
        _spawnvp(_P_NOWAIT, spawn.executable.c_str(), const_cast<char* const*>(argv.data()));

    // Restore parent stdio regardless of spawn outcome.
    _dup2(savedStdin, _fileno(stdin));
    _dup2(savedStdout, _fileno(stdout));
    _close(savedStdin);
    _close(savedStdout);

    // Close the child-end fds; the child has its own copies
    // through the dup2'd stdio.
    _close(hostToChild[0]);
    _close(childToHost[1]);

    if (handle == -1) {
        _close(hostToChild[1]);
        _close(childToHost[0]);
        throw std::runtime_error("OutOfProcessPluginHost: _spawnvp() failed");
    }

    m_impl->processHandle = handle;
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
                   "out-of-process child ready (win32): handle={} python={}",
                   static_cast<long long>(m_impl->processHandle),
                   m_impl->pythonVersion);
}

OutOfProcessPluginHost::~OutOfProcessPluginHost() {
    if (!m_impl) {
        return;
    }
    if (m_impl->aliveCached && m_impl->processHandle != -1) {
        try {
            std::lock_guard const lock(m_impl->mutex);
            m_impl->writeLine(buildOp("shutdown"));
        } catch (std::exception const&) {
            // Already-dead child or broken pipe — fall through to kill.
        }
        if (!m_impl->waitChildExit(m_impl->config.shutdownTimeout)) {
            AJAZZ_LOG_WARN("plugin-host",
                           "child did not exit within {} ms; TerminateProcess",
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
    if (!m_impl->aliveCached || m_impl->processHandle == -1) {
        return false;
    }
    // STILL_ACTIVE is 259, returned by GetExitCodeProcess for a
    // running process. Documented edge case: a process that
    // legitimately exits with code 259 looks "alive" — but our
    // child uses exit codes 0 (clean) and 127 (spawn error), so
    // this is safe for our wire protocol.
    DWORD exitCode = 0;
    if (GetExitCodeProcess(reinterpret_cast<HANDLE>(m_impl->processHandle), &exitCode) == 0) {
        return false;
    }
    if (exitCode == STILL_ACTIVE) {
        return true;
    }
    // Reap on observation of exit so isAlive becomes idempotent.
    CloseHandle(reinterpret_cast<HANDLE>(m_impl->processHandle));
    m_impl->processHandle = -1;
    m_impl->aliveCached = false;
    return false;
}

int OutOfProcessPluginHost::childPid() const noexcept {
    // On Windows we expose the process HANDLE-as-int. The POSIX side
    // returns the actual pid. Callers should treat the value as
    // opaque ("a unique-per-process identifier the host knows
    // about") rather than as a real OS pid; the existing logging
    // call sites already do.
    if (!m_impl || m_impl->processHandle == -1) {
        return -1;
    }
    return static_cast<int>(
        reinterpret_cast<intptr_t>(reinterpret_cast<HANDLE>(m_impl->processHandle)));
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
            info.permissions = findStringArrayField(result.line, "permissions");
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
            // Pipe broken as the child was already going down; that
            // counts as observation that the child is dead.
        }
    }
    return m_impl->waitChildExit(std::chrono::milliseconds{1000}) || !isAlive();
}

} // namespace ajazz::plugins

#endif // _WIN32
