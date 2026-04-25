// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file plugin_host.cpp
 * @brief Implementation of PluginHost — the embedded Python plugin manager.
 *
 * Uses pybind11's scoped_interpreter to embed CPython.  Each plugin directory
 * that contains a @c plugin.py is imported as a Python package; the module
 * must expose a @c Plugin class whose @c id attribute is used as the key.
 */
#include "ajazz/plugins/plugin_host.hpp"

#include "ajazz/core/logger.hpp"

#include <mutex>
#include <unordered_map>

#include <pybind11/embed.h>

namespace py = pybind11;

namespace ajazz::plugins {

// pybind11 types carry hidden visibility; annotate Impl hidden to avoid
// -Werror=attributes on GCC when its containing class has default visibility.
#if defined(__GNUC__) && !defined(_WIN32)
#define AJAZZ_HIDDEN __attribute__((visibility("hidden")))
#else
#define AJAZZ_HIDDEN
#endif

/**
 * @brief Private implementation data for PluginHost (pimpl idiom).
 *
 * Isolates pybind11 headers from public API consumers and sidesteps
 * visibility attribute mismatches on GCC/Linux.
 */
struct AJAZZ_HIDDEN PluginHost::Impl {
    py::scoped_interpreter guard{};                 ///< Owns the CPython interpreter lifetime.
    std::vector<std::filesystem::path> searchPaths; ///< Directories scanned by loadAll().
    std::unordered_map<std::string, py::object>
        plugins;      ///< Loaded plugin instances keyed by plugin id.
    std::mutex mutex; ///< Guards searchPaths and plugins.
};

PluginHost::PluginHost() : m_impl(std::make_unique<Impl>()) {
    // Ensure our `ajazz` Python runtime module is importable. It is built
    // by `python_bindings.cpp` into the same binary.
    py::module_::import("sys").attr("path").cast<py::list>().append(".");
}

PluginHost::~PluginHost() = default;

void PluginHost::addSearchPath(std::filesystem::path path) {
    std::lock_guard const lock(m_impl->mutex);
    m_impl->searchPaths.push_back(std::move(path));
}

void PluginHost::loadAll() {
    std::lock_guard const lock(m_impl->mutex);
    try {
        auto sys = py::module_::import("sys");
        auto sysPath = sys.attr("path").cast<py::list>();

        for (auto const& path : m_impl->searchPaths) {
            if (!std::filesystem::is_directory(path)) {
                continue;
            }
            sysPath.append(path.string());

            for (auto const& entry : std::filesystem::directory_iterator(path)) {
                if (!entry.is_directory()) {
                    continue;
                }
                auto const pluginPy = entry.path() / "plugin.py";
                if (!std::filesystem::is_regular_file(pluginPy)) {
                    continue;
                }

                auto const pkgName = entry.path().filename().string();
                try {
                    auto mod = py::module_::import(pkgName.c_str());
                    auto cls = mod.attr("Plugin");
                    py::object instance = cls();
                    std::string const id = py::str(instance.attr("id")).cast<std::string>();
                    m_impl->plugins.emplace(id, std::move(instance));
                    AJAZZ_LOG_INFO("plugins", "loaded {} from {}", id, entry.path().string());
                } catch (std::exception const& e) {
                    AJAZZ_LOG_ERROR("plugins", "failed to load {}: {}", pkgName, e.what());
                }
            }
        }
    } catch (std::exception const& e) {
        AJAZZ_LOG_ERROR("plugins", "loadAll: {}", e.what());
    }
}

std::vector<PluginInfo> PluginHost::plugins() const {
    std::lock_guard const lock(m_impl->mutex);
    std::vector<PluginInfo> out;
    out.reserve(m_impl->plugins.size());
    for (auto const& [id, obj] : m_impl->plugins) {
        PluginInfo info;
        info.id = id;
        info.name = py::hasattr(obj, "name") ? py::str(obj.attr("name")).cast<std::string>() : id;
        info.version =
            py::hasattr(obj, "version") ? py::str(obj.attr("version")).cast<std::string>() : "";
        info.authors =
            py::hasattr(obj, "authors") ? py::str(obj.attr("authors")).cast<std::string>() : "";
        out.push_back(std::move(info));
    }
    return out;
}

void PluginHost::dispatch(core::Action const& action) {
    std::lock_guard const lock(m_impl->mutex);
    auto const dot = action.id.find('.');
    if (dot == std::string::npos) {
        AJAZZ_LOG_WARN("plugins", "invalid action id: {}", action.id);
        return;
    }
    auto const pluginId = action.id.substr(0, dot);
    auto const actionId = action.id.substr(dot + 1);

    auto const it = m_impl->plugins.find(pluginId);
    if (it == m_impl->plugins.end()) {
        AJAZZ_LOG_WARN("plugins", "unknown plugin: {}", pluginId);
        return;
    }
    try {
        it->second.attr("dispatch")(actionId, action.settingsJson);
    } catch (std::exception const& e) {
        AJAZZ_LOG_ERROR("plugins", "dispatch {}: {}", action.id, e.what());
    }
}

} // namespace ajazz::plugins
