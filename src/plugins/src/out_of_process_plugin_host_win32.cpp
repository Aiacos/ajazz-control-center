// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file out_of_process_plugin_host_win32.cpp
 * @brief Windows implementation of @ref OutOfProcessPluginHost
 *        (audit finding A4 — slice 3d).
 *
 * Mirrors @ref out_of_process_plugin_host.cpp method-for-method, but
 * substitutes the POSIX `fork()` + `pipe(2)` + `poll(2)` machinery
 * with Windows Win32 equivalents:
 *
 *   - `CreatePipe` creates inheritable anonymous pipes. We pass them
 *     to the child through `STARTUPINFO::hStdInput` / `hStdOutput`
 *     with `STARTF_USESTDHANDLES` and `bInheritHandles = TRUE`. The
 *     parent's end of each pipe is set non-inheritable via
 *     `SetHandleInformation` so closing the parent's end cleanly
 *     tears the child's end down to EOF.
 *   - `CreateProcessW` (or `CreateProcessAsUserW` under a restricted
 *     token) starts the child. When the sandbox populates an
 *     AppContainer configuration on the opaque `ProcessAttributes`,
 *     we build a `STARTUPINFOEX` with
 *     `PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES` and the
 *     `EXTENDED_STARTUPINFO_PRESENT` creation flag. Without an
 *     AppContainer we use a plain `STARTUPINFO`.
 *   - `PeekNamedPipe` (from @c <windows.h>, applied to the HANDLE
 *     obtained via `_get_osfhandle(fd)`) gives us the timeout-read
 *     semantics that `poll(2)` provides on POSIX. We don't use
 *     overlapped I/O — pipe-bytes-available polling with a small
 *     sleep is adequate for line-delimited JSON IPC and is far
 *     simpler to reason about than overlapped completion.
 *   - The parent-end pipe HANDLEs are wrapped in CRT fds via
 *     `_open_osfhandle` so the readLine / writeLine helpers keep
 *     using the fd-based `_read` / `_write` calls for symmetry
 *     with the POSIX backend.
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
 * Sandbox status: Windows AppContainer + restricted token support
 * lands in slice 3d-ii alongside this backend. When the
 * @c OutOfProcessHostConfig::sandbox is a @c WindowsAppContainerSandbox
 * with @c hasAppContainer() == true we route the spawn through
 * @c CreateProcessW with a @c STARTUPINFOEX carrying the AppContainer
 * SID + capability SID array (populated by
 * @c WindowsAppContainerSandbox::configureProcessAttributes on the
 * opaque @c ProcessAttributes pimpl), and optionally a restricted
 * token via @c CreateProcessAsUserW. For @c NoOpSandbox and the
 * passthrough path (e.g. @c LinuxBwrapSandbox on a Windows machine
 * where @c bwrap is obviously absent, degrading to passthrough) we
 * still go through @c CreateProcessW but with a plain @c STARTUPINFO
 * — no EXTENDED_STARTUPINFO_PRESENT flag, no AppContainer attribute
 * list — so the cost of the un-sandboxed path is unchanged.
 */
#ifdef _WIN32

#include "ajazz/core/logger.hpp"
#include "ajazz/plugins/out_of_process_plugin_host.hpp"
#include "ajazz/plugins/sandbox.hpp"
// Complete @c ProcessAttributes::Impl definition. Needed because the spawn
// path below reads pimpl members (winAttrs->appContainerSid, capabilities,
// restrictedToken) when populating STARTUPINFOEX. The forward declaration
// in sandbox.hpp is insufficient for member access.
#include "process_attributes_impl_win32.hpp"
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

// Windows headers: <windows.h> brings in HANDLE, PeekNamedPipe,
// CreateProcessW, STARTUPINFOEX, etc. <io.h> + <fcntl.h> are kept
// for the CRT fd-based readLine/writeLine path; the spawn itself
// no longer uses _spawnvp / _pipe.
// NOMINMAX prevents the legacy macros from clobbering std::min/max.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <fcntl.h>
#include <io.h>
#include <windows.h>

namespace ajazz::plugins {

// Declared in sandbox.cpp. Returns null if the caller's
// ProcessAttributes carries no win32 state — which is the case for
// NoOp / passthrough / POSIX sandboxes. Non-null means the host
// should route the spawn through CreateProcessW with a
// STARTUPINFOEX that carries the AppContainer SID + capability list.
extern ProcessAttributes::Impl const*
windowsProcessAttributes(ProcessAttributes const& attrs) noexcept;

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

/// Quote one argv entry for `CreateProcessW`'s single command-line
/// string, following the `CommandLineToArgvW` round-trip rules
/// documented in the "Parsing C Command-Line Arguments" MSDN article.
/// Rules:
///   * Surround with double quotes only if the string is empty or
///     contains a space / tab / quote.
///   * Backslashes NOT followed by a quote are literal.
///   * Backslashes followed by a quote: each pair becomes one
///     literal backslash, the trailing run doubles before the
///     quote.
/// This is the inverse of the Microsoft C runtime's argv parser,
/// so `argv[0]` in the child reads exactly what we put in here.
void appendQuoted(std::wstring& out, std::wstring const& arg) {
    bool const needsQuotes = arg.empty() || arg.find_first_of(L" \t\"") != std::wstring::npos;
    if (!needsQuotes) {
        out.append(arg);
        return;
    }
    out.push_back(L'"');
    std::size_t backslashes = 0;
    for (wchar_t c : arg) {
        if (c == L'\\') {
            ++backslashes;
            continue;
        }
        if (c == L'"') {
            // Double every preceding backslash + the quote itself.
            out.append(backslashes * 2 + 1, L'\\');
            out.push_back(L'"');
        } else {
            // Emit backslashes literally.
            out.append(backslashes, L'\\');
            out.push_back(c);
        }
        backslashes = 0;
    }
    // Trailing backslashes: double them so the closing quote is a
    // real closing quote (not an escaped one).
    out.append(backslashes * 2, L'\\');
    out.push_back(L'"');
}

/// Build a `CreateProcessW`-compatible command-line from an argv.
/// The first entry is the executable name (by convention argv[0]),
/// the rest are the arguments.
std::wstring buildCommandLine(std::vector<std::string> const& argv) {
    std::wstring out;
    for (std::size_t i = 0; i < argv.size(); ++i) {
        if (i != 0) {
            out.push_back(L' ');
        }
        std::wstring wide;
        int const size = MultiByteToWideChar(
            CP_UTF8, 0, argv[i].data(), static_cast<int>(argv[i].size()), nullptr, 0);
        if (size > 0) {
            wide.resize(static_cast<std::size_t>(size));
            MultiByteToWideChar(
                CP_UTF8, 0, argv[i].data(), static_cast<int>(argv[i].size()), wide.data(), size);
        }
        appendQuoted(out, wide);
    }
    return out;
}

/// UTF-8 ⇒ wide for the executable path / env. Factored out so
/// CreateProcessW can take a typed `wchar_t const*` without a second
/// MBCS conversion at the call site.
std::wstring utf8ToWide(std::string const& s) {
    if (s.empty()) {
        return {};
    }
    int const size =
        MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (size <= 0) {
        return {};
    }
    std::wstring out(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), size);
    return out;
}

} // namespace

struct OutOfProcessPluginHost::Impl {
    OutOfProcessHostConfig config;
    intptr_t processHandle{-1}; ///< HANDLE-as-intptr from CreateProcess
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

    // Compute the decorated spawn. Same pattern as the POSIX side:
    // a null sandbox in the config means "no isolation" — synthesise
    // NoOpSandbox so the spawn path is uniform.
    NoOpSandbox const noOp;
    Sandbox const& sandbox = m_impl->config.sandbox
                                 ? static_cast<Sandbox const&>(*m_impl->config.sandbox)
                                 : static_cast<Sandbox const&>(noOp);
    DecoratedSpawn const spawn =
        sandbox.decorate(m_impl->config.pythonExecutable, m_impl->config.childScript);

    // Ask the sandbox for any Windows-specific process attributes
    // (AppContainer SID, capabilities, restricted token). On POSIX
    // sandboxes and NoOp this leaves the struct empty; the win32
    // view `windowsProcessAttributes(...)` then returns null and
    // we take the fast CreateProcessW path without STARTUPINFOEX.
    ProcessAttributes procAttrs;
    sandbox.configureProcessAttributes(procAttrs);
    ProcessAttributes::Impl const* const winAttrs = windowsProcessAttributes(procAttrs);

    // Two pipes with SECURITY_ATTRIBUTES.bInheritHandle = TRUE so
    // the child inherits the right ends. We CANNOT use _pipe for
    // this because the CRT pipes are not inheritable by default,
    // and dup2()-ing them over stdin/stdout only works for
    // _spawnvp (which inherits the fds, not the handles). Going
    // through CreatePipe gives us direct HANDLE control.
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = nullptr;

    HANDLE childStdinRead = nullptr;
    HANDLE childStdinWrite = nullptr;
    HANDLE childStdoutRead = nullptr;
    HANDLE childStdoutWrite = nullptr;

    if (CreatePipe(&childStdinRead, &childStdinWrite, &sa, 65536) == 0) {
        throw std::runtime_error("OutOfProcessPluginHost: CreatePipe(stdin) failed");
    }
    // The parent's end of stdin must NOT be inherited by the child,
    // else it keeps the read end alive through the child's own
    // handle table and our "EOF on close" semantics break.
    if (SetHandleInformation(childStdinWrite, HANDLE_FLAG_INHERIT, 0) == 0) {
        CloseHandle(childStdinRead);
        CloseHandle(childStdinWrite);
        throw std::runtime_error("OutOfProcessPluginHost: SetHandleInformation(stdin) failed");
    }
    if (CreatePipe(&childStdoutRead, &childStdoutWrite, &sa, 65536) == 0) {
        CloseHandle(childStdinRead);
        CloseHandle(childStdinWrite);
        throw std::runtime_error("OutOfProcessPluginHost: CreatePipe(stdout) failed");
    }
    if (SetHandleInformation(childStdoutRead, HANDLE_FLAG_INHERIT, 0) == 0) {
        CloseHandle(childStdinRead);
        CloseHandle(childStdinWrite);
        CloseHandle(childStdoutRead);
        CloseHandle(childStdoutWrite);
        throw std::runtime_error("OutOfProcessPluginHost: SetHandleInformation(stdout) failed");
    }

    // Build PYTHONPATH for the child. Windows uses ';' as the path
    // separator. PYTHONPATH is read by the child python interpreter
    // at startup. We use the parent's environment (plus the new
    // PYTHONPATH) so the child inherits PATH etc — CreateProcessW
    // inherits the parent's env when lpEnvironment is null.
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

    // Build the wide command line.
    std::wstring cmdline = buildCommandLine(spawn.args);
    std::wstring const exePathWide = utf8ToWide(spawn.executable);

    // STARTUPINFO (either plain or extended) wires up the inherited
    // stdio handles.
    STARTUPINFOEXW siex{};
    siex.StartupInfo.cb = sizeof(siex);
    siex.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    siex.StartupInfo.hStdInput = childStdinRead;
    siex.StartupInfo.hStdOutput = childStdoutWrite;
    // The child does NOT inherit our stderr — let it go to the
    // parent's console so AJAZZ_LOG_WARN on the plugin side surfaces
    // in the terminal that launched the host. Same as POSIX.
    siex.StartupInfo.hStdError = GetStdHandle(STD_ERROR_HANDLE);

    // Process creation flags. CREATE_NO_WINDOW keeps the child from
    // flashing a console window when the host is a GUI app.
    DWORD creationFlags = CREATE_NO_WINDOW;

    // AppContainer attribute list — only populated if the sandbox
    // gave us a container SID. Size query first, then Initialize +
    // Update.
    std::vector<std::byte> attrListBuf;
    LPPROC_THREAD_ATTRIBUTE_LIST attrList = nullptr;
    SECURITY_CAPABILITIES secCaps{};

    if (winAttrs != nullptr && winAttrs->appContainerSid != nullptr) {
        SIZE_T attrListSize = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &attrListSize);
        attrListBuf.resize(attrListSize);
        attrList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attrListBuf.data());
        if (InitializeProcThreadAttributeList(attrList, 1, 0, &attrListSize) == 0) {
            CloseHandle(childStdinRead);
            CloseHandle(childStdinWrite);
            CloseHandle(childStdoutRead);
            CloseHandle(childStdoutWrite);
            throw std::runtime_error(
                "OutOfProcessPluginHost: InitializeProcThreadAttributeList failed");
        }

        secCaps.AppContainerSid = winAttrs->appContainerSid;
        secCaps.CapabilityCount = static_cast<DWORD>(winAttrs->capabilities.size());
        // Const-cast is safe: UpdateProcThreadAttribute takes PVOID
        // but does not mutate the capability array.
        secCaps.Capabilities = winAttrs->capabilities.empty()
                                   ? nullptr
                                   : const_cast<PSID_AND_ATTRIBUTES>(winAttrs->capabilities.data());

        if (UpdateProcThreadAttribute(attrList,
                                      0,
                                      PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES,
                                      &secCaps,
                                      sizeof(secCaps),
                                      nullptr,
                                      nullptr) == 0) {
            DeleteProcThreadAttributeList(attrList);
            CloseHandle(childStdinRead);
            CloseHandle(childStdinWrite);
            CloseHandle(childStdoutRead);
            CloseHandle(childStdoutWrite);
            throw std::runtime_error("OutOfProcessPluginHost: UpdateProcThreadAttribute failed");
        }
        siex.lpAttributeList = attrList;
        creationFlags |= EXTENDED_STARTUPINFO_PRESENT;
    }

    PROCESS_INFORMATION pi{};
    // The restricted-token path uses CreateProcessAsUserW; the
    // plain path uses CreateProcessW. Both take the same STARTUPINFO
    // (ex) and return a PROCESS_INFORMATION.
    BOOL spawnOk = FALSE;
    if (winAttrs != nullptr && winAttrs->restrictedToken != nullptr) {
        spawnOk = CreateProcessAsUserW(winAttrs->restrictedToken,
                                       exePathWide.c_str(),
                                       cmdline.data(),
                                       nullptr,
                                       nullptr,
                                       TRUE, // bInheritHandles
                                       creationFlags,
                                       nullptr, // lpEnvironment = parent's
                                       nullptr, // lpCurrentDirectory = parent's
                                       &siex.StartupInfo,
                                       &pi);
    } else {
        spawnOk = CreateProcessW(exePathWide.c_str(),
                                 cmdline.data(),
                                 nullptr,
                                 nullptr,
                                 TRUE,
                                 creationFlags,
                                 nullptr,
                                 nullptr,
                                 &siex.StartupInfo,
                                 &pi);
    }

    // Tear down the attribute list regardless of spawn outcome.
    if (attrList != nullptr) {
        DeleteProcThreadAttributeList(attrList);
    }

    // Close the child-end handles in the parent. The child already
    // inherited them; we only keep our ends.
    CloseHandle(childStdinRead);
    CloseHandle(childStdoutWrite);

    if (spawnOk == 0) {
        DWORD const err = GetLastError();
        CloseHandle(childStdinWrite);
        CloseHandle(childStdoutRead);
        throw std::runtime_error("OutOfProcessPluginHost: CreateProcess failed, GetLastError=" +
                                 std::to_string(err));
    }

    // Convert the parent-end HANDLEs to CRT fds so the rest of the
    // file (readLine / writeLine) keeps using the same fd-based path
    // as before. _open_osfhandle takes ownership of the HANDLE —
    // _close will call CloseHandle. We do NOT keep both the HANDLE
    // and the fd pointing at the same pipe end.
    int const writeFd = _open_osfhandle(reinterpret_cast<intptr_t>(childStdinWrite), _O_BINARY);
    // If the first _open_osfhandle succeeded, ownership has
    // transferred and we must NOT CloseHandle(childStdinWrite) on
    // cleanup — _close(writeFd) or the fd's destructor will do it.
    // Track the transfer with a flag so the error path stays clean.
    bool writeHandleOwned = (writeFd >= 0);
    int const readFd =
        _open_osfhandle(reinterpret_cast<intptr_t>(childStdoutRead), _O_BINARY | _O_RDONLY);
    bool const readHandleOwned = (readFd >= 0);
    if (writeFd < 0 || readFd < 0) {
        if (writeHandleOwned) {
            _close(writeFd); // also closes childStdinWrite
            writeHandleOwned = false;
        } else {
            CloseHandle(childStdinWrite);
        }
        if (readHandleOwned) {
            _close(readFd);
        } else {
            CloseHandle(childStdoutRead);
        }
        TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        throw std::runtime_error("OutOfProcessPluginHost: _open_osfhandle failed");
    }

    // The thread handle is not needed — the child's main thread runs
    // to completion of python3. Only the process handle is kept.
    CloseHandle(pi.hThread);

    m_impl->processHandle = reinterpret_cast<intptr_t>(pi.hProcess);
    m_impl->writeFd = writeFd;
    m_impl->readFd = readFd;
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
                   "out-of-process child ready (win32): handle={} python={} sandbox={}",
                   static_cast<long long>(m_impl->processHandle),
                   m_impl->pythonVersion,
                   winAttrs != nullptr ? "app-container" : "none");
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

            // SEC-003 #51: see the matching block in the POSIX backend.
            auto const manifestPath = findStringField(result.line, "manifest_path");
            if (!manifestPath.empty() && m_impl->config.manifestVerifier.has_value()) {
                auto const verdict = verifyManifest(manifestPath, *m_impl->config.manifestVerifier);
                info.signed_ = verdict.valid;
                info.publisher = verdict.publisherName.empty() && verdict.valid
                                     ? "self-signed"
                                     : verdict.publisherName;
            }
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
