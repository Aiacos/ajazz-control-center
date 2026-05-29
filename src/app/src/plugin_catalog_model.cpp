// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file plugin_catalog_model.cpp
 * @brief Implementation of @ref ajazz::app::PluginCatalogModel.
 *
 * The model holds a flat vector of @ref CatalogEntry rows plus a side
 * map of per-row install state. All public mutators emit @c dataChanged
 * for the affected row so QML grid delegates re-render in place.
 */
#include "plugin_catalog_model.hpp"

#include "ajazz/core/logger.hpp"
#include "opendeck_catalog_fetcher.hpp"
#include "plugin_manifest.hpp"
#include "plugin_verify_gate.hpp"
#include "sdplugin_extractor.hpp"
#include "streamdock_catalog_fetcher.hpp"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMetaEnum>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QQmlEngine>
#include <QSettings>
#include <QStandardPaths>
#include <QtGlobal>
#include <QUrl>

#include <algorithm>

namespace ajazz::app {

namespace {

/// Pointer set by PluginCatalogModel::registerInstance, consumed by ::create.
PluginCatalogModel* s_pluginCatalogInstance = nullptr;

/// Test seam: non-empty overrides userPluginsDir(). Set via
/// PluginCatalogModel::setPluginsDirOverride (test-only API).
QString g_pluginsDirOverride{};

/// Forward declaration for the constructor sweep — the body lives in the
/// install-path helpers namespace further down because that's where the
/// install() codepath also calls it.
[[nodiscard]] QString userPluginsDir();

} // namespace

PluginCatalogModel* PluginCatalogModel::create(QQmlEngine* /*qml*/, QJSEngine* /*js*/) {
    Q_ASSERT_X(s_pluginCatalogInstance != nullptr,
               "PluginCatalogModel::create",
               "registerInstance() must be called before the QML engine loads");
    QQmlEngine::setObjectOwnership(s_pluginCatalogInstance, QQmlEngine::CppOwnership);
    return s_pluginCatalogInstance;
}

void PluginCatalogModel::registerInstance(PluginCatalogModel* instance) noexcept {
    s_pluginCatalogInstance = instance;
}

namespace {

/// Map a fetcher state enum to the lower-case string surfaced via QML.
QString stateToString(StreamdockCatalogFetcher::State s) {
    switch (s) {
    case StreamdockCatalogFetcher::State::Idle:
    case StreamdockCatalogFetcher::State::Loading:
        return QStringLiteral("loading");
    case StreamdockCatalogFetcher::State::Online:
        return QStringLiteral("online");
    case StreamdockCatalogFetcher::State::Cached:
        return QStringLiteral("cached");
    case StreamdockCatalogFetcher::State::Offline:
        return QStringLiteral("offline");
    }
    return QStringLiteral("loading");
}

/// Mirror of @ref stateToString for the OpenDeck fetcher's enum. The
/// two enums are intentionally type-distinct (one per fetcher) so the
/// type system catches accidental cross-wiring; the lower-case strings
/// they emit to QML are the same vocabulary so a single banner
/// component can render either source.
QString stateToString(OpenDeckCatalogFetcher::State s) {
    switch (s) {
    case OpenDeckCatalogFetcher::State::Idle:
    case OpenDeckCatalogFetcher::State::Loading:
        return QStringLiteral("loading");
    case OpenDeckCatalogFetcher::State::Online:
        return QStringLiteral("online");
    case OpenDeckCatalogFetcher::State::Cached:
        return QStringLiteral("cached");
    case OpenDeckCatalogFetcher::State::Offline:
        return QStringLiteral("offline");
    }
    return QStringLiteral("loading");
}

} // namespace

PluginCatalogModel::PluginCatalogModel(QObject* parent)
    : QAbstractListModel(parent),
      m_streamdockFetcher(std::make_unique<StreamdockCatalogFetcher>(this)),
      m_opendeckFetcher(std::make_unique<OpenDeckCatalogFetcher>(this)) {
    // PLUGIN-14 (T-22-phonehome): load the persisted online-catalog flag.
    // The catalog is now ON by default so a fresh install can browse and
    // install Stream Dock / OpenDeck plugins without first hunting for a
    // toggle. A user who explicitly turned it OFF has a stored `false` that
    // still wins over this default — only the first-launch / never-set case
    // sees online enabled. Network is still gated entirely on this flag, so
    // disabling it restores the no-outbound-request behaviour.
    {
        QSettings settings;
        m_onlineCatalogEnabled =
            settings.value(QStringLiteral("plugins/onlineCatalogEnabled"), true).toBool();
    }
    // First-launch (or post-upgrade) sweep: convert any `.sdPlugin`
    // archive files left in the user plugins directory by older code
    // (the install path in df01d3b wrote zips without extracting them)
    // into the new extracted-directory layout the plugin host expects.
    // Safe to run on every launch; an already-extracted install ends up
    // as a directory and is skipped by the file-only entry filter
    // inside the extractor (issue #62).
    QString const pluginsDir = userPluginsDir();
    extractStandalonePluginArchives(pluginsDir);

    // PLUGIN-14 verify gate (T-22-backdoor): scan every freshly-extracted
    // (or pre-existing) `.sdPlugin` directory and quarantine any whose
    // manifest fails Ed25519 verification. This closes the launch-sweep
    // back door: a tampered package dropped into the plugins dir is never
    // left in a discoverable state for the Phase-18 PluginManager.
    // Seam: verify after the sweep; do NOT modify sdplugin_extractor internals.
    {
        QDir const dir(pluginsDir);
        QStringList const entries = dir.entryList(QStringList{QStringLiteral("*.sdPlugin")},
                                                  QDir::Dirs | QDir::NoDotAndDotDot);
        for (QString const& entry : entries) {
            QString const manifestPath = dir.filePath(entry + QStringLiteral("/manifest.json"));
            if (!QFile::exists(manifestPath)) {
                continue; // no manifest -> not a valid plugin dir; skip
            }
            VerifyOutcome const vout = verifyStagedPlugin(manifestPath);
            if (vout.verdict == VerifyVerdict::Refused) {
                AJAZZ_LOG_WARN("plugin-catalog",
                               "launch-sweep verify: '{}' refused ({}); removing from plugins dir",
                               entry.toStdString(),
                               vout.reason.toStdString());
                QDir(dir.filePath(entry)).removeRecursively();
            } else {
                AJAZZ_LOG_INFO("plugin-catalog",
                               "launch-sweep verify: '{}' -> {}",
                               entry.toStdString(),
                               verdictToTrustLevel(vout.verdict).toStdString());
            }
        }
    }

    // Wire the upstream fetcher: each successful snapshot replaces the
    // streamdock-sourced rows in place. Local / community rows are
    // unaffected so install state survives a network refresh.
    QObject::connect(m_streamdockFetcher.get(),
                     &StreamdockCatalogFetcher::snapshotReady,
                     this,
                     [this](StreamdockCatalogFetcher::Snapshot snapshot) {
                         m_streamdockFetchedAtUnixMs = snapshot.fetchedAtUnixMs;
                         replaceStreamdockRows(std::move(snapshot.rows));
                     });
    QObject::connect(m_streamdockFetcher.get(),
                     &StreamdockCatalogFetcher::stateChanged,
                     this,
                     [this](StreamdockCatalogFetcher::State s) {
                         QString const updated = stateToString(s);
                         if (updated != m_streamdockStateString) {
                             m_streamdockStateString = updated;
                             emit streamdockStateChanged();
                         }
                     });

    // OpenDeck mirror — same wiring shape as the Streamdock fetcher.
    QObject::connect(m_opendeckFetcher.get(),
                     &OpenDeckCatalogFetcher::snapshotReady,
                     this,
                     [this](OpenDeckCatalogFetcher::Snapshot snapshot) {
                         m_opendeckFetchedAtUnixMs = snapshot.fetchedAtUnixMs;
                         replaceOpendeckRows(std::move(snapshot.rows));
                     });
    QObject::connect(m_opendeckFetcher.get(),
                     &OpenDeckCatalogFetcher::stateChanged,
                     this,
                     [this](OpenDeckCatalogFetcher::State s) {
                         QString const updated = stateToString(s);
                         if (updated != m_opendeckStateString) {
                             m_opendeckStateString = updated;
                             emit opendeckStateChanged();
                         }
                     });

    // Populate with the mock fixture so the QML grid has rows in dev
    // builds. The offline cached / bundled snapshot is always served
    // (no outbound request). A live fetch only happens when the user
    // explicitly enables online catalog (T-22-phonehome / PLUGIN-14).
    //
    // Call reload() which resets mock rows; the live fetch inside
    // reload() is now gated on m_onlineCatalogEnabled.
    reload();
}

PluginCatalogModel::~PluginCatalogModel() = default;

int PluginCatalogModel::rowCount(QModelIndex const& parent) const {
    if (parent.isValid()) {
        return 0;
    }
    return static_cast<int>(m_rows.size());
}

QVariant PluginCatalogModel::data(QModelIndex const& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(m_rows.size())) {
        return {};
    }
    auto const& row = m_rows[static_cast<std::size_t>(index.row())];
    auto const installState = m_install.value(row.uuid);

    switch (role) {
    case UuidRole:
        return row.uuid;
    case NameRole:
        return row.name;
    case VersionRole:
        return row.version;
    case AuthorRole:
        return row.author;
    case DescriptionRole:
        return row.description;
    case IconUrlRole:
        return row.iconUrl;
    case CategoryRole:
        return row.category;
    case TagsRole:
        return row.tags;
    case DevicesRole:
        return row.devices;
    case CompatibilityRole:
        return row.compatibility;
    case SizeBytesRole:
        return row.sizeBytes;
    case VerifiedRole:
        return row.verified;
    case InstalledRole:
        return installState.installed;
    case EnabledRole:
        return installState.enabled;
    case SourceRole:
        return row.source;
    case StreamdockProductIdRole:
        return row.streamdockProductId;
    case DownloadUrlRole:
        return row.downloadUrl;
    default:
        return {};
    }
}

QHash<int, QByteArray> PluginCatalogModel::roleNames() const {
    return {
        {UuidRole, "uuid"},
        {NameRole, "name"},
        {VersionRole, "version"},
        {AuthorRole, "author"},
        {DescriptionRole, "description"},
        {IconUrlRole, "iconUrl"},
        {CategoryRole, "category"},
        {TagsRole, "tags"},
        {DevicesRole, "devices"},
        {CompatibilityRole, "compatibility"},
        {SizeBytesRole, "sizeBytes"},
        {VerifiedRole, "verified"},
        {InstalledRole, "installed"},
        {EnabledRole, "enabled"},
        {SourceRole, "source"},
        {StreamdockProductIdRole, "streamdockProductId"},
        {DownloadUrlRole, "downloadUrl"},
    };
}

int PluginCatalogModel::installedCount() const {
    int count = 0;
    for (auto const& state : m_install) {
        if (state.installed) {
            ++count;
        }
    }
    return count;
}

QVariantList PluginCatalogModel::installedActions() const {
    QVariantList out;

    QString const pluginsDirPath = userPluginsDir();
    QDir const dir(pluginsDirPath);
    if (!dir.exists()) {
        return out;
    }

    // Same install layout the verify-gate sweep and Phase-18 discovery use:
    // <pluginsDir>/<name>.sdPlugin/manifest.json.
    QString const platform = currentPlatformString();
    QString const appVer = QCoreApplication::applicationVersion();
    QStringList const entries =
        dir.entryList(QStringList{QStringLiteral("*.sdPlugin")}, QDir::Dirs | QDir::NoDotAndDotDot);

    for (QString const& entry : entries) {
        QString const pluginDir = dir.filePath(entry);
        QFile manifestFile(QDir(pluginDir).filePath(QStringLiteral("manifest.json")));
        if (!manifestFile.open(QIODevice::ReadOnly)) {
            continue;
        }
        QByteArray const json = manifestFile.readAll();
        manifestFile.close();

        auto const parsed = parsePluginManifest(json);
        if (!parsed) {
            continue; // unparsable / missing required keys (T-18-MANIFEST)
        }
        if (!manifestRunnableHere(*parsed, platform, appVer)) {
            continue; // not for this OS / below software minimum version
        }

        for (PluginAction const& action : parsed->actions) {
            if (action.uuid.isEmpty() || action.name.isEmpty()) {
                continue; // an action with no id cannot be bound or routed
            }

            // Prefer the per-action icon, fall back to the plugin icon. Elgato
            // manifests routinely omit the extension, so probe `.png` too.
            // Returned as a file:// URL so QML Image renders it directly and the
            // C++ load boundary (normaliseImagePath) can strip it on persist.
            QString iconUrl;
            QString const iconRel = !action.icon.isEmpty() ? action.icon : parsed->icon;
            if (!iconRel.isEmpty()) {
                QString const base = QDir(pluginDir).filePath(iconRel);
                QString resolved;
                if (QFileInfo::exists(base)) {
                    resolved = base;
                } else if (QFileInfo::exists(base + QStringLiteral(".png"))) {
                    resolved = base + QStringLiteral(".png");
                }
                if (!resolved.isEmpty()) {
                    iconUrl = QUrl::fromLocalFile(resolved).toString();
                }
            }

            QVariantMap m;
            m.insert(QStringLiteral("pluginName"), parsed->name);
            m.insert(QStringLiteral("actionId"), action.uuid);
            m.insert(QStringLiteral("actionName"), action.name);
            m.insert(QStringLiteral("icon"), iconUrl);
            m.insert(QStringLiteral("propertyInspectorPath"), action.propertyInspectorPath);
            m.insert(QStringLiteral("controllers"), action.controllers);
            out.append(m);
        }
    }

    return out;
}

void PluginCatalogModel::reload() {
    beginResetModel();
    m_rows = mockFixture();
    // Keep the install map synchronised with the new row set: drop
    // entries whose UUID is no longer in the catalogue, leave the
    // others untouched so the user's locally-installed plugins survive
    // a refresh.
    QHash<QString, InstallState> kept;
    kept.reserve(static_cast<int>(m_rows.size()));
    for (auto const& row : m_rows) {
        if (auto const it = m_install.find(row.uuid); it != m_install.end()) {
            kept.insert(row.uuid, *it);
        }
    }
    m_install = std::move(kept);
    endResetModel();
    emit countChanged();
    emit installedCountChanged();

    // PLUGIN-14 anti-feature (T-22-phonehome): only fire the live HTTP fetch
    // when the user has explicitly opted in. The "disabled" sentinel on
    // setCatalogUrlOverride makes the fetcher emit the offline snapshot only
    // (cache first, bundled fallback if cache is empty) without issuing any
    // outbound network request.
    QString const liveUrlOverride = m_onlineCatalogEnabled ? QString{} : QStringLiteral("disabled");

    if (m_streamdockFetcher) {
        m_streamdockFetcher->setCatalogUrlOverride(liveUrlOverride);
        m_streamdockFetcher->refresh();
    }
    if (m_opendeckFetcher) {
        m_opendeckFetcher->setCatalogUrlOverride(liveUrlOverride);
        m_opendeckFetcher->refresh();
    }
}

QString PluginCatalogModel::streamdockState() const {
    return m_streamdockStateString;
}

int PluginCatalogModel::streamdockCount() const {
    int n = 0;
    for (auto const& row : m_rows) {
        if (row.source == QStringLiteral("streamdock")) {
            ++n;
        }
    }
    return n;
}

QString PluginCatalogModel::opendeckState() const {
    return m_opendeckStateString;
}

int PluginCatalogModel::opendeckCount() const {
    int n = 0;
    for (auto const& row : m_rows) {
        if (row.source == QStringLiteral("opendeck")) {
            ++n;
        }
    }
    return n;
}

bool PluginCatalogModel::onlineCatalogEnabled() const {
    return m_onlineCatalogEnabled;
}

void PluginCatalogModel::setOnlineCatalogEnabled(bool enabled) {
    if (m_onlineCatalogEnabled == enabled) {
        return;
    }
    m_onlineCatalogEnabled = enabled;
    {
        QSettings settings;
        settings.setValue(QStringLiteral("plugins/onlineCatalogEnabled"), enabled);
    }
    emit onlineCatalogEnabledChanged();
    if (enabled) {
        // Immediately trigger the live fetch so the user sees updated rows.
        refreshOnline();
    }
}

void PluginCatalogModel::refreshOnline() {
    // WR-04 fix (PLUGIN-14 / T-22-phonehome): honour the persisted opt-in flag.
    // A user who has explicitly set onlineCatalogEnabled = false must not see
    // outbound HTTP traffic just because the Refresh button is visible.
    // The button is gated in QML too (disabled when the switch is off), but
    // the C++ guard is the authoritative no-phone-home enforcement point.
    if (!m_onlineCatalogEnabled) {
        AJAZZ_LOG_INFO("plugin-catalog", "refreshOnline: skipped (onlineCatalogEnabled=false)");
        return;
    }
    if (m_streamdockFetcher) {
        m_streamdockFetcher->setCatalogUrlOverride(QString{}); // clear any "disabled" override
        m_streamdockFetcher->refresh();
    }
    if (m_opendeckFetcher) {
        m_opendeckFetcher->setCatalogUrlOverride(QString{});
        m_opendeckFetcher->refresh();
    }
}

void PluginCatalogModel::setPluginsDirOverride(QString const& dir) {
    g_pluginsDirOverride = dir;
}

void PluginCatalogModel::replaceStreamdockRows(std::vector<CatalogEntry> rows) {
    // Strategy: drop every existing streamdock row, append the new ones,
    // then preserve install state by UUID. We use a full reset rather
    // than fine-grained dataChanged because the upstream order can shift
    // arbitrarily between fetches; QML's GridView re-renders the visible
    // delegates only, so the cost is negligible.
    beginResetModel();
    std::vector<CatalogEntry> kept;
    kept.reserve(m_rows.size() + rows.size());
    for (auto& row : m_rows) {
        if (row.source != QStringLiteral("streamdock")) {
            kept.push_back(std::move(row));
        }
    }
    for (auto& row : rows) {
        kept.push_back(std::move(row));
    }
    m_rows = std::move(kept);

    // Reconcile the install map against the new row set so the side-map
    // never grows unbounded across refreshes.
    QHash<QString, InstallState> reconciled;
    reconciled.reserve(static_cast<int>(m_rows.size()));
    for (auto const& row : m_rows) {
        if (auto const it = m_install.find(row.uuid); it != m_install.end()) {
            reconciled.insert(row.uuid, *it);
        }
    }
    m_install = std::move(reconciled);
    endResetModel();
    emit countChanged();
    emit installedCountChanged();
}

void PluginCatalogModel::replaceOpendeckRows(std::vector<CatalogEntry> rows) {
    // Same strategy as replaceStreamdockRows but scoped to source =
    // "opendeck". The two fetchers run independently and never collide
    // because their rows live in disjoint partitions of m_rows.
    beginResetModel();
    std::vector<CatalogEntry> kept;
    kept.reserve(m_rows.size() + rows.size());
    for (auto& row : m_rows) {
        if (row.source != QStringLiteral("opendeck")) {
            kept.push_back(std::move(row));
        }
    }
    for (auto& row : rows) {
        kept.push_back(std::move(row));
    }
    m_rows = std::move(kept);

    QHash<QString, InstallState> reconciled;
    reconciled.reserve(static_cast<int>(m_rows.size()));
    for (auto const& row : m_rows) {
        if (auto const it = m_install.find(row.uuid); it != m_install.end()) {
            reconciled.insert(row.uuid, *it);
        }
    }
    m_install = std::move(reconciled);
    endResetModel();
    emit countChanged();
    emit installedCountChanged();
}

namespace {

/// Find the row index for @p uuid, or -1 if not present.
int findRow(std::vector<CatalogEntry> const& rows, QString const& uuid) {
    auto const it = std::find_if(
        rows.begin(), rows.end(), [&](CatalogEntry const& e) { return e.uuid == uuid; });
    if (it == rows.end()) {
        return -1;
    }
    return static_cast<int>(std::distance(rows.begin(), it));
}

} // namespace

namespace {

/// Upper bound on a downloaded `.sdPlugin` archive (WR-04). A hostile or
/// compromised CDN could otherwise stream an unbounded body and fill the
/// user's home filesystem. 64 MiB comfortably covers real Stream Deck
/// plugins (typically < a few MB) with generous headroom.
inline constexpr qint64 kMaxPluginDownloadBytes = 64LL * 1024 * 1024;

/// ZIP local-file-header magic. A `.sdPlugin` is a zip per the Elgato SDK;
/// reject anything that does not begin with it before writing to disk.
inline constexpr char kZipMagic[4] = {'P', 'K', 0x03, 0x04};

/// Per-user plugin directory.
///
/// We park downloaded `.sdPlugin` archives here so the Stream Deck
/// runtime + the AJAZZ plugin host can pick them up on next start. The
/// path follows the platform's standard data-location convention so it
/// survives an app upgrade and respects each OS's XDG / AppData rules:
///
///   - Linux:   `~/.local/share/AJAZZ Control Center/plugins`
///   - macOS:   `~/Library/Application Support/AJAZZ Control Center/plugins`
///   - Windows: `%APPDATA%/AJAZZ Control Center/plugins`
///
/// Test seam: when g_pluginsDirOverride is non-empty the override path
/// is returned directly (tests write to a temp dir instead of the real
/// QStandardPaths::AppDataLocation).
[[nodiscard]] QString userPluginsDir() {
    if (!g_pluginsDirOverride.isEmpty()) {
        QDir const overrideDir(g_pluginsDirOverride);
        if (!overrideDir.exists()) {
            overrideDir.mkpath(QStringLiteral("."));
        }
        return overrideDir.absolutePath();
    }
    QString const base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir const dir(base + QStringLiteral("/plugins"));
    if (!dir.exists()) {
        dir.mkpath(QStringLiteral("."));
    }
    return dir.absolutePath();
}

} // namespace

QString PluginCatalogModel::validateDownloadedArchive(QByteArray const& body) {
    // WR-04: reject before writing to disk. Size cap defends against a hostile
    // CDN streaming an unbounded body; the ZIP magic rejects non-archives a
    // `.sdPlugin` could never be (the extractor would refuse them anyway).
    if (body.size() > kMaxPluginDownloadBytes) {
        return QStringLiteral("Downloaded archive exceeds the %1 MB limit.")
            .arg(kMaxPluginDownloadBytes / (1024 * 1024));
    }
    if (!body.startsWith(QByteArray(kZipMagic, sizeof kZipMagic))) {
        return QStringLiteral("Downloaded file is not a valid .sdPlugin (zip) archive.");
    }
    return {};
}

bool PluginCatalogModel::installFromFile(QString const& localPathOrUrl,
                                         bool userConfirmedUnsigned) {
    // Normalise: accept either a local path or a file:// URL (FileDialog).
    QUrl const asUrl = QUrl::fromUserInput(localPathOrUrl);
    QString const localPath = asUrl.isLocalFile() ? asUrl.toLocalFile() : localPathOrUrl;

    if (localPath.isEmpty()) {
        emit installFinished(localPathOrUrl, false, QStringLiteral("Invalid file path or URL."));
        return false;
    }

    // Step 1: read the file and apply the size + ZIP magic gate (WR-04 /
    // T-22-bomb). Mirrors the download-path guard so the rules are identical.
    QFile f(localPath);
    if (!f.open(QIODevice::ReadOnly)) {
        QString const err = QStringLiteral("Cannot open file: %1").arg(f.errorString());
        AJAZZ_LOG_WARN("plugin-catalog", "installFromFile: {}", err.toStdString());
        emit installFinished(localPath, false, err);
        return false;
    }
    // Cap the read at kMaxPluginDownloadBytes — do NOT read the whole file
    // if it is larger; that is the decompression-bomb defence.
    QByteArray const body = f.read(kMaxPluginDownloadBytes + 1);
    f.close();
    if (QString const err = validateDownloadedArchive(body); !err.isEmpty()) {
        AJAZZ_LOG_WARN("plugin-catalog",
                       "installFromFile '{}': {}",
                       localPath.toStdString(),
                       err.toStdString());
        emit installFinished(localPath, false, err);
        return false;
    }

    // Step 2: extract into a STAGING directory (sibling of installedPlugins/,
    // NOT inside it — T-22-toctou staging-before-promote invariant).
    QString const pluginsDir = userPluginsDir();
    if (pluginsDir.isEmpty()) {
        emit installFinished(
            localPath, false, QStringLiteral("Cannot resolve user plugins directory."));
        return false;
    }
    // Derive a stable archive name from the file's basename.
    QFileInfo const fi(localPath);
    QString const archiveName =
        fi.fileName().isEmpty() ? QStringLiteral("install.sdPlugin") : fi.fileName();
    // Staging parent: a sibling of the plugins/ directory so the discovered
    // path (installedPlugins/ == pluginsDir) is never touched until promote.
    QString const stagingParent =
        QDir(pluginsDir).absoluteFilePath(QStringLiteral("../.plugin_staging"));
    QDir().mkpath(stagingParent);

    // extractSdPluginArchive extracts into stagingParent/<archiveName>/
    bool const extractOk = extractSdPluginArchive(localPath, stagingParent, archiveName);
    if (!extractOk) {
        QString const err = QStringLiteral("Failed to extract plugin archive.");
        AJAZZ_LOG_WARN("plugin-catalog",
                       "installFromFile '{}': {}",
                       localPath.toStdString(),
                       err.toStdString());
        emit installFinished(localPath, false, err);
        return false;
    }

    // Step 3: verify the staged manifest (T-22-toctou, T-22-tamper-local,
    // T-22-unsigned). The staging dir is NOT in installedPlugins/ so even on
    // Refused the discoverable directory is unaffected.
    QString const stagedManifest =
        QDir(stagingParent).filePath(archiveName + QStringLiteral("/manifest.json"));

    VerifyOutcome const vout = verifyStagedPlugin(stagedManifest);

    if (vout.verdict == VerifyVerdict::Refused) {
        // Tampered OR unsigned (hard-refuse) — quarantine staging dir.
        AJAZZ_LOG_WARN("plugin-catalog",
                       "installFromFile '{}': signature Refused ({}); quarantining",
                       localPath.toStdString(),
                       vout.reason.toStdString());
        QDir(QDir(stagingParent).filePath(archiveName)).removeRecursively();
        QString const reason =
            vout.reason.isEmpty() ? QStringLiteral("signature verification failed") : vout.reason;
        emit installFinished(
            localPath, false, tr("Plugin signature verification failed: %1").arg(reason));
        return false;
    }

    if (vout.verdict == VerifyVerdict::SelfSigned && !userConfirmedUnsigned) {
        // Developer-sideload policy: SelfSigned requires explicit confirmation.
        // WR-02 fix: the confirm path re-extracts from the source file into a
        // fresh staging dir, so the old staging dir is never re-used. Remove it
        // now so orphaned staging dirs do not accumulate across cancel/retry cycles.
        AJAZZ_LOG_INFO("plugin-catalog",
                       "installFromFile '{}': SelfSigned — awaiting user confirm; "
                       "removing staging dir",
                       localPath.toStdString());
        QDir(QDir(stagingParent).filePath(archiveName)).removeRecursively();
        QDir(stagingParent).rmdir(QStringLiteral("."));
        emit installFinished(
            localPath, false, QStringLiteral("self-signed plugin -- confirm to install"));
        return false;
    }

    // Step 4: promote — atomic rename from staging into installedPlugins/.
    // The Phase-18 layout expects: <pluginsDir>/<name>.sdPlugin/manifest.json
    // so we rename the staging subdir into pluginsDir directly.
    QString const stagedDir = QDir(stagingParent).filePath(archiveName);
    QString const promotedDir = QDir(pluginsDir).filePath(archiveName);

    // Remove any existing install at the target path before rename
    // (idempotent re-install case).
    if (QDir(promotedDir).exists()) {
        QDir(promotedDir).removeRecursively();
    }

    bool const renamed = QDir().rename(stagedDir, promotedDir);
    if (!renamed) {
        // Rename across filesystems can fail — fall back to a file-by-file
        // copy of the already-verified staged directory.
        // CR-02 fix: do NOT re-extract from localPath here. localPath is
        // user-controlled and a TOCTOU race could swap the file between the
        // first extract (into staging) and this point. Instead copy the
        // verified staged bits into promotedDir, then re-verify.
        AJAZZ_LOG_WARN("plugin-catalog",
                       "installFromFile '{}': rename failed (cross-fs?); "
                       "falling back to copy of staged dir",
                       localPath.toStdString());
        QDir().mkpath(promotedDir);
        bool copyOk = true;
        {
            QDir const srcDir(stagedDir);
            QStringList const entries =
                srcDir.entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
            for (QString const& entry : entries) {
                QString const srcPath = srcDir.filePath(entry);
                QString const dstPath = QDir(promotedDir).filePath(entry);
                QFileInfo const info(srcPath);
                if (info.isDir()) {
                    QDir().mkpath(dstPath);
                    // Recurse one level (manifest + Code/ sub-directory is the
                    // typical sdPlugin layout; deep trees are unusual).
                    QDir const subSrc(srcPath);
                    for (QString const& sub :
                         subSrc.entryList(QDir::Files | QDir::NoDotAndDotDot)) {
                        if (!QFile::copy(subSrc.filePath(sub), QDir(dstPath).filePath(sub))) {
                            copyOk = false;
                            break;
                        }
                    }
                } else {
                    if (!QFile::copy(srcPath, dstPath)) {
                        copyOk = false;
                    }
                }
                if (!copyOk) {
                    break;
                }
            }
        }
        if (!copyOk) {
            QDir(promotedDir).removeRecursively();
            QDir(stagedDir).removeRecursively();
            emit installFinished(
                localPath, false, QStringLiteral("Failed to promote plugin to install directory."));
            return false;
        }

        // CR-02: re-verify the freshly-copied result before treating it as
        // promoted. The copy should be bit-identical to the staged dir, but
        // re-verification is the invariant that must hold for every promoted
        // plugin directory.
        QString const copiedManifest = QDir(promotedDir).filePath(QStringLiteral("manifest.json"));
        VerifyOutcome const vout2 = verifyStagedPlugin(copiedManifest);
        if (vout2.verdict == VerifyVerdict::Refused) {
            AJAZZ_LOG_WARN("plugin-catalog",
                           "installFromFile '{}': re-verify after copy-fallback refused ({}); "
                           "quarantining",
                           localPath.toStdString(),
                           vout2.reason.toStdString());
            QDir(promotedDir).removeRecursively();
            QDir(stagedDir).removeRecursively();
            emit installFinished(
                localPath,
                false,
                QStringLiteral("Re-verification after copy failed: %1").arg(vout2.reason));
            return false;
        }
    }

    // Clean up staging parent if empty.
    QDir(stagingParent).removeRecursively();

    // Flip install state and emit signals (same pattern as network install()).
    // WR-03 fix: key by UUID (not by localPath) so m_install stays bounded.
    // The localPath key was never cleaned up by reload()/uninstall() and caused
    // installedCount() to drift above the true count over repeated installs
    // from different file paths.
    // Try to find a matching catalogue row by the promoted dir name (archiveName
    // may match a UUID in the catalogue if the user is re-installing).
    QString const candidateUuid =
        archiveName.endsWith(QStringLiteral(".sdPlugin")) ? archiveName.chopped(9) : archiveName;
    int const r = findRow(m_rows, candidateUuid);
    if (r >= 0) {
        auto& rowState = m_install[candidateUuid];
        rowState.installed = true;
        rowState.enabled = true;
        QModelIndex const idx = index(r);
        emit dataChanged(idx, idx, {InstalledRole, EnabledRole});
    }
    emit installedCountChanged();
    AJAZZ_LOG_INFO("plugin-catalog",
                   "installFromFile '{}' OK -> {} ({})",
                   localPath.toStdString(),
                   promotedDir.toStdString(),
                   verdictToTrustLevel(vout.verdict).toStdString());
    emit installFinished(localPath, true, QString{});
    return true;
}

bool PluginCatalogModel::install(QString const& uuid) {
    int const row = findRow(m_rows, uuid);
    if (row < 0) {
        AJAZZ_LOG_WARN("plugin-catalog", "install: uuid '{}' not in catalogue", uuid.toStdString());
        return false;
    }
    auto& state = m_install[uuid];
    if (state.installed) {
        // Idempotent: surface a successful "already installed" outcome
        // so the QML button binding can flip without ambiguity.
        emit installFinished(uuid, true, QString{});
        return true;
    }

    auto const& entry = m_rows[static_cast<std::size_t>(row)];
    if (!entry.downloadUrl.isValid() || entry.downloadUrl.isEmpty() ||
        entry.downloadUrl.scheme().toLower() != QStringLiteral("https")) {
        // No direct download URL on file: fall back to the browser
        // bridge so the user can still grab the plugin from the
        // upstream catalogue. This is the legitimate path for
        // OpenDeck (no per-plugin API) and any local/community row
        // that ships only a landing page.
        AJAZZ_LOG_INFO("plugin-catalog",
                       "install: no direct downloadUrl for '{}'; opening upstream page",
                       uuid.toStdString());
        bool const opened = openUpstream(uuid);
        emit installFinished(uuid,
                             opened,
                             opened ? QString{}
                                    : QStringLiteral("No download URL on file and no "
                                                     "browser-openable fallback."));
        return opened;
    }

    // Real in-app install path: HTTPS GET against the upstream CDN, save
    // the .sdPlugin archive under userPluginsDir() so the plugin host
    // can pick it up on next start. Extraction of the .sdPlugin
    // (which is a zip per the Elgato Stream Deck SDK) is deferred to
    // the plugin host's first-launch scan — the archive landing on
    // disk is the load-bearing signal that the install succeeded.
    if (m_downloader == nullptr) {
        m_downloader = new QNetworkAccessManager(this);
    }
    QString const destDir = userPluginsDir();
    if (destDir.isEmpty()) {
        QString const err = QStringLiteral("Cannot resolve user plugins directory.");
        AJAZZ_LOG_WARN("plugin-catalog", "install: {}", err.toStdString());
        emit installFinished(uuid, false, err);
        return false;
    }
    QString const fileBase = entry.streamdockProductId.isEmpty() ? uuid : entry.streamdockProductId;
    QString const destPath = QDir(destDir).filePath(fileBase + QStringLiteral(".sdPlugin"));

    AJAZZ_LOG_INFO("plugin-catalog",
                   "install: GET {} -> {}",
                   entry.downloadUrl.toString().toStdString(),
                   destPath.toStdString());

    QNetworkRequest req(entry.downloadUrl);
    // Follow CDN redirects (cdn1.key123.vip occasionally 301s to alt mirrors).
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* const reply = m_downloader->get(req);

    QPointer<PluginCatalogModel> self(this);
    QString const uuidCopy = uuid;
    QString const destCopy = destPath;

    QObject::connect(reply,
                     &QNetworkReply::downloadProgress,
                     this,
                     [self, reply, uuidCopy](qint64 received, qint64 total) {
                         if (!self) {
                             return;
                         }
                         // Abort an oversized transfer (WR-04) — up-front when the
                         // advertised Content-Length is too big, and defensively if
                         // a server streams past the cap without a length. The
                         // finished handler maps the resulting cancel to a clear error.
                         if (received > kMaxPluginDownloadBytes ||
                             (total > 0 && total > kMaxPluginDownloadBytes)) {
                             reply->abort();
                             return;
                         }
                         if (total <= 0) {
                             return;
                         }
                         int const pct =
                             static_cast<int>(std::clamp<qint64>((received * 100) / total, 0, 100));
                         emit self->installProgressChanged(uuidCopy, pct);
                     });

    QObject::connect(reply, &QNetworkReply::finished, this, [self, reply, uuidCopy, destCopy]() {
        reply->deleteLater();
        if (!self) {
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            // The only abort() we issue is the size-cap guard above, so a
            // cancel here means the download exceeded the limit (WR-04).
            QString const err = (reply->error() == QNetworkReply::OperationCanceledError)
                                    ? QStringLiteral("Download exceeds the %1 MB limit.")
                                          .arg(kMaxPluginDownloadBytes / (1024 * 1024))
                                    : reply->errorString();
            AJAZZ_LOG_WARN("plugin-catalog",
                           "install '{}' failed: {}",
                           uuidCopy.toStdString(),
                           err.toStdString());
            emit self->installFinished(uuidCopy, false, err);
            return;
        }
        QByteArray const body = reply->readAll();
        // Final authoritative gates before touching disk (WR-04): the size cap
        // (in case the body arrived without progress signals) and the ZIP magic
        // — a `.sdPlugin` is a zip, so reject non-archives up front with a clear
        // message rather than writing a bogus blob the extractor would refuse.
        if (QString const err = validateDownloadedArchive(body); !err.isEmpty()) {
            AJAZZ_LOG_WARN(
                "plugin-catalog", "install '{}': {}", uuidCopy.toStdString(), err.toStdString());
            emit self->installFinished(uuidCopy, false, err);
            return;
        }
        QFile out(destCopy);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            QString const err =
                QStringLiteral("Cannot write %1: %2").arg(destCopy, out.errorString());
            AJAZZ_LOG_WARN("plugin-catalog", "{}", err.toStdString());
            emit self->installFinished(uuidCopy, false, err);
            return;
        }
        if (out.write(body) != body.size()) {
            QString const err = QStringLiteral("Short write to %1").arg(destCopy);
            AJAZZ_LOG_WARN("plugin-catalog", "{}", err.toStdString());
            out.close();
            out.remove();
            emit self->installFinished(uuidCopy, false, err);
            return;
        }
        out.close();

        // Extract the archive in place so the plugin host
        // finds an expanded `<id>.sdPlugin/manifest.json`
        // tree instead of an opaque zip.
        QFileInfo const archiveInfo(destCopy);
        QString const archiveDir = archiveInfo.absolutePath();
        QString const archiveName = archiveInfo.fileName();
        bool const extractOk = extractSdPluginArchive(destCopy, archiveDir, archiveName);
        if (!extractOk) {
            // CR-01 fix: extraction failure must NOT fall through to
            // markInstalled + installFinished(success=true) — the verify gate
            // would be completely bypassed. Remove the archive so the
            // launch-sweep can never pick it up, and emit failure.
            AJAZZ_LOG_WARN("plugin-catalog",
                           "install '{}': extract failed; removing archive at {}",
                           uuidCopy.toStdString(),
                           destCopy.toStdString());
            QFile::remove(destCopy);
            emit self->installFinished(
                uuidCopy, false, QStringLiteral("Failed to extract plugin archive."));
            return;
        }
        QFile::remove(destCopy);

        // PLUGIN-14 verify gate (T-22-backdoor): run Ed25519 signature
        // verification on the extracted manifest BEFORE marking as installed.
        // A tampered or unsigned package is refused here so it is never
        // promoted by the network path. SelfSigned is allowed (explicit
        // sideload UX is plan 02's concern; network path treats it as
        // allowed-but-logged). Refused = quarantine the extracted dir.
        {
            QString const extractedManifest =
                QDir(archiveDir).filePath(archiveName + QStringLiteral("/manifest.json"));
            VerifyOutcome const vout = verifyStagedPlugin(extractedManifest);
            if (vout.verdict == VerifyVerdict::Refused) {
                AJAZZ_LOG_WARN(
                    "plugin-catalog",
                    "install '{}': signature verification refused ({}); quarantining extracted dir",
                    uuidCopy.toStdString(),
                    vout.reason.toStdString());
                // Remove the extracted directory so the refused package is not
                // discoverable by the plugin host on next launch.
                QDir(QDir(archiveDir).filePath(archiveName)).removeRecursively();
                emit self->installFinished(
                    uuidCopy,
                    false,
                    PluginCatalogModel::tr("Plugin signature verification failed: %1")
                        .arg(vout.reason));
                return;
            }
            AJAZZ_LOG_INFO("plugin-catalog",
                           "install '{}': signature verification OK ({})",
                           uuidCopy.toStdString(),
                           verdictToTrustLevel(vout.verdict).toStdString());
        }

        int const r = findRow(self->m_rows, uuidCopy);
        if (r >= 0) {
            auto& s = self->m_install[uuidCopy];
            s.installed = true;
            s.enabled = true;
            QModelIndex const idx = self->index(r);
            emit self->dataChanged(idx, idx, {InstalledRole, EnabledRole});
            emit self->installedCountChanged();
        }
        AJAZZ_LOG_INFO("plugin-catalog",
                       "install '{}' OK ({} bytes) -> {}",
                       uuidCopy.toStdString(),
                       static_cast<long long>(body.size()),
                       destCopy.toStdString());
        emit self->installFinished(uuidCopy, true, QString{});
    });
    return true;
}

bool PluginCatalogModel::uninstall(QString const& uuid) {
    int const row = findRow(m_rows, uuid);
    if (row < 0) {
        return false;
    }
    auto const it = m_install.find(uuid);
    if (it == m_install.end() || !it->installed) {
        return false; // nothing to do.
    }
    it->installed = false;
    it->enabled = false;
    QModelIndex const idx = index(row);
    emit dataChanged(idx, idx, {InstalledRole, EnabledRole});
    emit installedCountChanged();
    return true;
}

bool PluginCatalogModel::openUpstream(QString const& uuid) const {
    int const row = findRow(m_rows, uuid);
    if (row < 0) {
        AJAZZ_LOG_WARN(
            "plugin-catalog", "openUpstream: uuid '{}' not in catalogue", uuid.toStdString());
        return false;
    }
    auto const& entry = m_rows[static_cast<std::size_t>(row)];

    QUrl target;
    if (entry.source == QStringLiteral("streamdock")) {
        // Stream Dock store URL pattern per the vendor's published web
        // catalogue. Falls through to the store landing page when no
        // product id is on file (still better than nothing).
        if (!entry.streamdockProductId.isEmpty()) {
            target = QUrl(QStringLiteral("https://stream-dock.com/store/product?id=%1")
                              .arg(entry.streamdockProductId));
        } else {
            target = QUrl(QStringLiteral("https://stream-dock.com/store"));
        }
    } else if (entry.source == QStringLiteral("community") ||
               entry.compatibility == QStringLiteral("opendeck")) {
        // OpenDeck has no per-plugin URL contract; landing page lists
        // every catalogue entry alphabetically.
        target = QUrl(QStringLiteral("https://opendeck.app/plugins"));
    } else if (!entry.iconUrl.isEmpty() && entry.iconUrl.isValid()) {
        // Last-resort heuristic: the icon usually lives on the same
        // origin as the plugin listing page, so opening the icon URL
        // lands the user "near" the right page even when no canonical
        // URL is on file. Better than a silent failure.
        target = entry.iconUrl;
    }

    if (target.isEmpty() || !target.isValid()) {
        AJAZZ_LOG_WARN("plugin-catalog",
                       "openUpstream: no usable URL for uuid '{}' (source='{}')",
                       uuid.toStdString(),
                       entry.source.toStdString());
        return false;
    }
    AJAZZ_LOG_INFO(
        "plugin-catalog", "openUpstream: launching browser to {}", target.toString().toStdString());
    return QDesktopServices::openUrl(target);
}

bool PluginCatalogModel::toggleEnabled(QString const& uuid) {
    int const row = findRow(m_rows, uuid);
    if (row < 0) {
        return false;
    }
    auto& state = m_install[uuid];
    if (!state.installed) {
        return false; // can't enable something that isn't installed.
    }
    state.enabled = !state.enabled;
    QModelIndex const idx = index(row);
    emit dataChanged(idx, idx, {EnabledRole});
    return state.enabled;
}

QVariantMap PluginCatalogModel::entryFor(QString const& uuid) const {
    int const row = findRow(m_rows, uuid);
    if (row < 0) {
        return {};
    }
    auto const& src = m_rows[static_cast<std::size_t>(row)];
    auto const state = m_install.value(uuid);
    return {
        {"uuid", src.uuid},
        {"name", src.name},
        {"version", src.version},
        {"author", src.author},
        {"description", src.description},
        {"iconUrl", src.iconUrl},
        {"category", src.category},
        {"tags", src.tags},
        {"devices", src.devices},
        {"compatibility", src.compatibility},
        {"sizeBytes", src.sizeBytes},
        {"verified", src.verified},
        {"installed", state.installed},
        {"enabled", state.enabled},
        {"source", src.source},
        {"streamdockProductId", src.streamdockProductId},
    };
}

std::vector<CatalogEntry> PluginCatalogModel::mockFixture() {
    // Hand-curated fixture covering the local + community catalogue
    // sources used by the "All" / "Installed" / "Community" tabs of the
    // Plugin Store. The AJAZZ Streamdock tab is filled in by
    // @ref StreamdockCatalogFetcher — either from the live upstream
    // catalogue, the on-disk mirror, or the bundled offline fallback
    // — so this fixture intentionally does not list any streamdock rows
    // (they would otherwise be replaced on the very next snapshot, with
    // a momentary flicker).
    std::vector<CatalogEntry> rows;
    rows.reserve(8);

    rows.push_back({
        /*uuid*/ QStringLiteral("com.aiacos.spotify-now-playing"),
        /*name*/ QStringLiteral("Spotify Now Playing"),
        /*version*/ QStringLiteral("1.2.0"),
        /*author*/ QStringLiteral("Aiacos"),
        /*description*/
        QStringLiteral("Show the currently playing track on a key, with album art."),
        /*iconUrl*/ QUrl(QStringLiteral("qrc:/qt/qml/AjazzControlCenter/icons/app.svg")),
        /*category*/ QStringLiteral("Streaming"),
        /*tags*/ {QStringLiteral("music"), QStringLiteral("media")},
        /*devices*/ {QStringLiteral("akp153"), QStringLiteral("akp153e"), QStringLiteral("akp815")},
        /*compatibility*/ QStringLiteral("native"),
        /*sizeBytes*/ QStringLiteral("1.4 MB"),
        /*verified*/ true,
    });
    rows.push_back({
        QStringLiteral("com.elgato.mute"),
        QStringLiteral("OBS Mute Toggle"),
        QStringLiteral("3.0.1"),
        QStringLiteral("Elgato (compat)"),
        QStringLiteral("Mute / unmute an OBS audio source. Stream Deck SDK-2 plugin "
                       "loaded via the streamdeck compatibility layer."),
        QUrl(QStringLiteral("qrc:/qt/qml/AjazzControlCenter/icons/app.svg")),
        QStringLiteral("Streaming"),
        {QStringLiteral("obs"), QStringLiteral("audio")},
        {QStringLiteral("akp153"), QStringLiteral("akp815")},
        QStringLiteral("streamdeck"),
        QStringLiteral("3.8 MB"),
        false,
    });
    rows.push_back({
        QStringLiteral("dev.opendeck.weather"),
        QStringLiteral("Weather"),
        QStringLiteral("0.4.2"),
        QStringLiteral("OpenDeck community"),
        QStringLiteral("Display the current weather for a configurable location. "
                       "OpenDeck plugin loaded via the opendeck compatibility layer."),
        QUrl(QStringLiteral("qrc:/qt/qml/AjazzControlCenter/icons/app.svg")),
        QStringLiteral("Information"),
        {QStringLiteral("weather"), QStringLiteral("api")},
        {QStringLiteral("akp03"), QStringLiteral("akp153"), QStringLiteral("akp815")},
        QStringLiteral("opendeck"),
        QStringLiteral("680 KB"),
        true,
    });
    rows.push_back({
        QStringLiteral("com.aiacos.system-monitor"),
        QStringLiteral("System Monitor"),
        QStringLiteral("0.9.0"),
        QStringLiteral("Aiacos"),
        QStringLiteral("CPU / memory / network usage tiles for AKP153 and AKP815."),
        QUrl(QStringLiteral("qrc:/qt/qml/AjazzControlCenter/icons/app.svg")),
        QStringLiteral("System"),
        {QStringLiteral("monitoring"), QStringLiteral("cpu")},
        {QStringLiteral("akp153"), QStringLiteral("akp815")},
        QStringLiteral("native"),
        QStringLiteral("520 KB"),
        true,
    });
    rows.push_back({
        QStringLiteral("com.aiacos.philips-hue"),
        QStringLiteral("Philips Hue"),
        QStringLiteral("2.1.0"),
        QStringLiteral("Aiacos"),
        QStringLiteral("Toggle Hue scenes and groups from the deck."),
        QUrl(QStringLiteral("qrc:/qt/qml/AjazzControlCenter/icons/app.svg")),
        QStringLiteral("Smart Home"),
        {QStringLiteral("hue"), QStringLiteral("lighting")},
        {QStringLiteral("akp153"), QStringLiteral("akp815")},
        QStringLiteral("native"),
        QStringLiteral("980 KB"),
        true,
    });
    rows.push_back({
        QStringLiteral("dev.community.discord"),
        QStringLiteral("Discord PTT"),
        QStringLiteral("1.0.4"),
        QStringLiteral("community"),
        QStringLiteral("Push-to-talk + mute toggle for Discord. OpenDeck plugin."),
        QUrl(QStringLiteral("qrc:/qt/qml/AjazzControlCenter/icons/app.svg")),
        QStringLiteral("Communication"),
        {QStringLiteral("discord"), QStringLiteral("voice")},
        {QStringLiteral("akp153"), QStringLiteral("akp815"), QStringLiteral("akp03")},
        QStringLiteral("opendeck"),
        QStringLiteral("710 KB"),
        /*verified*/ false,
        /*source*/ QStringLiteral("community"),
    });
    rows.push_back({
        QStringLiteral("com.aiacos.macro-recorder"),
        QStringLiteral("Macro Recorder"),
        QStringLiteral("0.3.0"),
        QStringLiteral("Aiacos"),
        QStringLiteral("Record and replay keyboard / mouse macros bound to any key."),
        QUrl(QStringLiteral("qrc:/qt/qml/AjazzControlCenter/icons/app.svg")),
        QStringLiteral("Productivity"),
        {QStringLiteral("macro"), QStringLiteral("automation")},
        {QStringLiteral("akp153"), QStringLiteral("akp815"), QStringLiteral("akp03")},
        QStringLiteral("native"),
        QStringLiteral("310 KB"),
        true,
    });
    rows.push_back({
        QStringLiteral("com.aiacos.timer"),
        QStringLiteral("Pomodoro Timer"),
        QStringLiteral("1.1.0"),
        QStringLiteral("Aiacos"),
        QStringLiteral("Pomodoro / countdown timer with on-key progress display."),
        QUrl(QStringLiteral("qrc:/qt/qml/AjazzControlCenter/icons/app.svg")),
        QStringLiteral("Productivity"),
        {QStringLiteral("timer"), QStringLiteral("pomodoro")},
        {QStringLiteral("akp153"), QStringLiteral("akp815")},
        QStringLiteral("native"),
        QStringLiteral("210 KB"),
        true,
    });

    return rows;
}

} // namespace ajazz::app
