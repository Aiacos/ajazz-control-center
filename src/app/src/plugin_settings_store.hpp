// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file plugin_settings_store.hpp
 * @brief Shared on-disk store for plugin per-context and global settings.
 *
 * Single source of truth for Stream Deck plugin settings, used by BOTH:
 *   - the Property Inspector bridge (PIBridge, $SD.setSettings/getSettings), and
 *   - the plugin process itself over its WebSocket (setSettings/getSettings/
 *     setGlobalSettings/getGlobalSettings inbound events).
 *
 * Layout (mirrors the proven PIBridge M4 scheme):
 *   <AppDataLocation>/plugins/<pluginUuid>/settings/<contextId>.json   (per-context)
 *   <AppDataLocation>/plugins/<pluginUuid>/global.json                 (plugin-wide)
 *
 * Per-context settings are keyed by the WIRE context id minted by the host
 * (`deviceId#pageId#controller#row#column`) so the plugin and its Property
 * Inspector address the exact same record. '#' is a safe, printable, non-separator
 * filename byte, so the wire context id passes the sanitiser unchanged.
 *
 * All writes are atomic (QSaveFile temp+rename), all reads return "{}" on any
 * error (missing file, corrupt JSON, oversize), and every entry is path-traversal
 * guarded + size-capped (1 MiB). No nlohmann (COD-031, app-layer Qt-Core only).
 */
#pragma once

#include <QString>

namespace ajazz::app::plugin_settings_store {

/// Max accepted settings payload (per record). 1 MiB — two orders of magnitude
/// above any realistic settings blob; the upper bound on both read and write.
inline constexpr long long kMaxSettingsBytes = 1LL << 20;

/// Reject any id that could escape the plugin sandbox as a filesystem component
/// (control bytes, non-ASCII, path separators, `.`/`..`/embedded `..`). Plugin
/// and PI-supplied ids are untrusted.
[[nodiscard]] bool isSafeComponent(QString const& s);

/// Read the per-context settings record. Returns "{}" on any error / first load.
[[nodiscard]] QString readContext(QString const& pluginUuid, QString const& contextId);

/// Persist the per-context settings record (json must already parse + be capped).
/// Returns true on a committed atomic write; logs + returns false otherwise.
[[nodiscard]] bool
writeContext(QString const& pluginUuid, QString const& contextId, QString const& json);

/// Read the plugin-wide (global) settings record. Returns "{}" on any error.
[[nodiscard]] QString readGlobal(QString const& pluginUuid);

/// Persist the plugin-wide (global) settings record. Returns true on success.
[[nodiscard]] bool writeGlobal(QString const& pluginUuid, QString const& json);

} // namespace ajazz::app::plugin_settings_store
