// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file opendeck_catalog_fetcher.hpp
 * @brief Live mirror of the OpenDeck (archived Elgato Stream Deck)
 *        plugin catalogue.
 *
 * Sister of @ref StreamdockCatalogFetcher. OpenDeck does not run its
 * own catalogue API — instead, the community has scraped the archived
 * Elgato App Store into a static JSON file at
 * `https://plugins.amankhanna.me/catalogue.json`. The file is a flat
 * JSON array of plugin entries, each shaped roughly as:
 *
 * ```json
 * {
 *   "id": "com.barraider.soundpad",
 *   "name": "Soundpad Integration",
 *   "author": "BarRaider",
 *   "link": "https://barraider.github.io/",
 *   "version": "1.6",
 *   "download": "https://appstore.elgato.com/.../com.barraider.soundpad.streamDeckPlugin",
 *   "icon": "https://appstore.elgato.com/.../com.barraider.soundpad.png"
 * }
 * ```
 *
 * `download` and `icon` may be missing on entries the original Elgato
 * App Store entry no longer publishes. `link` is also optional.
 *
 * The fetcher resolves rows in three layers, identical in shape to
 * the Streamdock backend:
 *
 *   1. @b Live: the upstream catalogue URL (configurable via
 *      `ACC_OPENDECK_CATALOG_URL`); a single GET with a 10 s timeout.
 *   2. @b Cached: the previous successful snapshot, written atomically
 *      to `<XDG_CACHE_HOME>/ajazz-control-center/opendeck-catalog.json`.
 *   3. @b Fallback: a small bundled fixture compiled into the binary
 *      at `qrc:/qt/qml/AjazzControlCenter/opendeck-fallback.json` so
 *      the OpenDeck tab is never empty in dev / CI / air-gapped
 *      builds.
 *
 * The translated rows carry `source = "opendeck"` and
 * `compatibility = "opendeck"` so the Plugin Store source filter can
 * scope to them, and so the future loader can route them through the
 * Stream-Deck-SDK-2 compat shim.
 *
 * @note Network I/O happens on the Qt main thread via the asynchronous
 *       @c QNetworkAccessManager API. The fetcher emits @ref completed
 *       on the same thread.
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
 * @class OpenDeckCatalogFetcher
 * @brief Mirrors the OpenDeck plugin catalogue into @ref CatalogEntry rows.
 *
 * Lifetime: owned by @ref PluginCatalogModel; created on the Qt main
 * thread and destroyed before the QML engine to avoid stranded
 * @c QNetworkReply callbacks.
 */
class OpenDeckCatalogFetcher : public QObject {
    Q_OBJECT

public:
    /// Last-known fetch state, used by the model to drive QML banners.
    enum class State {
        Idle,    ///< Constructed but not yet started.
        Loading, ///< A live fetch is in flight.
        Online,  ///< Last successful source was the upstream URL.
        Cached,  ///< Last successful source was the on-disk mirror.
        Offline, ///< Both upstream and cache failed; serving the bundled fallback.
    };
    Q_ENUM(State)

    /// A successful catalogue snapshot ready for the model.
    struct Snapshot {
        std::vector<CatalogEntry> rows;
        State state = State::Idle;
        qint64 fetchedAtUnixMs = 0;
        QString sourceUrl;
    };

    explicit OpenDeckCatalogFetcher(QObject* parent = nullptr);
    ~OpenDeckCatalogFetcher() override;

    /// Override the cache directory. Must be called before @ref refresh().
    void setCacheDirOverride(QString const& dir);

    /// Override the upstream catalogue URL. Setting to the literal
    /// `"disabled"` skips the live fetch entirely (offline / CI builds).
    void setCatalogUrlOverride(QString const& url);

    /// Default upstream URL when no override is in effect.
    [[nodiscard]] static QUrl defaultCatalogUrl();

    /// Path of the on-disk mirror file currently in use.
    [[nodiscard]] QString cacheFilePath() const;

    /**
     * @brief Translate a raw upstream JSON document into @ref CatalogEntry rows.
     *
     * Accepts both the live HTTP response shape (a flat JSON array of
     * plugin objects) and the cached snapshot shape
     * `{rows: [...], fetchedAtUnixMs, sourceUrl, state}` written by
     * the fetcher itself.
     *
     * Pure function — no I/O. Exposed as `static` so tests can drive
     * it with hand-crafted JSON.
     */
    [[nodiscard]] static std::vector<CatalogEntry> parseUpstreamJson(QByteArray const& json,
                                                                     QUrl const& origin = {});

    /// Last-known state. Updated on every fetch attempt.
    [[nodiscard]] State state() const noexcept { return m_state; }

    /// Read the bundled offline fixture, translated to @ref CatalogEntry rows.
    [[nodiscard]] static Snapshot loadBundledFallback();

    /// Read the on-disk mirror, if any. Returns an empty snapshot on miss.
    [[nodiscard]] Snapshot loadFromCache() const;

public slots:
    /**
     * @brief Kick off a refresh.
     *
     * Emits @ref snapshotReady at most twice per call: once with the
     * cache or bundled fallback, then again on successful live fetch.
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
