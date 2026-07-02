// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file i_plugin_host2.hpp
 * @brief IPluginHost2 — the SINGLE unified spawn/lifecycle/IPC contract for ALL plugin runtimes.
 *
 * **This interface SUPERSEDES the per-runtime split** and is NOT the Python-only
 * @ref ajazz::plugins::IPluginHost. That interface is a low-level STL-typed contract used
 * internally by OutOfProcessPluginHost. IPluginHost2 is the app-layer contract (Qt types)
 * that callers outside the aggregator should use.
 *
 * The four runtimes (Node.js, HTML/QWebEngine, native, Python) all live behind this one
 * surface. Routing by runtime is an INTERNAL detail of the concrete implementation
 * (@ref UnifiedPluginHost); no caller of IPluginHost2 needs to know which sub-host
 * handles a given UUID.
 *
 * COD-031: this header is app-layer (src/app/src/) and uses Qt types only (QString,
 * QJsonObject). It MUST NOT include nlohmann/json.hpp or any ajazz_core installed header
 * that pulls in nlohmann.
 *
 * Phase: 30-plugin-host-modular-foundation / Plan 30-03 (HOST-01)
 */
#pragma once

#include "ajazz/plugins/i_plugin_host.hpp" // for PluginInfo (STL type)
#include "plugin_manifest.hpp"             // for PluginManifest

#include <QJsonObject>
#include <QString>

#include <vector>

namespace ajazz::app {

class SdPluginServer;

/**
 * @interface IPluginHost2
 * @brief Unified app-layer contract for all four plugin runtimes.
 *
 * Concrete implementation: @ref UnifiedPluginHost (owns PluginManager for the
 * .sdPlugin WebSocket path + OutOfProcessPluginHost for the Python path).
 *
 * All parameters are Qt types (QString/QJsonObject) so the interface can be
 * used by any app-layer caller without depending on the STL types of the
 * plugins layer.
 */
class IPluginHost2 {
public:
    IPluginHost2() = default;
    virtual ~IPluginHost2() = default;

    IPluginHost2(IPluginHost2 const&) = delete;
    IPluginHost2& operator=(IPluginHost2 const&) = delete;
    IPluginHost2(IPluginHost2&&) = delete;
    IPluginHost2& operator=(IPluginHost2&&) = delete;

    // ---- Lifecycle ----------------------------------------------------------

    /**
     * @brief Scan plugin directories and return all runnable manifests.
     *
     * For the .sdPlugin path, runs the discovery algorithm (extract archives,
     * parse manifests, filter by runnability). Python plugins discovered via
     * loadAll() are NOT represented as manifests — they are returned via
     * @ref plugins().
     *
     * @return Vector of validated, runnable PluginManifest objects (.sdPlugin only).
     */
    [[nodiscard]] virtual std::vector<PluginManifest> discover() = 0;

    /**
     * @brief Spawn the plugin described by @p manifest.
     *
     * Dispatches to the appropriate runtime sub-host. For Python plugins, the
     * equivalent of loadAll() is triggered internally; Python plugins do not
     * use this surface directly.
     *
     * @param manifest  Validated, runnable manifest from @ref discover().
     */
    virtual void spawn(PluginManifest const& manifest) = 0;

    /**
     * @brief Gracefully shut down all live plugins across all runtimes.
     *
     * .sdPlugin plugins: exitApp -> terminate -> kill.
     * Python host: shutdown round-trip -> SIGKILL fallback.
     */
    virtual void shutdown() = 0;

    // ---- IPC / dispatch -----------------------------------------------------

    /**
     * @brief Dispatch an action event to the owning plugin.
     *
     * Routes by UUID internally: if the UUID belongs to a .sdPlugin runtime the
     * event goes via the WebSocket path (PluginManager / SdPluginServer); if it
     * belongs to a Python plugin it is delivered to OutOfProcessPluginHost::dispatch
     * (Qt->STL conversion at this boundary).
     *
     * @param pluginUuid  The plugin's UUID (as registered over the WS or as
     *                    loaded by the Python host).
     * @param actionId    Action identifier within the plugin.
     * @param payload     Per-binding settings/event payload (JSON object).
     * @return true if the dispatch was accepted; false on soft failure.
     */
    virtual bool
    dispatch(QString const& pluginUuid, QString const& actionId, QJsonObject const& payload) = 0;

    // ---- Inventory ----------------------------------------------------------

    /**
     * @brief Return the MERGED live plugin inventory across all runtimes.
     *
     * Folds the .sdPlugin registered plugins and the Python-loaded plugins into
     * a single vector. The shape is @ref ajazz::plugins::PluginInfo (STL types)
     * — callers that render this to QML convert at the model layer.
     *
     * @note Non-const because the Python host may round-trip to its child.
     */
    [[nodiscard]] virtual std::vector<plugins::PluginInfo> plugins() = 0;

    /**
     * @brief Return the count of live registered .sdPlugin connections.
     *
     * Delegates to the .sdPlugin sub-host (SdPluginServer::connectedPluginCount).
     * Python plugins are NOT counted here — they are not WebSocket connections.
     */
    [[nodiscard]] virtual int connectedPluginCount() const noexcept = 0;

    /**
     * @brief Access the underlying SdPluginServer (non-owning).
     *
     * Used by callers that need to send low-level WebSocket events (e.g. the
     * debug control channel). Returns nullptr if WebSockets are disabled.
     */
    [[nodiscard]] virtual SdPluginServer* pluginServer() const noexcept = 0;
};

} // namespace ajazz::app
