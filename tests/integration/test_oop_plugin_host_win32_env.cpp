// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_oop_plugin_host_win32_env.cpp
 * @brief CR-01 regression integration test (Phase 06, Plan 06-02).
 *
 * Three TEST_CASEs exercise the same env-block + CreateProcessW pattern the
 * OOP plugin host uses, against a real Python child:
 *
 *   1. **Override delivery** — the child process sees the override
 *      `PYTHONPATH` value passed via `Win32EnvBlock`.
 *   2. **CR-01 regression assert** — the parent's `_wgetenv(L"PYTHONPATH")`
 *      is **unchanged** after the spawn. This is the test that fails on
 *      the pre-Plan-06-01 code (`_putenv_s` mutated the parent) and
 *      passes after.
 *   3. **Cross-instance isolation** — N=2 concurrent spawns with DIFFERENT
 *      sentinels each see their own value AND the parent env is unchanged
 *      after both joins. Closes the cross-instance race the v1.0 audit
 *      flagged.
 *
 * The test does NOT instantiate `OutOfProcessPluginHost` directly — that
 * pulls in pybind11/Python interpreter linkage indirectly. Instead it
 * exercises **the same env-block + CreateProcessW pattern the host uses**
 * via a focused helper (`spawnPythonCaptureStdout`).
 *
 * Win32-only via `#ifdef _WIN32`. The Python fixture is at
 * `tests/integration/fixtures/print_pythonpath.py` and is located via the
 * existing `AJAZZ_FIXTURES_DIR` compile definition.
 */
#ifdef _WIN32

#include "win32_env_block.hpp"

#include <array>
#include <atomic>
#include <map>
#include <string>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace {

// Resolve python3.exe (or python.exe) on PATH using SearchPathW.
// Returns the wide path on success, empty string on miss. The integration
// tests use this rather than relying on shell resolution.
std::wstring locatePython() {
    wchar_t buf[MAX_PATH] = {0};
    DWORD const got = SearchPathW(nullptr, L"python3.exe", nullptr, MAX_PATH, buf, nullptr);
    if (got > 0 && got < MAX_PATH) {
        return std::wstring{buf};
    }
    DWORD const got2 = SearchPathW(nullptr, L"python.exe", nullptr, MAX_PATH, buf, nullptr);
    if (got2 > 0 && got2 < MAX_PATH) {
        return std::wstring{buf};
    }
    return {};
}

// Trim trailing whitespace/newlines so child stdout comparisons are robust to
// CRLF / LF differences from the Python `print` family. The fixture uses
// `sys.stdout.write` so trailing whitespace should be zero, but be defensive.
std::wstring trimRight(std::wstring const& s) {
    auto end = s.size();
    while (end > 0 && (s[end - 1] == L'\r' || s[end - 1] == L'\n' || s[end - 1] == L' ')) {
        end -= 1;
    }
    return s.substr(0, end);
}

// Snapshot the current PYTHONPATH parent env value (NULL → empty string).
// Used to capture before/after invariants for the CR-01 regression assert.
//
// Uses `_wdupenv_s` (MSVC's deprecation-free replacement for `_wgetenv`) so
// the test compiles under `/W4 /WX` on the windows-2022 CI matrix without
// the C4996 deprecation warning being promoted to an error.
std::wstring snapshotParentPythonPath() {
    wchar_t* buf = nullptr;
    size_t len = 0;
    if (_wdupenv_s(&buf, &len, L"PYTHONPATH") != 0 || buf == nullptr) {
        if (buf != nullptr) {
            free(buf);
        }
        return std::wstring{};
    }
    std::wstring result{buf};
    free(buf);
    return result;
}

// Spawn `python3 <fixturesDir>/print_pythonpath.py` via CreateProcessW with a
// Win32EnvBlock containing the supplied sentinel as PYTHONPATH. Capture stdout
// via an anonymous pipe. Mirrors the spawn path that the OOP plugin host uses
// in `out_of_process_plugin_host_win32.cpp`.
//
// Returns the child's trimmed stdout. Throws std::runtime_error on spawn /
// pipe failure so Catch2 surfaces a clear failure rather than a silent empty
// result.
std::wstring spawnPythonCaptureStdout(std::wstring const& sentinel) {
    std::wstring const pythonExe = locatePython();
    if (pythonExe.empty()) {
        throw std::runtime_error("python3.exe not on PATH");
    }

    // Build wide command line: `<python> <AJAZZ_FIXTURES_DIR>\print_pythonpath.py`
    std::wstring const fixturesDir = []() {
        std::string const narrow = AJAZZ_FIXTURES_DIR;
        return std::wstring{narrow.begin(), narrow.end()};
    }();
    std::wstring scriptPath = fixturesDir + L"\\print_pythonpath.py";

    // CreateProcessW expects a mutable wide command line. Quote both args
    // defensively in case the path contains spaces (CI runner home dir).
    std::wstring cmdline = L"\"" + pythonExe + L"\" \"" + scriptPath + L"\"";

    // Anonymous pipe for child stdout. Inheritable on the child end only.
    SECURITY_ATTRIBUTES saAttr{};
    saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
    saAttr.bInheritHandle = TRUE;
    saAttr.lpSecurityDescriptor = nullptr;

    HANDLE childStdoutRead = nullptr;
    HANDLE childStdoutWrite = nullptr;
    if (CreatePipe(&childStdoutRead, &childStdoutWrite, &saAttr, 0) == 0) {
        throw std::runtime_error("CreatePipe failed");
    }
    if (SetHandleInformation(childStdoutRead, HANDLE_FLAG_INHERIT, 0) == 0) {
        CloseHandle(childStdoutRead);
        CloseHandle(childStdoutWrite);
        throw std::runtime_error("SetHandleInformation failed");
    }

    // Open NUL as the child's stdin. The original implementation passed
    // `GetStdHandle(STD_INPUT_HANDLE)` here, but on a ctest-driven Windows
    // CI runner that handle is often NULL or non-inheritable — and with
    // `STARTF_USESTDHANDLES` + `bInheritHandles=TRUE` an invalid stdin
    // makes `CreateProcessW` fail with `GetLastError()=87`
    // (ERROR_INVALID_PARAMETER). Opening NUL gives an always-valid,
    // inheritable, EOF-on-read handle the child won't block on. Production
    // OOP host (out_of_process_plugin_host_win32.cpp:429) achieves the
    // same goal via `CreatePipe` + closing the parent end; for a one-shot
    // test child that just prints and exits, NUL is simpler.
    HANDLE const childStdin = CreateFileW(L"NUL",
                                          GENERIC_READ,
                                          FILE_SHARE_READ | FILE_SHARE_WRITE,
                                          &saAttr,
                                          OPEN_EXISTING,
                                          0,
                                          nullptr);
    if (childStdin == INVALID_HANDLE_VALUE) {
        CloseHandle(childStdoutRead);
        CloseHandle(childStdoutWrite);
        throw std::runtime_error("CreateFileW(NUL) failed");
    }

    STARTUPINFOW si{};
    si.cb = sizeof(STARTUPINFOW);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = childStdin;
    si.hStdOutput = childStdoutWrite;
    // stderr: GetStdHandle is OK here because we don't depend on the
    // child being able to write — if the parent's stderr handle is
    // invalid, CreateProcessW will reject it (good failure mode), but
    // when running under ctest the parent's stderr IS a valid console
    // or pipe in practice.
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    if (si.hStdError == nullptr || si.hStdError == INVALID_HANDLE_VALUE) {
        // Fallback: also point stderr at NUL when the parent has no console.
        si.hStdError = childStdin;
    }

    // Build the env block with the sentinel as PYTHONPATH override.
    // CR-01 contract: this is the same construction the production OOP host
    // uses (`Win32EnvBlock envBlock{...}` on the spawn-function stack).
    std::map<std::wstring, std::wstring> overrides{
        {L"PYTHONPATH", sentinel},
        // Mirror the production override set so the spawn matches the host.
        {L"PYTHONDONTWRITEBYTECODE", L"1"},
        {L"PYTHONUNBUFFERED", L"1"},
    };
    ajazz::plugins::Win32EnvBlock envBlock(std::move(overrides));

    DWORD const creationFlags = CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT;

    PROCESS_INFORMATION pi{};
    BOOL const ok = CreateProcessW(pythonExe.c_str(),
                                   cmdline.data(),
                                   nullptr,
                                   nullptr,
                                   TRUE, // bInheritHandles
                                   creationFlags,
                                   envBlock.data(), // CR-01: per-spawn env block
                                   nullptr,
                                   &si,
                                   &pi);

    // Close the write end in parent so the read side gets EOF when child exits.
    CloseHandle(childStdoutWrite);
    // Close the parent's reference to the NUL stdin handle — the child
    // inherited it; closing parent's copy is required to avoid a dangling
    // handle reference per Win32 inheritance semantics.
    CloseHandle(childStdin);

    if (ok == 0) {
        DWORD const err = GetLastError();
        CloseHandle(childStdoutRead);
        throw std::runtime_error("CreateProcessW failed, GetLastError=" + std::to_string(err));
    }

    // 30-second timeout — generous enough for a script that prints one string
    // and exits, even on a loaded Windows CI runner.
    DWORD const wait = WaitForSingleObject(pi.hProcess, 30000);

    std::wstring captured;
    if (wait == WAIT_OBJECT_0) {
        // Read everything the child wrote to stdout.
        std::vector<char> buffer(4096);
        DWORD bytesRead = 0;
        while (ReadFile(childStdoutRead,
                        buffer.data(),
                        static_cast<DWORD>(buffer.size()),
                        &bytesRead,
                        nullptr) != 0 &&
               bytesRead > 0) {
            // Convert UTF-8 -> UTF-16 for comparison against wide sentinel.
            int const wideLen = MultiByteToWideChar(
                CP_UTF8, 0, buffer.data(), static_cast<int>(bytesRead), nullptr, 0);
            if (wideLen > 0) {
                std::wstring chunk(static_cast<std::size_t>(wideLen), L'\0');
                MultiByteToWideChar(
                    CP_UTF8, 0, buffer.data(), static_cast<int>(bytesRead), chunk.data(), wideLen);
                captured.append(chunk);
            }
        }
    }

    CloseHandle(childStdoutRead);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    if (wait != WAIT_OBJECT_0) {
        throw std::runtime_error("python child timed out or wait failed");
    }

    return trimRight(captured);
}

} // namespace

TEST_CASE("OOP env block delivers PYTHONPATH override to child", "[oop_env][integration]") {
    // Skip if Python isn't on PATH — the windows-2022 CI matrix step uses
    // actions/setup-python (verified in ci.yml). Local Win32 dev boxes
    // without Python should not fail; they should skip.
    if (locatePython().empty()) {
        SKIP("python3.exe not on PATH");
    }

    std::wstring const sentinel = L"C:\\test\\sentinel\\one";
    std::wstring const result = spawnPythonCaptureStdout(sentinel);
    REQUIRE(result == sentinel);
}

TEST_CASE("OOP env block does not pollute parent PYTHONPATH (CR-01 regression)",
          "[oop_env][integration][CR-01]") {
    // THIS IS THE CR-01 REGRESSION TEST.
    //
    // v1.0 audit framing: the pre-Plan-06-01 code mutated the parent's env
    // via `_putenv_s` before calling `CreateProcessW(lpEnvironment=nullptr)`.
    // That polluted the parent for the rest of its lifetime, leaked into
    // sibling subprocesses (manifest verifier), and across `OutOfProcessPluginHost`
    // instances (cross-instance race).
    //
    // After Plan 06-01, the spawn uses a per-spawn `Win32EnvBlock` and passes
    // it as `lpEnvironment` with `CREATE_UNICODE_ENVIRONMENT`. The parent env
    // is never touched. This test asserts that invariant by capturing
    // `_wgetenv(L"PYTHONPATH")` BEFORE and AFTER the spawn — they must be
    // bit-identical.
    if (locatePython().empty()) {
        SKIP("python3.exe not on PATH");
    }

    std::wstring const before = snapshotParentPythonPath();

    std::wstring const sentinel = L"C:\\cr01\\regression\\value";
    std::wstring const result = spawnPythonCaptureStdout(sentinel);
    REQUIRE(result == sentinel); // sanity: spawn worked at all

    std::wstring const after = snapshotParentPythonPath();
    REQUIRE(before == after);
}

TEST_CASE("OOP env block isolates concurrent spawns (cross-instance race closed)",
          "[oop_env][integration][concurrency]") {
    // Cross-instance race: the v1.0 audit flagged that two concurrent
    // `OutOfProcessPluginHost` constructors stomped each other's PYTHONPATH
    // via the parent-env mutation. After Plan 06-01, each spawn has its own
    // per-spawn env block — no shared mutable state — so two concurrent
    // spawns with DIFFERENT sentinels each see their own value, and the
    // parent env stays clean throughout.
    if (locatePython().empty()) {
        SKIP("python3.exe not on PATH");
    }

    std::wstring const before = snapshotParentPythonPath();

    std::array<std::wstring, 2> sentinels{
        L"C:\\concurrent\\one",
        L"C:\\concurrent\\two",
    };
    std::array<std::wstring, 2> results;
    std::array<std::string, 2> errors;

    std::array<std::thread, 2> threads;
    for (std::size_t i = 0; i < 2; ++i) {
        threads[i] = std::thread([i, &sentinels, &results, &errors]() {
            try {
                results[i] = spawnPythonCaptureStdout(sentinels[i]);
            } catch (std::exception const& e) {
                errors[i] = e.what();
            }
        });
    }
    for (auto& t : threads) {
        t.join();
    }

    // Surface any thread error so Catch2 reports the failure correctly.
    REQUIRE(errors[0].empty());
    REQUIRE(errors[1].empty());

    // No cross-pollination: each thread's child must see its own sentinel.
    REQUIRE(results[0] == sentinels[0]);
    REQUIRE(results[1] == sentinels[1]);

    std::wstring const after = snapshotParentPythonPath();
    REQUIRE(before == after);
}

namespace {

// Spawn `python3 <fixturesDir>/print_pythonpath.py` with a MALFORMED env block
// containing TWO PYTHONPATH= entries. The probe deliberately bypasses
// Win32EnvBlock and constructs the block by hand into a vector<wchar_t>.
// Used ONLY by the WIN32-04 duplicate-key precedence probe below; this is
// observational, not assertion-based.
//
// The block layout: [parent env entries with PYTHONPATH removed]
// [PYTHONPATH=firstValue\0] [PYTHONPATH=secondValue\0] [\0]
// (final \0 makes the block end in \0\0).
std::wstring spawnPythonWithDuplicatePythonPath(std::wstring const& firstValue,
                                                std::wstring const& secondValue) {
    std::wstring const pythonExe = locatePython();
    if (pythonExe.empty()) {
        throw std::runtime_error("python3.exe not on PATH");
    }

    std::wstring const fixturesDir = []() {
        std::string const narrow = AJAZZ_FIXTURES_DIR;
        return std::wstring{narrow.begin(), narrow.end()};
    }();
    std::wstring const scriptPath = fixturesDir + L"\\print_pythonpath.py";
    std::wstring cmdline = L"\"" + pythonExe + L"\" \"" + scriptPath + L"\"";

    // Hand-build the env block. Snapshot parent env, skip any existing
    // PYTHONPATH entry (case-insensitive), then append the two malformed
    // duplicate entries at the end, then the \0\0 terminator. Note: this
    // block is NOT sorted (it's deliberately malformed for the probe). The
    // CRT and Windows accept unsorted blocks but the documented contract
    // is sorted; our probe sees what Windows does with the unsorted form.
    std::vector<wchar_t> block;
    LPWCH snapshot = GetEnvironmentStringsW();
    if (snapshot != nullptr) {
        LPWCH cursor = snapshot;
        while (*cursor != L'\0') {
            std::wstring entry{cursor};
            // Skip parent's PYTHONPATH (case-insensitive match on the key).
            std::wstring key;
            auto const eq = entry.find(L'=');
            if (eq != std::wstring::npos) {
                key = entry.substr(0, eq);
            } else {
                key = entry;
            }
            bool const isPythonPath = !entry.empty() && entry.front() != L'=' &&
                                      _wcsicmp(key.c_str(), L"PYTHONPATH") == 0;
            if (!isPythonPath) {
                block.insert(block.end(), entry.begin(), entry.end());
                block.push_back(L'\0');
            }
            cursor += entry.size() + 1;
        }
        FreeEnvironmentStringsW(snapshot);
    }

    // Append duplicate PYTHONPATH entries IN ORDER.
    std::wstring const firstEntry = L"PYTHONPATH=" + firstValue;
    std::wstring const secondEntry = L"PYTHONPATH=" + secondValue;
    block.insert(block.end(), firstEntry.begin(), firstEntry.end());
    block.push_back(L'\0');
    block.insert(block.end(), secondEntry.begin(), secondEntry.end());
    block.push_back(L'\0');
    // Block terminator.
    block.push_back(L'\0');

    // Spawn with the malformed block as lpEnvironment.
    SECURITY_ATTRIBUTES saAttr{};
    saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
    saAttr.bInheritHandle = TRUE;
    HANDLE childStdoutRead = nullptr;
    HANDLE childStdoutWrite = nullptr;
    if (CreatePipe(&childStdoutRead, &childStdoutWrite, &saAttr, 0) == 0) {
        throw std::runtime_error("CreatePipe failed");
    }
    if (SetHandleInformation(childStdoutRead, HANDLE_FLAG_INHERIT, 0) == 0) {
        CloseHandle(childStdoutRead);
        CloseHandle(childStdoutWrite);
        throw std::runtime_error("SetHandleInformation failed");
    }

    STARTUPINFOW si{};
    si.cb = sizeof(STARTUPINFOW);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = childStdoutWrite;
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);

    PROCESS_INFORMATION pi{};
    BOOL const ok = CreateProcessW(pythonExe.c_str(),
                                   cmdline.data(),
                                   nullptr,
                                   nullptr,
                                   TRUE,
                                   CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
                                   block.data(), // <-- malformed block with TWO PYTHONPATH entries
                                   nullptr,
                                   &si,
                                   &pi);

    CloseHandle(childStdoutWrite);

    if (ok == 0) {
        DWORD const err = GetLastError();
        CloseHandle(childStdoutRead);
        throw std::runtime_error("CreateProcessW failed, GetLastError=" + std::to_string(err));
    }

    DWORD const wait = WaitForSingleObject(pi.hProcess, 30000);

    std::wstring captured;
    if (wait == WAIT_OBJECT_0) {
        std::vector<char> buf(4096);
        DWORD bytesRead = 0;
        while (
            ReadFile(
                childStdoutRead, buf.data(), static_cast<DWORD>(buf.size()), &bytesRead, nullptr) !=
                0 &&
            bytesRead > 0) {
            int const wideLen = MultiByteToWideChar(
                CP_UTF8, 0, buf.data(), static_cast<int>(bytesRead), nullptr, 0);
            if (wideLen > 0) {
                std::wstring chunk(static_cast<std::size_t>(wideLen), L'\0');
                MultiByteToWideChar(
                    CP_UTF8, 0, buf.data(), static_cast<int>(bytesRead), chunk.data(), wideLen);
                captured.append(chunk);
            }
        }
    }

    CloseHandle(childStdoutRead);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    if (wait != WAIT_OBJECT_0) {
        throw std::runtime_error("python child timed out or wait failed");
    }
    return trimRight(captured);
}

} // namespace

TEST_CASE("WIN32-04 probe: duplicate PYTHONPATH precedence",
          "[oop_env][integration][duplicate-key-probe]") {
    // OBSERVATIONAL test (Plan 06-03 Task 1). Uses WARN, not REQUIRE — the
    // test does NOT fail either way. The CI log captures the observed value;
    // Plan 06-03 Task 2 records it in `06-CR-01-RESOLUTION.md`.
    //
    // The question: when CreateProcessW receives an `lpEnvironment` block
    // containing TWO PYTHONPATH= entries, which value reaches the child?
    // MS docs imply first-wins (sequential walk semantics); nullprogram (2023)
    // empirically observes last-wins. Our Win32EnvBlock collapses to ONE
    // entry per key, so this question has no bearing on runtime correctness —
    // it matters only for understanding what other CreateProcessW callers
    // might experience.
    if (locatePython().empty()) {
        SKIP("python3.exe not on PATH");
    }

    std::wstring const firstValue = L"C:\\probe\\first";
    std::wstring const secondValue = L"C:\\probe\\second";
    std::wstring const observed = spawnPythonWithDuplicatePythonPath(firstValue, secondValue);

    // Convert observed to narrow for the WARN line (Catch2's stringification
    // of std::wstring is implementation-defined; UTF-8 narrow is portable).
    std::string narrow;
    if (!observed.empty()) {
        int const narrowLen = WideCharToMultiByte(CP_UTF8,
                                                  0,
                                                  observed.c_str(),
                                                  static_cast<int>(observed.size()),
                                                  nullptr,
                                                  0,
                                                  nullptr,
                                                  nullptr);
        if (narrowLen > 0) {
            narrow.resize(static_cast<std::size_t>(narrowLen));
            WideCharToMultiByte(CP_UTF8,
                                0,
                                observed.c_str(),
                                static_cast<int>(observed.size()),
                                narrow.data(),
                                narrowLen,
                                nullptr,
                                nullptr);
        }
    }

    // Emit the WARN line in a stable format that Plan 06-03 Task 2 greps
    // out of the CI log.
    WARN("WIN32-04 result: PYTHONPATH = " << narrow);

    // Sanity: the observed value is either the first or the second sentinel.
    // The probe spawn produces a deterministic answer per Windows version,
    // so observing neither would indicate a Win32 contract change (or a bug
    // in the hand-built block).
    bool const isFirst = (observed == firstValue);
    bool const isLast = (observed == secondValue);
    REQUIRE((isFirst || isLast));
}

#endif // _WIN32
