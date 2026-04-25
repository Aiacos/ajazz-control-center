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

#include "ajazz/core/profile.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace ajazz::plugins {

/**
 * @brief Metadata snapshot for a loaded plugin.
 *
 * All fields are read from the plugin object's Python attributes at load time.
 */
struct PluginInfo {
    std::string id;      ///< Reverse-domain plugin identifier, e.g. @c "com.example.obs".
    std::string name;    ///< Human-readable display name.
    std::string version; ///< Semantic version string (e.g. @c "1.2.0").
    std::string authors; ///< Author list (free-form string).
    std::vector<std::string> actionIds; ///< Action ids exposed by the plugin.
};

/**
 * @brief Host that manages the embedded Python interpreter and loaded plugins.
 *
 * Only one PluginHost may exist per process (pybind11 restriction: a single
 * scoped_interpreter per process).  The host is non-copyable and non-movable.
 *
 * @note All public methods acquire an internal mutex; they may be called from
 *       any thread, but avoid calling dispatch() from a high-frequency path
 *       as it blocks on Python execution.
 */
class PluginHost {
public:
    /**
     * @brief Construct the host and start the embedded Python interpreter.
     *
     * Appends @c "." to @c sys.path so that the @c ajazz runtime module
     * (registered by python_bindings.cpp) is importable.
     */
    PluginHost();

    /// Destroy the host and shut down the interpreter.
    ~PluginHost();

    PluginHost(PluginHost const&) = delete;
    PluginHost& operator=(PluginHost const&) = delete;
    PluginHost(PluginHost&&) = delete;
    PluginHost& operator=(PluginHost&&) = delete;

    /// Add a directory to scan for plugin packages. Each sub-directory
    /// containing `plugin.py` is loaded as a package.
    void addSearchPath(std::filesystem::path path);

    /// Discover and load all plugins found on the search paths. Safe to
    /// call more than once; already-loaded plugins are skipped.
    void loadAll();

    /// List currently loaded plugins.
    [[nodiscard]] std::vector<PluginInfo> plugins() const;

    /// Dispatch an action to the owning plugin. Blocks until the plugin
    /// returns (plugins should keep their handlers short and delegate
    /// long work to Python threads).
    void dispatch(core::Action const& action);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace ajazz::plugins
