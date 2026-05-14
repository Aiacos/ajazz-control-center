// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file win32_env_block.cpp
 * @brief Implementation of @ref Win32EnvBlock — per-spawn wide env block.
 *
 * See `win32_env_block.hpp` for the lifetime contract and CR-01 rationale.
 * All implementation rules below come from PITFALLS.md Pitfall 5's four
 * sub-traps + Phase 06 CONTEXT D-04 case-insensitive override semantics.
 */
#ifdef _WIN32

#include "win32_env_block.hpp"

#include <algorithm>
#include <cstddef>     // size_t
#include <cwchar>      // _wcsicmp, _wcsnicmp
#include <string_view> // std::wstring_view (sort comparator)
#include <utility>

namespace ajazz::plugins {

namespace {

// Split a `KEY=VALUE` wide entry into key (up-to first `=`) and value.
// Returns the key substring; `outValue` receives the substring AFTER the `=`.
// If the entry contains no `=` (malformed), returns the full string as key
// and empty value — this should never happen on a well-formed env block.
std::wstring splitKey(std::wstring const& entry, std::wstring& outValue) {
    auto const eq = entry.find(L'=');
    if (eq == std::wstring::npos) {
        outValue.clear();
        return entry;
    }
    outValue = entry.substr(eq + 1);
    return entry.substr(0, eq);
}

// Case-insensitive key comparison via `_wcsicmp` on the substring up to the
// first `=`. Used both for override matching (Pitfall 5 sub-trap implicit:
// Windows env keys are case-insensitive in lookup) and for final sort order.
int compareKeyCI(std::wstring const& a, std::wstring const& b) {
    return _wcsicmp(a.c_str(), b.c_str());
}

} // namespace

Win32EnvBlock::Win32EnvBlock(std::map<std::wstring, std::wstring> overrides) {
    // Step 1: snapshot the parent env.
    // `GetEnvironmentStringsW` returns `\0`-terminated entries, list ends `\0\0`.
    LPWCH snapshot = GetEnvironmentStringsW();

    // Pitfall 5 sub-trap 3: preserve `=`-prefixed drive-letter entries verbatim
    // at the FRONT. They MUST NOT participate in override merging.
    std::vector<std::wstring> frontEntries;
    // All other parent entries, after override merge + sort.
    std::vector<std::wstring> mergedEntries;

    if (snapshot != nullptr) {
        LPWCH cursor = snapshot;
        while (*cursor != L'\0') {
            std::wstring entry{cursor};

            // CRITICAL: snapshot the cursor advance BEFORE moving `entry`.
            // Both branches below `std::move(entry)` into one of the vectors;
            // reading `entry.size()` after the move is implementation-defined
            // (a moved-from std::wstring is in a "valid but unspecified state").
            // On MSVC's STL the moved-from string is cleared (size() == 0) for
            // heap-allocated strings (i.e. anything longer than the 7-wchar SSO
            // threshold) — that includes `PATH=...`, `SYSTEMROOT=...`, and most
            // real env entries. Cursor would advance by 1 instead of size+1,
            // silently dropping every subsequent entry past the first long one.
            // This was the root cause of windows-2022 CI failures 167 (=Z: not
            // in produced block) and 181/182/183 (CreateProcessW=87 because
            // the env block we hand the child is missing SYSTEMROOT/PATH).
            // Confirmed via diagnostic instrumentation in commit aeecc87:
            //   envBlock entries=93, hasSYSTEMROOT=NO, hasPATH=NO
            // libstdc++ and libc++ happen to leave moved-from size intact for
            // SSO strings, masking the bug on Linux + macOS.
            size_t const advance = entry.size() + 1;

            if (!entry.empty() && entry.front() == L'=') {
                // `=C:=C:\foo` style. Goes to the front verbatim.
                frontEntries.push_back(std::move(entry));
            } else {
                std::wstring parentValue;
                std::wstring const parentKey = splitKey(entry, parentValue);

                // Look up via case-insensitive key match (D-04).
                auto match =
                    std::find_if(overrides.begin(), overrides.end(), [&parentKey](auto const& kv) {
                        return compareKeyCI(kv.first, parentKey) == 0;
                    });

                if (match != overrides.end()) {
                    // Override replaces parent. Use OVERRIDE's key casing
                    // (D-04 stipulates the override casing wins).
                    std::wstring replacement = match->first;
                    replacement.push_back(L'=');
                    replacement.append(match->second);
                    mergedEntries.push_back(std::move(replacement));
                    overrides.erase(match);
                } else {
                    // No override for this key — keep the parent entry verbatim.
                    mergedEntries.push_back(std::move(entry));
                }
            }

            cursor += advance; // advance past the `\0`
        }

        // Pitfall 5 sub-trap "free the snapshot via FreeEnvironmentStringsW":
        // NEVER `delete[]`, NEVER `LocalFree`. Free eagerly — we've copied
        // everything we need.
        FreeEnvironmentStringsW(snapshot);
    }

    // Append any remaining overrides (brand-new keys not in parent env).
    for (auto& [key, value] : overrides) {
        std::wstring entry = key;
        entry.push_back(L'=');
        entry.append(value);
        mergedEntries.push_back(std::move(entry));
    }

    // Pitfall 5 sub-trap 2: sort case-insensitively BY KEY (the substring up
    // to the first `=`). Windows requires env blocks to be sorted by variable
    // name, NOT by the full KEY=VALUE entry — see
    // https://learn.microsoft.com/en-us/windows/win32/procthread/changing-environment-variables
    // ("The order of the variables in the environment is alphabetical, by
    // name. Sort order is not case-sensitive.")
    //
    // The previous impl compared full entries via `_wcsicmp(a, b)`. That
    // happens to match key-sort order in most cases — the `=` separator
    // (0x3D) collates before letter chars, so differing keys collate before
    // any value comparison kicks in — but it diverges when two keys collate
    // equal case-insensitively (different casing, e.g. `Path=...` vs
    // `PATH=...`) and differ by value: the impl sorted by value casing,
    // which Windows considers an unsorted block. After the UB cursor-advance
    // fix in 62e786b filled the block with the full parent env, test 166
    // surfaced this latent sort-by-name regression on the real CI runner.
    auto const keyView = [](std::wstring const& entry) -> std::wstring_view {
        auto const eq = entry.find(L'=');
        return std::wstring_view{entry.data(), eq == std::wstring::npos ? entry.size() : eq};
    };
    std::sort(mergedEntries.begin(), mergedEntries.end(), [&keyView](auto const& a, auto const& b) {
        auto const ka = keyView(a);
        auto const kb = keyView(b);
        // _wcsnicmp compares up to min(|ka|,|kb|) wchars. If the
        // common prefix is equal, the shorter key sorts first —
        // which matches Win32's name-based ordering.
        size_t const n = std::min(ka.size(), kb.size());
        int const cmp = _wcsnicmp(ka.data(), kb.data(), n);
        if (cmp != 0) {
            return cmp < 0;
        }
        return ka.size() < kb.size();
    });

    // Step 6: serialize into m_block. Layout:
    //   [front entries: KEY=VALUE\0]
    //   [sorted entries: KEY=VALUE\0]
    //   final \0  (so the block ends in \0\0)
    SIZE_T totalChars = 0;
    for (auto const& e : frontEntries) {
        totalChars += e.size() + 1;
    }
    for (auto const& e : mergedEntries) {
        totalChars += e.size() + 1;
    }
    totalChars += 1; // final terminating wchar_t for the \0\0 block terminator

    m_block.reserve(totalChars);
    for (auto const& e : frontEntries) {
        m_block.insert(m_block.end(), e.begin(), e.end());
        m_block.push_back(L'\0');
    }
    for (auto const& e : mergedEntries) {
        m_block.insert(m_block.end(), e.begin(), e.end());
        m_block.push_back(L'\0');
    }
    m_block.push_back(L'\0'); // Pitfall 5 sub-trap 1: \0\0 block terminator

    // Defensive check: m_block.size() >= 2 (at minimum two \0 even if both
    // entry vectors are empty — the loops emit nothing, then we push the
    // final \0; that's only ONE \0. Special-case the empty-env path so the
    // block terminator invariant always holds.
    if (m_block.size() == 1) {
        m_block.push_back(L'\0');
    }
}

LPVOID Win32EnvBlock::data() noexcept {
    return static_cast<LPVOID>(m_block.data());
}

SIZE_T Win32EnvBlock::size_bytes() const noexcept {
    return m_block.size() * sizeof(wchar_t);
}

} // namespace ajazz::plugins

#endif // _WIN32
