// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file manifest_signer_common.cpp
 * @brief Shared (platform-agnostic) implementation of
 *        `ajazz::plugins::loadTrustRoots` per Phase 7 / ARCH-01 / D-01.
 *
 * Replaces the legacy mini-grep cursor walk (formerly duplicated across
 * `manifest_signer.cpp` and `manifest_signer_win32.cpp`) with a single
 * nlohmann::json-based parse. The two platform TUs `#include
 * "manifest_signer_common.hpp"` and route through this definition — drift
 * between the two TUs (the historical re-introduction vector for WR-01) is
 * structurally impossible by construction.
 *
 * Hardening (TRUST-02 / Pitfall 7):
 *   - 1 MB byte cap on the input blob. Read short-circuits at cap+1 byte;
 *     oversize input returns empty list, fails closed.
 *   - 1024-entry cap on parsed roots. Overcount returns empty list.
 *
 * Failure modes (all fail closed — empty result, never throw to caller):
 *   - Missing file
 *   - Oversize input (>1 MB)
 *   - Overcount input (>1024 entries)
 *   - JSON parse error
 *   - Unexpected top-level shape (neither {"publishers":[…]} nor a bare array)
 *
 * COD-031 invariant: nlohmann::json is included ONLY in this .cpp. It is
 * PRIVATE-linked to `ajazz_plugins` and MUST NOT appear in any installed
 * public header. Verified at link time + via repo-level grep.
 */
#include "manifest_signer_common.hpp"

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <system_error>
#include <vector>

#include <nlohmann/json.hpp>

namespace ajazz::plugins {
namespace {

// 1 MB byte cap (TRUST-02 / Pitfall 7). Bounds the parser's DoS surface — a
// malicious or runaway trust-roots file cannot allocate arbitrarily large
// std::strings inside the read loop.
constexpr std::size_t kMaxTrustRootsBytes = 1024U * 1024U;

// 1024-entry cap (TRUST-02 / Pitfall 7). Bounds the post-parse for-loop and
// downstream `out.reserve` on the returned vector. nlohmann's own default
// recursion depth (~10000) further bounds nesting, but the entry cap is the
// observable contract here.
constexpr std::size_t kMaxTrustRootsEntries = 1024U;

/// Read up to kMaxTrustRootsBytes + 1 bytes from @p path. Sets @p oversize to
/// true and returns empty string if the file is longer than the cap. Empty
/// return + oversize=false signals "missing or empty file".
std::string readBlob(std::filesystem::path const& path, bool& oversize) {
    oversize = false;
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }
    std::string blob;
    blob.reserve(4096);
    char buf[4096];
    while (in.read(buf, sizeof(buf)) || in.gcount() > 0) {
        blob.append(buf, static_cast<std::size_t>(in.gcount()));
        if (blob.size() > kMaxTrustRootsBytes) {
            oversize = true;
            return {};
        }
    }
    return blob;
}

} // namespace

std::vector<TrustedPublisher> loadTrustRoots(std::filesystem::path const& jsonPath) {
    std::error_code ec;
    if (!std::filesystem::exists(jsonPath, ec) || ec) {
        return {};
    }

    bool oversize = false;
    auto const blob = readBlob(jsonPath, oversize);
    if (oversize) {
        std::cerr << "loadTrustRoots: input exceeds " << kMaxTrustRootsBytes
                  << " byte cap, refusing to parse: " << jsonPath << '\n';
        return {};
    }
    if (blob.empty()) {
        return {};
    }

    nlohmann::json root;
    try {
        // allow_exceptions=true (default); parse_error → caught below.
        root = nlohmann::json::parse(blob);
    } catch (nlohmann::json::parse_error const& e) {
        std::cerr << "loadTrustRoots: JSON parse error: " << e.what() << " (file: " << jsonPath
                  << ")\n";
        return {};
    } catch (nlohmann::json::exception const& e) {
        // Catch-all for any other nlohmann surface (out_of_range, type_error,
        // invalid_iterator, ...). The contract is fail-closed regardless of
        // which sub-exception fires.
        std::cerr << "loadTrustRoots: JSON exception: " << e.what() << " (file: " << jsonPath
                  << ")\n";
        return {};
    }

    // Accept either {"publishers":[…]} (the bundled shape per
    // `resources/trusted_publishers.json`) or a top-level array as a
    // fallback. Anything else fails closed (empty list).
    nlohmann::json const* arr = nullptr;
    if (root.is_object() && root.contains("publishers") && root["publishers"].is_array()) {
        arr = &root["publishers"];
    } else if (root.is_array()) {
        arr = &root;
    } else {
        std::cerr << "loadTrustRoots: top-level shape unexpected (want object with "
                     "'publishers' array, or top-level array): "
                  << jsonPath << '\n';
        return {};
    }

    if (arr->size() > kMaxTrustRootsEntries) {
        std::cerr << "loadTrustRoots: input has " << arr->size() << " entries, exceeds "
                  << kMaxTrustRootsEntries << " entry cap, refusing to parse: " << jsonPath << '\n';
        return {};
    }

    std::vector<TrustedPublisher> out;
    out.reserve(arr->size());
    for (auto const& entry : *arr) {
        if (!entry.is_object()) {
            continue; // skip non-object entries silently
        }
        auto const keyIt = entry.find("key");
        auto const nameIt = entry.find("name");
        if (keyIt == entry.end() || !keyIt->is_string()) {
            // Missing or non-string `key` → skip. Matches the legacy
            // "malformed entry never cross-pairs" semantic.
            continue;
        }
        if (nameIt == entry.end() || !nameIt->is_string()) {
            // Missing or non-string `name` → skip.
            continue;
        }
        out.push_back({keyIt->get<std::string>(), nameIt->get<std::string>()});
    }
    return out;
}

} // namespace ajazz::plugins
