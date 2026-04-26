// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file plugin_host.hpp
 * @brief Embedded Python plugin host for the AJAZZ Control Center.
 *
 * PluginHost owns a pybind11 interpreter, scans user-specified directories
 * for plugin packages, and routes Action dispatches from the profile engine
 * to the correct Python Plugin instance.
 *
 * Lifecycle:
 * 1. Construct PluginHost (starts the interpreter).
 * 2. Call addSearchPath() for each directory to scan.
 * 3. Call loadAll() to discover and import plugins.
 * 4. Call dispatch() from the profile engine on each key press.
 */
#pragma once

#include "ajazz/plugins/i_plugin_host.hpp" // PluginInfo + IPluginHost

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string_view>
#include <vector>

namespace ajazz::plugins {

/**
 * @brief Host that manages the embedded Python interpreter and loaded plugins.
 *
 * Only one PluginHost may exist per process (pybind11 restriction: a single
 * scoped_interpreter per process).  The host is non-copyable and non-movable.
 *
 * Implements @ref IPluginHost (audit finding A4 slice 2.5) so callers can
 * be written against the abstraction and switch to
 * @ref OutOfProcessPluginHost without code changes once slice 3 lands.
 *
 * @note All public methods acquire an internal mutex; they may be called from
 *       any thread, but avoid calling dispatch() from a high-frequency path
 *       as it blocks on Python execution.
 */
class PluginHost final : public IPluginHost {
public:
    /**
     * @brief Construct the host and start the embedded Python interpreter.
     *
     * Appends @c "." to @c sys.path so that the @c ajazz runtime module
     * (registered by python_bindings.cpp) is importable.
     */
    PluginHost();

    /// Destroy the host and shut down the interpreter.
    ~PluginHost() override;

    void addSearchPath(std::filesystem::path const& path) override;

    /// Discover and load all plugins found on the search paths. Returns
    /// the count of plugins newly loaded by this call (excludes those
    /// already loaded by a previous call); already-loaded plugins are
    /// updated in place via the host's `insert_or_assign` semantics.
    std::size_t loadAll() override;

    /// List currently loaded plugins. Non-const to satisfy
    /// @ref IPluginHost (the OOP backend roundtrips to fetch live).
    [[nodiscard]] std::vector<PluginInfo> plugins() override;

    /// Dispatch an action to the owning plugin. Blocks until the plugin
    /// returns (plugins should keep their handlers short and delegate
    /// long work to Python threads). Returns `false` on a soft failure
    /// (unknown plugin id, unknown action id, handler raised), `true`
    /// on success; throws @c std::runtime_error only on hard failures.
    bool dispatch(std::string_view pluginId,
                  std::string_view actionId,
                  std::string_view settingsJson) override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace ajazz::plugins
