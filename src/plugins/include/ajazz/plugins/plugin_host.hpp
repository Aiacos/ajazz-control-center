// SPDX-License-Identifier: GPL-3.0-or-later
//
// Embedded Python plugin host. Loads user plugin packages from a set of
// directories, exposes the `ajazz` runtime module to Python, and routes
// action invocations from the profile engine to the right plugin.
//
#pragma once

#include "ajazz/core/profile.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace ajazz::plugins {

struct PluginInfo {
    std::string id;      ///< e.g. "com.example.obs"
    std::string name;
    std::string version;
    std::string authors;
    std::vector<std::string> actionIds;
};

class PluginHost {
public:
    PluginHost();
    ~PluginHost();

    PluginHost(PluginHost const&)            = delete;
    PluginHost& operator=(PluginHost const&) = delete;
    PluginHost(PluginHost&&)                 = delete;
    PluginHost& operator=(PluginHost&&)      = delete;

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

}  // namespace ajazz::plugins
