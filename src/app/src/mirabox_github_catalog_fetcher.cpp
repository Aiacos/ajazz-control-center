// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file mirabox_github_catalog_fetcher.cpp
 * @brief Implementation of @ref ajazz::app::MiraboxGithubCatalogFetcher.
 *
 * Sources rows from the GitHub Contents API for the @c Plugins/ directory of
 * @c MiraboxSpace/StreamDock-Plugins (one non-recursive call → the immediate
 * child directories, each a plugin). Writes a JSON mirror to disk so the tab
 * survives an offline relaunch. NO dependency on the proprietary Space CDN.
 */
#include "mirabox_github_catalog_fetcher.hpp"

#include <QByteArray>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLoggingCategory>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>

namespace ajazz::app {

namespace {

Q_LOGGING_CATEGORY(lcMiraboxGh, "ajazz.plugins.mirabox-github")

/// GitHub owner/repo + branch + plugins root. The Contents API lists the
/// immediate children of <root>; each child directory is one plugin bundle.
constexpr char kOwnerRepo[] = "MiraboxSpace/StreamDock-Plugins";
constexpr char kBranch[] = "main";
constexpr char kPluginsRoot[] = "Plugins";
constexpr char kDefaultCatalogUrl[] =
    "https://api.github.com/repos/MiraboxSpace/StreamDock-Plugins/contents/Plugins?ref=main";
constexpr char kEnvOverride[] = "ACC_MIRABOX_GITHUB_CATALOG_URL";
constexpr char kCacheFileName[] = "mirabox-github-catalog.json";

/// Title-case a single word, leaving an already-capitalised acronym alone.
QString titleCaseWord(QString const& w) {
    if (w.isEmpty()) {
        return w;
    }
    return w.left(1).toUpper() + w.mid(1);
}

} // namespace

MiraboxGithubCatalogFetcher::MiraboxGithubCatalogFetcher(QObject* parent) : QObject(parent) {}
MiraboxGithubCatalogFetcher::~MiraboxGithubCatalogFetcher() = default;

QUrl MiraboxGithubCatalogFetcher::defaultCatalogUrl() {
    return QUrl{QString::fromLatin1(kDefaultCatalogUrl)};
}

QString MiraboxGithubCatalogFetcher::repoOwnerRepo() {
    return QString::fromLatin1(kOwnerRepo);
}

QString MiraboxGithubCatalogFetcher::repoBranch() {
    return QString::fromLatin1(kBranch);
}

QString MiraboxGithubCatalogFetcher::pluginsRoot() {
    return QString::fromLatin1(kPluginsRoot);
}

void MiraboxGithubCatalogFetcher::setCacheDirOverride(QString const& dir) {
    m_cacheDirOverride = dir;
}

void MiraboxGithubCatalogFetcher::setCatalogUrlOverride(QString const& url) {
    m_catalogUrlOverride = url;
}

QString MiraboxGithubCatalogFetcher::cacheFilePath() const {
    QString const dir = m_cacheDirOverride.isEmpty()
                            ? QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
                            : m_cacheDirOverride;
    return QDir{dir}.filePath(QString::fromLatin1(kCacheFileName));
}

QUrl MiraboxGithubCatalogFetcher::effectiveCatalogUrl() const {
    QString const env = qEnvironmentVariable(kEnvOverride);
    QString const& explicitOverride = m_catalogUrlOverride.isEmpty() ? env : m_catalogUrlOverride;
    if (!explicitOverride.isEmpty()) {
        return QUrl{explicitOverride};
    }
    return defaultCatalogUrl();
}

QString MiraboxGithubCatalogFetcher::humaniseDirName(QString const& dirName) {
    QString name = dirName.trimmed();
    // Strip a trailing ".sdPlugin" bundle suffix.
    if (name.endsWith(QStringLiteral(".sdPlugin"), Qt::CaseInsensitive)) {
        name.chop(QStringLiteral(".sdPlugin").size());
    }
    // For reverse-DNS bundle ids (com.vendor.x.worldWeather) the meaningful
    // label is the last dotted segment.
    qsizetype const lastDot = name.lastIndexOf(QLatin1Char('.'));
    if (lastDot >= 0 && lastDot + 1 < name.size()) {
        name = name.mid(lastDot + 1);
    }
    // Split on separators AND camelCase / digit boundaries into words.
    QString spaced;
    spaced.reserve(name.size() + 8);
    QChar prev;
    for (QChar const c : name) {
        bool const isSep =
            (c == QLatin1Char('-') || c == QLatin1Char('_') || c == QLatin1Char(' '));
        if (isSep) {
            if (!spaced.isEmpty() && spaced.back() != QLatin1Char(' ')) {
                spaced.append(QLatin1Char(' '));
            }
            prev = QLatin1Char(' ');
            continue;
        }
        bool const boundary =
            !prev.isNull() && prev != QLatin1Char(' ') &&
            ((prev.isLower() && c.isUpper()) || (prev.isLetter() && c.isDigit()) ||
             (prev.isDigit() && c.isLetter()));
        if (boundary) {
            spaced.append(QLatin1Char(' '));
        }
        spaced.append(c);
        prev = c;
    }
    // Title-case each word.
    QStringList const words = spaced.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    QStringList out;
    out.reserve(words.size());
    for (QString const& w : words) {
        out.append(titleCaseWord(w));
    }
    QString const result = out.join(QLatin1Char(' '));
    return result.isEmpty() ? dirName : result;
}

namespace {

/// Build a CatalogEntry from one GitHub Contents directory entry.
/// @p dirName is the directory name (a plugin bundle dir); @p repoPath is its
/// repo-relative path (e.g. "Plugins/WorldWeather").
CatalogEntry entryFromDir(QString const& dirName, QString const& repoPath) {
    CatalogEntry e;
    // UUID: reuse a reverse-DNS bundle dir name verbatim (minus .sdPlugin);
    // otherwise synthesise a stable id under our github namespace.
    QString uuid = dirName;
    if (uuid.endsWith(QStringLiteral(".sdPlugin"), Qt::CaseInsensitive)) {
        uuid.chop(QStringLiteral(".sdPlugin").size());
    }
    if (!uuid.contains(QLatin1Char('.'))) {
        uuid = QStringLiteral("com.mirabox.github.%1").arg(uuid.toLower());
    }
    e.uuid = uuid;
    e.name = MiraboxGithubCatalogFetcher::humaniseDirName(dirName);
    e.version = QString{};
    e.author = QStringLiteral("MiraboxSpace");
    e.description =
        QStringLiteral("Mirabox Stream Dock plugin from the open StreamDock-Plugins repository.");
    e.iconUrl = QUrl(QStringLiteral("qrc:/qt/qml/AjazzControlCenter/icons/app.svg"));
    e.category = QStringLiteral("Mirabox");
    e.devices = {}; // unknown without the manifest; do not over-claim support.
    e.compatibility = QStringLiteral("streamdock");
    e.sizeBytes = QStringLiteral("—"); // em-dash: size unknown.
    e.verified = false;
    e.source = QStringLiteral("mirabox-github");
    // Stash the repo-relative path in streamdockProductId (the generic upstream
    // identifier field) so the install path can fetch this plugin's subtree.
    // downloadUrl stays EMPTY for now: these rows are browse-only until the
    // GitHub multi-file install path lands (the install path will recognise the
    // mirabox-github source and assemble the bundle from repoPath).
    e.streamdockProductId = repoPath;
    e.downloadUrl = QUrl{};
    return e;
}

} // namespace

std::vector<CatalogEntry> MiraboxGithubCatalogFetcher::parseUpstreamJson(QByteArray const& json) {
    std::vector<CatalogEntry> rows;
    QJsonParseError err{};
    QJsonDocument const doc = QJsonDocument::fromJson(json, &err);
    if (err.error != QJsonParseError::NoError) {
        return rows;
    }

    // Cached-snapshot shape: { "rows": [ <CatalogEntry-as-json> ], ... }.
    if (doc.isObject() && doc.object().contains(QStringLiteral("rows"))) {
        QJsonArray const arr = doc.object().value(QStringLiteral("rows")).toArray();
        rows.reserve(static_cast<std::size_t>(arr.size()));
        for (QJsonValue const& v : arr) {
            QJsonObject const o = v.toObject();
            CatalogEntry e;
            e.uuid = o.value(QStringLiteral("uuid")).toString();
            e.name = o.value(QStringLiteral("name")).toString();
            e.version = o.value(QStringLiteral("version")).toString();
            e.author = o.value(QStringLiteral("author")).toString();
            e.description = o.value(QStringLiteral("description")).toString();
            e.iconUrl = QUrl(o.value(QStringLiteral("iconUrl")).toString());
            e.category = o.value(QStringLiteral("category")).toString();
            for (QJsonValue const& tv : o.value(QStringLiteral("tags")).toArray()) {
                e.tags.append(tv.toString());
            }
            e.compatibility = o.value(QStringLiteral("compatibility")).toString();
            e.sizeBytes = o.value(QStringLiteral("sizeBytes")).toString();
            e.source = QStringLiteral("mirabox-github");
            e.streamdockProductId = o.value(QStringLiteral("repoPath")).toString();
            if (!e.uuid.isEmpty() && !e.name.isEmpty()) {
                rows.push_back(std::move(e));
            }
        }
        return rows;
    }

    // Live GitHub Contents shape: a JSON ARRAY of { name, path, type } entries.
    if (doc.isArray()) {
        QJsonArray const arr = doc.array();
        rows.reserve(static_cast<std::size_t>(arr.size()));
        for (QJsonValue const& v : arr) {
            QJsonObject const o = v.toObject();
            if (o.value(QStringLiteral("type")).toString() != QStringLiteral("dir")) {
                continue; // only directories are plugin bundles
            }
            QString const dirName = o.value(QStringLiteral("name")).toString();
            QString const repoPath = o.value(QStringLiteral("path")).toString();
            if (dirName.isEmpty() || repoPath.isEmpty()) {
                continue;
            }
            rows.push_back(entryFromDir(dirName, repoPath));
        }
    }
    return rows;
}

namespace {

QByteArray serialiseSnapshot(std::vector<CatalogEntry> const& rows,
                             qint64 fetchedAtUnixMs,
                             QUrl const& sourceUrl) {
    QJsonArray arr;
    for (CatalogEntry const& e : rows) {
        QJsonArray tags;
        for (QString const& t : e.tags) {
            tags.append(t);
        }
        arr.append(QJsonObject{
            {QStringLiteral("uuid"), e.uuid},
            {QStringLiteral("name"), e.name},
            {QStringLiteral("version"), e.version},
            {QStringLiteral("author"), e.author},
            {QStringLiteral("description"), e.description},
            {QStringLiteral("iconUrl"), e.iconUrl.toString()},
            {QStringLiteral("category"), e.category},
            {QStringLiteral("tags"), tags},
            {QStringLiteral("compatibility"), e.compatibility},
            {QStringLiteral("sizeBytes"), e.sizeBytes},
            {QStringLiteral("source"), e.source},
            {QStringLiteral("repoPath"), e.streamdockProductId},
        });
    }
    QJsonObject const root{
        {QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("fetchedAtUnixMs"), fetchedAtUnixMs},
        {QStringLiteral("sourceUrl"), sourceUrl.toString()},
        {QStringLiteral("rows"), arr},
    };
    return QJsonDocument{root}.toJson(QJsonDocument::Indented);
}

qint64 readSnapshotTimestamp(QByteArray const& json) {
    QJsonParseError err{};
    QJsonDocument const doc = QJsonDocument::fromJson(json, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        return 0;
    }
    return static_cast<qint64>(doc.object().value(QStringLiteral("fetchedAtUnixMs")).toDouble(0));
}

} // namespace

void MiraboxGithubCatalogFetcher::writeCache(std::vector<CatalogEntry> const& rows,
                                             QUrl const& origin) {
    QString const path = cacheFilePath();
    QDir().mkpath(QFileInfo{path}.absolutePath());
    QSaveFile out{path};
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qCWarning(lcMiraboxGh) << "cache write failed (open):" << path << out.errorString();
        return;
    }
    QByteArray const payload = serialiseSnapshot(rows, QDateTime::currentMSecsSinceEpoch(), origin);
    if (out.write(payload) != payload.size()) {
        qCWarning(lcMiraboxGh) << "cache write failed (short write):" << path;
        out.cancelWriting();
        return;
    }
    out.commit();
    qCInfo(lcMiraboxGh) << "cache written:" << path << "(" << payload.size() << "bytes)";
}

MiraboxGithubCatalogFetcher::Snapshot MiraboxGithubCatalogFetcher::loadFromCache() const {
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
    s.state = State::Cached;
    s.fetchedAtUnixMs = readSnapshotTimestamp(bytes);
    s.sourceUrl = cacheFilePath();
    return s;
}

void MiraboxGithubCatalogFetcher::emitSnapshot(Snapshot snapshot) {
    if (m_state != snapshot.state) {
        m_state = snapshot.state;
        emit stateChanged(m_state);
    }
    emit snapshotReady(std::move(snapshot));
}

void MiraboxGithubCatalogFetcher::refresh() {
    if (m_state == State::Loading) {
        qCInfo(lcMiraboxGh) << "refresh() ignored — fetch already in flight";
        return;
    }

    // 1. Immediate: emit the cached snapshot if any.
    Snapshot const cached = loadFromCache();
    if (!cached.rows.empty()) {
        emitSnapshot(cached);
    }

    // 2. Live fetch — unless explicitly disabled.
    if (m_catalogUrlOverride == QStringLiteral("disabled") ||
        qEnvironmentVariable(kEnvOverride) == QStringLiteral("disabled")) {
        qCInfo(lcMiraboxGh) << "live fetch disabled via override — staying on cache";
        if (cached.rows.empty()) {
            Snapshot offline;
            offline.state = State::Offline;
            emitSnapshot(std::move(offline));
        }
        return;
    }

    QUrl const url = effectiveCatalogUrl();
    if (!url.isValid() || url.scheme().isEmpty()) {
        qCWarning(lcMiraboxGh) << "invalid catalog URL:" << url.toString();
        return;
    }
    qCInfo(lcMiraboxGh) << "starting live GitHub catalogue fetch:" << url.toString();

    if (!m_netAccessManager) {
        m_netAccessManager = new QNetworkAccessManager{this};
    }
    if (m_state != State::Loading) {
        m_state = State::Loading;
        emit stateChanged(m_state);
    }

    QNetworkRequest req{url};
    req.setRawHeader("Accept", "application/vnd.github+json");
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("ajazz-control-center/mirabox-github-mirror "
                                 "(+https://github.com/Aiacos/ajazz-control-center)"));
    req.setTransferTimeout(kFetchTimeoutMs);
    QNetworkReply* const reply = m_netAccessManager->get(req);
    QObject::connect(
        reply, &QNetworkReply::finished, this, [this, reply]() { onReplyFinished(reply); });
}

void MiraboxGithubCatalogFetcher::onReplyFinished(QNetworkReply* reply) {
    reply->deleteLater();
    QUrl const origin = effectiveCatalogUrl();

    if (reply->error() != QNetworkReply::NoError) {
        qCWarning(lcMiraboxGh) << "fetch failed:" << reply->error() << reply->errorString()
                               << "(HTTP:"
                               << reply->attribute(QNetworkRequest::HttpStatusCodeAttribute) << ")";
        if (m_state == State::Loading) {
            m_state = loadFromCache().rows.empty() ? State::Offline : State::Cached;
            emit stateChanged(m_state);
        }
        return;
    }

    QByteArray const bytes = reply->readAll();
    std::vector<CatalogEntry> rows = parseUpstreamJson(bytes);
    if (rows.empty()) {
        qCWarning(lcMiraboxGh) << "fetch returned no plugin directories";
        if (m_state == State::Loading) {
            m_state = loadFromCache().rows.empty() ? State::Offline : State::Cached;
            emit stateChanged(m_state);
        }
        return;
    }

    qCInfo(lcMiraboxGh) << "fetched" << rows.size() << "plugin(s) — writing cache";
    writeCache(rows, origin);
    Snapshot snap;
    snap.rows = std::move(rows);
    snap.state = State::Online;
    snap.fetchedAtUnixMs = QDateTime::currentMSecsSinceEpoch();
    snap.sourceUrl = origin.toString();
    emitSnapshot(std::move(snap));
}

} // namespace ajazz::app
