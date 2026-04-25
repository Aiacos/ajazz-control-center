// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file streamdock_catalog_fetcher.cpp
 * @brief Implementation of @ref ajazz::app::StreamdockCatalogFetcher.
 *
 * The fetcher walks the upstream Streamdock Space catalogue page by
 * page, accumulates the resulting plugin records, translates them to
 * @ref CatalogEntry rows and writes the snapshot to disk so subsequent
 * launches can render the AJAZZ Streamdock tab without a network
 * round-trip. On failure it falls back to the cached mirror, then to a
 * bundled fixture.
 */
#include "streamdock_catalog_fetcher.hpp"

#include <QByteArray>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QStandardPaths>
#include <QStringList>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <cmath>

namespace ajazz::app {

namespace {

/// Public Space endpoint. Anonymous, accepts JSON POST bodies.
constexpr char kDefaultCatalogUrl[] = "https://space.key123.vip/interface/user/productInfo/list";

/// Tenant ID hard-coded in the upstream web UI; same value for every plugin.
constexpr char kTenantId[] = "10000000";

/// `productType` selecting the "plugins" facet (1=plugin, 2=icons, 5=profile, …).
constexpr char kProductTypePlugin[] = "1";

/// Environment variable that overrides the catalogue URL.
constexpr char kEnvOverride[] = "ACC_STREAMDOCK_CATALOG_URL";

/// File name of the on-disk mirror in the cache directory.
constexpr char kCacheFileName[] = "streamdock-catalog.json";

/// Filesystem-friendly slug for the deterministic UUID derivation.
QString slugify(QString const& s) {
    QString out;
    out.reserve(s.size());
    bool prevDash = false;
    for (QChar const c : s.toLower()) {
        if (c.isLetterOrNumber()) {
            out.append(c);
            prevDash = false;
        } else if (!prevDash && !out.isEmpty()) {
            out.append(QLatin1Char('-'));
            prevDash = true;
        }
    }
    while (out.endsWith(QLatin1Char('-'))) {
        out.chop(1);
    }
    return out;
}

/// First paragraph of a multi-line description, trimmed to @p maxLen characters.
QString firstParagraph(QString const& body, qsizetype maxLen = 240) {
    QString trimmed = body.trimmed();
    qsizetype const para = trimmed.indexOf(QStringLiteral("\n\n"));
    if (para > 0) {
        trimmed.truncate(para);
    } else {
        qsizetype const nl = trimmed.indexOf(QLatin1Char('\n'));
        if (nl > 0) {
            trimmed.truncate(nl);
        }
    }
    trimmed = trimmed.trimmed();
    if (trimmed.size() > maxLen) {
        trimmed.truncate(maxLen - 1);
        trimmed.append(QChar(0x2026)); // horizontal ellipsis
    }
    return trimmed;
}

/// Curated mapping from upstream `deviceUuid` strings to the AKP codenames
/// `acc` actually drives. Devices not in the table are dropped from the
/// translated `devices` list \u2014 we don't want to claim support we
/// can't deliver.
QHash<QString, QString> const& upstreamToAjazzDeviceCodename() {
    static QHash<QString, QString> const table = {
        // Streamdock 153 family (15-key) \u2192 AJAZZ AKP153 / AKP153E.
        {QStringLiteral("293"), QStringLiteral("akp153")},
        {QStringLiteral("293E"), QStringLiteral("akp153e")},
        {QStringLiteral("HSV293"), QStringLiteral("akp153")},
        // Streamdock 6-key \u2192 AJAZZ AKP03.
        {QStringLiteral("N3"), QStringLiteral("akp03")},
        {QStringLiteral("N3E"), QStringLiteral("akp03")},
        // Streamdock dial \u2192 AJAZZ AKP815.
        {QStringLiteral("N4"), QStringLiteral("akp815")},
        {QStringLiteral("293V3"), QStringLiteral("akp153")},
        {QStringLiteral("293V3E"), QStringLiteral("akp153e")},
        {QStringLiteral("293V25"), QStringLiteral("akp153")},
        {QStringLiteral("293V2"), QStringLiteral("akp153")},
        // AK 980 \u2014 the upstream catalogue lists D92 (knob keyboard); the
        // AKP815 happens to be the closest hardware peer for now.
        {QStringLiteral("D92"), QStringLiteral("ak980pro")},
        {QStringLiteral("D92SE"), QStringLiteral("ak980pro")},
    };
    return table;
}

} // namespace

StreamdockCatalogFetcher::StreamdockCatalogFetcher(QObject* parent) : QObject(parent) {}

StreamdockCatalogFetcher::~StreamdockCatalogFetcher() = default;

QUrl StreamdockCatalogFetcher::defaultCatalogUrl() {
    return QUrl{QString::fromLatin1(kDefaultCatalogUrl)};
}

void StreamdockCatalogFetcher::setCacheDirOverride(QString const& dir) {
    m_cacheDirOverride = dir;
}

void StreamdockCatalogFetcher::setCatalogUrlOverride(QString const& url) {
    m_catalogUrlOverride = url;
}

QString StreamdockCatalogFetcher::cacheFilePath() const {
    QString const dir = m_cacheDirOverride.isEmpty()
                            ? QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
                            : m_cacheDirOverride;
    return QDir{dir}.filePath(QString::fromLatin1(kCacheFileName));
}

QUrl StreamdockCatalogFetcher::effectiveCatalogUrl() const {
    QString const env = qEnvironmentVariable(kEnvOverride);
    QString const& explicitOverride = m_catalogUrlOverride.isEmpty() ? env : m_catalogUrlOverride;
    if (!explicitOverride.isEmpty()) {
        return QUrl{explicitOverride};
    }
    return defaultCatalogUrl();
}

QString StreamdockCatalogFetcher::deriveUuid(QString const& upstreamId, QString const& name) {
    QString const slug = slugify(name);
    if (slug.isEmpty()) {
        return QStringLiteral("com.streamdock.id.%1").arg(upstreamId);
    }
    return QStringLiteral("com.streamdock.%1.%2").arg(slug, upstreamId);
}

QStringList StreamdockCatalogFetcher::mapUpstreamDevices(QStringList const& upstreamDeviceUuids) {
    auto const& table = upstreamToAjazzDeviceCodename();
    QStringList result;
    result.reserve(upstreamDeviceUuids.size());
    for (auto const& deviceUuid : upstreamDeviceUuids) {
        // The upstream `deviceUuid` field is sometimes a comma-joined
        // list (e.g. "D92,D92SE") \u2014 treat each component as its own
        // candidate.
        for (auto const& token : deviceUuid.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
            QString const trimmed = token.trimmed();
            auto const it = table.constFind(trimmed);
            if (it != table.cend() && !result.contains(it.value())) {
                result.append(it.value());
            }
        }
    }
    return result;
}

QString StreamdockCatalogFetcher::humaniseSize(double bytes) {
    if (bytes <= 0.0 || std::isnan(bytes)) {
        return QStringLiteral("\u2014"); // em-dash, "size unknown".
    }
    static constexpr char const* const kUnits[] = {"B", "KB", "MB", "GB"};
    int unit = 0;
    while (bytes >= 1024.0 && unit < 3) {
        bytes /= 1024.0;
        ++unit;
    }
    if (bytes >= 100.0 || unit == 0) {
        return QStringLiteral("%1 %2")
            .arg(static_cast<int>(std::round(bytes)))
            .arg(QString::fromLatin1(kUnits[unit]));
    }
    return QStringLiteral("%1 %2")
        .arg(QString::number(bytes, 'f', bytes >= 10.0 ? 1 : 2))
        .arg(QString::fromLatin1(kUnits[unit]));
}

namespace {

/// Parse a single upstream plugin record into a @ref CatalogEntry.
/// Returns @c std::nullopt when the record is malformed or fully
/// incompatible (no supported devices).
std::optional<CatalogEntry> translateRecord(QJsonObject const& obj) {
    QString const id = obj.value(QStringLiteral("id")).toVariant().toString();
    QString const name = obj.value(QStringLiteral("name")).toString().trimmed();
    if (id.isEmpty() || name.isEmpty()) {
        return std::nullopt;
    }

    // Devices: the upstream list has both an `id` (PK) and a free-form
    // `deviceUuid` (e.g. "293,293V3"). We map by the `deviceUuid` field
    // since the underlying hardware family is the stable identifier.
    QStringList upstreamDeviceUuids;
    auto const devicesArr = obj.value(QStringLiteral("devices")).toArray();
    upstreamDeviceUuids.reserve(static_cast<int>(devicesArr.size()));
    for (auto const& dv : devicesArr) {
        auto const devObj = dv.toObject();
        QString const uuid = devObj.value(QStringLiteral("deviceUuid")).toString();
        if (!uuid.isEmpty()) {
            upstreamDeviceUuids.append(uuid);
        }
    }
    QStringList const devices = StreamdockCatalogFetcher::mapUpstreamDevices(upstreamDeviceUuids);
    if (devices.isEmpty()) {
        // Plugin doesn't target any of the AKP devices `acc` drives.
        // Drop it instead of pretending we can install it.
        return std::nullopt;
    }

    // Categories: `types` is an array; pick the first as the primary
    // category and use the rest as tags. Both fields fall back to safe
    // defaults so the UI never shows an empty badge.
    QString category = QStringLiteral("Streaming");
    QStringList tags;
    auto const typesArr = obj.value(QStringLiteral("types")).toArray();
    for (auto const& tv : typesArr) {
        QString const en = tv.toObject().value(QStringLiteral("nameen")).toString().trimmed();
        if (en.isEmpty()) {
            continue;
        }
        if (category == QStringLiteral("Streaming")) {
            category = en;
        }
        tags.append(en.toLower());
    }

    // Icon URL must be HTTPS; an http:// row would mixed-content-fail in
    // the QML Image loader anyway.
    QString const iconUrlStr = obj.value(QStringLiteral("headUrl")).toString();
    QUrl const iconUrl{iconUrlStr};
    if (!iconUrl.isValid() || iconUrl.scheme().toLower() != QStringLiteral("https")) {
        // Fall back to the bundled placeholder icon \u2014 we still want the
        // row to be installable.
    }

    QString author = obj.value(QStringLiteral("author")).toString().trimmed();
    if (author.isEmpty()) {
        author = QStringLiteral("AJAZZ Streamdock");
    }

    CatalogEntry entry;
    entry.uuid = StreamdockCatalogFetcher::deriveUuid(id, name);
    entry.name = name;
    entry.version = obj.value(QStringLiteral("version")).toString();
    if (entry.version.isEmpty()) {
        entry.version = QStringLiteral("0.0.0");
    }
    entry.author = author;
    entry.description = firstParagraph(obj.value(QStringLiteral("overview")).toString());
    if (iconUrl.isValid() && iconUrl.scheme().toLower() == QStringLiteral("https")) {
        entry.iconUrl = iconUrl;
    } else {
        entry.iconUrl = QUrl(QStringLiteral("qrc:/qt/qml/AjazzControlCenter/icons/app.svg"));
    }
    entry.category = category;
    entry.tags = tags;
    entry.devices = devices;
    entry.compatibility = QStringLiteral("streamdock");
    entry.sizeBytes =
        StreamdockCatalogFetcher::humaniseSize(obj.value(QStringLiteral("size")).toDouble());
    entry.verified = false;
    entry.source = QStringLiteral("streamdock");
    entry.streamdockProductId = id;
    return entry;
}

} // namespace

std::vector<CatalogEntry> StreamdockCatalogFetcher::parseUpstreamJson(QByteArray const& json,
                                                                      QUrl const& /*origin*/) {
    std::vector<CatalogEntry> rows;
    QJsonParseError err{};
    QJsonDocument const doc = QJsonDocument::fromJson(json, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        return rows;
    }
    QJsonObject const root = doc.object();

    // Two accepted shapes:
    //   1. live response  : { "code": 200, "data": { "list": [...] } }
    //   2. cached snapshot: { "rows": [ <CatalogEntry-as-json> ], ... }
    if (root.contains(QStringLiteral("rows"))) {
        auto const arr = root.value(QStringLiteral("rows")).toArray();
        rows.reserve(static_cast<std::size_t>(arr.size()));
        for (auto const& v : arr) {
            auto const o = v.toObject();
            CatalogEntry e;
            e.uuid = o.value(QStringLiteral("uuid")).toString();
            e.name = o.value(QStringLiteral("name")).toString();
            e.version = o.value(QStringLiteral("version")).toString();
            e.author = o.value(QStringLiteral("author")).toString();
            e.description = o.value(QStringLiteral("description")).toString();
            e.iconUrl = QUrl(o.value(QStringLiteral("iconUrl")).toString());
            e.category = o.value(QStringLiteral("category")).toString();
            for (auto const& tv : o.value(QStringLiteral("tags")).toArray()) {
                e.tags.append(tv.toString());
            }
            for (auto const& dv : o.value(QStringLiteral("devices")).toArray()) {
                e.devices.append(dv.toString());
            }
            e.compatibility = o.value(QStringLiteral("compatibility")).toString();
            e.sizeBytes = o.value(QStringLiteral("sizeBytes")).toString();
            e.verified = o.value(QStringLiteral("verified")).toBool();
            e.source = o.value(QStringLiteral("source")).toString();
            if (e.source.isEmpty()) {
                e.source = QStringLiteral("streamdock");
            }
            e.streamdockProductId = o.value(QStringLiteral("streamdockProductId")).toString();
            if (!e.uuid.isEmpty() && !e.name.isEmpty()) {
                rows.push_back(std::move(e));
            }
        }
        return rows;
    }

    // Live response shape.
    QJsonObject const data = root.value(QStringLiteral("data")).toObject();
    QJsonArray const list = data.value(QStringLiteral("list")).toArray();
    rows.reserve(static_cast<std::size_t>(list.size()));
    for (auto const& v : list) {
        if (auto e = translateRecord(v.toObject())) {
            rows.push_back(std::move(*e));
        }
    }
    return rows;
}

namespace {

/// Serialise a vector of @ref CatalogEntry to the cached-snapshot shape.
QByteArray serialiseSnapshot(std::vector<CatalogEntry> const& rows,
                             qint64 fetchedAtUnixMs,
                             QUrl const& sourceUrl) {
    QJsonArray arr;
    for (auto const& e : rows) {
        QJsonArray tags;
        for (auto const& t : e.tags) {
            tags.append(t);
        }
        QJsonArray devices;
        for (auto const& d : e.devices) {
            devices.append(d);
        }
        QJsonObject o{
            {QStringLiteral("uuid"), e.uuid},
            {QStringLiteral("name"), e.name},
            {QStringLiteral("version"), e.version},
            {QStringLiteral("author"), e.author},
            {QStringLiteral("description"), e.description},
            {QStringLiteral("iconUrl"), e.iconUrl.toString()},
            {QStringLiteral("category"), e.category},
            {QStringLiteral("tags"), tags},
            {QStringLiteral("devices"), devices},
            {QStringLiteral("compatibility"), e.compatibility},
            {QStringLiteral("sizeBytes"), e.sizeBytes},
            {QStringLiteral("verified"), e.verified},
            {QStringLiteral("source"), e.source},
            {QStringLiteral("streamdockProductId"), e.streamdockProductId},
        };
        arr.append(o);
    }
    QJsonObject root{
        {QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("fetchedAtUnixMs"), fetchedAtUnixMs},
        {QStringLiteral("sourceUrl"), sourceUrl.toString()},
        {QStringLiteral("rows"), arr},
    };
    return QJsonDocument{root}.toJson(QJsonDocument::Indented);
}

/// Parse the metadata wrapper of a cached snapshot.
struct SnapshotMetadata {
    qint64 fetchedAtUnixMs = 0;
    QString sourceUrl;
};
SnapshotMetadata readSnapshotMetadata(QByteArray const& json) {
    SnapshotMetadata m{};
    QJsonParseError err{};
    auto const doc = QJsonDocument::fromJson(json, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        return m;
    }
    auto const o = doc.object();
    m.fetchedAtUnixMs = static_cast<qint64>(o.value(QStringLiteral("fetchedAtUnixMs")).toDouble(0));
    m.sourceUrl = o.value(QStringLiteral("sourceUrl")).toString();
    return m;
}

} // namespace

void StreamdockCatalogFetcher::writeCache(std::vector<CatalogEntry> const& rows,
                                          QUrl const& origin) {
    QString const path = cacheFilePath();
    QFileInfo const fi{path};
    QDir().mkpath(fi.absolutePath());
    QSaveFile out{path};
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return; // non-fatal; we'll retry on the next refresh.
    }
    QByteArray const payload = serialiseSnapshot(rows, QDateTime::currentMSecsSinceEpoch(), origin);
    if (out.write(payload) != payload.size()) {
        out.cancelWriting();
        return;
    }
    out.commit(); // atomic rename of the .tmp into place.
}

StreamdockCatalogFetcher::Snapshot StreamdockCatalogFetcher::loadFromCache() const {
    Snapshot s;
    QFile in{cacheFilePath()};
    if (!in.exists() || !in.open(QIODevice::ReadOnly)) {
        return s;
    }
    QByteArray const bytes = in.readAll();
    s.rows = parseUpstreamJson(bytes);
    if (s.rows.empty()) {
        return s;
    }
    auto const meta = readSnapshotMetadata(bytes);
    s.state = State::Cached;
    s.fetchedAtUnixMs = meta.fetchedAtUnixMs;
    s.sourceUrl = meta.sourceUrl;
    return s;
}

StreamdockCatalogFetcher::Snapshot StreamdockCatalogFetcher::loadBundledFallback() {
    Snapshot s;
    QFile in{QStringLiteral(":/qt/qml/AjazzControlCenter/streamdock-fallback.json")};
    if (!in.open(QIODevice::ReadOnly)) {
        return s; // shouldn't happen \u2014 the resource is required at build time.
    }
    QByteArray const bytes = in.readAll();
    s.rows = parseUpstreamJson(bytes);
    s.state = State::Offline;
    s.fetchedAtUnixMs = QDateTime::currentMSecsSinceEpoch();
    s.sourceUrl = QStringLiteral("qrc:/qt/qml/AjazzControlCenter/streamdock-fallback.json");
    return s;
}

void StreamdockCatalogFetcher::emitSnapshot(Snapshot snapshot) {
    if (m_state != snapshot.state) {
        m_state = snapshot.state;
        emit stateChanged(m_state);
    }
    emit snapshotReady(std::move(snapshot));
}

void StreamdockCatalogFetcher::refresh() {
    // 1. Immediate: emit the cached snapshot if any, else the bundled
    //    fallback. The UI gets rows to render before the first network
    //    round-trip even returns.
    Snapshot const cached = loadFromCache();
    if (!cached.rows.empty()) {
        emitSnapshot(cached);
    } else {
        emitSnapshot(loadBundledFallback());
    }

    // 2. Live fetch \u2014 unless explicitly disabled.
    QUrl const url = effectiveCatalogUrl();
    if (m_catalogUrlOverride == QStringLiteral("disabled") ||
        qEnvironmentVariable(kEnvOverride) == QStringLiteral("disabled")) {
        return;
    }

    // Reset accumulated state and start at page 1.
    m_accumulated.clear();
    m_inFlightTotalPages = 0;
    if (!m_netAccessManager) {
        m_netAccessManager = new QNetworkAccessManager{this};
    }
    if (m_state != State::Loading) {
        m_state = State::Loading;
        emit stateChanged(m_state);
    }
    fetchPage(1);
    Q_UNUSED(url);
}

void StreamdockCatalogFetcher::fetchPage(int pageNum) {
    QUrl const url = effectiveCatalogUrl();
    if (!url.isValid() || url.scheme().isEmpty()) {
        return;
    }

    QNetworkRequest req{url};
    req.setHeader(QNetworkRequest::ContentTypeHeader,
                  QStringLiteral("application/json; charset=utf-8"));
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("ajazz-control-center/streamdock-mirror "
                                 "(+https://github.com/Aiacos/ajazz-control-center)"));
    req.setRawHeader("Accept", "application/json");
    req.setTransferTimeout(kPerPageTimeoutMs);

    QJsonObject const body{
        {QStringLiteral("pageNum"), pageNum},
        {QStringLiteral("pageSize"), kPageSize},
        {QStringLiteral("productType"), QString::fromLatin1(kProductTypePlugin)},
        {QStringLiteral("tenantId"), QString::fromLatin1(kTenantId)},
    };
    auto* const reply =
        m_netAccessManager->post(req, QJsonDocument{body}.toJson(QJsonDocument::Compact));
    QObject::connect(
        reply, &QNetworkReply::finished, this, [this, reply]() { onPageFinished(reply); });
}

void StreamdockCatalogFetcher::onPageFinished(QNetworkReply* reply) {
    reply->deleteLater();
    QUrl const origin = effectiveCatalogUrl();

    if (reply->error() != QNetworkReply::NoError) {
        // Live fetch failed. Keep the previously emitted cached / fallback
        // snapshot \u2014 we don't want to flap the UI back to "loading".
        if (m_state == State::Loading) {
            m_state = m_accumulated.empty()
                          ? (loadFromCache().rows.empty() ? State::Offline : State::Cached)
                          : State::Cached;
            emit stateChanged(m_state);
        }
        return;
    }

    QByteArray const bytes = reply->readAll();
    QJsonParseError err{};
    QJsonDocument const doc = QJsonDocument::fromJson(bytes, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject() ||
        doc.object().value(QStringLiteral("code")).toInt() != 200) {
        if (m_state == State::Loading) {
            m_state = m_accumulated.empty() ? State::Offline : State::Cached;
            emit stateChanged(m_state);
        }
        return;
    }

    QJsonObject const data = doc.object().value(QStringLiteral("data")).toObject();
    int const totalPage = data.value(QStringLiteral("totalPage")).toInt(1);
    int const pageNum = data.value(QStringLiteral("pageNum")).toInt(1);
    if (m_inFlightTotalPages == 0) {
        m_inFlightTotalPages = std::min(totalPage, kMaxPages);
    }

    auto const pageRows = parseUpstreamJson(bytes, origin);
    m_accumulated.insert(m_accumulated.end(), pageRows.begin(), pageRows.end());

    int const nextPage = pageNum + 1;
    if (nextPage <= m_inFlightTotalPages) {
        fetchPage(nextPage);
        return;
    }

    // All pages collected \u2014 commit to disk and emit.
    writeCache(m_accumulated, origin);
    Snapshot final;
    final.rows = std::move(m_accumulated);
    final.state = State::Online;
    final.fetchedAtUnixMs = QDateTime::currentMSecsSinceEpoch();
    final.sourceUrl = origin.toString();
    emitSnapshot(std::move(final));
}

} // namespace ajazz::app
