// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file unified_plugin_host.cpp
 * @brief UnifiedPluginHost aggregator implementation.
 *
 * Routes dispatch by UUID to the correct sub-host:
 *   - Python UUID   → OutOfProcessPluginHost::dispatch (Qt->STL at this boundary)
 *   - .sdPlugin UUID → PluginManager::dispatch (the WebSocket path)
 *
 * COD-031: app-layer TU. Uses Qt types only; no nlohmann includes.
 * HOST-03: zero SKU-specific branches in this file (SKU strings belong in PluginDeviceBridge only).
 *
 * Phase: 30-plugin-host-modular-foundation / Plan 30-03 (HOST-01)
 */
#include "unified_plugin_host.hpp"

#include "ajazz/plugins/i_plugin_host.hpp"
#include "plugin_manager.hpp"

#include <QJsonDocument>
#include <QString>

#include <string>
#include <vector>

namespace ajazz::app {

UnifiedPluginHost::UnifiedPluginHost(PluginManager* sdPluginHost,
                                     plugins::IPluginHost* pythonHost) noexcept
    : m_sdPluginHost(sdPluginHost), m_pythonHost(pythonHost) {}

// ---------------------------------------------------------------------------
// discover() / spawn() / shutdown()
// ---------------------------------------------------------------------------

std::vector<PluginManifest> UnifiedPluginHost::discover() {
    if (!m_sdPluginHost) {
        return {};
    }
    return m_sdPluginHost->discover();
}

void UnifiedPluginHost::spawn(PluginManifest const& manifest) {
    if (m_sdPluginHost) {
        m_sdPluginHost->spawn(manifest);
    }
}

void UnifiedPluginHost::shutdown() {
    // .sdPlugin plugins first: exitApp -> terminate -> kill per akp_plugin_sdk.md §3.
    if (m_sdPluginHost) {
        m_sdPluginHost->shutdown();
    }
    // Python host teardown is handled by its own destructor (owned by Application's
    // unique_ptr<IPluginHost>); we do not call an explicit shutdown here.
    // If the Python host has a loadAll() or addSearchPath() that needs cleanup,
    // it is the Application's responsibility to manage its lifetime.
}

// ---------------------------------------------------------------------------
// dispatch() — the SINGLE routing point for .sdPlugin vs Python
// ---------------------------------------------------------------------------

bool UnifiedPluginHost::dispatch(QString const& pluginUuid,
                                 QString const& actionId,
                                 QJsonObject const& payload) {
    // Route by UUID: Python plugins first (explicit UUID match in inventory),
    // then fall through to the .sdPlugin WebSocket path.
    if (isPythonPlugin(pluginUuid)) {
        if (!m_pythonHost) {
            return false;
        }
        // Qt->STL conversion at the boundary (RESEARCH assumption A4 + RESOLVED Q2).
        // QJsonObject payload -> JSON string -> std::string_view passed to OOP host.
        std::string const pluginIdStd = pluginUuid.toStdString();
        std::string const actionIdStd = actionId.toStdString();
        std::string const settingsJsonStd =
            QJsonDocument(payload).toJson(QJsonDocument::Compact).toStdString();
        return m_pythonHost->dispatch(pluginIdStd, actionIdStd, settingsJsonStd);
    }

    // .sdPlugin path: delegate to PluginManager.
    if (!m_sdPluginHost) {
        return false;
    }
    return m_sdPluginHost->dispatch(pluginUuid, actionId, payload);
}

// ---------------------------------------------------------------------------
// plugins() — merged inventory from both sub-hosts
// ---------------------------------------------------------------------------

std::vector<plugins::PluginInfo> UnifiedPluginHost::plugins() {
    std::vector<plugins::PluginInfo> result;

    // .sdPlugin inventory first.
    if (m_sdPluginHost) {
        auto sdPlugins = m_sdPluginHost->plugins();
        result.insert(result.end(),
                      std::make_move_iterator(sdPlugins.begin()),
                      std::make_move_iterator(sdPlugins.end()));
    }

    // Python inventory appended (OutOfProcessPluginHost::plugins() returns STL PluginInfo).
    if (m_pythonHost) {
        try {
            auto pyPlugins = m_pythonHost->plugins();
            result.insert(result.end(),
                          std::make_move_iterator(pyPlugins.begin()),
                          std::make_move_iterator(pyPlugins.end()));
        } catch (...) {
            // Python host IPC failure (child died, pipe broken). Return what we have.
        }
    }

    return result;
}

// ---------------------------------------------------------------------------
// connectedPluginCount() / pluginServer()
// ---------------------------------------------------------------------------

int UnifiedPluginHost::connectedPluginCount() const noexcept {
    return m_sdPluginHost ? m_sdPluginHost->connectedPluginCount() : 0;
}

SdPluginServer* UnifiedPluginHost::pluginServer() const noexcept {
    return m_sdPluginHost ? m_sdPluginHost->pluginServer() : nullptr;
}

// ---------------------------------------------------------------------------
// isPythonPlugin() — routing predicate
// ---------------------------------------------------------------------------

bool UnifiedPluginHost::isPythonPlugin(QString const& pluginUuid) {
    if (!m_pythonHost) {
        return false;
    }
    try {
        std::string const uuidStd = pluginUuid.toStdString();
        auto const pyPlugins = m_pythonHost->plugins();
        for (auto const& info : pyPlugins) {
            if (info.id == uuidStd) {
                return true;
            }
        }
    } catch (...) {
        // Python host IPC failure — treat as not a Python plugin, fall through to sdPlugin path.
    }
    return false;
}

} // namespace ajazz::app
