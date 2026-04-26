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

#include "opendeck_catalog_fetcher.hpp"
#include "streamdock_catalog_fetcher.hpp"

#include <QMetaEnum>
#include <QtGlobal>

#include <algorithm>

namespace ajazz::app {

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
    // builds. The Streamdock + OpenDeck rows are merged in
    // asynchronously by their respective fetchers — first from the
    // on-disk cache (or bundled fallback) and then from the upstream
    // HTTP catalogue once each returns.
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

    // Kick the live upstream Streamdock + OpenDeck catalogues: each
    // fetcher emits the cached / fallback snapshot synchronously and
    // then the HTTP fetch result asynchronously, both via the
    // matching `replace*Rows` slot.
    if (m_streamdockFetcher) {
        m_streamdockFetcher->refresh();
    }
    if (m_opendeckFetcher) {
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

bool PluginCatalogModel::install(QString const& uuid) {
    int const row = findRow(m_rows, uuid);
    if (row < 0) {
        return false;
    }
    auto& state = m_install[uuid];
    if (state.installed) {
        return false; // idempotent — already installed.
    }
    state.installed = true;
    state.enabled = true;
    QModelIndex const idx = index(row);
    emit dataChanged(idx, idx, {InstalledRole, EnabledRole});
    emit installedCountChanged();
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
