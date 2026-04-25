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
    // SECURITY: do not append "." to sys.path. CWE-427 (Untrusted Search
    // Path). The `ajazz` Python runtime module is built into the host
    // binary and is already importable via the embedded modules table.
    // Plugin search paths added via addSearchPath() get prepended to
    // sys.path explicitly during loadAll() with full path resolution.
}

PluginHost::~PluginHost() = default;

void PluginHost::addSearchPath(std::filesystem::path path) {
    std::lock_guard const lock(m_impl->mutex);
    m_impl->searchPaths.push_back(std::move(path));
}

namespace {

/**
 * @brief Validate a plugin manifest at @p manifestPath.
 *
 * Minimal manifest schema (plugin.toml or plugin.json sibling of plugin.py):
 *   id      : reverse-DNS, [a-z0-9._-]+
 *   name    : human display string
 *   version : semver-ish
 *   authors : free-form
 *
 * Until full code-signing is in place we treat absence of a manifest as a
 * loud warning, not a hard reject; signed-bundle enforcement will be added
 * once the plugin marketplace exists. See SEC-005 / SEC-019.
 */
bool validateManifest(std::filesystem::path const& dir) {
    auto const json = dir / "plugin.json";
    auto const toml = dir / "plugin.toml";
    return std::filesystem::is_regular_file(json) || std::filesystem::is_regular_file(toml);
}

/// Plugin id chars: lowercase ASCII letters, digits, dot/underscore/hyphen.
bool isSafePluginId(std::string_view id) {
    if (id.empty() || id.size() > 64) {
        return false;
    }
    for (char c : id) {
        bool ok =
            (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
        if (!ok) {
            return false;
        }
    }
    return true;
}

} // namespace

void PluginHost::loadAll() {
    std::lock_guard const lock(m_impl->mutex);
    py::gil_scoped_acquire gil;
    try {
        auto sys = py::module_::import("sys");
        auto sysPath = sys.attr("path").cast<py::list>();

        for (auto const& path : m_impl->searchPaths) {
            std::error_code ec;
            auto const canon = std::filesystem::canonical(path, ec);
            if (ec || !std::filesystem::is_directory(canon)) {
                AJAZZ_LOG_WARN("plugins", "skipping invalid search path: {}", path.string());
                continue;
            }
            sysPath.append(canon.string());

            for (auto const& entry : std::filesystem::directory_iterator(canon)) {
                if (!entry.is_directory()) {
                    continue;
                }
                auto const pluginPy = entry.path() / "plugin.py";
                if (!std::filesystem::is_regular_file(pluginPy)) {
                    continue;
                }

                auto const pkgName = entry.path().filename().string();
                if (!isSafePluginId(pkgName)) {
                    AJAZZ_LOG_WARN("plugins", "refusing unsafe plugin dir name: {}", pkgName);
                    continue;
                }
                if (!validateManifest(entry.path())) {
                    AJAZZ_LOG_WARN("plugins",
                                   "plugin {} has no manifest (plugin.json/toml); loading anyway "
                                   "because signed-bundle enforcement is not yet active",
                                   pkgName);
                }
                try {
                    auto mod = py::module_::import(pkgName.c_str());
                    auto cls = mod.attr("Plugin");
                    py::object instance = cls();
                    std::string const id = py::str(instance.attr("id")).cast<std::string>();
                    if (!isSafePluginId(id)) {
                        AJAZZ_LOG_WARN("plugins", "plugin {} reports unsafe id; rejected", pkgName);
                        continue;
                    }
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
    py::gil_scoped_acquire gil;
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
    auto const dot = action.id.find('.');
    if (dot == std::string::npos) {
        AJAZZ_LOG_WARN("plugins", "invalid action id: {}", action.id);
        return;
    }
    auto const pluginId = action.id.substr(0, dot);
    auto const actionId = action.id.substr(dot + 1);

    // COD-004: resolve the target Python object under the host lock, then
    // release it before calling into Python so plugins cannot deadlock the
    // host by re-entering plugin APIs.
    py::object instance;
    {
        std::lock_guard const lock(m_impl->mutex);
        auto const it = m_impl->plugins.find(pluginId);
        if (it == m_impl->plugins.end()) {
            AJAZZ_LOG_WARN("plugins", "unknown plugin: {}", pluginId);
            return;
        }
        instance = it->second; // py::object is refcounted, copy under lock
    }

    py::gil_scoped_acquire gil;
    try {
        instance.attr("dispatch")(actionId, action.settingsJson);
    } catch (std::exception const& e) {
        AJAZZ_LOG_ERROR("plugins", "dispatch {}: {}", action.id, e.what());
    }
}

} // namespace ajazz::plugins
