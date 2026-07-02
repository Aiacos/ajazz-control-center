// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file manifest_path_guard.hpp
 * @brief Containment check for child-reported manifest paths.
 *
 * Library-internal header (lives under `src/plugins/src/`, not the
 * public include tree), shared by the POSIX and Win32
 * @ref OutOfProcessPluginHost backends and exercised directly by the
 * unit tests. Pure logic, no platform dependencies — `inline` for ODR
 * safety across multiple including TUs (same pattern as
 * `wire_protocol.hpp`).
 *
 * The plugin-host child reports each plugin's `manifest_path` over the
 * stdio pipe. The child is less trusted than the parent — it runs
 * third-party plugin code, possibly sandboxed — so that string is
 * attacker-influenced input: passing it straight to verifyManifest()
 * would let a compromised child point the parent at any readable file
 * (CodeQL cpp/path-injection). The guard canonicalises the reported
 * path and only releases it for file access when it resolves inside a
 * search root the parent itself registered via addSearchPath();
 * everything else collapses to the existing unsigned-plugin contract.
 */
#pragma once

#include <algorithm>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

namespace ajazz::plugins::detail {

/// Canonical form of a search path as registered by addSearchPath().
/// Falls back to the absolute lexically-normal form when the directory
/// does not (yet) exist; an empty result never matches anything.
inline std::filesystem::path recordSearchRoot(std::filesystem::path const& path) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path canonical = fs::weakly_canonical(path, ec);
    if (ec || canonical.empty()) {
        canonical = fs::absolute(path, ec).lexically_normal();
    }
    return canonical;
}

/// Canonicalise @p reported and return it only when it is contained in
/// one of @p roots (each produced by @ref recordSearchRoot).
/// std::nullopt means "treat the plugin as unsigned".
inline std::optional<std::filesystem::path>
containedManifestPath(std::string const& reported,
                      std::vector<std::filesystem::path> const& roots) {
    namespace fs = std::filesystem;
    std::error_code ec;
    // weakly_canonical resolves symlinks in the existing prefix and
    // lexically normalises the rest, so an embedded `..` cannot escape
    // a root after the component-prefix comparison below.
    fs::path canonical = fs::weakly_canonical(fs::path(reported), ec);
    if (ec || canonical.empty()) {
        return std::nullopt;
    }
    for (auto const& root : roots) {
        if (root.empty()) {
            continue; // a failed recordSearchRoot() must not match everything
        }
        auto firstDiff =
            std::mismatch(root.begin(), root.end(), canonical.begin(), canonical.end()).first;
        if (firstDiff == root.end()) {
            return canonical;
        }
    }
    return std::nullopt;
}

} // namespace ajazz::plugins::detail
