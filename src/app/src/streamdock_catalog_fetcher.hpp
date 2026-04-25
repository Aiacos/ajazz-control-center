// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file streamdock_catalog_fetcher.hpp
 * @brief Live mirror of the upstream AJAZZ Streamdock plugin catalogue.
 *
 * The fetcher is the bridge between the official AJAZZ Streamdock
 * (\"Space\") plugin store and the in-app Plugin Store. It owns the HTTP
 * pagination loop, parses each page's plugin records, translates them
 * into the in-app @ref ajazz::app::CatalogEntry shape, and writes the
 * resulting snapshot to a JSON mirror on disk so the app can render the
 * AJAZZ Streamdock tab without a network round-trip on subsequent
 * launches.
 *
 * The fetcher resolves rows in three layers, each strictly more recent
 * than the previous:
 *
 *   1. @b Live: the upstream catalogue URL (configurable via
 *      `ACC_STREAMDOCK_CATALOG_URL`); pages are POSTed with a 10 s
 *      timeout and accumulated until @c totalPage is exhausted.
 *   2. @b Cached: the previous successful snapshot, written atomically
 *      to `<XDG_CACHE_HOME>/ajazz-control-center/streamdock-catalog.json`.
 *   3. @b Fallback: a small bundled fixture compiled into the binary at
 *      `qrc:/qt/qml/AjazzControlCenter/streamdock-fallback.json` so the
 *      AJAZZ Streamdock tab is never empty in dev / CI / air-gapped
 *      builds.
 *
 * The fetcher is intentionally decoupled from @ref PluginCatalogModel so
 * the model can also be exercised against pre-canned JSON snapshots in
 * unit tests without spinning up Qt's network stack.
 *
 * @note Network I/O happens on the Qt main thread via the asynchronous
 *       @c QNetworkAccessManager API. The fetcher emits @ref completed
 *       on the same thread.
 *
 * @see docs/architecture/PLUGIN-SDK.md "Upstream Streamdock Space catalogue".
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
 * @class StreamdockCatalogFetcher
 * @brief Mirrors the upstream Streamdock Space catalogue into @ref CatalogEntry rows.
 *
 * Lifetime: owned by @ref PluginCatalogModel; created on the Qt main
 * thread and destroyed before the QML engine to avoid stranded
 * @c QNetworkReply callbacks.
 */
class StreamdockCatalogFetcher : public QObject {
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

    /**
     * @brief A successful catalogue snapshot ready for the model.
     *
     * Carries the translated rows alongside metadata required by the
     * QML banner (snapshot age, origin, catalogue URL).
     */
    struct Snapshot {
        std::vector<CatalogEntry> rows; ///< Translated rows, may be empty.
        State state = State::Idle;      ///< Source layer that produced @ref rows.
        qint64 fetchedAtUnixMs = 0;     ///< When @ref rows were obtained.
        QString sourceUrl;              ///< Upstream URL or local path of the snapshot.
    };

    explicit StreamdockCatalogFetcher(QObject* parent = nullptr);
    ~StreamdockCatalogFetcher() override;

    /**
     * @brief Override the cache directory. Must be called before @ref refresh().
     *
     * Used by tests to point the fetcher at a tempdir; production callers
     * leave it unset and the fetcher uses
     * `<QStandardPaths::CacheLocation>/streamdock-catalog.json`.
     */
    void setCacheDirOverride(QString const& dir);

    /**
     * @brief Override the upstream catalogue URL.
     *
     * If unset (or empty), `ACC_STREAMDOCK_CATALOG_URL` is consulted, and
     * we finally fall back to the public Space endpoint. Setting the URL
     * to the literal string `"disabled"` skips the live fetch entirely
     * \u2014 useful for offline-only builds and CI sandboxes.
     */
    void setCatalogUrlOverride(QString const& url);

    /// Default upstream URL when no override is in effect.
    [[nodiscard]] static QUrl defaultCatalogUrl();

    /// Path of the on-disk mirror file currently in use.
    [[nodiscard]] QString cacheFilePath() const;

    /**
     * @brief Translate a raw upstream JSON document into @ref CatalogEntry rows.
     *
     * Accepts both the live HTTP response shape (an envelope of
     * `{code, data: {list: [...]}}`) and the cached snapshot shape
     * `{rows: [...], fetchedAtUnixMs, sourceUrl, state}` written by the
     * fetcher itself.
     *
     * Pure function \u2014 no I/O. Exposed as `static` so tests can drive it
     * with hand-crafted JSON without instantiating the Qt network stack.
     *
     * @param json   Raw bytes of a JSON document.
     * @param origin Optional URL recorded on each row's @c streamdockProductId
     *               trail; defaults to the public catalogue URL.
     * @return Translated rows; empty when @p json is malformed.
     */
    [[nodiscard]] static std::vector<CatalogEntry> parseUpstreamJson(QByteArray const& json,
                                                                     QUrl const& origin = {});

    /// Helper used by @ref parseUpstreamJson; exposed for unit tests.
    [[nodiscard]] static QStringList mapUpstreamDevices(QStringList const& upstreamDeviceUuids);

    /// Helper used by @ref parseUpstreamJson; exposed for unit tests.
    [[nodiscard]] static QString humaniseSize(double bytes);

    /// Helper used by @ref parseUpstreamJson; exposed for unit tests.
    [[nodiscard]] static QString deriveUuid(QString const& upstreamId, QString const& name);

    /// Last-known state. Updated on every fetch attempt.
    [[nodiscard]] State state() const noexcept { return m_state; }

    /**
     * @brief Read the bundled offline fixture, translated to @ref CatalogEntry rows.
     *
     * Always succeeds: the fixture is compiled into the binary as a Qt
     * resource so the AJAZZ Streamdock tab is never empty.
     */
    [[nodiscard]] static Snapshot loadBundledFallback();

    /// Read the on-disk mirror, if any. Returns an empty snapshot on miss.
    [[nodiscard]] Snapshot loadFromCache() const;

public slots:
    /**
     * @brief Kick off a refresh.
     *
     * The fetcher emits @ref snapshotReady at most twice per call:
     *   * once with the cache or the bundled fallback so the UI has
     *     something to render immediately;
     *   * a second time when the live fetch completes successfully.
     *
     * If the live fetch fails (timeout, HTTP error, JSON malformed) only
     * the first emission happens and @ref state remains @c Cached or
     * @c Offline.
     */
    void refresh();

signals:
    /// Emitted whenever a new snapshot is available; @p snapshot is by value.
    void snapshotReady(Snapshot snapshot);
    /// Emitted whenever @ref state changes; the new value is @p state.
    void stateChanged(State state);

private:
    /// Persist a successful live snapshot to disk; non-fatal on failure.
    void writeCache(std::vector<CatalogEntry> const& rows, QUrl const& origin);

    /// Resolve the catalogue URL honouring the override / env / default chain.
    [[nodiscard]] QUrl effectiveCatalogUrl() const;

    /// POST the next page of the catalogue and chain on success.
    void fetchPage(int pageNum);

    /// Handle a finished page reply; collects rows or aborts the chain.
    void onPageFinished(QNetworkReply* reply);

    /// Emit @ref snapshotReady and update state accordingly.
    void emitSnapshot(Snapshot snapshot);

    QNetworkAccessManager* m_name = nullptr;         ///< Lazily created on first refresh.
    QString m_cacheDirOverride;                     ///< Empty = use QStandardPaths::CacheLocation.
    QString m_catalogUrlOverride;                   ///< Empty = use env / default.
    State m_state = State::Idle;                    ///< Last-known fetch state.
    std::vector<CatalogEntry> m_accumulated;        ///< Rows accumulated across paged fetches.
    int m_inFlightTotalPages = 0;                   ///< Reported by upstream after page 1.
    static constexpr int kPageSize = 50;            ///< Matches the upstream UI's pageSize.
    static constexpr int kPerPageTimeoutMs = 10000; ///< Hard cap per page before we abort.
    static constexpr int kMaxPages = 20;            ///< Safety net (= 1000 plugins).
};

} // namespace ajazz::app
