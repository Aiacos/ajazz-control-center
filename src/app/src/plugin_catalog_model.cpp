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
#include "mirabox_github_catalog_fetcher.hpp"
#include "mirabox_github_installer.hpp"
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
#include <limits>

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

/// US1 (spec FR-006): a catalogue row is installable in-app iff it carries a
/// resolvable https package URL. Shared by data() (the InstallableInApp /
/// UnavailableReason roles) and install() (which is a no-op returning false for
/// a non-installable row — NO browser fallback). Same predicate both sites use,
/// so the row's button state and install()'s behaviour can never disagree.
[[nodiscard]] bool entryInstallableInApp(CatalogEntry const& entry) {
    // Mirabox-GitHub rows have no single archive URL; they are installed by
    // fetching the plugin's subtree from GitHub and assembling the bundle (see
    // MiraboxGithubInstaller). A non-empty repo path (stashed in
    // streamdockProductId at catalogue time) is the install handle. Whether the
    // dir actually ships a manifest.json (vs source-only) is resolved at fetch
    // time — a source-only row fails install() with a clear message.
    if (entry.source == QStringLiteral("mirabox-github")) {
        return !entry.streamdockProductId.isEmpty();
    }
    // StreamDock rows carry only a RELATIVE `download` in the `/list` payload, so
    // entry.downloadUrl is empty at catalogue time. The absolute CDN archive URL
    // is resolved on demand at install() via productInfo/get/<id>, so a non-empty
    // product id is the install handle (mirrors mirabox-github above). A row that
    // already carries a resolved https URL falls through to the generic check.
    if (entry.source == QStringLiteral("streamdock") && !entry.streamdockProductId.isEmpty()) {
        return true;
    }
    // OpenDeck legacy rows hard-code downloads on appstore.elgato.com, a host
    // Elgato decommissioned (no DNS). Treat those as not installable so the row
    // shows a disabled button + reason instead of failing with an obscure DNS
    // error on click. (Real Marketplace assets live on opaque-UUID mp-cdn paths
    // that cannot be derived from the plugin id, so there is no in-app rescue.)
    if (entry.downloadUrl.host().compare(QStringLiteral("appstore.elgato.com"),
                                         Qt::CaseInsensitive) == 0) {
        return false;
    }
    return entry.downloadUrl.isValid() && !entry.downloadUrl.isEmpty() &&
           entry.downloadUrl.scheme().toLower() == QStringLiteral("https");
}

/// WR-01: per-plugin consent predicate. Reads the QSettings key written by
/// PluginCatalogModel::allowPlugin() (`plugins/allowed/<uuid>=true`). @p
/// pluginDirName may be either the install dir name (`<uuid>.sdPlugin`) or a
/// bare uuid; the trailing `.sdPlugin` is stripped so the key matches the one
/// allowPlugin() stores. CR-01: callers MUST gate this behind a
/// VerifyVerdict::Unsigned check — it must NEVER promote a Refused/tampered
/// package (allowPlugin() refuses to even write the key for tampered rows, but
/// the verdict gate at every read site is the load-bearing invariant).
[[nodiscard]] bool perPluginAllowed(QString const& pluginDirName) {
    QString const uuid = pluginDirName.endsWith(QStringLiteral(".sdPlugin"), Qt::CaseInsensitive)
                             ? pluginDirName.chopped(9)
                             : pluginDirName;
    QSettings settings;
    return settings.value(QStringLiteral("plugins/allowed/") + uuid, false).toBool();
}

/// Suffix appended to an `<uuid>.sdPlugin` directory to quarantine it
/// NON-DESTRUCTIVELY. A `*.sdPlugin.disabled` directory no longer matches the
/// `*.sdPlugin` glob used by the launch-sweep, installedPlugins(), and
/// PluginManager::discover(), so it is never verified, listed, or spawned —
/// but the user's files stay on disk. The launch-sweep restores (renames back)
/// a quarantined dir once consent exists (global toggle, env var, or
/// per-plugin allow), and allowPlugin() restores it on explicit consent.
constexpr QLatin1StringView kQuarantineSuffix(".disabled");

/// List subdirectories of @p dir whose name ends with @p suffix, compared
/// CASE-INSENSITIVELY. QDir glob name-filters are case-sensitive on Unix, so a
/// hand-named `Foo.SDPlugin` dir would spawn (PluginManager::discover matches
/// the suffix case-insensitively) yet stay invisible to the launch-sweep /
/// installedPlugins() / verify passes that used the `*.sdPlugin` glob
/// (audit 3.9). Every pass over the user plugins dir must share this filter.
[[nodiscard]] QStringList listDirsWithSuffixCi(QDir const& dir, QString const& suffix) {
    QStringList out;
    QStringList const all = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (QString const& entry : all) {
        if (entry.endsWith(suffix, Qt::CaseInsensitive)) {
            out.append(entry);
        }
    }
    return out;
}

/// Resolve the plugin-owner UUID of an installed dir from its manifest.
/// Prefers the AJAZZ PUUID extension; Elgato manifests carry no top-level
/// plugin UUID, so fall back to the longest reverse-DNS prefix shared by the
/// action UUIDs (e.g. actions com.foo.bar.{a,b} -> owner com.foo.bar).
/// Returns an empty string when the manifest is missing or unusable.
[[nodiscard]] QString resolveOwnerUuidFromManifest(QString const& pluginDirPath) {
    QFile mf(QDir(pluginDirPath).filePath(QStringLiteral("manifest.json")));
    if (!mf.open(QIODevice::ReadOnly)) {
        return {};
    }
    auto const parsed = parsePluginManifest(mf.readAll());
    if (!parsed) {
        return {};
    }
    if (!parsed->puuid.isEmpty()) {
        return parsed->puuid;
    }
    auto const commonDottedPrefix = [](QString const& a, QString const& b) {
        QStringList const as = a.split(QLatin1Char('.'));
        QStringList const bs = b.split(QLatin1Char('.'));
        QStringList out;
        for (int i = 0; i < as.size() && i < bs.size() && as[i] == bs[i]; ++i) {
            out << as[i];
        }
        return out.join(QLatin1Char('.'));
    };
    QString owner;
    for (PluginAction const& a : parsed->actions) {
        if (a.uuid.isEmpty()) {
            continue;
        }
        owner = owner.isEmpty() ? a.uuid : commonDottedPrefix(owner, a.uuid);
    }
    return owner;
}

/// #81: derive a safe install-directory name (`<UUID>.sdPlugin`) from a
/// manifest's plugin UUID. The Stream Deck convention is that the install
/// directory is named for the manifest UUID, and the action-owner match keys
/// off that directory name (an action UUID is a dotted prefix of the plugin
/// UUID, compared against the install-dir key); the catalogue install() path
/// already follows it. The manifest is ATTACKER-CONTROLLED, so a UUID like
/// "../../etc" must never name a directory: accept only a conservative
/// single-path-component charset (reverse-DNS UUIDs are [A-Za-z0-9._-]) and
/// reject path separators, leading dots, and any ".." run. Returns an empty
/// string when the UUID is absent or unsafe, so the caller falls back to the
/// already-sanitized file-derived name.
[[nodiscard]] QString safeUuidDirName(QString const& uuid) {
    if (uuid.isEmpty() || uuid.size() > 255) {
        return {};
    }
    for (QChar const c : uuid) {
        bool const ok = (c >= QLatin1Char('A') && c <= QLatin1Char('Z')) ||
                        (c >= QLatin1Char('a') && c <= QLatin1Char('z')) ||
                        (c >= QLatin1Char('0') && c <= QLatin1Char('9')) || c == QLatin1Char('.') ||
                        c == QLatin1Char('_') || c == QLatin1Char('-');
        if (!ok) {
            return {};
        }
    }
    if (uuid.startsWith(QLatin1Char('.')) || uuid.contains(QStringLiteral(".."))) {
        return {};
    }
    return uuid + QStringLiteral(".sdPlugin");
}

/// #82: derive a plugin id from the longest common dotted-component prefix of the
/// action UUIDs. Manifests in the Elgato Stream Deck SDKv2 format (and many
/// community plugins) carry NO top-level UUID/PUUID — the plugin identity is the
/// `.sdPlugin` directory name, and every action UUID is a dotted child of it
/// (e.g. plugin `com.elgato.cpu`, action `com.elgato.cpu.cpu`). When such a
/// plugin is sideloaded from a file whose name diverges from that identity, #81's
/// file-name fallback breaks the action-owner match. Recover the identity here:
/// return the longest dotted prefix that is a PROPER prefix of EVERY action UUID
/// (so `<id>.` still prefix-matches each action), or empty when none exists.
[[nodiscard]] QString pluginIdFromActionUuids(std::vector<PluginAction> const& actions) {
    std::vector<QStringList> parts;
    int minLen = std::numeric_limits<int>::max();
    for (auto const& a : actions) {
        if (a.uuid.isEmpty()) {
            continue;
        }
        QStringList const comps = a.uuid.split(QLatin1Char('.'), Qt::SkipEmptyParts);
        if (comps.isEmpty()) {
            continue;
        }
        minLen = std::min(minLen, static_cast<int>(comps.size()));
        parts.push_back(comps);
    }
    if (parts.empty()) {
        return {};
    }
    // Longest run of leading components shared by ALL action UUIDs.
    int commonLen = 0;
    for (; commonLen < minLen; ++commonLen) {
        QString const& token = parts.front().at(commonLen);
        bool const shared = std::all_of(parts.begin(), parts.end(), [&](QStringList const& p) {
            return p.at(commonLen) == token;
        });
        if (!shared) {
            break;
        }
    }
    // The id must be a PROPER prefix of every action UUID so the owner-match
    // `<id>.` resolves: if the shared run spans an entire (shortest) action UUID,
    // drop its last component.
    if (commonLen >= minLen) {
        --commonLen;
    }
    if (commonLen < 1) {
        return {};
    }
    QStringList idParts;
    for (int i = 0; i < commonLen; ++i) {
        idParts << parts.front().at(i);
    }
    return idParts.join(QLatin1Char('.'));
}

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
      m_opendeckFetcher(std::make_unique<OpenDeckCatalogFetcher>(this)),
      m_miraboxGithubFetcher(std::make_unique<MiraboxGithubCatalogFetcher>(this)) {
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
        // Plan 27-04 / PLUGIN-16: load the allow-unsigned-plugins flag.
        // Default false — unsigned plugins are blocked until the user
        // explicitly enables the setting. CR-01: this flag gates ONLY
        // VerifyVerdict::Unsigned; the Refused/tampered branch is
        // unconditional-quarantine regardless of this flag.
        m_allowUnsignedPlugins =
            settings.value(QStringLiteral("plugins/allowUnsignedPlugins"), false).toBool();
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

    // Bundled first-party plugins (Phase 6c): seed any `<uuid>.sdPlugin`
    // shipped in the install payload (share/ajazz-control-center/
    // bundled-plugins/ on GenericDataLocation, or $AJAZZ_BUNDLED_PLUGINS_DIR
    // for dev/test runs) into the user plugins dir on first run. A dir that
    // already exists — or that the user quarantined (`.disabled`) or removed
    // after a previous seed (the seed marker records that) — is left alone,
    // so the user stays in control. Seeded plugins are first-party: persist
    // the same per-plugin consent installFromCatalog records, so the
    // unsigned-verify gate below admits them without the global toggle.
    seedBundledPlugins(pluginsDir);

    // PLUGIN-14 verify gate (T-22-backdoor): scan every freshly-extracted
    // (or pre-existing) `.sdPlugin` directory and quarantine any whose
    // manifest fails Ed25519 verification. This closes the launch-sweep
    // back door: a tampered package dropped into the plugins dir is never
    // left in a discoverable state for the Phase-18 PluginManager.
    // Seam: verify after the sweep; do NOT modify sdplugin_extractor internals.
    {
        QDir dir(pluginsDir); // non-const: QDir::rename() below is a mutator
        // Restore pass: a previously-quarantined `*.sdPlugin.disabled` dir is
        // renamed back once consent NOW exists (the user flipped the global
        // toggle, set the env var, or recorded a per-plugin allow since the
        // quarantine). The restored dir then flows through the verify loop
        // below like any other entry, so CR-01 still holds: a tampered dir
        // that somehow got restored is re-verified and removed as Refused.
        QStringList const disabled =
            listDirsWithSuffixCi(dir, QStringLiteral(".sdPlugin") + kQuarantineSuffix);
        for (QString const& entry : disabled) {
            QString const original = entry.chopped(kQuarantineSuffix.size());
            if (!consentToUnsigned() && !perPluginAllowed(original)) {
                continue; // still no consent — stays quarantined, untouched
            }
            if (QFileInfo::exists(dir.filePath(original))) {
                AJAZZ_LOG_WARN("plugin-catalog",
                               "launch-sweep restore: '{}' consented but '{}' already "
                               "exists; leaving quarantined copy in place",
                               entry.toStdString(),
                               original.toStdString());
                continue;
            }
            if (dir.rename(entry, original)) {
                AJAZZ_LOG_INFO("plugin-catalog",
                               "launch-sweep restore: '{}' -> '{}' (consent now present)",
                               entry.toStdString(),
                               original.toStdString());
            } else {
                AJAZZ_LOG_WARN("plugin-catalog",
                               "launch-sweep restore: rename '{}' -> '{}' failed",
                               entry.toStdString(),
                               original.toStdString());
            }
        }

        QStringList const entries = listDirsWithSuffixCi(dir, QStringLiteral(".sdPlugin"));
        for (QString const& entry : entries) {
            QString const manifestPath = dir.filePath(entry + QStringLiteral("/manifest.json"));
            if (!QFile::exists(manifestPath)) {
                continue; // no manifest -> not a valid plugin dir; skip
            }
            VerifyOutcome const vout = verifyStagedPlugin(manifestPath);
            if (vout.verdict == VerifyVerdict::Unsigned &&
                (consentToUnsigned() || perPluginAllowed(entry))) {
                // Opt-in: keep the unsigned (no-signature) plugin so it can be
                // discovered + run. Consent via the global setting, the env var,
                // OR an explicit per-plugin "Allow this plugin" decision
                // (WR-01: perPluginAllowed) — the per-plugin allow must survive
                // the launch-sweep even when the global toggle is OFF, otherwise
                // the affordance is undone on the next restart.
                // CR-01: this branch is Unsigned-ONLY; the Refused branch below
                // stays unconditional-quarantine and consults neither predicate.
                AJAZZ_LOG_WARN("plugin-catalog",
                               "launch-sweep verify: '{}' unsigned ({}); KEPT "
                               "(allowUnsignedPlugins, AJAZZ_ALLOW_UNTRUSTED_PLUGINS, "
                               "or per-plugin allow set)",
                               entry.toStdString(),
                               vout.reason.toStdString());
            } else if (vout.verdict == VerifyVerdict::Unsigned) {
                // No consent — quarantine NON-DESTRUCTIVELY by renaming the
                // dir to `<entry>.disabled`. A manually-sideloaded plugin never
                // passed through installFromFile()/allowPlugin(), so it has no
                // consent key; silently DELETING the user's files at startup
                // (the pre-fix behaviour) is hostile and unrecoverable. The
                // rename keeps the files but removes them from the `*.sdPlugin`
                // glob, so PluginManager::discover() still never spawns an
                // unconsented plugin (the rediscover() trust invariant holds).
                // Recovery: enable allowUnsignedPlugins (or per-plugin allow /
                // AJAZZ_ALLOW_UNTRUSTED_PLUGINS) and restart — the restore
                // pass above renames it back. Only if the rename fails do we
                // fall back to removal, because leaving an unconsented dir
                // discoverable would reopen the T-22 back door (fail-closed).
                QString const quarantineName = entry + kQuarantineSuffix;
                if (QFileInfo::exists(dir.filePath(quarantineName))) {
                    QDir(dir.filePath(quarantineName)).removeRecursively(); // stale copy
                }
                if (dir.rename(entry, quarantineName)) {
                    AJAZZ_LOG_WARN("plugin-catalog",
                                   "launch-sweep verify: '{}' unsigned ({}); no consent -> "
                                   "quarantined as '{}' (NOT deleted; enable allowUnsignedPlugins "
                                   "or allowPlugin() to restore)",
                                   entry.toStdString(),
                                   vout.reason.toStdString(),
                                   quarantineName.toStdString());
                } else {
                    AJAZZ_LOG_WARN("plugin-catalog",
                                   "launch-sweep verify: '{}' unsigned ({}); quarantine "
                                   "rename failed -> removing (fail-closed)",
                                   entry.toStdString(),
                                   vout.reason.toStdString());
                    QDir(dir.filePath(entry)).removeRecursively();
                }
            } else if (vout.verdict == VerifyVerdict::Refused) {
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

    // Mirabox-GitHub mirror — same snapshot-replaces-rows wiring. (No state
    // banner property yet; the tab renders from the snapshot rows directly.)
    QObject::connect(m_miraboxGithubFetcher.get(),
                     &MiraboxGithubCatalogFetcher::snapshotReady,
                     this,
                     [this](MiraboxGithubCatalogFetcher::Snapshot snapshot) {
                         replaceMiraboxGithubRows(std::move(snapshot.rows));
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
    case InstallableInAppRole:
        return entryInstallableInApp(row);
    case UnavailableReasonRole:
        return entryInstallableInApp(row) ? QString{} : QStringLiteral("Not installable in-app");
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
        {InstallableInAppRole, "installableInApp"},
        {UnavailableReasonRole, "unavailableReason"},
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

void PluginCatalogModel::refreshInstalled() {
    // installedActions() is disk-backed, so nothing to recompute here — just
    // re-emit the signal the QML Action Library listens on so it refetches the
    // current on-disk plugin set (covers sideload + rediscover paths that don't
    // go through the catalogue install flow).
    emit installedCountChanged();
}

namespace {
// Defined in the anonymous-namespace block below installedActions() (all
// anonymous namespaces in a TU merge); forward-declared so installedActions()
// can probe each action icon's on-disk spelling (.png/@2x.png/.svg).
[[nodiscard]] QString resolvePluginIconFile(QString const& pluginDir, QString const& rel);
} // namespace

QVariantList PluginCatalogModel::installedActions() const {
    QVariantList out;

    // Diagnostic counters (PLUGIN-18): hidden-by-visibility is NOT an error.
    int hiddenCount = 0;
    int errorSkipCount = 0;
    int parseFailureCount = 0;
    int osVersionSkipCount = 0; // GAP-28A: plugins rejected by manifestRunnableHere()
    int totalScanned = 0;

    QString const pluginsDirPath = userPluginsDir();
    QDir const dir(pluginsDirPath);
    if (!dir.exists()) {
        return out;
    }

    // Same install layout the verify-gate sweep and Phase-18 discovery use:
    // <pluginsDir>/<name>.sdPlugin/manifest.json.
    QString const platform = currentPlatformString();
    // GAP-2026-06-09: this gate MUST use the EMULATED Stream Deck version, not
    // QCoreApplication::applicationVersion(). Software.MinimumVersion refers to
    // the Elgato app; comparing against our own 0.1.x hid the actions of
    // plugins the PluginManager (which already used the emulated version) was
    // happily running — observed live with com.jk.weather (MinimumVersion 4.1):
    // registered over the WebSocket yet invisible in the action picker.
    QString const appVer = emulatedStreamDeckVersion();
    QStringList const entries = listDirsWithSuffixCi(dir, QStringLiteral(".sdPlugin"));

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
            ++parseFailureCount;
            continue; // unparsable / missing required keys (T-18-MANIFEST)
        }
        if (!manifestRunnableHere(*parsed, platform, appVer)) {
            ++osVersionSkipCount;
            AJAZZ_LOG_INFO("plugin-catalog",
                           "installedActions: skipped plugin '{}' (not runnable on {}, appVer={})",
                           parsed->name.toStdString(),
                           platform.toStdString(),
                           appVer.toStdString());
            continue; // not for this OS / below software minimum version
        }
        // Mirror the spawn step's "real gate" (LOCKED Linux OS-accept policy):
        // the OS gate above is best-effort, but a plugin with NO code path for
        // this platform (e.g. a Windows-native bundle shipping only CodePathWin)
        // can never be launched here — listing its actions would produce
        // bindable-but-dead keys. Same resolution PluginManager::spawn applies.
        if (resolveEffectiveCodePath(*parsed).isEmpty()) {
            ++osVersionSkipCount;
            AJAZZ_LOG_INFO("plugin-catalog",
                           "installedActions: skipped plugin '{}' (no code path for {} — "
                           "cannot run on this platform)",
                           parsed->name.toStdString(),
                           platform.toStdString());
            continue; // nothing the spawn step could ever launch here
        }

        for (PluginAction const& action : parsed->actions) {
            ++totalScanned;
            // Filter intentionally hidden actions BEFORE the UUID/Name check so
            // they are counted separately and not surfaced as errors (Pitfall 7,
            // PLUGIN-18, T-28-05 VisibleInActionsList filter).
            if (!action.visibleInActionsList) {
                ++hiddenCount;
                continue;
            }
            if (action.uuid.isEmpty() || action.name.isEmpty()) {
                ++errorSkipCount; // malformed — countable diagnostic
                continue;         // an action with no id cannot be bound or routed
            }

            // Prefer the per-action icon, fall back to the plugin icon. Probe the
            // common Elgato/OpenAction spellings (bare, .png, @2x.png, .svg) — e.g.
            // OpenAction Discord ships `actions/mute_0.svg` with Icon "actions/mute_0".
            QString iconUrl;
            QString const iconRel = !action.icon.isEmpty() ? action.icon : parsed->icon;
            QString const resolved = resolvePluginIconFile(pluginDir, iconRel);
            if (!resolved.isEmpty()) {
                // Emit the icon as an `opendeck/__pluginasset__/<dir>/<rel>` path.
                // The embedded OpenDeck SPA's icon renderers special-case an
                // `opendeck/` prefix (ActionList.svelte, getImage()): `opendeck/x`
                // -> `/x`, a root-relative URL resolved against the `opendeck://app/`
                // origin and served by OpenDeckSchemeHandler. A file:// / data: /
                // bare path instead routes through the dead local webserver origin
                // (http://localhost:PORT/...) and renders blank. The scheme handler
                // maps __pluginasset__/<rest> back to userPluginsDir()/<rest> on disk.
                QString const rel = QDir(pluginDir).relativeFilePath(resolved);
                iconUrl =
                    QStringLiteral("opendeck/__pluginasset__/") + entry + QLatin1Char('/') + rel;
            }

            // Absolute filesystem path to the action's Property Inspector HTML
            // (Workstream C). loadInspector() needs an absolute path; the
            // manifest stores it relative to the plugin dir.
            QString piAbs;
            if (!action.propertyInspectorPath.isEmpty()) {
                QString const cand = QDir(pluginDir).filePath(action.propertyInspectorPath);
                if (QFileInfo::exists(cand)) {
                    piAbs = cand;
                }
            }

            // The SPA's PropertyInspectorView builds the PI iframe src as
            // getWebserverUrl(property_inspector + "|opendeck_property_inspector")
            // = http://localhost:<portBase+2>/<property_inspector>|…, served by
            // PluginAssetServer. That server only resolves the `__pluginasset__/`
            // namespace, so the PI path handed to the SPA must be the webserver-
            // relative `__pluginasset__/<dir>/<rel>` form, NOT the bare manifest-
            // relative path (which 404s). Emit it only when the file exists so a
            // PI-less action stays blank instead of pointing at a 404.
            QString piWebPath;
            if (!piAbs.isEmpty()) {
                piWebPath = QStringLiteral("__pluginasset__/") + entry + QLatin1Char('/') +
                            action.propertyInspectorPath;
            }

            QVariantMap m;
            m.insert(QStringLiteral("pluginName"), parsed->name);
            m.insert(QStringLiteral("pluginVersion"), parsed->version);
            m.insert(QStringLiteral("actionId"), action.uuid);
            m.insert(QStringLiteral("actionName"), action.name);
            m.insert(QStringLiteral("icon"), iconUrl);
            m.insert(QStringLiteral("propertyInspectorPath"), piWebPath);
            m.insert(QStringLiteral("propertyInspectorAbsPath"), piAbs);
            // The install-dir name is the per-plugin settings-storage key the
            // PIBridge uses (AppDataLocation/plugins/<pluginUuid>/settings/).
            m.insert(QStringLiteral("pluginUuid"), entry);
            m.insert(QStringLiteral("controllers"), action.controllers);
            // Phase-28 Plan-02 extensions (PLUGIN-18 / PLUGIN-20):
            m.insert(QStringLiteral("affordanceMask"), affordanceMask(action.controllers));
            m.insert(QStringLiteral("visibleInActionsList"), action.visibleInActionsList);
            m.insert(QStringLiteral("stateCount"), static_cast<int>(action.states.size()));
            m.insert(QStringLiteral("disableAutomaticStates"), action.disableAutomaticStates);
            m.insert(QStringLiteral("defaultSettings"),
                     QString::fromStdString(action.defaultSettings));
            m.insert(QStringLiteral("encoderLayout"), action.encoderBlock.layout);
            // audit 4.8: plugin-level flags for the SPA PluginManager —
            // `builtin` = seeded from the app bundle (hides the trash icon);
            // `has_settings_interface` = manifest HasSettingsInterface.
            {
                QString base = entry;
                if (base.endsWith(QStringLiteral(".sdPlugin"), Qt::CaseInsensitive)) {
                    base.chop(9);
                }
                QSettings settings;
                m.insert(QStringLiteral("pluginBuiltin"),
                         settings.value(QStringLiteral("plugins/seeded/") + base, false).toBool());
            }
            m.insert(QStringLiteral("pluginHasSettingsInterface"), parsed->hasSettingsInterface);
            out.append(m);
        }
    }

    // Persist diagnostic counts for lastScanDiagnostics() so QML / tests can
    // observe the skip breakdown without re-scanning. installedActions() is
    // const so the members are declared mutable (PLUGIN-18).
    m_lastInstalledCount = static_cast<int>(out.size());
    m_lastHiddenByVisibility = hiddenCount;
    m_lastSkippedUuidName = errorSkipCount;
    m_lastSkippedParseFailure = parseFailureCount;
    m_lastSkippedOsVersion = osVersionSkipCount; // GAP-28A
    m_lastTotalScanned = totalScanned;

    return out;
}

namespace {

/// Probe the common on-disk spellings of a manifest icon path relative to the
/// plugin dir. Elgato manifests routinely omit the extension and ship a `@2x`
/// retina variant, so try the bare path, then `.png`, `@2x.png`, and `.svg`.
/// Returns the absolute path of the first hit, or an empty string.
[[nodiscard]] QString resolvePluginIconFile(QString const& pluginDir, QString const& rel) {
    if (rel.isEmpty()) {
        return {};
    }
    QString const base = QDir(pluginDir).filePath(rel);
    // Same probe list as PluginManager::stateImagePath — MiraBox bundles ship
    // .jpg icons (timeClock: Icon "images/icon" + images/icon.jpg on disk),
    // which the shorter png/svg-only list silently missed (blank icon in the
    // SPA plugin list, 2026-07-02).
    for (QString const& cand : {base,
                                base + QStringLiteral(".png"),
                                base + QStringLiteral("@2x.png"),
                                base + QStringLiteral(".jpg"),
                                base + QStringLiteral(".jpeg"),
                                base + QStringLiteral(".svg"),
                                base + QStringLiteral(".gif"),
                                base + QStringLiteral(".bmp")}) {
        if (QFileInfo::exists(cand)) {
            return cand;
        }
    }
    return {};
}

} // namespace

QString PluginCatalogModel::pluginIconDataUri(QString const& installDirName) const {
    // Path-traversal guard: installDirName is the on-disk `<id>.sdPlugin`
    // directory name the OpenDeck `list_plugins` bridge reports — it must be a
    // single path segment under userPluginsDir(), never an escape.
    if (installDirName.isEmpty() || installDirName.contains(QStringLiteral("..")) ||
        installDirName.contains(QLatin1Char('/')) || installDirName.contains(QLatin1Char('\\'))) {
        return {};
    }
    QString const pluginDir = QDir(userPluginsDir()).filePath(installDirName);
    QFile manifestFile(QDir(pluginDir).filePath(QStringLiteral("manifest.json")));
    if (!manifestFile.open(QIODevice::ReadOnly)) {
        return {};
    }
    QByteArray const json = manifestFile.readAll();
    manifestFile.close();

    auto const parsed = parsePluginManifest(json);
    if (!parsed) {
        return {};
    }
    // Plugin-level icon: the manifest top-level `Icon`, then `CategoryIcon`.
    // Both are stored relative to the plugin dir; resolve + inline the first
    // that exists on disk as a data: URI. A data: URI is the SPA-loadable form
    // (the OpenDeck renderer's getImage() passes `data:` through verbatim,
    // whereas a file:// or relative path is routed through the dead local
    // webserver origin and fails to load cross-origin in the SPA's webview).
    // NB: despite the historical name, this returns a `__pluginasset__/<dir>/<rel>`
    // path, NOT a data: URI. The plugin-store tab (PluginManager.svelte) wraps the
    // plugin icon in getWebserverUrl() UNCONDITIONALLY (no `data:`/`opendeck/`
    // escape hatch), so a data: URI is dead there; a webserver-relative path is
    // served by PluginAssetServer at http://localhost:<portBase+2>/__pluginasset__/.
    for (QString const& rel : {parsed->icon, parsed->categoryIcon}) {
        QString const resolved = resolvePluginIconFile(pluginDir, rel);
        if (!resolved.isEmpty()) {
            QString const relPath = QDir(pluginDir).relativeFilePath(resolved);
            return QStringLiteral("__pluginasset__/") + installDirName + QLatin1Char('/') + relPath;
        }
    }
    return {};
}

QVariantList PluginCatalogModel::installedUnsupportedPlugins() const {
    QVariantList out;

    QString const pluginsDirPath = userPluginsDir();
    QDir const dir(pluginsDirPath);
    if (!dir.exists()) {
        return out;
    }

    QString const platform = currentPlatformString();
    QString const appVer = emulatedStreamDeckVersion();
    QStringList const entries = listDirsWithSuffixCi(dir, QStringLiteral(".sdPlugin"));

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
            continue; // unparsable — not a usable plugin to report (#83)
        }

        // Mirror the two platform gates installedActions() applies, but instead
        // of dropping the plugin silently, classify WHY it cannot run so the UI
        // can list it as installed-but-unrunnable (#83).
        QString reason;
        QString detail;
        if (!manifestRunnableHere(*parsed, platform, appVer)) {
            reason = QStringLiteral("osVersion");
            detail = tr("Not available for this OS or Stream Deck version");
        } else if (resolveEffectiveCodePath(*parsed).isEmpty()) {
            reason = QStringLiteral("noCodePath");
            detail = tr("No build for this platform — installs but cannot run here");
        } else {
            continue; // runnable here — surfaced normally by installedActions()
        }

        QStringList platforms;
        for (PluginOsRequirement const& os : parsed->os) {
            if (!os.platform.isEmpty()) {
                platforms << os.platform;
            }
        }

        QString id = entry;
        if (id.endsWith(QStringLiteral(".sdPlugin"))) {
            id.chop(static_cast<int>(QStringLiteral(".sdPlugin").size()));
        }

        out.append(QVariantMap{
            {QStringLiteral("id"), id},
            {QStringLiteral("name"), parsed->name},
            {QStringLiteral("version"), parsed->version},
            {QStringLiteral("author"), parsed->author},
            {QStringLiteral("platforms"), platforms.join(QStringLiteral(", "))},
            {QStringLiteral("reason"), reason},
            {QStringLiteral("detail"), detail},
        });
    }

    return out;
}

QVariantMap PluginCatalogModel::lastScanDiagnostics() const {
    return QVariantMap{
        {QStringLiteral("installedCount"), m_lastInstalledCount},
        {QStringLiteral("hiddenByVisibility"), m_lastHiddenByVisibility},
        {QStringLiteral("skippedUuidName"), m_lastSkippedUuidName},
        {QStringLiteral("skippedParseFailure"), m_lastSkippedParseFailure},
        // GAP-28A: plugins rejected by manifestRunnableHere (OS mismatch or version below minimum).
        // A non-zero value here means at least one installed plugin is invisible on this
        // OS/version and explains "tools show 0 for this plugin".
        {QStringLiteral("skippedOsVersion"), m_lastSkippedOsVersion},
    };
}

QVariantMap PluginCatalogModel::actionInfo(QString const& actionId) const {
    if (actionId.isEmpty()) {
        return {};
    }
    QVariantList const all = installedActions();
    for (QVariant const& v : all) {
        QVariantMap const m = v.toMap();
        if (m.value(QStringLiteral("actionId")).toString() == actionId) {
            return m;
        }
    }
    return {};
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
    if (m_miraboxGithubFetcher) {
        m_miraboxGithubFetcher->setCatalogUrlOverride(liveUrlOverride);
        m_miraboxGithubFetcher->refresh();
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

bool PluginCatalogModel::allowUnsignedPlugins() const {
    return m_allowUnsignedPlugins;
}

void PluginCatalogModel::setAllowUnsignedPlugins(bool allow) {
    if (m_allowUnsignedPlugins == allow) {
        return;
    }
    m_allowUnsignedPlugins = allow;
    {
        QSettings settings;
        settings.setValue(QStringLiteral("plugins/allowUnsignedPlugins"), allow);
    }
    emit allowUnsignedPluginsChanged();
}

bool PluginCatalogModel::consentToUnsigned() const {
    // CR-01 guard: call this ONLY from the VerifyVerdict::Unsigned branch.
    // This predicate is the single source of truth for unsigned consent.
    return m_allowUnsignedPlugins ||
           !qEnvironmentVariable("AJAZZ_ALLOW_UNTRUSTED_PLUGINS").isEmpty();
}

namespace {
/// Recursive directory copy for the bundled-plugin seed. QFile::copy keeps
/// permission bits, so the plugin/helper executables stay executable.
bool copyDirRecursively(QString const& srcPath, QString const& dstPath) {
    QDir const src(srcPath);
    if (!src.exists() || !QDir().mkpath(dstPath)) {
        return false;
    }
    for (QFileInfo const& info :
         src.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot)) {
        QString const dst = dstPath + QLatin1Char('/') + info.fileName();
        if (info.isDir()) {
            if (!copyDirRecursively(info.absoluteFilePath(), dst)) {
                return false;
            }
        } else if (!QFile::copy(info.absoluteFilePath(), dst)) {
            return false;
        }
    }
    return true;
}
} // namespace

void PluginCatalogModel::seedBundledPlugins(QString const& pluginsDir) {
    QStringList roots;
    QString const envDir = qEnvironmentVariable("AJAZZ_BUNDLED_PLUGINS_DIR");
    if (!envDir.isEmpty()) {
        roots << envDir;
    }
    roots << QStandardPaths::locateAll(QStandardPaths::GenericDataLocation,
                                       QStringLiteral("ajazz-control-center/bundled-plugins"),
                                       QStandardPaths::LocateDirectory);
#ifdef AJAZZ_BUNDLED_PLUGINS_FALLBACK
    // Dev-build fallback (same pattern as AJAZZ_PLUGIN_TRUST_ROOTS): the
    // assembled bundle lives in the build tree, which is not on
    // GenericDataLocation — without this, first-party plugins (sysmon) only
    // seed from PACKAGED installs and a dev profile never gets them.
    if (QString const devRoot = QStringLiteral(AJAZZ_BUNDLED_PLUGINS_FALLBACK);
        QDir(devRoot).exists()) {
        roots << devRoot;
    }
#endif
    if (roots.isEmpty()) {
        return;
    }
    QSettings settings;
    for (QString const& root : roots) {
        QDir const bundleRoot(root);
        for (QString const& entry : bundleRoot.entryList({QStringLiteral("*.sdPlugin")},
                                                         QDir::Dirs | QDir::NoDotAndDotDot)) {
            QString const uuid = entry.chopped(9); // strip ".sdPlugin"
            QString const seededKey = QStringLiteral("plugins/seeded/") + uuid;
            QString const dst = QDir(pluginsDir).filePath(entry);
            // Present (live or quarantined): nothing to do, but record the
            // seed so a later user DELETE is respected on the next launch.
            // audit 3.5: RE-ASSERT the per-plugin consent too — a bundled dir
            // present without it (QSettings reset, manual copy, pre-f14b494f
            // seed) was quarantined as Unsigned by this constructor's sweep.
            if (QDir(dst).exists() || QDir(dst + QStringLiteral(".disabled")).exists()) {
                settings.setValue(seededKey, true);
                settings.setValue(QStringLiteral("plugins/allowed/") + uuid, true);
                continue;
            }
            if (settings.value(seededKey, false).toBool()) {
                continue; // seeded before and user removed it — stay removed
            }
            if (!copyDirRecursively(bundleRoot.filePath(entry), dst)) {
                AJAZZ_LOG_WARN("plugin-catalog",
                               "bundled seed: copy '{}' -> '{}' failed",
                               bundleRoot.filePath(entry).toStdString(),
                               dst.toStdString());
                // audit 3.6: remove the partial tree — leaving it made the
                // dst-exists branch above mark it seeded on the next launch,
                // permanently locking in a broken install.
                QDir(dst).removeRecursively();
                continue;
            }
            settings.setValue(seededKey, true);
            // First-party bundle: persist per-plugin consent (same key
            // installFromCatalog writes) so the unsigned-verify sweep below
            // keeps it without the global allow-unsigned toggle.
            settings.setValue(QStringLiteral("plugins/allowed/") + uuid, true);
            AJAZZ_LOG_INFO("plugin-catalog",
                           "bundled seed: '{}' -> '{}' (consent persisted)",
                           entry.toStdString(),
                           pluginsDir.toStdString());
        }
    }
}

bool PluginCatalogModel::allowPlugin(QString const& uuid) {
    // CR-01: refuse immediately for tampered rows. Read the installed
    // plugin's manifest to determine its trust level via the verify gate.
    // If the plugin dir doesn't exist or the manifest is tampered, return false.
    QString const pluginsDir = userPluginsDir();
    QString const dirName = uuid + QStringLiteral(".sdPlugin");
    QString const pluginDir = QDir(pluginsDir).filePath(dirName);
    QString const manifestPath = QDir(pluginDir).filePath(QStringLiteral("manifest.json"));

    if (!QFile::exists(manifestPath)) {
        // Not in the live plugins dir — it may have been quarantined by the
        // launch-sweep (`<uuid>.sdPlugin.disabled`). Explicit consent restores
        // it, but ONLY after verifying the quarantined manifest in place:
        // restoring first and verifying after would leave a Refused/tampered
        // dir discoverable between the rename and the next sweep (CR-01).
        QString const quarantinedManifest =
            QDir(pluginsDir)
                .filePath(dirName + kQuarantineSuffix + QStringLiteral("/manifest.json"));
        if (QFile::exists(quarantinedManifest)) {
            VerifyOutcome const qout = verifyStagedPlugin(quarantinedManifest);
            if (qout.verdict == VerifyVerdict::Refused) {
                AJAZZ_LOG_WARN("plugin-catalog",
                               "allowPlugin: quarantined '{}' is tampered (Refused); "
                               "refusing restore + consent (CR-01)",
                               uuid.toStdString());
                return false;
            }
            if (!QDir(pluginsDir).rename(dirName + kQuarantineSuffix, dirName)) {
                AJAZZ_LOG_WARN("plugin-catalog",
                               "allowPlugin: failed to restore quarantined '{}'; no-op",
                               uuid.toStdString());
                return false;
            }
            AJAZZ_LOG_INFO("plugin-catalog",
                           "allowPlugin: restored '{}' from quarantine on explicit consent",
                           uuid.toStdString());
        } else {
            // Unknown UUID — no installed plugin with this id.
            AJAZZ_LOG_WARN("plugin-catalog",
                           "allowPlugin: '{}' not found in plugins dir; no-op",
                           uuid.toStdString());
            return false;
        }
    }

    // Verify the staged manifest to determine trust level.
    VerifyOutcome const vout = verifyStagedPlugin(manifestPath);
    if (vout.verdict == VerifyVerdict::Refused) {
        // CR-01: tampered plugin — never consentable.
        AJAZZ_LOG_WARN(
            "plugin-catalog",
            "allowPlugin: '{}' is tampered (Refused); refusing per-plugin consent (CR-01)",
            uuid.toStdString());
        return false;
    }

    // Record per-plugin consent for Unsigned (developer sideload). WR-01: this
    // key is now READ by perPluginAllowed() in both the launch-sweep and the
    // installFromFile Unsigned gate, so the consent (a) survives restart (the
    // sweep keeps the plugin instead of deleting it) and (b) lets a re-install
    // of this specific plugin proceed without re-prompting — even when the
    // global allowUnsignedPlugins toggle is OFF.
    {
        QSettings settings;
        settings.setValue(QStringLiteral("plugins/allowed/") + uuid, true);
    }
    AJAZZ_LOG_INFO("plugin-catalog",
                   "allowPlugin: '{}' consent recorded ({}); driving rediscover",
                   uuid.toStdString(),
                   verdictToTrustLevel(vout.verdict).toStdString());
    // The plugin is already on disk (in pluginsDir) and now consented. Drive
    // promotion: installFinished(uuid, true, "") is wired (application.cpp) to
    // PluginManager::rediscover(), which idempotently spawns the now-allowed
    // plugin if it is not already live. installedCountChanged() refreshes the
    // Action Library so its actions appear without a restart. WR-01: this turns
    // the previously-dead per-plugin "Allow" button into a working affordance.
    emit installedCountChanged();
    emit installFinished(uuid, true, QString{});
    return true;
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
    if (m_miraboxGithubFetcher) {
        m_miraboxGithubFetcher->setCatalogUrlOverride(QString{});
        m_miraboxGithubFetcher->refresh();
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

void PluginCatalogModel::replaceMiraboxGithubRows(std::vector<CatalogEntry> rows) {
    // Same strategy as replaceStreamdockRows but scoped to source =
    // "mirabox-github". Disjoint partition of m_rows from the other fetchers.
    beginResetModel();
    std::vector<CatalogEntry> kept;
    kept.reserve(m_rows.size() + rows.size());
    for (auto& row : m_rows) {
        if (row.source != QStringLiteral("mirabox-github")) {
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
    // Derive the install directory name from the file's basename. It MUST end
    // in `.sdPlugin` — discover() only scans `*.sdPlugin` directories, so a
    // promoted dir that keeps its `.zip` / `.streamDeckPlugin` extension (e.g.
    // "foo.sdPlugin.zip") is silently never spawned. Strip a trailing archive
    // extension, then ensure the `.sdPlugin` suffix.
    QFileInfo const fi(localPath);
    QString archiveName = fi.fileName();
    for (auto const* ext : {".zip", ".streamDeckPlugin"}) {
        if (archiveName.endsWith(QLatin1String(ext), Qt::CaseInsensitive)) {
            archiveName.chop(static_cast<int>(qstrlen(ext)));
            break;
        }
    }
    if (archiveName.isEmpty()) {
        archiveName = QStringLiteral("install");
    }
    if (!archiveName.endsWith(QStringLiteral(".sdPlugin"), Qt::CaseInsensitive)) {
        archiveName += QStringLiteral(".sdPlugin");
    }
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

    // #81: name the final install directory `<manifest UUID>.sdPlugin`, mirroring
    // the catalogue install() path. The from-file sideload path previously named
    // the dir from the archive FILE NAME, so a plugin whose file name differs
    // from its manifest UUID (e.g. teams.streamDeckPlugin ->
    // com.niccohagedorn.teamsnavigator) landed at the wrong dir name; the
    // action-owner match keys off that name, so its actions surfaced in the
    // library but could fail to route/render. Read the staged manifest's UUID
    // now (the staged dir is what step 3 verifies) and fall back to the
    // already-sanitized file-derived `archiveName` when the manifest has no UUID
    // or carries one that is unsafe as a path component.
    QString installName = archiveName;
    {
        QFile staged(QDir(stagingParent).filePath(archiveName + QStringLiteral("/manifest.json")));
        if (staged.open(QIODevice::ReadOnly)) {
            if (auto const m = parsePluginManifest(staged.read(kMaxPluginDownloadBytes + 1))) {
                // Prefer the manifest UUID/PUUID; #82: when absent (Elgato SDKv2
                // and many community manifests carry no top-level UUID), recover
                // the identity from the common dotted prefix of the action UUIDs
                // before falling back to the file-derived name.
                QString pluginId = m->puuid;
                if (pluginId.isEmpty()) {
                    pluginId = pluginIdFromActionUuids(m->actions);
                }
                if (QString const byId = safeUuidDirName(pluginId); !byId.isEmpty()) {
                    installName = byId;
                }
            }
        }
    }

    // Step 3: verify the staged manifest (T-22-toctou, T-22-tamper-local,
    // T-22-unsigned). The staging dir is NOT in installedPlugins/ so even on
    // Refused the discoverable directory is unaffected.
    QString const stagedManifest =
        QDir(stagingParent).filePath(archiveName + QStringLiteral("/manifest.json"));

    VerifyOutcome const vout = verifyStagedPlugin(stagedManifest);

    if (vout.verdict == VerifyVerdict::Refused) {
        // CR-01 invariant: Refused means signature block present but Ed25519-invalid
        // (tampered). This is an ATTACK — quarantine unconditionally, even when
        // userConfirmedUnsigned==true. Consent applies ONLY to the Unsigned branch;
        // it must NEVER leak into the Refused/tampered branch.
        AJAZZ_LOG_WARN("plugin-catalog",
                       "installFromFile '{}': signature Refused/tampered ({}); quarantining",
                       localPath.toStdString(),
                       vout.reason.toStdString());
        QDir(QDir(stagingParent).filePath(archiveName)).removeRecursively();
        // WR-03: clean up the .plugin_staging parent dir too, mirroring the
        // Unsigned (:828-829) and SelfSigned (:844-845) branches. Without this
        // a hostile/tampered install leaves an empty staging dir adjacent to
        // the plugins tree on every refused attempt.
        QDir(stagingParent).rmdir(QStringLiteral("."));
        QString const reason =
            vout.reason.isEmpty() ? QStringLiteral("signature verification failed") : vout.reason;
        emit installFinished(
            localPath, false, tr("Plugin signature verification failed: %1").arg(reason));
        return false;
    }

    if (vout.verdict == VerifyVerdict::Unsigned && !userConfirmedUnsigned && !consentToUnsigned() &&
        !perPluginAllowed(installName)) {
        // Unsigned (no signature block) — developer sideload. Requires explicit
        // user consent, the allowUnsignedPlugins setting, the env var override,
        // OR a prior per-plugin "Allow this plugin" decision (WR-01) so a
        // re-install of an explicitly-allowed plugin does not re-prompt.
        // CR-01: this is the Unsigned branch only; the Refused branch above
        // already quarantined tampered packages unconditionally.
        AJAZZ_LOG_INFO("plugin-catalog",
                       "installFromFile '{}': Unsigned — awaiting user confirm; "
                       "removing staging dir",
                       localPath.toStdString());
        QDir(QDir(stagingParent).filePath(archiveName)).removeRecursively();
        QDir(stagingParent).rmdir(QStringLiteral("."));
        emit installFinished(
            localPath, false, QStringLiteral("unsigned plugin -- confirm to install"));
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
    QString const promotedDir = QDir(pluginsDir).filePath(installName);

    // Remove any existing install at the target path before rename
    // (idempotent re-install case). Announce the replacement first so the
    // wiring tears down the RUNNING old copy (unloadPlugin) before its dir
    // vanishes — otherwise the stale m_live key makes rediscover() skip the
    // fresh install forever (audit 3.1/3.2).
    if (QDir(promotedDir).exists()) {
        emit pluginWillBeReplaced(installName);
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
        // audit 3.7: the old hand-rolled walk recursed exactly ONE level and
        // silently dropped deeper trees (bin/<triple>/, pi/assets/ are common
        // in real bundles) with copyOk still true — a truncated plugin was
        // promoted as success. Reuse the full-depth seed helper.
        bool const copyOk = copyDirRecursively(stagedDir, promotedDir);
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

    // audit 3.3: retire any duplicate of this plugin installed under a
    // different directory name (CDN installs use the numeric product id).
    dedupeDuplicateInstalls(installName);

    // Flip install state and emit signals (same pattern as network install()).
    // WR-03 fix: key by UUID (not by localPath) so m_install stays bounded.
    // The localPath key was never cleaned up by reload()/uninstall() and caused
    // installedCount() to drift above the true count over repeated installs
    // from different file paths.
    // Try to find a matching catalogue row by the promoted dir name. #81: this
    // is now the manifest-UUID-derived `installName` (falling back to the
    // file-derived name), so a sideloaded plugin whose file name differed from
    // its UUID flips the correct catalogue row and persists consent under the
    // real plugin UUID — the same key the launch-sweep and allowPlugin() use.
    QString const candidateUuid =
        installName.endsWith(QStringLiteral(".sdPlugin")) ? installName.chopped(9) : installName;

    // FIX-CONSENT: when the user explicitly confirmed a non-trusted install
    // (Unsigned / SelfSigned via userConfirmedUnsigned), persist that consent so
    // the next launch-sweep KEEPS the plugin instead of quarantining it. Without
    // this, confirm:true was a one-shot: the plugin installed, but the launch
    // sweep deleted it on the next start (the install-then-vanish trap). Mirrors
    // the key read by perPluginAllowed() (plugins/allowed/<uuid>). Refused never
    // reaches here (quarantined above, CR-01), so this never whitelists tampered.
    if (userConfirmedUnsigned && vout.verdict != VerifyVerdict::Trusted) {
        QSettings settings;
        settings.setValue(QStringLiteral("plugins/allowed/") + candidateUuid, true);
        AJAZZ_LOG_INFO("plugin-catalog",
                       "installFromFile '{}': persisted per-plugin consent (survives launch-sweep)",
                       candidateUuid.toStdString());
    }

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

void PluginCatalogModel::finalizeAssembledInstall(QString const& uuid,
                                                  QString const& stagingDir,
                                                  QString const& destDir) {
    QString const stagedManifest = QDir(stagingDir).filePath(QStringLiteral("manifest.json"));

    // Install dir name = manifest UUID/PUUID (Stream Deck convention; the
    // action-owner match keys off it), falling back to the catalogue row uuid.
    QString installName;
    if (QFile mf(stagedManifest); mf.open(QIODevice::ReadOnly)) {
        if (auto const m = parsePluginManifest(mf.read(kMaxPluginDownloadBytes + 1))) {
            installName = safeUuidDirName(m->puuid);
        }
    }
    if (installName.isEmpty()) {
        installName = safeUuidDirName(uuid);
    }
    if (installName.isEmpty()) {
        QDir(stagingDir).removeRecursively();
        emit installFinished(uuid, false, QStringLiteral("Plugin has no usable UUID."));
        return;
    }

    // Verify gate — mirror the network install path: only Refused is quarantined
    // (these are community GPL plugins; Unsigned/SelfSigned are allowed + logged).
    VerifyOutcome const vout = verifyStagedPlugin(stagedManifest);
    if (vout.verdict == VerifyVerdict::Refused) {
        AJAZZ_LOG_WARN("plugin-catalog",
                       "install '{}': signature verification refused ({}); quarantining staging",
                       uuid.toStdString(),
                       vout.reason.toStdString());
        QDir(stagingDir).removeRecursively();
        emit installFinished(
            uuid, false, tr("Plugin signature verification failed: %1").arg(vout.reason));
        return;
    }

    // Promote: atomic rename of the staging dir into <destDir>/<installName>
    // (same filesystem — staging lives inside destDir). Replace any prior copy.
    QString const promoted = QDir(destDir).filePath(installName);
    if (QDir(promoted).exists()) {
        // Same replace-announcement as installFromFile's promote (audit 3.2).
        emit pluginWillBeReplaced(installName);
        QDir(promoted).removeRecursively();
    }
    if (!QDir().rename(stagingDir, promoted)) {
        AJAZZ_LOG_WARN("plugin-catalog",
                       "install '{}': promote rename failed ({} -> {})",
                       uuid.toStdString(),
                       stagingDir.toStdString(),
                       promoted.toStdString());
        QDir(stagingDir).removeRecursively();
        emit installFinished(
            uuid, false, QStringLiteral("Failed to promote plugin to install dir."));
        return;
    }

    // audit 3.3: retire any duplicate of this plugin installed under a
    // different directory name.
    dedupeDuplicateInstalls(installName);

    int const r = findRow(m_rows, uuid);
    if (r >= 0) {
        auto& s = m_install[uuid];
        s.installed = true;
        s.enabled = true;
        QModelIndex const idx = index(r);
        emit dataChanged(idx, idx, {InstalledRole, EnabledRole});
    }
    emit installedCountChanged(); // re-queries installedActions() so the new actions surface
    AJAZZ_LOG_INFO("plugin-catalog",
                   "install '{}': mirabox-github OK -> {} ({})",
                   uuid.toStdString(),
                   promoted.toStdString(),
                   verdictToTrustLevel(vout.verdict).toStdString());
    emit installFinished(uuid, true, QString{});
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
    if (!entryInstallableInApp(entry)) {
        // US1 (spec FR-006): no resolvable https package URL → this row is
        // "not installable in-app". Install is a no-op returning false; we do
        // NOT open a browser (the old openUpstream/QDesktopServices fallback is
        // removed). The store row presents a disabled button with the
        // UnavailableReason instead of an "Open page" affordance.
        AJAZZ_LOG_INFO("plugin-catalog",
                       "install: '{}' is not installable in-app (no resolvable https "
                       "download URL); no-op (no browser fallback)",
                       uuid.toStdString());
        emit installFinished(uuid, false, QStringLiteral("Not installable in-app"));
        return false;
    }

    // Mirabox-GitHub source: there is no single archive URL — assemble the
    // .sdPlugin bundle by fetching the plugin's subtree from GitHub, then run
    // the SAME verify -> promote stages as the other paths (finalizeAssembledInstall).
    if (entry.source == QStringLiteral("mirabox-github")) {
        if (m_downloader == nullptr) {
            m_downloader = new QNetworkAccessManager(this);
        }
        QString const destDir = userPluginsDir();
        if (destDir.isEmpty()) {
            emit installFinished(
                uuid, false, QStringLiteral("Cannot resolve user plugins directory."));
            return false;
        }
        // Stage INSIDE the scan dir but dot-prefixed so the `*.sdPlugin` scanner
        // ignores it; rename into place atomically (same filesystem) after verify.
        QString safeSuffix;
        safeSuffix.reserve(uuid.size());
        for (QChar const c : uuid) {
            bool const ok = c.isLetterOrNumber() || c == QLatin1Char('.') ||
                            c == QLatin1Char('-') || c == QLatin1Char('_');
            safeSuffix.append(ok ? c : QLatin1Char('_'));
        }
        QString const stagingDir =
            QDir(destDir).filePath(QStringLiteral(".mgh-staging-") + safeSuffix);
        QDir(stagingDir).removeRecursively(); // clear any stale staging dir

        auto* const installer = new MiraboxGithubInstaller(m_downloader, this);
        QPointer<PluginCatalogModel> self(this);
        QString const uuidCopy = uuid;
        QString const destDirCopy = destDir;
        QObject::connect(
            installer,
            &MiraboxGithubInstaller::finished,
            this,
            [self, uuidCopy, destDirCopy](bool ok, QString assembledDir, QString error) {
                if (!self) {
                    return;
                }
                if (!ok) {
                    if (!assembledDir.isEmpty()) {
                        QDir(assembledDir).removeRecursively();
                    }
                    emit self->installFinished(uuidCopy, false, error);
                    return;
                }
                self->finalizeAssembledInstall(uuidCopy, assembledDir, destDirCopy);
            });
        AJAZZ_LOG_INFO("plugin-catalog",
                       "install: mirabox-github '{}' -> assembling from {} into staging",
                       uuid.toStdString(),
                       entry.streamdockProductId.toStdString());
        emit installProgressChanged(uuid, 0);
        installer->start(MiraboxGithubCatalogFetcher::repoOwnerRepo(),
                         MiraboxGithubCatalogFetcher::repoBranch(),
                         entry.streamdockProductId,
                         stagingDir);
        return true;
    }

    // StreamDock rows resolve their absolute CDN archive URL on demand. The
    // catalogue `/list` payload only carried a RELATIVE `download` path, so the
    // row's downloadUrl is empty here; fetch the real https URL from
    // productInfo/get/<id>, cache it on the row, then re-enter install() to run
    // the standard download+verify pipeline below.
    if (entry.source == QStringLiteral("streamdock") && !entry.downloadUrl.isValid()) {
        QUrl const resolveUrl = StreamdockCatalogFetcher::productGetUrl(entry.streamdockProductId);
        if (!resolveUrl.isValid()) {
            emit installFinished(
                uuid, false, QStringLiteral("Cannot resolve plugin download URL."));
            return false;
        }
        if (m_downloader == nullptr) {
            m_downloader = new QNetworkAccessManager(this);
        }
        AJAZZ_LOG_INFO("plugin-catalog",
                       "install: streamdock '{}' resolving archive URL via {}",
                       uuid.toStdString(),
                       resolveUrl.toString().toStdString());
        emit installProgressChanged(uuid, 0);
        QNetworkRequest resolveReq{resolveUrl};
        resolveReq.setRawHeader("Accept", "application/json");
        resolveReq.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                                QNetworkRequest::NoLessSafeRedirectPolicy);
        QNetworkReply* const resolveReply = m_downloader->get(resolveReq);
        QPointer<PluginCatalogModel> self(this);
        QString const uuidCopy = uuid;
        QObject::connect(
            resolveReply, &QNetworkReply::finished, this, [self, resolveReply, uuidCopy]() {
                resolveReply->deleteLater();
                if (!self) {
                    return;
                }
                if (resolveReply->error() != QNetworkReply::NoError) {
                    emit self->installFinished(
                        uuidCopy,
                        false,
                        PluginCatalogModel::tr("Could not resolve plugin download URL: %1")
                            .arg(resolveReply->errorString()));
                    return;
                }
                QUrl const resolved =
                    StreamdockCatalogFetcher::parseProductDownloadUrl(resolveReply->readAll());
                int const r = findRow(self->m_rows, uuidCopy);
                if (!resolved.isValid() || r < 0) {
                    emit self->installFinished(
                        uuidCopy,
                        false,
                        PluginCatalogModel::tr("Plugin has no downloadable archive."));
                    return;
                }
            // Cache the resolved URL on the row and re-enter install(): downloadUrl
            // is now populated, so this branch is skipped and the standard
            // download+extract+verify path runs.
#if defined(__GNUC__) && !defined(__clang__)
            // GCC 13 -O2 FP: inlining vector::operator[] into the queued Qt
            // functor trips -Wnull-dereference even though findRow() just
            // proved r indexes a live row (ubuntu-24.04 packaging leg,
            // Release run 28620095630). Clang and GCC >= 14 are clean.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnull-dereference"
#endif
                self->m_rows[static_cast<std::size_t>(r)].downloadUrl = resolved;
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
                self->install(uuidCopy);
            });
        return true;
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

            // FIX-CONSENT (network path): clicking Install in the store is the
            // user's explicit consent, so persist it for a non-Trusted
            // (unsigned / self-signed) plugin — otherwise the next launch-sweep
            // quarantines it (install-then-vanish: "ho installato spotify ma non
            // posso usarlo"). The sweep keys consent on the install DIR name
            // (perPluginAllowed strips .sdPlugin), which is destCopy's basename
            // here — NOT the catalogue uuid (streamdock dirs are <productId>).
            // Refused never reaches this point (quarantined above, CR-01).
            if (vout.verdict != VerifyVerdict::Trusted) {
                QString dirName = QFileInfo(destCopy).fileName();
                if (dirName.endsWith(QStringLiteral(".sdPlugin"))) {
                    dirName.chop(static_cast<int>(QStringLiteral(".sdPlugin").size()));
                }
                QSettings settings;
                settings.setValue(QStringLiteral("plugins/allowed/") + dirName, true);
                AJAZZ_LOG_INFO(
                    "plugin-catalog",
                    "install '{}': persisted per-plugin consent for '{}' (survives launch-sweep)",
                    uuidCopy.toStdString(),
                    dirName.toStdString());
            }
        }

        // audit 3.3: retire any duplicate of this plugin installed under a
        // different directory name (file installs use the manifest UUID).
        self->dedupeDuplicateInstalls(archiveName);

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

void PluginCatalogModel::dedupeDuplicateInstalls(QString const& keepDirName) {
    // audit 3.3: CDN installs are named by numeric product id, file installs by
    // manifest UUID — the SAME plugin could exist under two directory names,
    // with both copies spawning and racing each other's contexts. After any
    // successful install, sweep the plugins dir for OTHER directories whose
    // manifest resolves to the same owner UUID and retire them: the fresh
    // install is the user's intent. pluginWillBeReplaced (NOT
    // pluginUninstalled) tears down the running old copy while PRESERVING the
    // user's bindings — the owner UUID stays installed under keepDirName.
    QDir const dir(userPluginsDir());
    QString const keepUuid = resolveOwnerUuidFromManifest(dir.filePath(keepDirName));
    if (keepUuid.isEmpty()) {
        return;
    }
    QStringList const entries = listDirsWithSuffixCi(dir, QStringLiteral(".sdPlugin"));
    for (QString const& entry : entries) {
        if (entry.compare(keepDirName, Qt::CaseInsensitive) == 0) {
            continue;
        }
        if (resolveOwnerUuidFromManifest(dir.filePath(entry)) != keepUuid) {
            continue;
        }
        emit pluginWillBeReplaced(entry);
        if (!QDir(dir.filePath(entry)).removeRecursively()) {
            AJAZZ_LOG_WARN("plugin-catalog",
                           "dedupe: failed to remove duplicate install '{}' (owner '{}')",
                           entry.toStdString(),
                           keepUuid.toStdString());
            continue;
        }
        // The duplicate's consent key must not outlive its dir (same rule as
        // removeInstalledPlugin, audit 3.13).
        QString consentBase = entry;
        if (consentBase.endsWith(QStringLiteral(".sdPlugin"), Qt::CaseInsensitive)) {
            consentBase.chop(9);
        }
        QSettings settings;
        settings.remove(QStringLiteral("plugins/allowed/") + consentBase);
        AJAZZ_LOG_INFO("plugin-catalog",
                       "dedupe: removed duplicate install '{}' of owner '{}' (kept '{}')",
                       entry.toStdString(),
                       keepUuid.toStdString(),
                       keepDirName.toStdString());
    }
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
    // T037: let any key/dial bound to this plugin's actions revert to unbound
    // (wired in application.cpp to ProfileController::clearBindingsForPlugin).
    emit pluginUninstalled(uuid);
    emit installedCountChanged();
    return true;
}

bool PluginCatalogModel::removeInstalledPlugin(QString const& installDirName) {
    // Defence-in-depth path sanitation: the name crosses the SPA -> bridge ->
    // C++ boundary (list_plugins -> remove_plugin) and is therefore untrusted.
    // It must be a bare `<...>.sdPlugin` leaf — reject anything that could
    // escape userPluginsDir() before we removeRecursively() a directory.
    // audit 3.13: quarantined dirs (`<x>.sdPlugin.disabled`) are removable too.
    bool const quarantined =
        installDirName.endsWith(QStringLiteral(".sdPlugin") + kQuarantineSuffix);
    if (installDirName.isEmpty() ||
        (!installDirName.endsWith(QStringLiteral(".sdPlugin")) && !quarantined) ||
        installDirName.startsWith(QLatin1Char('.')) || installDirName.contains(QLatin1Char('/')) ||
        installDirName.contains(QLatin1Char('\\')) ||
        installDirName.contains(QStringLiteral(".."))) {
        AJAZZ_LOG_WARN("plugin-catalog",
                       "removeInstalledPlugin: rejected unsafe name '{}'",
                       installDirName.toStdString());
        return false;
    }

    QDir const pluginsDir(userPluginsDir());
    QFileInfo const targetInfo(pluginsDir.filePath(installDirName));
    if (!targetInfo.isDir()) {
        AJAZZ_LOG_WARN("plugin-catalog",
                       "removeInstalledPlugin: '{}' is not an installed plugin dir",
                       installDirName.toStdString());
        return false;
    }
    // Canonical containment (belt-and-braces vs symlink games): the resolved
    // target must live directly under the resolved plugins dir.
    QString const canonicalTarget = targetInfo.canonicalFilePath();
    QString const canonicalRoot = QFileInfo(pluginsDir.absolutePath()).canonicalFilePath();
    if (canonicalTarget.isEmpty() || canonicalRoot.isEmpty() ||
        !canonicalTarget.startsWith(canonicalRoot + QLatin1Char('/'))) {
        AJAZZ_LOG_WARN("plugin-catalog",
                       "removeInstalledPlugin: '{}' escapes the plugins dir",
                       installDirName.toStdString());
        return false;
    }

    // Resolve the plugin-owner UUID BEFORE deletion so we can clear the bindings
    // the plugin owns. clearBindingsForPlugin matches an owner uuid plus its
    // dotted action children (NOT the catalogue uuid or the install-dir name).
    QString const manifestUuid = resolveOwnerUuidFromManifest(targetInfo.filePath());

    if (!QDir(targetInfo.filePath()).removeRecursively()) {
        AJAZZ_LOG_WARN("plugin-catalog",
                       "removeInstalledPlugin: failed to delete '{}'",
                       targetInfo.filePath().toStdString());
        return false;
    }
    AJAZZ_LOG_INFO(
        "plugin-catalog", "removeInstalledPlugin: removed '{}'", installDirName.toStdString());

    // Best-effort catalogue-row reconcile: the install-dir base is the
    // streamdock product id (or, for non-streamdock installs, the catalogue
    // uuid) — see install()'s fileBase naming. Flip the matching row back to
    // not-installed so the store tile re-offers "Install".
    QString const base =
        installDirName.left(installDirName.size() - QStringLiteral(".sdPlugin").size());
    for (auto it = m_install.begin(); it != m_install.end(); ++it) {
        int const row = findRow(m_rows, it.key());
        if (row < 0) {
            continue;
        }
        CatalogEntry const& entry = m_rows[static_cast<std::size_t>(row)];
        if (entry.streamdockProductId == base || entry.uuid == base) {
            if (it.value().installed) {
                it.value().installed = false;
                it.value().enabled = false;
                QModelIndex const idx = index(row);
                emit dataChanged(idx, idx, {InstalledRole, EnabledRole});
            }
            break;
        }
    }

    // Clear any key/dial binding owned by the now-gone plugin (T037 contract).
    if (!manifestUuid.isEmpty()) {
        // audit 3.13: consent must not outlive the install — a future unsigned
        // plugin landing under the same dir name inherited it silently.
        {
            QSettings settings;
            QString consentBase = installDirName;
            if (consentBase.endsWith(kQuarantineSuffix)) {
                consentBase.chop(kQuarantineSuffix.size());
            }
            if (consentBase.endsWith(QStringLiteral(".sdPlugin"))) {
                consentBase.chop(9);
            }
            settings.remove(QStringLiteral("plugins/allowed/") + consentBase);
        }
        emit pluginUninstalled(manifestUuid);
    }
    emit installedCountChanged(); // re-queries the disk-backed installedActions()
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
        // Same predicate the data() InstallableInApp / UnavailableReason roles use,
        // so the side-sheet Install button can disable + show a reason in lockstep
        // with the grid tile (and never present an actionable button that no-ops).
        {"installableInApp", entryInstallableInApp(src)},
        {"unavailableReason",
         entryInstallableInApp(src) ? QString{} : QStringLiteral("Not installable in-app")},
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
