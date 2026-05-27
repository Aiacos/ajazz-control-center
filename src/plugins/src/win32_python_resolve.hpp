// SPDX-License-Identifier: GPL-3.0-or-later
/** @file win32_python_resolve.hpp
 *  @brief Resolve a real Python interpreter on Windows, skipping the
 *         Microsoft Store "App Execution Alias" stub.
 *
 *  On a default Windows box the python.org installer ships `python.exe` but
 *  NOT `python3.exe`; the only `python3.exe` present is the Microsoft Store
 *  App Execution Alias — a reparse-point stub under
 *  `%LOCALAPPDATA%\\Microsoft\\WindowsApps` that prints "Python was not found"
 *  and exits 9009 when no Store Python is installed. Passing the bare name
 *  "python3" to `_wspawnvp` / `CreateProcessW` therefore launches that stub and
 *  the child produces no output (the failure mode behind the manifest-signer
 *  and OOP-env-block test failures on bare-Windows dev boxes).
 *
 *  This helper resolves a concrete interpreter by preferring any candidate that
 *  is NOT under `\\WindowsApps\\`. It is header-only and `_WIN32`-guarded so the
 *  POSIX backends never see it.
 */
#pragma once
#ifdef _WIN32

#include <cwctype>
#include <string>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace ajazz::plugins::win32 {

/// True if @p path lives under `...\\WindowsApps\\...` (the Store-alias dir).
[[nodiscard]] inline bool isWindowsAppsStub(std::wstring const& path) {
    std::wstring lower;
    lower.reserve(path.size());
    for (wchar_t const c : path) {
        lower.push_back(static_cast<wchar_t>(std::towlower(c)));
    }
    return lower.find(L"\\windowsapps\\") != std::wstring::npos;
}

/// True if @p name is a bare Python interpreter command — `python`,
/// `python3`, `pythonX`, or `pythonX.Y` (case-insensitive, optional trailing
/// `.exe`), with NO path separator. Only such a name may be upgraded to a
/// concrete `python.exe` / `python3.exe`; an arbitrary configured program name
/// must be returned unchanged so an unresolvable interpreter fails closed
/// (CWE-426) instead of silently falling back to the system Python — the
/// security contract pinned by `tests/unit/test_manifest_signer.cpp`.
[[nodiscard]] inline bool isPythonCommandName(std::wstring const& name) {
    if (name.empty() || name.find_first_of(L"\\/:") != std::wstring::npos) {
        return false; // empty, or a path rather than a bare command name
    }
    std::wstring lower;
    lower.reserve(name.size());
    for (wchar_t const c : name) {
        lower.push_back(static_cast<wchar_t>(std::towlower(c)));
    }
    if (lower.size() > 4 && lower.compare(lower.size() - 4, 4, L".exe") == 0) {
        lower.erase(lower.size() - 4); // drop an optional trailing ".exe"
    }
    constexpr std::size_t kStemLen = 6; // length of "python"
    if (lower.compare(0, kStemLen, L"python") != 0) {
        return false;
    }
    // Any suffix after "python" must look like a version (digits and dots):
    // "python3", "python3.11" — never "python-evil" or "pythonista".
    for (std::size_t i = kStemLen; i < lower.size(); ++i) {
        wchar_t const c = lower[i];
        if ((c < L'0' || c > L'9') && c != L'.') {
            return false;
        }
    }
    return true;
}

/// Resolve a Python interpreter to a concrete path, skipping the Store stub.
///
/// If @p preferred already names an existing file it is returned unchanged.
/// Otherwise, ONLY when @p preferred is a bare Python command name (see
/// @ref isPythonCommandName), candidates {preferred(.exe), python.exe,
/// python3.exe} are looked up on PATH via `SearchPathW`; the first hit NOT
/// under `\\WindowsApps\\` wins. If only a WindowsApps hit exists (a real
/// Store-installed Python, or the not-installed stub) it is returned as a
/// best-effort fallback. An arbitrary non-Python name that does not resolve to
/// a concrete file is returned unchanged — it is NEVER upgraded to the system
/// Python, so a misconfigured/unresolvable interpreter fails closed (CWE-426)
/// rather than running an unintended program with a forged-valid verdict.
[[nodiscard]] inline std::wstring resolveRealPythonW(std::wstring const& preferred) {
    if (!preferred.empty()) {
        DWORD const attrs = ::GetFileAttributesW(preferred.c_str());
        if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0) {
            return preferred; // caller passed a concrete interpreter path
        }
    }
    if (!isPythonCommandName(preferred)) {
        return preferred; // do not substitute an arbitrary name (fail closed)
    }
    auto search = [](std::wstring name) -> std::wstring {
        if (name.empty()) {
            return {};
        }
        if (name.find(L".exe") == std::wstring::npos) {
            name += L".exe";
        }
        wchar_t buf[MAX_PATH] = {0};
        DWORD const got = ::SearchPathW(nullptr, name.c_str(), nullptr, MAX_PATH, buf, nullptr);
        return (got > 0 && got < MAX_PATH) ? std::wstring{buf} : std::wstring{};
    };
    std::wstring fallback;
    for (std::wstring const& name :
         {preferred, std::wstring{L"python.exe"}, std::wstring{L"python3.exe"}}) {
        std::wstring const hit = search(name);
        if (hit.empty()) {
            continue;
        }
        if (isWindowsAppsStub(hit)) {
            if (fallback.empty()) {
                fallback = hit;
            }
            continue;
        }
        return hit;
    }
    return fallback.empty() ? preferred : fallback;
}

/// UTF-8 convenience wrapper around @ref resolveRealPythonW. Returns
/// @p preferredUtf8 unchanged if resolution or conversion fails.
[[nodiscard]] inline std::string resolveRealPython(std::string const& preferredUtf8) {
    auto toWide = [](std::string const& s) -> std::wstring {
        if (s.empty()) {
            return {};
        }
        int const n =
            ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
        if (n <= 0) {
            return {};
        }
        std::wstring w(static_cast<std::size_t>(n), L'\0');
        ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
        return w;
    };
    auto toUtf8 = [](std::wstring const& w) -> std::string {
        if (w.empty()) {
            return {};
        }
        int const n = ::WideCharToMultiByte(
            CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
        if (n <= 0) {
            return {};
        }
        std::string s(static_cast<std::size_t>(n), '\0');
        ::WideCharToMultiByte(
            CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
        return s;
    };
    std::string const out = toUtf8(resolveRealPythonW(toWide(preferredUtf8)));
    return out.empty() ? preferredUtf8 : out;
}

} // namespace ajazz::plugins::win32

#endif // _WIN32
