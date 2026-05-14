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
#include <cwchar> // _wcsicmp
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

            cursor += entry.size() + 1; // advance past the `\0`
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

    // Pitfall 5 sub-trap 2: sort case-insensitively. Windows requires sorted
    // env blocks; incorrect sort order = silent env corruption in the child.
    // `=`-prefixed front entries do NOT participate in sort.
    std::sort(mergedEntries.begin(), mergedEntries.end(), [](auto const& a, auto const& b) {
        return compareKeyCI(a, b) < 0;
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
