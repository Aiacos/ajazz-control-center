// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file opendeck_catalog_fetcher.cpp
 * @brief Implementation of @ref ajazz::app::OpenDeckCatalogFetcher.
 *
 * Single GET to `https://plugins.amankhanna.me/catalogue.json`,
 * parse a flat JSON array of plugin entries, translate each one to a
 * @ref CatalogEntry with `source = "opendeck"` and
 * `compatibility = "opendeck"`. Three-layer resolution mirrors the
 * Streamdock fetcher (live → cache → bundled fallback) so the
 * OpenDeck tab is never empty.
 */
#include "opendeck_catalog_fetcher.hpp"

#include <QByteArray>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

#include <optional>

namespace ajazz::app {

namespace {

constexpr char kDefaultCatalogUrl[] = "https://plugins.amankhanna.me/catalogue.json";
constexpr char kEnvOverride[] = "ACC_OPENDECK_CATALOG_URL";
constexpr char kCacheFileName[] = "opendeck-catalog.json";
constexpr char kBundledFallbackPath[] = ":/qt/qml/AjazzControlCenter/opendeck-fallback.json";

/// Translate a single upstream entry to @ref CatalogEntry. Required
/// fields: `id` and `name`. Everything else is best-effort and the
/// missing-field defaults match what the rendering layer expects
/// (empty strings, `null`-ish QUrls). Returns @c std::nullopt when
/// the entry is too broken to render.
std::optional<CatalogEntry> translateEntry(QJsonObject const& obj) {
    QString const id = obj.value(QStringLiteral("id")).toString();
    QString const name = obj.value(QStringLiteral("name")).toString();
    if (id.isEmpty() || name.isEmpty()) {
        return std::nullopt;
    }

    CatalogEntry entry;
    entry.uuid = id;
    entry.name = name;
    entry.version = obj.value(QStringLiteral("version")).toString();
    entry.author = obj.value(QStringLiteral("author")).toString();
    // The upstream JSON has no description field. Synthesise a short
    // one-liner from author/link so the tile body is not empty.
    QString const link = obj.value(QStringLiteral("link")).toString();
    entry.description = link.isEmpty()
                            ? QStringLiteral("Stream Deck plugin from the OpenDeck archive.")
                            : QStringLiteral("Stream Deck plugin from %1.").arg(link);
    QString const iconUrl = obj.value(QStringLiteral("icon")).toString();
    if (!iconUrl.isEmpty()) {
        entry.iconUrl = QUrl(iconUrl);
    }
    entry.compatibility = QStringLiteral("opendeck");
    entry.source = QStringLiteral("opendeck");
    // OpenDeck plugins target Stream Deck families generically — we
    // do not have a per-plugin compatibility list from upstream, so
    // leave `devices` empty and let the install-time hardware check
    // (slice 4) catch incompatibilities.
    entry.tags = {QStringLiteral("stream-deck"), QStringLiteral("opendeck")};
    entry.verified = false; // OpenDeck archive has no Sigstore bundle.
    return entry;
}

/// Parse the cached snapshot envelope `{rows: [...]}`. Returns the
/// embedded rows verbatim — the cache file already stores translated
/// `CatalogEntry`-shaped objects, not the upstream wire format.
std::vector<CatalogEntry> parseSnapshotEnvelope(QJsonObject const& envelope) {
    std::vector<CatalogEntry> out;
    QJsonArray const rows = envelope.value(QStringLiteral("rows")).toArray();
    out.reserve(static_cast<std::size_t>(rows.size()));
    for (auto const& v : rows) {
        QJsonObject const r = v.toObject();
        CatalogEntry e;
        e.uuid = r.value(QStringLiteral("uuid")).toString();
        e.name = r.value(QStringLiteral("name")).toString();
        e.version = r.value(QStringLiteral("version")).toString();
        e.author = r.value(QStringLiteral("author")).toString();
        e.description = r.value(QStringLiteral("description")).toString();
        QString const iconUrl = r.value(QStringLiteral("iconUrl")).toString();
        if (!iconUrl.isEmpty()) {
            e.iconUrl = QUrl(iconUrl);
        }
        e.compatibility = r.value(QStringLiteral("compatibility")).toString();
        e.source = r.value(QStringLiteral("source")).toString();
        // tags / devices: optional in the cache shape; default empty.
        for (auto const& t : r.value(QStringLiteral("tags")).toArray()) {
            e.tags.append(t.toString());
        }
        if (e.uuid.isEmpty() || e.name.isEmpty()) {
            continue;
        }
        out.push_back(std::move(e));
    }
    return out;
}

/// Encode a CatalogEntry into the cache-snapshot row shape. Inverse
/// of @ref parseSnapshotEnvelope's row decoder; both must agree on
/// the keys.
QJsonObject encodeRow(CatalogEntry const& e) {
    QJsonObject obj;
    obj.insert(QStringLiteral("uuid"), e.uuid);
    obj.insert(QStringLiteral("name"), e.name);
    obj.insert(QStringLiteral("version"), e.version);
    obj.insert(QStringLiteral("author"), e.author);
    obj.insert(QStringLiteral("description"), e.description);
    obj.insert(QStringLiteral("iconUrl"), e.iconUrl.toString());
    obj.insert(QStringLiteral("compatibility"), e.compatibility);
    obj.insert(QStringLiteral("source"), e.source);
    QJsonArray tags;
    for (QString const& t : e.tags) {
        tags.append(t);
    }
    obj.insert(QStringLiteral("tags"), tags);
    return obj;
}

} // namespace

QUrl OpenDeckCatalogFetcher::defaultCatalogUrl() {
    return QUrl(QString::fromLatin1(kDefaultCatalogUrl));
}

OpenDeckCatalogFetcher::OpenDeckCatalogFetcher(QObject* parent) : QObject(parent) {}

OpenDeckCatalogFetcher::~OpenDeckCatalogFetcher() = default;

void OpenDeckCatalogFetcher::setCacheDirOverride(QString const& dir) {
    m_cacheDirOverride = dir;
}

void OpenDeckCatalogFetcher::setCatalogUrlOverride(QString const& url) {
    m_catalogUrlOverride = url;
}

QString OpenDeckCatalogFetcher::cacheFilePath() const {
    QString dir = m_cacheDirOverride;
    if (dir.isEmpty()) {
        dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    }
    if (dir.isEmpty()) {
        return {};
    }
    QDir().mkpath(dir);
    return QDir(dir).filePath(QString::fromLatin1(kCacheFileName));
}

QUrl OpenDeckCatalogFetcher::effectiveCatalogUrl() const {
    if (!m_catalogUrlOverride.isEmpty()) {
        return QUrl(m_catalogUrlOverride);
    }
    QByteArray const env = qgetenv(kEnvOverride);
    if (!env.isEmpty()) {
        return QUrl(QString::fromUtf8(env));
    }
    return defaultCatalogUrl();
}

std::vector<CatalogEntry> OpenDeckCatalogFetcher::parseUpstreamJson(QByteArray const& json,
                                                                    QUrl const& /*origin*/) {
    QJsonParseError err{};
    QJsonDocument const doc = QJsonDocument::fromJson(json, &err);
    if (err.error != QJsonParseError::NoError) {
        return {};
    }
    // Cached snapshot: object with a "rows" array. Live upstream:
    // bare JSON array of plugin entries.
    if (doc.isObject()) {
        return parseSnapshotEnvelope(doc.object());
    }
    if (!doc.isArray()) {
        return {};
    }
    std::vector<CatalogEntry> out;
    QJsonArray const arr = doc.array();
    out.reserve(static_cast<std::size_t>(arr.size()));
    for (auto const& v : arr) {
        if (!v.isObject()) {
            continue;
        }
        if (auto entry = translateEntry(v.toObject()); entry) {
            out.push_back(std::move(*entry));
        }
    }
    return out;
}

OpenDeckCatalogFetcher::Snapshot OpenDeckCatalogFetcher::loadBundledFallback() {
    Snapshot s;
    QFile f(QString::fromLatin1(kBundledFallbackPath));
    if (!f.open(QIODevice::ReadOnly)) {
        s.state = State::Offline;
        return s;
    }
    s.rows = parseUpstreamJson(f.readAll());
    s.state = State::Offline;
    s.fetchedAtUnixMs = 0;
    s.sourceUrl = QString::fromLatin1(kBundledFallbackPath);
    return s;
}

OpenDeckCatalogFetcher::Snapshot OpenDeckCatalogFetcher::loadFromCache() const {
    Snapshot s;
    QString const path = cacheFilePath();
    if (path.isEmpty()) {
        return s;
    }
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        return s;
    }
    QByteArray const bytes = f.readAll();
    QJsonParseError err{};
    QJsonDocument const doc = QJsonDocument::fromJson(bytes, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        return s;
    }
    QJsonObject const env = doc.object();
    s.rows = parseSnapshotEnvelope(env);
    if (s.rows.empty()) {
        return s;
    }
    s.fetchedAtUnixMs =
        static_cast<qint64>(env.value(QStringLiteral("fetchedAtUnixMs")).toDouble());
    s.sourceUrl = env.value(QStringLiteral("sourceUrl")).toString();
    s.state = State::Cached;
    return s;
}

void OpenDeckCatalogFetcher::writeCache(std::vector<CatalogEntry> const& rows, QUrl const& origin) {
    QString const path = cacheFilePath();
    if (path.isEmpty()) {
        return;
    }
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return;
    }
    QJsonArray rowArr;
    for (CatalogEntry const& e : rows) {
        rowArr.append(encodeRow(e));
    }
    QJsonObject env;
    env.insert(QStringLiteral("rows"), rowArr);
    env.insert(QStringLiteral("fetchedAtUnixMs"),
               static_cast<double>(QDateTime::currentMSecsSinceEpoch()));
    env.insert(QStringLiteral("sourceUrl"), origin.toString());
    env.insert(QStringLiteral("state"), QStringLiteral("online"));
    f.write(QJsonDocument(env).toJson(QJsonDocument::Compact));
    f.commit();
}

void OpenDeckCatalogFetcher::emitSnapshot(Snapshot snapshot) {
    State const previous = m_state;
    m_state = snapshot.state;
    emit snapshotReady(std::move(snapshot));
    if (m_state != previous) {
        emit stateChanged(m_state);
    }
}

void OpenDeckCatalogFetcher::refresh() {
    // 1. Always emit cache OR bundled fallback first so the UI has
    //    something to render immediately.
    Snapshot initial = loadFromCache();
    if (initial.rows.empty()) {
        initial = loadBundledFallback();
    }
    emitSnapshot(initial);

    QUrl const url = effectiveCatalogUrl();
    if (url.isEmpty() || url.toString() == QStringLiteral("disabled")) {
        return; // Live fetch disabled — keep the cache/fallback layer.
    }

    if (m_netAccessManager == nullptr) {
        m_netAccessManager = new QNetworkAccessManager(this);
    }

    State const previous = m_state;
    m_state = State::Loading;
    if (m_state != previous) {
        emit stateChanged(m_state);
    }

    QNetworkRequest req(url);
    req.setRawHeader(QByteArrayLiteral("Accept"), QByteArrayLiteral("application/json"));
    req.setTransferTimeout(kFetchTimeoutMs);

    QNetworkReply* reply = m_netAccessManager->get(req);
    QObject::connect(
        reply, &QNetworkReply::finished, this, [this, reply]() { onReplyFinished(reply); });
}

void OpenDeckCatalogFetcher::onReplyFinished(QNetworkReply* reply) {
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
        // Live fetch failed — keep whatever the initial emission
        // already gave us. We only update state to Offline if the
        // initial emission was the bundled fallback (no usable
        // cache); otherwise stay on Cached.
        if (m_state == State::Loading) {
            // We just bumped state to Loading on top of Cached/Offline;
            // restore the pre-Loading state via re-emit. Simplest is
            // to load from cache once more and emit it.
            Snapshot restored = loadFromCache();
            if (restored.rows.empty()) {
                restored = loadBundledFallback();
            }
            emitSnapshot(restored);
        }
        return;
    }
    QByteArray const bytes = reply->readAll();
    auto rows = parseUpstreamJson(bytes, reply->url());
    if (rows.empty()) {
        // Bad payload — same handling as a network failure.
        Snapshot restored = loadFromCache();
        if (restored.rows.empty()) {
            restored = loadBundledFallback();
        }
        emitSnapshot(restored);
        return;
    }
    writeCache(rows, reply->url());
    Snapshot s;
    s.rows = std::move(rows);
    s.state = State::Online;
    s.fetchedAtUnixMs = QDateTime::currentMSecsSinceEpoch();
    s.sourceUrl = reply->url().toString();
    emitSnapshot(std::move(s));
}

} // namespace ajazz::app
