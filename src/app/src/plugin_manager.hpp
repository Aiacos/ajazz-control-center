// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file plugin_manager.hpp
 * @brief PluginManager — discovery + spawn dispatch + crash lifecycle orchestrator (PLUGIN-07/08).
 *
 * PluginManager is the Wave-4 convergence class for Phase 18. It composes:
 *   - `extractStandalonePluginArchives` (sdplugin_extractor — zip-slip-guarded REUSE)
 *   - `parsePluginManifest` + `manifestRunnableHere` (plugin_manifest — 18-01 REUSE)
 *   - `buildNodeArgv` + `resolveNode20Plus` (node_runner — 18-02 REUSE)
 *   - `makeMiraboxShim` (plugin_mirabox_shim — 18-03 REUSE, WebEngine-gated)
 *   - `SdPluginServer::sendEvent` (Phase 17 — the exitApp seam; 17-02 REUSE)
 *   - `PluginCrashTracker` (18-04 — 3-in-30s crash window; owned member)
 *
 * **Discovery flow** (PLUGIN-07):
 *   1. `extractStandalonePluginArchives(pluginsDir)` — expand any leftover *.sdPlugin zips.
 *   2. Scan `pluginsDir` for `<x>.sdPlugin/manifest.json`.
 *   3. `parsePluginManifest` each; keep only those where `manifestRunnableHere` is true.
 *   4. Return the runnable PluginManifest list to the caller.
 *
 * **Spawn dispatch** (PLUGIN-08 — per akp_plugin_sdk.md §3):
 *   - `.js`/`.mjs`/`.cjs` -> `resolveNode20Plus`; if absent -> `disableWithNotice`; else
 *     `QProcess::start(*nodeExe, buildNodeArgv(code, port, uuid, infoJson))`.
 *   - `.html`/`.htm`      -> WebEngine hosting via `PropertyInspectorController` surface
 *                            (attach `makeMiraboxShim()` to the per-plugin profile first).
 *                            Gated on `AJAZZ_HAVE_WEBENGINE`.
 *   - else (.exe/.app/no-ext) -> native `QProcess::start(codePath, {})`.
 *     `RunAsAdministrator:true` honoured ONLY on Windows (UAC); on Linux/macOS the flag is
 *     logged and ignored — no silent elevation path (T-18-ELEVATE mitigation).
 *
 * **Lifecycle** (PLUGIN-07):
 *   - `QProcess` owned (NEVER `startDetached` — crash signals require owned process).
 *   - `onProcessFailed(uuid)` -> `PluginCrashTracker::recordCrash` + `shouldDisable`:
 *       - disabled: `disableWithNotice` + emit `pluginDisabled` signal.
 *       - restartable: teardown + re-spawn.
 *   - `shutdown()` -> for each live plugin: `SdPluginServer::sendEvent(uuid, "exitApp")`,
 *     then `QProcess::terminate()` (1 s grace), then `QProcess::kill()` if still running.
 *     (Pitfall 5: `terminate()` is a no-op for windowless Windows children; `exitApp` first
 *     allows self-cleanup; final `kill()` guarantees no zombies — akp_plugin_sdk.md §3.)
 *
 * **Security (threat register):**
 *   - T-18-ZIPSLIP: extraction delegated to `extractStandalonePluginArchives` (guarded).
 *   - T-18-ARGV: argv passed as QStringList to QProcess::start(program, args) — no shell.
 *   - T-18-PATHTRAV: UUIDs used in fs paths checked via reused `isSafeUuidComponent`.
 *   - T-18-CHILD-PERSIST: owned QProcess + exitApp-then-terminate-then-kill.
 *   - T-18-ELEVATE: RunAsAdministrator honoured Windows-only; Linux/macOS: log+refuse.
 *
 * COD-031: `src/app/src/` — QJson/Qt-Core only, no nlohmann. Never QCefView (CLAUDE.md).
 * Phase: 18-plugin-manifest-discovery-lifecycle-spawn / Plan 18-04 (PLUGIN-07/08)
 */
#pragma once

#include "node_runner.hpp"
#include "plugin_crash_tracker.hpp"
#include "plugin_manifest.hpp"

#include <QHash>
#include <QObject>
#include <QString>

#include <functional>
#include <map>
#include <memory>
#include <vector>

#if defined(AJAZZ_HAVE_WEBENGINE)
class QWebEngineProfile;
class QWebEnginePage;
#endif

// QProcess must be a complete type wherever unique_ptr<QProcess> is destroyed.
// Including QProcess here ensures any TU that includes this header can destroy LivePlugin.
#include <QProcess>
#include <QProcessEnvironment>

namespace ajazz::app {

class SdPluginServer;
class PropertyInspectorController;

/**
 * @brief Orchestrates plugin discovery, spawn, crash lifecycle, and shutdown.
 *
 * Constructed with injectable dependencies so tests can exercise the logic
 * without a real node binary, real server port, or real process.
 */
class PluginManager : public QObject {
    Q_OBJECT

public:
    /**
     * @brief Construct a PluginManager.
     *
     * @param pluginsDir      Filesystem path scanned by `discover()`.
     *                        Canonical: `QStandardPaths::AppDataLocation/plugins/`.
     * @param server          Non-owning pointer to the running SdPluginServer.
     *                        Used to obtain the server port (for node `-port` argv)
     *                        and to send `exitApp` on shutdown.  Must outlive this
     *                        object.  May be nullptr in tests where WebSockets are
     *                        unavailable (spawn-dispatch only tested; no shutdown).
     * @param probe           Injectable NodeProbe. Production: pass
     *                        `makeDefaultNodeProbe()`. Tests: inject fakes.
     * @param piController    Optional PropertyInspectorController for HTML plugins.
     *                        May be nullptr when `AJAZZ_HAVE_WEBENGINE` is not set
     *                        or when the controller is not yet constructed.
     * @param clock           Injectable millisecond clock. Defaults to
     *                        `QDateTime::currentMSecsSinceEpoch`. Tests inject a
     *                        lambda returning synthetic values.
     * @param parent          QObject parent (optional).
     */
    explicit PluginManager(QString const& pluginsDir,
                           SdPluginServer* server,
                           NodeProbe probe = {},
                           PropertyInspectorController* piController = nullptr,
                           std::function<qint64()> clock = {},
                           QObject* parent = nullptr);

    ~PluginManager() override;

    // Non-copyable, non-movable (owns QProcess children).
    PluginManager(PluginManager const&) = delete;
    PluginManager& operator=(PluginManager const&) = delete;
    PluginManager(PluginManager&&) = delete;
    PluginManager& operator=(PluginManager&&) = delete;

    /**
     * @brief Discover runnable plugins in the plugins directory.
     *
     * Algorithm:
     *   1. `extractStandalonePluginArchives(pluginsDir)` — expand leftover *.sdPlugin zips.
     *   2. Enumerate `<pluginsDir>/<x>.sdPlugin/manifest.json` paths.
     *   3. `parsePluginManifest` each; discard nullopt results (logged).
     *   4. Apply `manifestRunnableHere(m, currentPlatformString(), appVersion())`:
     *      non-runnable manifests are skipped with a log message.
     *   5. Return the runnable manifest list.
     *
     * @return Vector of validated, runnable PluginManifest objects.
     */
    [[nodiscard]] std::vector<PluginManifest> discover();

    /**
     * @brief Spawn the plugin described by @p manifest.
     *
     * Resolves the platform-appropriate CodePath, then dispatches:
     *   - `.js`/`.mjs`/`.cjs` -> node (disableWithNotice if absent/old).
     *   - `.html`/`.htm`       -> WebEngine + Mirabox shim (if WebEngine available).
     *   - else                 -> native QProcess.
     *
     * The process is owned by this manager (never startDetached).
     *
     * @param manifest  Validated, runnable manifest from `discover()`.
     */
    void spawn(PluginManifest const& manifest);

    /**
     * @brief Re-scan the plugins directory and spawn ONLY newly-added plugins.
     *
     * Idempotent by design (D-27-3): re-runs the same directory scan as
     * `discover()`, diffs the runnable manifest list against the already-live
     * set (`m_live`, keyed by `.sdPlugin` directory name per commit `5725cb0`),
     * and calls `spawn()` ONLY for manifests whose key is absent from `m_live`.
     *
     * Already-live plugins are NEVER torn down, restarted, or double-spawned.
     * Repeated calls with no new directories on disk are silent no-ops.
     *
     * Typical use: wire to `PluginCatalogModel::installFinished(ok==true)` in
     * `Application` so a plugin installed from the GUI runs live with no restart.
     *
     * If the persisted disabled-set (Plan 27-03) is not yet present, the check
     * is limited to the `m_live` membership test only (no disabled-set consulted).
     *
     * Logs: `rediscover: N already-live, M newly-spawned`.
     */
    void rediscover();

    /**
     * @brief React to a plugin process failure.
     *
     * Records the crash via PluginCrashTracker using the injected clock. If
     * `shouldDisable` returns true, calls `disableWithNotice` and emits
     * `pluginDisabled`. Otherwise the process is cleaned up and re-spawned.
     *
     * Connected to `QProcess::finished` (abnormal exit) and `QProcess::errorOccurred`.
     *
     * @param uuid  Plugin UUID of the failed process.
     */
    void onProcessFailed(QString const& uuid);

    /**
     * @brief Gracefully shut down all live plugins.
     *
     * For each live plugin:
     *   1. `SdPluginServer::sendEvent(uuid, "exitApp")` — the plugin can flush state.
     *      A false return (no live socket) is silently ignored.
     *   2. `QProcess::terminate()` — 1 s grace period (POSIX SIGTERM).
     *   3. `QProcess::kill()` if still running — no zombies (Pitfall 5 / T-18-CHILD-PERSIST).
     *
     * Source: akp_plugin_sdk.md §3 shutdown protocol + Pitfall 5.
     */
    void shutdown();

    /**
     * @brief Disable a plugin and surface the reason.
     *
     * Marks the UUID as disabled, emits `pluginDisabled(uuid, reason)`, and
     * logs the reason. Does not touch the process (caller handles teardown).
     *
     * @param uuid    Plugin UUID to disable.
     * @param reason  Human-readable reason surfaced to QML.
     */
    void disableWithNotice(QString const& uuid, QString const& reason);

    /// @return true if @p uuid is in the disabled set.
    [[nodiscard]] bool isDisabled(QString const& uuid) const;

    /**
     * @brief Test seam: return the argv that would be passed to QProcess for @p uuid.
     *
     * Returns the QStringList from `buildNodeArgv(...)` that was stored when
     * `spawn()` dispatched a .js manifest. Empty if the uuid has not been
     * spawn-attempted or is not a node plugin.
     *
     * This exposes the argv WITHOUT actually launching node, so tests can assert
     * the exact token list without a live process (plan acceptance criterion).
     */
    [[nodiscard]] QStringList lastNodeArgvForTesting(QString const& uuid) const;

    /**
     * @brief Test seam: return the child process environment built by buildChildEnv().
     *
     * Exposes the allowlist QProcessEnvironment that spawn() sets on every child
     * QProcess. Tests use this to assert:
     *   - Known-safe keys (PATH, HOME) are present when set in the host env.
     *   - Banned keys (DBUS_SESSION_BUS_ADDRESS, *_TOKEN, *_SECRET, XDG_RUNTIME_DIR) are absent.
     *
     * This is a pure accessor — it does not start any process.
     */
    [[nodiscard]] static QProcessEnvironment buildChildEnvironmentForTesting();

signals:
    /**
     * @brief Emitted when a plugin is permanently disabled (3-in-30s crash or node absent).
     * @param uuid    Plugin UUID.
     * @param reason  Human-readable reason string.
     */
    void pluginDisabled(QString const& uuid, QString const& reason);

private:
    /// Resolve the platform-appropriate CodePath from the manifest.
    [[nodiscard]] static QString resolveCodePath(PluginManifest const& manifest);

    /// Build the JSON string passed as the `-info` argv to spawned plugins.
    /// Shape: `{application:{version,platform},devicePixelRatio:1,devices:[]}`.
    /// Minimal but correct per 18-RESEARCH.md A2; real-device envelope pinned in Phase 25.
    [[nodiscard]] static QString buildInfoJson();

    QString m_pluginsDir;
    SdPluginServer* m_server; ///< Non-owning. May be nullptr in test contexts.
    NodeProbe m_probe;
    PropertyInspectorController* m_piController; ///< Non-owning. May be nullptr.
    std::function<qint64()> m_clock;

    PluginCrashTracker m_crashTracker;

    /// Live plugin state keyed by UUID.
    struct LivePlugin {
        PluginManifest manifest;
        std::unique_ptr<QProcess> process; ///< Owned; nullptr for WebEngine/HTML plugins.
        // Non-copyable due to unique_ptr; must be moved.
        LivePlugin() = default;
        LivePlugin(LivePlugin&&) = default;
        LivePlugin& operator=(LivePlugin&&) = default;
        LivePlugin(LivePlugin const&) = delete;
        LivePlugin& operator=(LivePlugin const&) = delete;

        LivePlugin(PluginManifest m, std::unique_ptr<QProcess> p)
            : manifest(std::move(m)), process(std::move(p)) {}
    };
    // std::map supports move-only value types via emplace/insert(std::move(...)).
    // QHash requires copyable values; std::map does not.
    std::map<QString, LivePlugin> m_live;

    /// UUIDs that have been permanently disabled.
    QHash<QString, QString> m_disabled; ///< uuid -> reason

    /// Last buildNodeArgv result per uuid, stored for test assertions.
    QHash<QString, QStringList> m_lastNodeArgv;

#if defined(AJAZZ_HAVE_WEBENGINE)
    /// Headless Chromium hosting for HTML plugins (CodePath = *.html). The
    /// shared profile carries the Mirabox compat shim; one page per HTML
    /// plugin loads its index.html and is invoked with
    /// connectElgatoStreamDeckSocket() so it registers over the loopback WS
    /// exactly like a Node plugin. Profile declared first so it outlives the
    /// pages (reverse-of-declaration destruction). Both owned solely by these
    /// unique_ptrs (no QObject parent) to avoid double-delete.
    std::unique_ptr<QWebEngineProfile> m_htmlProfile;
    std::vector<std::unique_ptr<QWebEnginePage>> m_htmlPages;
#endif
};

} // namespace ajazz::app
