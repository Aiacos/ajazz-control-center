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

#include <QMetaEnum>
#include <QtGlobal>

#include <algorithm>

namespace ajazz::app {

PluginCatalogModel::PluginCatalogModel(QObject* parent) : QAbstractListModel(parent) {
    // Populate with the mock fixture so the QML grid has rows in dev
    // builds. Real catalogue fetch lands behind the same reload() entry
    // point, so the UI is unaffected.
    reload();
}

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
    // Hand-curated fixture covering every compatibility mode (native,
    // opendeck, streamdeck, streamdock) across all three Plugin Store
    // sources (local, community, streamdock) and a representative spread
    // of categories / device classes. The shape matches the signed
    // catalogue index documented in docs/architecture/PLUGIN-SDK.md
    // (section "Plugin Store") so the QML page is exercised against
    // realistic data.
    std::vector<CatalogEntry> rows;
    rows.reserve(11);

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

    // ----------------------------------------------------------------------
    // AJAZZ Streamdock store mirror (compatibility=streamdock, source=streamdock).
    // These rows demonstrate the upstream catalogue surfaced under the
    // "AJAZZ Streamdock" tab of the Plugin Store. UUIDs follow the
    // `com.streamdock.*` reverse-DNS convention used by the official
    // Streamdock store; each entry carries an opaque StreamdockProductId
    // that the catalogue mirror resolves to a signed bundle URL.
    // ----------------------------------------------------------------------
    rows.push_back({
        QStringLiteral("com.streamdock.dial.audio-mixer"),
        QStringLiteral("Audio Mixer Dial"),
        QStringLiteral("2.4.1"),
        QStringLiteral("AJAZZ Streamdock"),
        QStringLiteral("Per-app volume mixer driving the AKP815 hardware dials, "
                       "with haptic detents at 0/50/100 \u0025."),
        QUrl(QStringLiteral("qrc:/qt/qml/AjazzControlCenter/icons/app.svg")),
        QStringLiteral("Audio"),
        {QStringLiteral("audio"), QStringLiteral("dial"), QStringLiteral("haptics")},
        {QStringLiteral("akp815")},
        QStringLiteral("streamdock"),
        QStringLiteral("4.6 MB"),
        /*verified*/ true,
        /*source*/ QStringLiteral("streamdock"),
        /*streamdockProductId*/ QStringLiteral("sd-audio-mixer"),
    });
    rows.push_back({
        QStringLiteral("com.streamdock.stream.scene-switcher"),
        QStringLiteral("Streamdock Scene Switcher"),
        QStringLiteral("1.7.3"),
        QStringLiteral("AJAZZ Streamdock"),
        QStringLiteral("One-tap OBS / Twitch scene switching with animated tile "
                       "transitions; first-party Streamdock plugin."),
        QUrl(QStringLiteral("qrc:/qt/qml/AjazzControlCenter/icons/app.svg")),
        QStringLiteral("Streaming"),
        {QStringLiteral("obs"), QStringLiteral("twitch"), QStringLiteral("streaming")},
        {QStringLiteral("akp153"), QStringLiteral("akp153e"), QStringLiteral("akp815")},
        QStringLiteral("streamdock"),
        QStringLiteral("5.1 MB"),
        /*verified*/ true,
        /*source*/ QStringLiteral("streamdock"),
        /*streamdockProductId*/ QStringLiteral("sd-scene-switcher"),
    });
    rows.push_back({
        QStringLiteral("com.streamdock.gaming.elite-dangerous"),
        QStringLiteral("Elite Dangerous HUD"),
        QStringLiteral("0.8.2"),
        QStringLiteral("AJAZZ Streamdock"),
        QStringLiteral("Live ship status, fuel and cargo readouts streamed from "
                       "Elite Dangerous to the AKP153 keys."),
        QUrl(QStringLiteral("qrc:/qt/qml/AjazzControlCenter/icons/app.svg")),
        QStringLiteral("Gaming"),
        {QStringLiteral("gaming"), QStringLiteral("hud"), QStringLiteral("telemetry")},
        {QStringLiteral("akp153"), QStringLiteral("akp153e")},
        QStringLiteral("streamdock"),
        QStringLiteral("2.9 MB"),
        /*verified*/ false,
        /*source*/ QStringLiteral("streamdock"),
        /*streamdockProductId*/ QStringLiteral("sd-elite-dangerous"),
    });

    return rows;
}

} // namespace ajazz::app
