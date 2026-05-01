// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file i_plugin_host.hpp
 * @brief Common interface for plugin host backends.
 *
 * Audit finding A4 — slice 2.5 introduced the abstraction; slice 3e
 * retired the legacy in-process pybind11 backend. Today the only
 * concrete implementation is @ref ajazz::plugins::OutOfProcessPluginHost
 * (POSIX subprocess + line-delimited JSON IPC, sandboxed via
 * @ref Sandbox); the slice-3c macOS port and slice-3d Windows port
 * will land behind the same interface so callers don't have to change.
 *
 * The interface signatures encode the slice-3a/3b functional contract:
 * `loadAll` returns a count, `dispatch` returns `bool` so a soft
 * failure (unknown plugin/action, handler raised) can be observed
 * without exceptions, and the action's `plugin.action` id is split by
 * the caller (the dispatch entry point takes the two halves separately).
 * Hard failures (child died, IPC pipe broken) are reported via
 * `std::runtime_error`.
 *
 * Construction is **not** part of the interface — different host
 * backends need different ctor arguments (a config struct for the OOP
 * one). Callers wanting backend abstraction at construction time
 * should use a factory function or a `std::unique_ptr<IPluginHost>`
 * slot they assign at app boot.
 */
#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace ajazz::plugins {

/**
 * @brief Metadata snapshot for a loaded plugin.
 *
 * All fields are read from the plugin object's manifest / Python
 * attributes at load time. Defined here (in the interface header)
 * because the `plugins()` accessor returns a vector of these — and
 * so a caller can include just the interface to get both the type
 * and the contract.
 */
struct PluginInfo {
    std::string id;      ///< Reverse-domain plugin identifier, e.g. @c "com.example.obs".
    std::string name;    ///< Human-readable display name.
    std::string version; ///< Semantic version string (e.g. @c "1.2.0").
    std::string authors; ///< Author list (free-form string).
    std::vector<std::string> actionIds; ///< Action ids exposed by the plugin.

    /// Coarse permissions the plugin requests, declared as a class
    /// attribute on the Python @c Plugin subclass and validated against
    /// the @c Ajazz.Permissions enum in
    /// @c docs/schemas/plugin_manifest.schema.json (e.g. @c "notifications",
    /// @c "shell-exec", @c "clipboard-read"). Slice 3a is read-only —
    /// permissions are surfaced for UI install-time review and for
    /// future sandbox enforcement; nothing in slice 3a actually gates
    /// behaviour on this list.
    std::vector<std::string> permissions;

    /// Manifest signature verification status (SEC-003 follow-up #51).
    /// Set by the host *before* the plugin is dispatched: a manifest
    /// without an Ed25519 signature, or with a tampered one, gets
    /// `signed=false`. Verification uses
    /// @c scripts/sign-plugin-manifest.py (subprocess) so the host
    /// shares the canonical-form rules with the publisher tool —
    /// see @c docs/architecture/PLUGIN-SYSTEM.md §"Manifest signing".
    bool signed_{false};

    /// Friendly publisher name resolved from
    /// @c resources/trusted_publishers.json. Empty when @c signed_ is
    /// @c false. Equal to @c "self-signed" when the manifest verifies
    /// but its public key is not in the bundled trust roots — UI uses
    /// this distinction to colour the verification chip.
    std::string publisher;
};

/**
 * @brief Common contract implemented by every plugin host backend.
 *
 * Backend today: @ref OutOfProcessPluginHost (POSIX subprocess + IPC,
 * sandboxed via @ref Sandbox). Slice-3c (macOS) and slice-3d
 * (Windows) will add per-OS backends behind the same interface.
 *
 * Thread-safety: every method takes the implementation's internal
 * mutex; calls may come from any thread but they serialise through
 * the host (subprocess IPC pipelines are sequential by design).
 *
 * Lifetime: implementations destroy the underlying subprocess in
 * their destructor; no separate `shutdown()` call is exposed.
 * Construct, use, let scope expire.
 */
class IPluginHost {
public:
    IPluginHost() = default;
    virtual ~IPluginHost() = default;

    IPluginHost(IPluginHost const&) = delete;
    IPluginHost& operator=(IPluginHost const&) = delete;
    IPluginHost(IPluginHost&&) = delete;
    IPluginHost& operator=(IPluginHost&&) = delete;

    /**
     * @brief Register a directory the next @ref loadAll will scan.
     *
     * Idempotent: re-adding the same path is a no-op. Symlinks /
     * `..` segments are resolved before the path is stored so a
     * later @ref loadAll sees a canonical filesystem location.
     */
    virtual void addSearchPath(std::filesystem::path const& path) = 0;

    /**
     * @brief Walk every registered search path and import each plugin found.
     *
     * Per-plugin failures (syntax error, missing dependency, unsafe
     * id) are logged but do NOT abort the sweep — partial coverage
     * is preferable to a single bad plugin breaking everything.
     *
     * @return The count of plugins newly loaded by *this* call. Does
     *         not include plugins already loaded by a previous
     *         @ref loadAll. The full inventory is available via
     *         @ref plugins.
     */
    virtual std::size_t loadAll() = 0;

    /**
     * @brief Snapshot the currently loaded plugin inventory.
     *
     * Each entry carries the plugin's manifest metadata (id, name,
     * version, authors). Mutations to the host afterwards do NOT
     * affect the returned vector.
     *
     * @note Non-const because the OOP backend roundtrips to its
     *       child to fetch the live inventory.
     */
    [[nodiscard]] virtual std::vector<PluginInfo> plugins() = 0;

    /**
     * @brief Dispatch an action to the matching plugin's handler.
     *
     * The caller must split the wire-format `<plugin>.<action>` id
     * before calling — the host does not parse it. Soft failures
     * (unknown plugin id, unknown action id, handler raised an
     * exception) return `false`; the host logs the cause and the
     * caller decides whether to surface it. Hard failures (subprocess
     * died, IPC pipe broken) throw `std::runtime_error`.
     *
     * @param pluginId    Plugin manifest id (the part before the dot).
     * @param actionId    Action id within that plugin (the part after the dot).
     * @param settingsJson Per-binding settings JSON; the host passes
     *                     it through verbatim, the plugin decodes it.
     */
    virtual bool dispatch(std::string_view pluginId,
                          std::string_view actionId,
                          std::string_view settingsJson) = 0;
};

} // namespace ajazz::plugins
