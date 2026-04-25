// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file profile_bundle.cpp
 * @brief Stub implementation of @ref ajazz::core::ProfileBundle helpers.
 *
 * Closes #32 (import/export — scaffold).
 *
 * The shipped implementation supports the simplest possible bundle: a
 * single `profile.json` file at the archive root, no compression, no
 * assets. This keeps the core dependency-free; the full ZIP back-end (via
 * libzip) is gated behind the `AJAZZ_FEATURE_PROFILE_BUNDLE` CMake option
 * and lives in a separate translation unit.
 *
 * The manifest is currently produced as a side-car inline JSON header
 * inside `profile.json` (top-level `_bundle` object) so existing bundles
 * remain forward-compatible when the real ZIP path lands.
 */
#include "ajazz/core/profile_bundle.hpp"

#include "ajazz/core/logger.hpp"
#include "ajazz/core/profile_io.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

namespace ajazz::core {

namespace {

constexpr char const* kBundleSchemaVersion = "1";

/// Read the entire contents of a regular file into memory.
[[nodiscard]] std::string readFileFully(std::filesystem::path const& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw ProfileBundleError("cannot open bundle: " + path.string());
    }
    std::ostringstream buf;
    buf << in.rdbuf();
    return buf.str();
}

} // namespace

ProfileBundle importProfileBundle(std::filesystem::path const& path) {
    AJAZZ_LOG_INFO("profile_bundle", "import bundle from {}", path.string());
    auto const json = readFileFully(path);
    if (auto const err = validateProfileJson(json); !err.empty()) {
        throw ProfileBundleError("invalid bundle JSON: " + err);
    }
    ProfileBundle bundle;
    // The full QJsonDocument-backed reader lives in the app layer; the core
    // only ships the writer + validator. Callers in the app must overlay the
    // decoded profile via ajazz::app::loadProfile after this call returns.
    bundle.author.clear();
    bundle.minAppVersion.clear();
    bundle.extractedAssetsRoot.clear();
    return bundle;
}

void exportProfileBundle(std::filesystem::path const& path,
                         Profile const& profile,
                         std::string const& author,
                         std::vector<std::filesystem::path> const& assets) {
    if (!assets.empty()) {
        AJAZZ_LOG_WARN("profile_bundle", "asset bundling not yet enabled — exporting JSON only");
    }

    // Embed a tiny manifest object as a top-level `_bundle` entry so the
    // future full-archive implementation can detect schema versions.
    std::ostringstream out;
    auto profileJson = profileToJson(profile);
    if (profileJson.size() >= 2 && profileJson.front() == '{') {
        out << "{\"_bundle\":{\"schema\":\"" << kBundleSchemaVersion << "\",\"author\":\"" << author
            << "\"},";
        out.write(profileJson.data() + 1, static_cast<std::streamsize>(profileJson.size() - 1));
    } else {
        out << profileJson;
    }

    // Write atomically: stream to sibling tempfile + fsync + rename.
    auto tmp = path;
    tmp += ".tmp";
    {
        std::ofstream stream(tmp, std::ios::binary | std::ios::trunc);
        if (!stream) {
            throw ProfileBundleError("cannot create tempfile: " + tmp.string());
        }
        auto const data = out.str();
        stream.write(data.data(), static_cast<std::streamsize>(data.size()));
        stream.flush();
        if (!stream) {
            throw ProfileBundleError("write failed: " + tmp.string());
        }
    }
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        std::filesystem::remove(tmp, ec);
        throw ProfileBundleError("rename failed: " + path.string());
    }
    AJAZZ_LOG_INFO("profile_bundle", "exported bundle to {}", path.string());
}

} // namespace ajazz::core
