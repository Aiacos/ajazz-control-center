// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file profile_bundle.hpp
 * @brief `.ajazzprofile` import / export — a single-file portable profile
 *        bundle for sharing between users and machines.
 *
 * Closes #32 (import/export).
 *
 * Layout
 * ------
 *
 * The bundle is a renamed-extension ZIP archive containing:
 *
 * @verbatim
 * /
 * ├─ manifest.json     (schema version + minimum app version + author)
 * ├─ profile.json      (output of profileToJson())
 * └─ assets/
 *    ├─ <key-icon-1>
 *    └─ <key-icon-N>
 * @endverbatim
 *
 * The assets directory is optional: profiles that reference no key icons
 * produce a 2-file bundle. The manifest is intentionally tiny and parsed
 * with a hand-rolled minimal-state JSON walker so the core stays free of
 * Qt / RapidJSON dependencies.
 *
 * Bundles are written through @ref ProfileIo's atomic write helper so
 * partially-written archives are never visible to the file system.
 */
#pragma once

#include "ajazz/core/profile.hpp"

#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace ajazz::core {

/// Thrown when bundle parsing or writing fails.
class ProfileBundleError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/**
 * @brief Snapshot of a parsed bundle.
 *
 * `profile` is the decoded profile; `assets` lists relative paths under
 * `assets/` that were extracted to a temporary directory and need to be
 * referenced from `profile` via `KeyState::imagePath`.
 */
struct ProfileBundle {
    Profile profile;                           ///< Decoded profile.
    std::filesystem::path extractedAssetsRoot; ///< Tempdir holding extracted assets.
    std::vector<std::filesystem::path> assets; ///< Extracted asset paths (relative).
    std::string author;                        ///< Free-form author field from the manifest.
    std::string minAppVersion;                 ///< Minimum required app version.
};

/**
 * @brief Read a `.ajazzprofile` archive and return its decoded contents.
 *
 * @param path  Path to the `.ajazzprofile` (a ZIP-format file).
 * @return Decoded bundle; assets are extracted to a temporary directory
 *         under the OS temp folder. Caller is responsible for cleaning up
 *         `extractedAssetsRoot` when done.
 * @throws ProfileBundleError on I/O or parse failure.
 */
[[nodiscard]] ProfileBundle importProfileBundle(std::filesystem::path const& path);

/**
 * @brief Write a profile + its referenced assets to a `.ajazzprofile`.
 *
 * The file is written atomically: the archive is built next to the target,
 * fsync()'d, then renamed into place.
 *
 * @param path     Output path; overwritten if it exists.
 * @param profile  Profile to serialise.
 * @param author   Optional author name embedded in the manifest.
 * @param assets   Absolute paths of asset files referenced by the profile.
 * @throws ProfileBundleError on I/O failure.
 */
void exportProfileBundle(std::filesystem::path const& path,
                         Profile const& profile,
                         std::string const& author = {},
                         std::vector<std::filesystem::path> const& assets = {});

} // namespace ajazz::core
