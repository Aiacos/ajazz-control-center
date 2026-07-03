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
 *   - `.html`/`.htm`      -> headless WebEngine hosting via a private `QWebEnginePage`
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

#include "i_plugin_host2.hpp"
#include "node_runner.hpp"
#include "plugin_crash_tracker.hpp"
#include "plugin_manifest.hpp"

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>

#include <functional>
#include <map>
#include <memory>
#include <utility>
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

/**
 * @brief Orchestrates plugin discovery, spawn, crash lifecycle, and shutdown.
 *
 * Implements @ref IPluginHost2 as the .sdPlugin sub-host (Node/HTML/native over
 * WebSocket). Device I/O from plugins must flow ONLY through PluginDeviceBridge —
 * this class has zero SKU-specific branches in dispatch.
 *
 * Constructed with injectable dependencies so tests can exercise the logic
 * without a real node binary, real server port, or real process.
 */
class PluginManager : public QObject, public IPluginHost2 {
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
     * @param clock           Injectable millisecond clock. Defaults to
     *                        `QDateTime::currentMSecsSinceEpoch`. Tests inject a
     *                        lambda returning synthetic values.
     * @param parent          QObject parent (optional).
     */
    explicit PluginManager(QString const& pluginsDir,
                           SdPluginServer* server,
                           NodeProbe probe = {},
                           std::function<qint64()> clock = {},
                           QObject* parent = nullptr);

    ~PluginManager() override;

    // ---- IPluginHost2 overrides (the .sdPlugin sub-host) -------------------
    //
    // discover() and spawn() already exist as the primary public API below.
    // These overrides provide IPluginHost2 implementations delegating to them.
    // dispatch() handles ONLY the .sdPlugin WebSocket action path — Python routing
    // lives in UnifiedPluginHost, NOT here.
    // plugins() returns ONLY the .sdPlugin inventory — Python folding is in the aggregator.

    /**
     * @brief IPluginHost2::dispatch — routes a .sdPlugin action via SdPluginServer::sendEvent.
     *
     * Looks up the plugin in m_live to confirm it is registered, then sends the event
     * JSON over the WebSocket. This is the .sdPlugin-only dispatch path; Python UUIDs
     * must be handled by UnifiedPluginHost before reaching here.
     */
    bool dispatch(QString const& pluginUuid,
                  QString const& actionId,
                  QJsonObject const& payload) override;

    /**
     * @brief IPluginHost2::plugins — returns the .sdPlugin-only inventory.
     *
     * Converts the m_live manifest entries to PluginInfo objects. Python plugins are
     * NOT included — the aggregator (UnifiedPluginHost) folds both inventories.
     */
    [[nodiscard]] std::vector<plugins::PluginInfo> plugins() override;

    /**
     * @brief IPluginHost2::connectedPluginCount — delegates to SdPluginServer.
     */
    [[nodiscard]] int connectedPluginCount() const noexcept override;

    /**
     * @brief IPluginHost2::pluginServer — returns the injected SdPluginServer.
     */
    [[nodiscard]] SdPluginServer* pluginServer() const noexcept override { return m_server; }

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
    [[nodiscard]] std::vector<PluginManifest> discover() override;

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
    void spawn(PluginManifest const& manifest) override;

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
     * This slot runs DURING the QProcess signal emission, so it must not destroy
     * the QProcess synchronously (erasing m_live would delete the QProcess from
     * within its own finished() handler — a use-after-free that crashes the host
     * when a plugin is SIGKILLed). It records the crash and defers the actual
     * teardown/respawn to handleProcessFailure() via the event loop.
     *
     * @param uuid  Plugin UUID of the failed process.
     */
    void onProcessFailed(QString const& uuid);

    /**
     * @brief Deferred teardown/respawn for a failed plugin (off the signal stack).
     *
     * Invoked via a 0ms single-shot timer from onProcessFailed() so the QProcess
     * is no longer on the call stack when m_live erases (and thus deletes) it.
     * Applies the 3-in-30s disable policy, else tears down + re-spawns. Idempotent
     * via m_failurePending so a double-fire schedules a single handling.
     *
     * @param uuid  Plugin UUID of the failed process.
     */
    void handleProcessFailure(QString const& uuid);

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
    void shutdown() override;

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

    /// @return true if @p uuid is in the session crash-disabled set (m_disabled).
    [[nodiscard]] bool isDisabled(QString const& uuid) const;

    /**
     * @brief Persist the user intent to enable or disable a plugin.
     *
     * **User-disable** (enabled == false):
     *   - Writes `plugins/disabled/<pluginId> = true` to QSettings.
     *   - Tears down the live plugin if it is currently running (sends
     *     `exitApp`, terminates process, erases from `m_live`).
     *   - Distinct from the session-only crash-disable (`m_disabled` /
     *     `disableWithNotice`): a user-disable survives app restart; a
     *     crash-disable does not.
     *
     * **User-enable** (enabled == true):
     *   - Removes `plugins/disabled/<pluginId>` from QSettings.
     *   - If the plugin's manifest is discoverable and it is NOT already
     *     live, calls `spawn()` to start it immediately.
     *
     * @param pluginId  The `.sdPlugin` directory-name key used in `m_live`
     *                  (e.g. `"com.example.myplugin.sdPlugin"`).
     * @param enabled   true = enable (clear persisted flag); false = disable
     *                  (persist flag + tear down).
     */
    void setPluginEnabled(QString const& pluginId, bool enabled);

    /**
     * @brief Tear down a live plugin (exitApp -> terminate -> kill -> forget)
     *        WITHOUT persisting any enable/disable intent.
     *
     * The uninstall/update seam: `remove_plugin` used to delete the on-disk dir
     * while the process (or HTML page) kept running and painting, and the stale
     * `m_live` key made rediscover() skip a re-install forever (audit 3.1/3.2).
     * Wire this before dir deletion/replacement; a subsequent rediscover()
     * respawns from the fresh dir. No-op for an unknown/not-live @p pluginId.
     *
     * @param pluginId  The `.sdPlugin` install-dir name (the m_live key).
     */
    void unloadPlugin(QString const& pluginId);

    /// Full Elgato `-info` JSON for a LIVE plugin (by its install-dir key or
    /// registration PUUID) — the same envelope spawn() hands the plugin child.
    /// Backs the SPA's make_info (PI bootstrap): stock Elgato PI libs read
    /// info.application.* before registering, and an empty object made them
    /// throw (audit 5.3). Returns "" when the plugin is not live.
    [[nodiscard]] QString infoJsonForPlugin(QString const& pluginId) const;

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
     * @brief Test/inspection seam (B6): number of live HTML plugin pages.
     *
     * Returns 0 on builds without WebEngine. Used to assert that a disabled or
     * shut-down plugin's QWebEnginePage is destroyed rather than leaked for the
     * app lifetime.
     */
    [[nodiscard]] std::size_t htmlPageCountForTesting() const noexcept;

    /**
     * @brief Test seam: insert a synthetic live-plugin entry for @p uuid WITHOUT spawning.
     *
     * Inserts `{PluginManifest{}, nullptr}` into `m_live` under @p uuid so that
     * `onProcessFailed` treats the UUID as a registered plugin and applies the normal
     * crash-window logic (rather than the pre-registration early-exit guard).
     *
     * Use this in Catch2 test cases that need to verify the 3-in-30s crash-disable
     * behaviour without launching a real node/native process.
     *
     * HOST-02 context: the `m_live.find` guard in `onProcessFailed` distinguishes a
     * registered-plugin crash (UUID in m_live → apply crash window) from a
     * pre-registration exit (UUID absent → skip crash credit). Tests that simulate
     * post-registration crashes MUST call this before calling `onProcessFailed`.
     *
     * @warning Do NOT use in production code.  Call only from unit tests.
     */
    void seedLiveForTest(QString const& uuid);

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

    /**
     * @brief Resolve the absolute image path for an action's manifest state.
     *
     * Looks across the live plugins for the manifest action whose `uuid` matches
     * @p actionUuid, then returns the `States[stateIndex].Image` entry resolved
     * to an absolute path against the plugin's `sourceDir`. Used by
     * PluginDeviceBridge to auto-render the declared state image on `setState`
     * (so a multi-state action changes its key image without the plugin having
     * to push a `setImage` itself).
     *
     * Image entries may carry an extension or omit it (Elgato allows both); when
     * the path as-declared does not exist, common raster extensions are probed.
     *
     * @param actionUuid  Dotted action id, e.g. com.vendor.plugin.action.
     * @param stateIndex  0-based state index (out-of-range yields "").
     * @return            Absolute image path, or "" if unresolved.
     */
    [[nodiscard]] QString stateImagePath(QString const& actionUuid, int stateIndex) const;

    /**
     * @brief Manifest-declared default Settings for an action, as compact JSON.
     *
     * MiraBox manifests may carry a per-action `Settings` object holding the
     * complete default configuration the vendor host seeds a NEW instance with.
     * SDVueSDK draw code dereferences these keys unguarded (e.g.
     * `settings.checkboxGroup.includes(...)` in timeClock), so an instance that
     * appears with empty settings throws before its first setImage. The bridge
     * uses this as the last fallback when composing willAppear settings.
     *
     * @param actionUuid  Dotted action id, e.g. com.vendor.plugin.action.
     * @return            Compact JSON object string, or "" when the manifest
     *                    declares no Settings (Elgato manifests never do).
     */
    [[nodiscard]] QString defaultSettingsForAction(QString const& actionUuid) const;

    /**
     * @brief State metadata for an action: {state count, DisableAutomaticStates}.
     *
     * Drives the host-side automatic state cycle (Elgato/OpenDeck keyUp
     * semantics: a 2-state action advances state on keyUp unless the manifest
     * sets DisableAutomaticStates). Unknown action -> {0, false}, which callers
     * treat as "no automatic cycling".
     *
     * @param actionUuid  Dotted action id, e.g. com.vendor.plugin.action.
     * @return            {stateCount, disableAutomaticStates}.
     */
    [[nodiscard]] std::pair<int, bool> actionStateMeta(QString const& actionUuid) const;

    /**
     * @brief Dial layout metadata for an action: {Encoder.layout id, icon path}.
     *
     * The icon resolves Encoder.Icon (falling back to the action Icon) against
     * the plugin dir, probing the usual Elgato extension-less variants. Feeds
     * the bridge's built-in dial layout renderer at mount time.
     *
     * @param actionUuid  Dotted action id.
     * @return            {layout id ("" -> $X1 default), absolute icon path or ""}.
     */
    [[nodiscard]] std::pair<QString, QString> encoderLayoutInfo(QString const& actionUuid) const;

    /**
     * @brief Resolve the owning plugin UUID for an action UUID (stored-owner map).
     *
     * Scans the live plugins for the manifest action whose `uuid` matches
     * @p actionUuid and returns the SAME identity the plugin was spawned with as
     * its `-pluginUUID` argument (PUUID when present, else the `.sdPlugin`
     * directory-name key in `m_live`). This is the OpenDeck stored-owner model
     * (`action.plugin` stamped at load): the bridge consults this instead of the
     * brittle dotted-prefix match, so an action UUID need NOT be a dotted prefix
     * of the plugin UUID (the silent-no-willAppear trap, GAP-PLUGIN-OWNER).
     *
     * @param actionUuid  Dotted action id, e.g. com.vendor.plugin.action.
     * @return            Owning plugin UUID, or "" if no live plugin declares it.
     */
    [[nodiscard]] QString ownerForAction(QString const& actionUuid) const;

    /**
     * @brief Whether a registered plugin asked to be told about @p appId (WR-02).
     *
     * Honors each plugin's manifest @c ApplicationsToMonitor list so the host-
     * level applicationDidLaunch/Terminate fan-out is NOT broadcast to every
     * registered plugin: an empty list means "monitor everything" (backwards-
     * compatible), a non-empty list filters delivery to the listed apps only.
     * @p pluginUuid is matched against the same -pluginUUID identity
     * registeredPlugins() carries (PUUID, else the .sdPlugin directory key).
     *
     * @param pluginUuid  Registered plugin UUID.
     * @param appId       Foreground app-identity token (normalized internally).
     * @return            true if the event should be delivered to this plugin.
     */
    [[nodiscard]] bool monitorsApplication(QString const& pluginUuid, QString const& appId) const;

    /**
     * @brief Resolve a switchToProfile token against @p pluginUuid's shipped Profiles[].
     *
     * elgato_plugin_protocol.md §1.4: a plugin may switchToProfile only to a
     * profile it ships. The plugin is resolved by the same -pluginUUID identity
     * as monitorsApplication (PUUID, else the .sdPlugin directory key); the
     * token is then matched by matchShippedProfileName (full Name or basename,
     * exact then case-insensitive).
     *
     * @param pluginUuid  Registered plugin UUID.
     * @param token       The switchToProfile "profile" payload value.
     * @return            Canonical user-visible profile name, or empty when the
     *                    plugin is unknown or ships no matching profile.
     */
    [[nodiscard]] QString shippedProfileName(QString const& pluginUuid, QString const& token) const;

    /**
     * @brief Inject the provider for the `-info.devices[]` array (F1).
     *
     * The registration `-info` payload must advertise the currently-connected
     * devices (id/name/size/type) so device-aware plugins behave; a plugin that
     * reads an empty `devices:[]` cannot target any key. PluginManager has no
     * DeviceRegistry, so Application supplies a provider that walks the connected
     * codenames and builds the Elgato deviceInfo entries (see
     * docs/protocols/streamdeck/elgato_plugin_protocol.md §2.3). Unset => empty
     * array (prior behaviour).
     *
     * @param provider  () -> QJsonArray of Elgato device-info objects.
     */
    void setDevicesInfoProvider(std::function<QJsonArray()> provider);

    /// Build the JSON string passed as the `-info` argv to spawned plugins.
    /// Full Elgato RegistrationInfo shape (F1): application{font,language,
    /// platform,platformVersion,version}, colors{}, devicePixelRatio,
    /// devices[] (from m_devicesInfoProvider), and a per-plugin plugin{uuid,
    /// version} sourced from @p manifest. Public so it can be unit-tested
    /// directly. See docs/protocols/streamdeck/elgato_plugin_protocol.md §2.3.
    [[nodiscard]] QString buildInfoJson(PluginManifest const& manifest) const;

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

    /**
     * @brief Single shared predicate consulted by both the launch loop (discover+spawn)
     *        and `rediscover()` to decide whether a plugin should be skipped at spawn
     *        time. Returns true if the plugin is in the QSettings persisted disabled-set.
     *
     * Checking ONLY the persisted user-disable (QSettings `plugins/disabled/<pluginId>`).
     * The session crash-disable (`m_disabled`) is NOT checked here — the crash path
     * has its own path via `isDisabled()`. Keeping the two predicates separate is the
     * T-27-DISABLE-LEAK mitigation: crash-disable recovers on restart; user-disable does not.
     *
     * @param pluginId  `.sdPlugin` directory-name key (e.g. "com.example.myplugin.sdPlugin").
     * @return true if the user has persisted a disabled flag for @p pluginId.
     */
    [[nodiscard]] static bool shouldSkipSpawn(QString const& pluginId);

    /// Provider for the `-info.devices[]` array, injected by Application over the
    /// DeviceRegistry (setDevicesInfoProvider). Unset => empty array.
    std::function<QJsonArray()> m_devicesInfoProvider;

    QString m_pluginsDir;
    SdPluginServer* m_server; ///< Non-owning. May be nullptr in test contexts.
    NodeProbe m_probe;
    std::function<qint64()> m_clock;

    PluginCrashTracker m_crashTracker;

    /// UUIDs with a teardown/respawn already scheduled (off the signal stack) so
    /// a QProcess double-fire (errorOccurred + finished) handles the failure once.
    QSet<QString> m_failurePending;

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
    /// B6: keyed by the plugin's registration UUID so a disabled/uninstalled
    /// plugin's page can be torn down (destroying the QWebEnginePage) instead of
    /// leaking for the app lifetime. Was a flat vector that only ever grew.
    std::map<QString, std::unique_ptr<QWebEnginePage>> m_htmlPages;
#endif
};

} // namespace ajazz::app
