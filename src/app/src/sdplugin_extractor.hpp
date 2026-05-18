// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file sdplugin_extractor.hpp
 * @brief In-process extractor for downloaded Elgato `.sdPlugin` archives.
 *
 * `.sdPlugin` files are zip archives whose canonical layout puts every
 * entry under a single top-level directory named after the bundle id
 * (`com.elgato.foo.sdPlugin/manifest.json`, `…/Code/…`, `…/Icons/…`).
 * Plugin hosts (Elgato's original, OpenDeck, and ours) all scan an
 * expanded directory tree, so we extract once at install time rather
 * than handing the host an opaque zip.
 *
 * This header exposes two free functions:
 *
 *  - @ref extractSdPluginArchive : extract a single archive into a
 *    deterministically-named directory, stripping the optional
 *    single-directory wrapper. Atomic via a hidden tmp dir + rename.
 *  - @ref extractStandalonePluginArchives : sweep a plugins directory
 *    once on launch, converting any leftover `*.sdPlugin` zip files
 *    (from older code that wrote archives without extracting) into the
 *    new on-disk layout.
 *
 * Tracking: issue #62 / P3.x plugin store unpacking.
 */
#pragma once

#include <QString>

namespace ajazz::app {

/// Extract a `.sdPlugin` zip into @p destDir as the subdirectory named
/// @p targetSubdir. Strips the optional single-directory wrapper so the
/// final layout is `<destDir>/<targetSubdir>/manifest.json` regardless
/// of how the upstream packed it. Atomic-ish: extracts to a hidden
/// `.tmp_` staging dir, then renames into place. On failure the target
/// subdirectory is untouched and the staging dir is removed; the source
/// archive at @p archivePath is left in place so the install can be
/// retried.
[[nodiscard]] bool extractSdPluginArchive(QString const& archivePath,
                                          QString const& destDir,
                                          QString const& targetSubdir);

/// One-shot sweep of @p pluginsDir: find any `*.sdPlugin` *files*
/// (typically left there by older install paths that wrote the
/// archive without extracting it) and round-trip them through
/// @ref extractSdPluginArchive, deleting the archive on success.
/// Best-effort — an archive we can't open as a zip is left in place
/// so the user can inspect / retry without losing data.
void extractStandalonePluginArchives(QString const& pluginsDir);

} // namespace ajazz::app
