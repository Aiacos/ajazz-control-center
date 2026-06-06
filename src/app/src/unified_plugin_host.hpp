// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file unified_plugin_host.hpp
 * @brief UnifiedPluginHost — the single IPluginHost2 implementation that aggregates
 *        all four plugin runtimes (Node, HTML, native, Python).
 *
 * UnifiedPluginHost is the aggregator. It OWNS two concrete sub-hosts:
 *   - PluginManager        — .sdPlugin (Node/HTML/native) over WebSocket IPC
 *   - OutOfProcessPluginHost — Python subprocess over line-delimited JSON IPC
 *
 * The sub-hosts are injected at construction (non-owning raw pointers); ownership
 * stays in Application. UnifiedPluginHost acts as a routing layer: dispatch() routes
 * by UUID internally, so callers of pluginHost2()->dispatch() get correct routing
 * for ALL four runtimes with no routing logic in Application::dispatch() or any
 * other call site.
 *
 * **Routing algorithm:**
 *   1. If the pluginUuid matches a key in the Python host's plugins() inventory
 *      → forward to OutOfProcessPluginHost::dispatch (Qt->STL conversion at boundary).
 *   2. Otherwise (including .sdPlugin registered UUIDs)
 *      → forward to PluginManager::dispatch (the .sdPlugin WS path).
 *
 * COD-031: app-layer header (src/app/src/). Uses Qt types (QString, QJsonObject).
 * MUST NOT include nlohmann/json.hpp or any ajazz_core installed header that pulls
 * in nlohmann.
 *
 * Phase: 30-plugin-host-modular-foundation / Plan 30-03 (HOST-01)
 */
#pragma once

#include "i_plugin_host2.hpp"
#include "plugin_manifest.hpp"

#include <QJsonObject>
#include <QString>

#include <string>
#include <vector>

namespace ajazz::plugins {
class IPluginHost;
} // namespace ajazz::plugins

namespace ajazz::app {

class PluginManager;
class SdPluginServer;

/**
 * @class UnifiedPluginHost
 * @brief Aggregator implementing IPluginHost2 that owns both the .sdPlugin and Python sub-hosts.
 *
 * Ownership model:
 *   - PluginManager* and IPluginHost* (OutOfProcessPluginHost) are NON-OWNING raw pointers.
 *   - Application owns both concrete hosts; UnifiedPluginHost is merely a routing layer.
 *
 * @note Either sub-host may be nullptr (e.g. AJAZZ_HAVE_WEBSOCKETS disabled, or Python host
 *       failed to start). Methods degrade gracefully — dispatch() returns false, plugins()
 *       returns only what is available.
 */
class UnifiedPluginHost : public IPluginHost2 {
public:
    /**
     * @brief Construct with the two concrete sub-hosts (non-owning).
     *
     * @param sdPluginHost   The .sdPlugin runtime manager (PluginManager). May be nullptr.
     * @param pythonHost     The Python OOP host (OutOfProcessPluginHost). May be nullptr.
     */
    explicit UnifiedPluginHost(PluginManager* sdPluginHost,
                               plugins::IPluginHost* pythonHost) noexcept;

    ~UnifiedPluginHost() override = default;

    // Non-copyable, non-movable (holds raw sub-host pointers).
    UnifiedPluginHost(UnifiedPluginHost const&) = delete;
    UnifiedPluginHost& operator=(UnifiedPluginHost const&) = delete;
    UnifiedPluginHost(UnifiedPluginHost&&) = delete;
    UnifiedPluginHost& operator=(UnifiedPluginHost&&) = delete;

    // ---- IPluginHost2 implementation ----------------------------------------

    /**
     * @brief Unified discover — delegates to the .sdPlugin sub-host.
     *
     * Python plugins are discovered via loadAll() (called internally); they do not
     * appear as PluginManifest entries and are surfaced via plugins() only.
     */
    [[nodiscard]] std::vector<PluginManifest> discover() override;

    /**
     * @brief Unified spawn — delegates to the .sdPlugin sub-host.
     *
     * For Python plugins, addSearchPath()/loadAll() are called internally at
     * construction time (in Application::initPluginHost), not via this surface.
     */
    void spawn(PluginManifest const& manifest) override;

    /**
     * @brief Unified shutdown — tears down .sdPlugin plugins, then Python host.
     *
     * Order: PluginManager::shutdown() first (exitApp -> terminate -> kill per
     * akp_plugin_sdk.md §3), then the Python host destructor / shutdown().
     * The Python host destructor is handled by its own lifecycle; we do not
     * call explicit Python shutdown here (that is owned by the Application's
     * unique_ptr<IPluginHost> destructor).
     */
    void shutdown() override;

    /**
     * @brief Routes dispatch by UUID to the correct sub-host.
     *
     * If pluginUuid is known to the Python host (present in the Python inventory),
     * forwards to OutOfProcessPluginHost::dispatch (Qt->STL conversion at boundary).
     * Otherwise forwards to PluginManager::dispatch (the .sdPlugin WS path).
     *
     * This is the ONLY place where .sdPlugin-vs-Python routing is decided.
     * No caller of IPluginHost2 may make this decision; they all call dispatch()
     * through an IPluginHost2* pointer and get correct routing automatically.
     */
    bool dispatch(QString const& pluginUuid,
                  QString const& actionId,
                  QJsonObject const& payload) override;

    /**
     * @brief Returns the MERGED inventory from both sub-hosts.
     *
     * Python PluginInfo entries are appended after the .sdPlugin entries. The
     * Qt->STL conversion of payload types happens inside PluginManager::plugins()
     * (already STL PluginInfo) and the Python host's plugins() (also STL PluginInfo).
     */
    [[nodiscard]] std::vector<plugins::PluginInfo> plugins() override;

    /**
     * @brief Count of live .sdPlugin registered connections.
     *
     * Python connections are not WebSocket-based; they are NOT counted here.
     */
    [[nodiscard]] int connectedPluginCount() const noexcept override;

    /**
     * @brief Non-owning accessor to the underlying SdPluginServer.
     */
    [[nodiscard]] SdPluginServer* pluginServer() const noexcept override;

private:
    /// Non-owning pointer to the .sdPlugin sub-host. May be nullptr.
    PluginManager* m_sdPluginHost;
    /// Non-owning pointer to the Python OOP sub-host. May be nullptr.
    plugins::IPluginHost* m_pythonHost;

    /**
     * @brief Check whether @p pluginUuid belongs to the Python host.
     *
     * Queries the Python host's plugins() and checks if any entry's id matches
     * the UUID. Returns false if m_pythonHost is nullptr.
     *
     * Note: this is a linear scan. The Python plugin count is small (< 100),
     * so this is acceptable.
     */
    [[nodiscard]] bool isPythonPlugin(QString const& pluginUuid);
};

} // namespace ajazz::app
