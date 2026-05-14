// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file win32_env_block.hpp
 * @brief Per-spawn UTF-16 environment block for `CreateProcessW(lpEnvironment=...)`.
 *
 * CR-01 fix (Phase 06): replaces the previous belt-and-braces pattern of
 * mutating the parent process's environment with `_putenv_s` before calling
 * `CreateProcessW(lpEnvironment=nullptr)`. That pattern polluted the parent
 * env for the rest of the process lifetime, leaked across `OutOfProcessPluginHost`
 * instances (the v1.0 review's central cross-instance race), and bled into
 * sibling subprocesses (notably the manifest verifier).
 *
 * This helper builds a stand-alone env block per-spawn so each `CreateProcessW`
 * call gets exactly the env it needs without touching the parent's globals.
 *
 * **Lifetime contract:** the caller MUST keep the `Win32EnvBlock` instance
 * alive until `CreateProcessW` (or `CreateProcessAsUserW`) returns — the
 * buffer returned by `data()` points into `m_block` and is only valid while
 * `*this` lives.
 *
 * **Win32-only:** the entire header is gated by `#ifdef _WIN32`. Never include
 * from cross-platform translation units.
 */
#pragma once
#ifdef _WIN32

#include <map>
#include <string>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace ajazz::plugins {

/**
 * @brief RAII helper that builds a UTF-16 environment block suitable for
 *        `CreateProcessW(lpEnvironment=...)` with `CREATE_UNICODE_ENVIRONMENT`.
 *
 * The constructor snapshots the parent env via `GetEnvironmentStringsW`,
 * merges `overrides` using **case-insensitive** key matching (an override
 * replaces an existing parent value; an override with a brand-new key is
 * appended in sort order), preserves `=`-prefixed drive-letter-current-dir
 * entries verbatim at the FRONT, sorts the remaining entries case-insensitively
 * (`_wcsicmp` on the key portion up to the first `=`), and serializes into an
 * internal `std::vector<wchar_t>` ending in the mandatory `\0\0` double-null
 * terminator. The parent-env snapshot is freed eagerly inside the constructor
 * via `FreeEnvironmentStringsW`.
 *
 * @see CR-01 in `.planning/milestones/v1.0-phases/01-sec-003-plugin-host/01-FIX-DEFERRED.md`
 * @see Pitfall 5 in `.planning/research/PITFALLS.md`
 */
class Win32EnvBlock {
public:
    /**
     * @brief Snapshot parent env, merge `overrides`, build the wide env block.
     *
     * Constructor algorithm (matches Plan 06-01 D-01 + D-04):
     *  1. `GetEnvironmentStringsW` → walk the `\0`-terminated list.
     *  2. Partition: `=`-prefixed entries (drive-letter-current-dir, e.g.
     *     `=C:=C:\\foo`) are preserved verbatim at the FRONT and do NOT
     *     participate in override merging.
     *  3. For each non-`=`-prefixed parent entry, look up the key in `overrides`
     *     via `_wcsicmp`. Match → emit the override (using the override's key
     *     casing) and drop it from the working `overrides` copy. No match →
     *     emit the parent entry verbatim.
     *  4. Append any remaining `overrides` (new keys) as `key=value`.
     *  5. Sort the non-`=`-prefixed entries case-insensitively by key.
     *  6. Serialize into `m_block` as `KEY=VALUE\0...\0\0`.
     *  7. `FreeEnvironmentStringsW(snapshot)`.
     *
     * @param overrides Map of override entries. Take by value: the constructor
     *                  drains it during merging.
     */
    explicit Win32EnvBlock(std::map<std::wstring, std::wstring> overrides);

    /**
     * @brief Pointer to the wide env block — pass to
     *        `CreateProcessW(lpEnvironment=...)`.
     *
     * The buffer is only valid while `*this` is alive. Pair with
     * `CREATE_UNICODE_ENVIRONMENT` in `creationFlags`.
     */
    LPVOID data() noexcept;

    /// Total size of the wide env block in bytes (`m_block.size() * sizeof(wchar_t)`).
    [[nodiscard]] SIZE_T size_bytes() const noexcept;

    // Non-copyable, movable. The buffer can't be safely shared via copy because
    // the lifetime contract is tied to a single `*this`.
    Win32EnvBlock(Win32EnvBlock const&) = delete;
    Win32EnvBlock& operator=(Win32EnvBlock const&) = delete;
    Win32EnvBlock(Win32EnvBlock&&) noexcept = default;
    Win32EnvBlock& operator=(Win32EnvBlock&&) noexcept = default;
    ~Win32EnvBlock() = default;

private:
    // Wide env block layout: KEY=VALUE\0KEY=VALUE\0...\0\0
    // The final two wchar_t are both 0 (Pitfall 5 sub-trap 1).
    std::vector<wchar_t> m_block;
};

} // namespace ajazz::plugins

#endif // _WIN32
