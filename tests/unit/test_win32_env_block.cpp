// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_win32_env_block.cpp
 * @brief Unit tests for @ref ajazz::plugins::Win32EnvBlock — the per-spawn
 *        UTF-16 env block helper that replaced the parent-env-mutating
 *        `_putenv_s` pattern in CR-01 (Phase 06, Plan 06-01).
 *
 * Black-box verification of the produced wide env block. Each test asserts one
 * of the four Pitfall 5 sub-traps (`\0\0` terminator, sort order, `=`-prefix
 * preservation, override-on-collision) plus the D-04 case-insensitive override
 * semantics. No spawn — the integration coverage of an actual `CreateProcessW`
 * call lives in `tests/integration/test_oop_plugin_host_win32_env.cpp`.
 *
 * Win32-only via `#ifdef _WIN32`; the file compiles to nothing on POSIX.
 */
#ifdef _WIN32

#include "win32_env_block.hpp"

#include <algorithm>
#include <map>
#include <string>
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

// Walk a Win32EnvBlock buffer and return each KEY=VALUE entry as a wstring.
// The block layout is `KEY=VALUE\0KEY=VALUE\0...\0\0`, so a `\0` cursor that
// hits an empty entry signals end-of-block.
std::vector<std::wstring> walkBlock(ajazz::plugins::Win32EnvBlock& block) {
    auto const* cursor = static_cast<wchar_t const*>(block.data());
    std::vector<std::wstring> entries;
    while (*cursor != L'\0') {
        std::wstring entry{cursor};
        entries.push_back(entry);
        cursor += entry.size() + 1;
    }
    return entries;
}

// Extract the key portion (substring up to the first `=`) of a `KEY=VALUE` entry.
// `=`-prefixed drive-letter entries (`=C:=C:\foo`) return the leading `=` plus the
// "drive letter" prefix — this preserves the natural prefix ordering of those
// special entries.
std::wstring keyOf(std::wstring const& entry) {
    auto const eq = entry.find(L'=');
    if (eq == std::wstring::npos) {
        return entry;
    }
    // For `=C:=...`, the key is `=C:`. For ordinary `FOO=bar`, the key is `FOO`.
    if (!entry.empty() && entry.front() == L'=') {
        // Find the SECOND `=` for the special drive-letter entries.
        auto const eq2 = entry.find(L'=', 1);
        return entry.substr(0, eq2 == std::wstring::npos ? entry.size() : eq2);
    }
    return entry.substr(0, eq);
}

} // namespace

TEST_CASE("Win32EnvBlock ends in double-null terminator", "[win32_env_block]") {
    // Pitfall 5 sub-trap 1: the block MUST end in two consecutive wide nulls.
    // Windows reads the env block until it hits `\0\0`; if we emit only one
    // trailing `\0`, the OS reads past the buffer and the child sees garbage
    // (or, more likely, AccessViolation on the kernel side).
    std::map<std::wstring, std::wstring> overrides{{L"FOO", L"bar"}};
    ajazz::plugins::Win32EnvBlock block(std::move(overrides));

    auto const sizeBytes = block.size_bytes();
    REQUIRE(sizeBytes >= 2 * sizeof(wchar_t));

    auto const* buf = static_cast<wchar_t const*>(block.data());
    auto const totalChars = sizeBytes / sizeof(wchar_t);

    REQUIRE(buf[totalChars - 1] == L'\0');
    REQUIRE(buf[totalChars - 2] == L'\0');
}

TEST_CASE("Win32EnvBlock entries are sorted case-insensitively", "[win32_env_block]") {
    // Pitfall 5 sub-trap 2: Windows requires sorted env blocks. Sort key is
    // the substring up to the first `=`, comparison is case-insensitive
    // (`_wcsicmp`). Wrong sort = silent env corruption in the child.
    std::map<std::wstring, std::wstring> overrides{
        {L"ZED", L"1"},
        {L"alpha", L"2"},
        {L"MIDDLE", L"3"},
    };
    ajazz::plugins::Win32EnvBlock block(std::move(overrides));

    auto const entries = walkBlock(block);

    // Skip leading `=`-prefixed entries (they preserve front position
    // regardless of sort).
    std::vector<std::wstring> sortable;
    for (auto const& e : entries) {
        if (e.empty() || e.front() != L'=') {
            sortable.push_back(e);
        }
    }

    // Within the sortable group, our three injected keys (alpha/MIDDLE/ZED)
    // must appear in `_wcsicmp` order: alpha < MIDDLE < ZED. There may be
    // other parent-env entries interleaved, so we check relative order only.
    auto const findKey = [&](std::wstring const& wantKey) -> std::ptrdiff_t {
        for (std::size_t i = 0; i < sortable.size(); ++i) {
            if (_wcsicmp(keyOf(sortable[i]).c_str(), wantKey.c_str()) == 0) {
                return static_cast<std::ptrdiff_t>(i);
            }
        }
        return -1;
    };

    auto const posAlpha = findKey(L"alpha");
    auto const posMiddle = findKey(L"MIDDLE");
    auto const posZed = findKey(L"ZED");

    REQUIRE(posAlpha >= 0);
    REQUIRE(posMiddle >= 0);
    REQUIRE(posZed >= 0);
    REQUIRE(posAlpha < posMiddle);
    REQUIRE(posMiddle < posZed);

    // Stronger invariant: full sortable list must be case-insensitively
    // monotonically non-decreasing by key.
    for (std::size_t i = 1; i < sortable.size(); ++i) {
        REQUIRE(_wcsicmp(keyOf(sortable[i - 1]).c_str(), keyOf(sortable[i]).c_str()) <= 0);
    }
}

TEST_CASE("Win32EnvBlock preserves =-prefixed drive-letter entries at front", "[win32_env_block]") {
    // Pitfall 5 sub-trap 3: drive-letter-current-dir entries (e.g. `=Z:=Z:\test`)
    // are returned by GetEnvironmentStringsW and MUST appear at the FRONT of the
    // produced block, verbatim. They are NOT subject to sort or override merging.
    //
    // `SetEnvironmentVariableW(L"=Z:", ...)` is the canonical way to inject one
    // of these entries, but its behavior across Windows versions is inconsistent:
    // on the windows-2022 CI runner the call silently succeeds while
    // GetEnvironmentStringsW does NOT then expose the `=Z:` entry. Detect that
    // case via a precondition probe and SKIP the test rather than asserting
    // against an empty snapshot — the underlying block-construction logic
    // (lines 61-63 of win32_env_block.cpp) is what's under test, and we cannot
    // exercise it without an actual `=`-prefixed entry in the parent env.
    SetEnvironmentVariableW(L"=Z:", L"Z:\\test");

    bool injectionTook = false;
    if (LPWCH probe = GetEnvironmentStringsW(); probe != nullptr) {
        for (wchar_t const* cursor = probe; *cursor != L'\0';) {
            std::wstring entry{cursor};
            if (entry.rfind(L"=Z:", 0) == 0) {
                injectionTook = true;
                break;
            }
            cursor += entry.size() + 1;
        }
        FreeEnvironmentStringsW(probe);
    }
    if (!injectionTook) {
        SetEnvironmentVariableW(L"=Z:", nullptr);
        SKIP("SetEnvironmentVariableW silently refuses =-prefixed keys on this "
             "Windows version; cannot inject a synthetic drive-letter entry.");
    }

    std::map<std::wstring, std::wstring> overrides{{L"FOO", L"bar"}};
    ajazz::plugins::Win32EnvBlock block(std::move(overrides));

    auto const entries = walkBlock(block);

    // Diagnostic dump: list all `=`-prefixed entries the BLOCK contains and
    // how many entries are in the BLOCK total. If the probe loop above
    // observed `=Z:` in the snapshot but the constructor missed it, this
    // INFO() (printed only on REQUIRE failure) tells us whether the
    // Win32EnvBlock constructor saw a DIFFERENT snapshot or whether the
    // `=`-prefix branch is broken.
    int blockEqCount = 0;
    std::wstring blockEqDump;
    for (auto const& e : entries) {
        if (!e.empty() && e.front() == L'=') {
            blockEqCount += 1;
            if (blockEqDump.size() < 200) {
                blockEqDump += e.substr(0, 30) + L" | ";
            }
        }
    }
    INFO("Win32EnvBlock walked entries: " << entries.size());
    INFO("Win32EnvBlock `=`-prefixed entries: " << blockEqCount);

    // Find the `=Z:` entry and the `FOO` entry. The `=Z:` entry MUST appear
    // before the `FOO` entry.
    std::ptrdiff_t posDriveLetter = -1;
    std::ptrdiff_t posFoo = -1;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        if (entries[i].rfind(L"=Z:", 0) == 0) {
            posDriveLetter = static_cast<std::ptrdiff_t>(i);
        }
        if (entries[i].rfind(L"FOO=", 0) == 0) {
            posFoo = static_cast<std::ptrdiff_t>(i);
        }
    }

    // Re-probe AFTER block construction to confirm the parent env still
    // contains `=Z:` (catches any race condition / kernel-side weirdness
    // between the precondition probe and the constructor).
    bool stillThere = false;
    if (LPWCH probe2 = GetEnvironmentStringsW(); probe2 != nullptr) {
        for (wchar_t const* cursor = probe2; *cursor != L'\0';) {
            std::wstring entry{cursor};
            if (entry.rfind(L"=Z:", 0) == 0) {
                stillThere = true;
                break;
            }
            cursor += entry.size() + 1;
        }
        FreeEnvironmentStringsW(probe2);
    }
    INFO("=Z: present in re-probe after construction: " << (stillThere ? "yes" : "no"));

    SetEnvironmentVariableW(L"=Z:", nullptr);

    REQUIRE(posDriveLetter >= 0);
    REQUIRE(posFoo >= 0);
    REQUIRE(posDriveLetter < posFoo);
}

TEST_CASE("Win32EnvBlock override replaces existing parent value (case-insensitive key)",
          "[win32_env_block]") {
    // D-04: case-insensitive key matching for override merging. The override's
    // key casing wins. A parent entry of `PythonPath=C:\parent` plus an override
    // of `PYTHONPATH=C:\override` produces ONE entry: `PYTHONPATH=C:\override`.
    SetEnvironmentVariableW(L"PythonPath", L"C:\\parent");

    std::map<std::wstring, std::wstring> overrides{{L"PYTHONPATH", L"C:\\override"}};
    ajazz::plugins::Win32EnvBlock block(std::move(overrides));

    auto const entries = walkBlock(block);

    int matchCount = 0;
    std::wstring observed;
    for (auto const& e : entries) {
        std::wstring const k = keyOf(e);
        if (_wcsicmp(k.c_str(), L"PYTHONPATH") == 0) {
            matchCount += 1;
            observed = e;
        }
    }

    SetEnvironmentVariableW(L"PythonPath", nullptr);

    REQUIRE(matchCount == 1);
    REQUIRE(observed == L"PYTHONPATH=C:\\override");
}

TEST_CASE("Win32EnvBlock new override key appends in sort order", "[win32_env_block]") {
    // D-04: an override whose key does NOT exist in the parent env appends
    // and participates in the case-insensitive sort. Verify that a synthetic
    // key chosen to sort early lands in sort order, not at the end.
    std::map<std::wstring, std::wstring> overrides{{L"NEWKEY_AAAA", L"v"}};
    ajazz::plugins::Win32EnvBlock block(std::move(overrides));

    auto const entries = walkBlock(block);

    std::ptrdiff_t newKeyPos = -1;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        if (_wcsicmp(keyOf(entries[i]).c_str(), L"NEWKEY_AAAA") == 0) {
            newKeyPos = static_cast<std::ptrdiff_t>(i);
            break;
        }
    }
    REQUIRE(newKeyPos >= 0);

    // The entry immediately before NEWKEY_AAAA (skipping any leading
    // `=`-prefixed entries) must sort case-insensitively <= NEWKEY_AAAA.
    // The entry immediately after must sort >= NEWKEY_AAAA.
    if (newKeyPos > 0) {
        // Find previous non-`=`-prefixed entry.
        for (std::ptrdiff_t i = newKeyPos - 1; i >= 0; --i) {
            if (entries[static_cast<std::size_t>(i)].empty() ||
                entries[static_cast<std::size_t>(i)].front() != L'=') {
                REQUIRE(_wcsicmp(keyOf(entries[static_cast<std::size_t>(i)]).c_str(),
                                 L"NEWKEY_AAAA") <= 0);
                break;
            }
        }
    }
    if (newKeyPos + 1 < static_cast<std::ptrdiff_t>(entries.size())) {
        REQUIRE(_wcsicmp(keyOf(entries[static_cast<std::size_t>(newKeyPos + 1)]).c_str(),
                         L"NEWKEY_AAAA") >= 0);
    }
}

#endif // _WIN32
