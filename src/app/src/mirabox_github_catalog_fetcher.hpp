// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file mirabox_github_catalog_fetcher.hpp
 * @brief Catalogue source backed by the open MiraboxSpace/StreamDock-Plugins
 *        GitHub repository (GPL-3.0).
 *
 * The official AJAZZ/Mirabox "Space" store (space.key123.vip) lists ~469
 * plugins but its `download` field is a RELATIVE path resolvable only by the
 * proprietary desktop app's embedded CDN base — so those rows are NOT
 * installable in-app (confirmed 2026-06-22; see the plugin-install memory). The
 * GitHub repo @c github.com/MiraboxSpace/StreamDock-Plugins is the open,
 * license-clean alternative: ~20 real plugin bundles as SOURCE DIRECTORIES
 * under @c Plugins/, each with public, fetchable GitHub URLs.
 *
 * This fetcher mirrors @ref OpenDeckCatalogFetcher's three-layer resolution
 * (live → cached → bundled-empty) but its "live" source is the GitHub Git
 * Trees API (one recursive call), from which it derives one @ref CatalogEntry
 * per immediate child directory of @c Plugins/. Rows carry
 * @c source = "mirabox-github" so the Plugin Store source filter can scope to
 * them, and a GitHub directory URL in @c downloadUrl that the install path
 * recognises (it fetches the subtree's files and assembles the @c .sdPlugin
 * bundle, rather than downloading a single archive — GitHub serves no
 * per-subdirectory archive).
 *
 * @note Network I/O happens on the Qt main thread via the asynchronous
 *       @c QNetworkAccessManager API. The fetcher emits @ref snapshotReady on
 *       the same thread. ZERO dependency on the proprietary Space CDN.
 */
#pragma once

#include "plugin_catalog_model.hpp"

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QUrl>

#include <vector>

class QNetworkAccessManager;
class QNetworkReply;

namespace ajazz::app {

/**
 * @class MiraboxGithubCatalogFetcher
 * @brief Mirrors the MiraboxSpace/StreamDock-Plugins GitHub repo into
 *        @ref CatalogEntry rows.
 *
 * Lifetime: owned by @ref PluginCatalogModel; created on the Qt main thread
 * and destroyed before the QML engine to avoid stranded @c QNetworkReply
 * callbacks.
 */
class MiraboxGithubCatalogFetcher : public QObject {
    Q_OBJECT

public:
    /// Last-known fetch state, used by the model to drive QML banners.
    enum class State {
        Idle,    ///< Constructed but not yet started.
        Loading, ///< A live fetch is in flight.
        Online,  ///< Last successful source was the GitHub API.
        Cached,  ///< Last successful source was the on-disk mirror.
        Offline, ///< Both GitHub and cache failed; no rows.
    };
    Q_ENUM(State)

    /// A successful catalogue snapshot ready for the model.
    struct Snapshot {
        std::vector<CatalogEntry> rows;
        State state = State::Idle;
        qint64 fetchedAtUnixMs = 0;
        QString sourceUrl;
    };

    explicit MiraboxGithubCatalogFetcher(QObject* parent = nullptr);
    ~MiraboxGithubCatalogFetcher() override;

    /// Override the cache directory. Must be called before @ref refresh().
    void setCacheDirOverride(QString const& dir);

    /// Override the GitHub Trees API URL. Setting to the literal `"disabled"`
    /// skips the live fetch entirely (offline / CI builds).
    void setCatalogUrlOverride(QString const& url);

    /// Default GitHub Git-Trees API URL when no override is in effect.
    [[nodiscard]] static QUrl defaultCatalogUrl();

    /// GitHub owner/repo/branch the rows are sourced from (used to build the
    /// per-plugin directory URLs the install path consumes).
    [[nodiscard]] static QString repoOwnerRepo();
    [[nodiscard]] static QString repoBranch();
    [[nodiscard]] static QString pluginsRoot(); ///< Top-level dir holding plugins ("Plugins").

    /// Path of the on-disk mirror file currently in use.
    [[nodiscard]] QString cacheFilePath() const;

    /**
     * @brief Translate a raw upstream document into @ref CatalogEntry rows.
     *
     * Accepts BOTH:
     *   1. the live GitHub Git-Trees response shape
     *      (`{ "tree": [ { "path": "...", "type": "blob|tree" }, ... ] }`), from
     *      which it derives one row per immediate child dir of @c Plugins/; and
     *   2. the cached snapshot shape `{rows:[...], fetchedAtUnixMs, sourceUrl}`
     *      written by the fetcher itself.
     *
     * Pure function — no I/O. Exposed as `static` so tests can drive it with
     * hand-crafted JSON without instantiating the Qt network stack.
     */
    [[nodiscard]] static std::vector<CatalogEntry> parseUpstreamJson(QByteArray const& json);

    /// Derive a human display name from a repo directory name (e.g.
    /// "com.mirabox.streamdock.worldWeather.sdPlugin" → "World Weather";
    /// "WorldWeather" → "World Weather"). Exposed for unit tests.
    [[nodiscard]] static QString humaniseDirName(QString const& dirName);

    /// Last-known state. Updated on every fetch attempt.
    [[nodiscard]] State state() const noexcept { return m_state; }

    /// Read the on-disk mirror, if any. Returns an empty snapshot on miss.
    [[nodiscard]] Snapshot loadFromCache() const;

public slots:
    /**
     * @brief Kick off a refresh.
     *
     * Emits @ref snapshotReady at most twice per call: once with the cache (if
     * any), then again on a successful live fetch.
     */
    void refresh();

signals:
    /// Emitted whenever a new snapshot is available.
    void snapshotReady(Snapshot snapshot);
    /// Emitted whenever @ref state changes.
    void stateChanged(State state);

private:
    void writeCache(std::vector<CatalogEntry> const& rows, QUrl const& origin);
    [[nodiscard]] QUrl effectiveCatalogUrl() const;
    void onReplyFinished(QNetworkReply* reply);
    void emitSnapshot(Snapshot snapshot);

    QNetworkAccessManager* m_netAccessManager = nullptr;
    QString m_cacheDirOverride;
    QString m_catalogUrlOverride;
    State m_state = State::Idle;
    static constexpr int kFetchTimeoutMs = 10000;
};

} // namespace ajazz::app
