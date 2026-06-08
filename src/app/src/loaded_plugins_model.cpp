// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file loaded_plugins_model.cpp
 * @brief Implementation of @ref ajazz::app::LoadedPluginsModel.
 */
#include "loaded_plugins_model.hpp"

#include <QQmlEngine>
#include <QtGlobal>

namespace ajazz::app {

namespace {

/// Singleton handle stashed by @ref Application at startup so the QML
/// factory can return it. nullptr until @ref registerInstance runs;
/// QML imports of @c LoadedPlugins before that point yield null —
/// @c Application registers very early so this is not a real risk in
/// production paths, but the QML engine asserts rather than crashing
/// if it ever happens.
LoadedPluginsModel* s_instance = nullptr;

} // namespace

LoadedPluginsModel* LoadedPluginsModel::create(QQmlEngine* /*qml*/, QJSEngine* /*js*/) {
    Q_ASSERT_X(s_instance != nullptr,
               "LoadedPluginsModel::create",
               "registerInstance must be called before QML imports the singleton");
    QQmlEngine::setObjectOwnership(s_instance, QQmlEngine::CppOwnership);
    return s_instance;
}

void LoadedPluginsModel::registerInstance(LoadedPluginsModel* instance) noexcept {
    s_instance = instance;
}

LoadedPluginsModel::LoadedPluginsModel(QObject* parent) : QAbstractListModel(parent) {}

int LoadedPluginsModel::rowCount(QModelIndex const& parent) const {
    if (parent.isValid()) {
        return 0;
    }
    return static_cast<int>(m_plugins.size());
}

int LoadedPluginsModel::rowCountSimple() const noexcept {
    return static_cast<int>(m_plugins.size());
}

int LoadedPluginsModel::untrustedCount() const noexcept {
    int n = 0;
    for (auto const& p : m_plugins) {
        if (trustLevelOf(p) != QStringLiteral("trusted")) {
            ++n;
        }
    }
    return n;
}

QVariant LoadedPluginsModel::data(QModelIndex const& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(m_plugins.size())) {
        return {};
    }
    auto const& info = m_plugins[static_cast<std::size_t>(index.row())];
    switch (role) {
    case IdRole:
        return QString::fromStdString(info.id);
    case NameRole:
        return QString::fromStdString(info.name);
    case VersionRole:
        return QString::fromStdString(info.version);
    case AuthorsRole:
        return QString::fromStdString(info.authors);
    case PermissionsRole: {
        QStringList out;
        out.reserve(static_cast<int>(info.permissions.size()));
        for (auto const& p : info.permissions) {
            out.append(QString::fromStdString(p));
        }
        return out;
    }
    case SignedRole:
        return info.signed_;
    case PublisherRole:
        return QString::fromStdString(info.publisher);
    case TrustLevelRole:
        return trustLevelOf(info);
    case PlatformStatusRole:
        return platformStatusOf(info);
    default:
        return {};
    }
}

QHash<int, QByteArray> LoadedPluginsModel::roleNames() const {
    return {
        {IdRole, "pluginId"},
        {NameRole, "name"},
        {VersionRole, "version"},
        {AuthorsRole, "authors"},
        {PermissionsRole, "permissions"},
        {SignedRole, "isSigned"},
        {PublisherRole, "publisher"},
        {TrustLevelRole, "trustLevel"},
        {PlatformStatusRole, "platformStatus"},
    };
}

void LoadedPluginsModel::setPlugins(std::vector<plugins::PluginInfo> plugins) {
    beginResetModel();
    m_plugins = std::move(plugins);
    endResetModel();
    emit countChanged();
}

void LoadedPluginsModel::setPluginHost(plugins::IPluginHost* host) noexcept {
    m_host = host;
}

void LoadedPluginsModel::refresh() {
    if (m_host == nullptr) {
        return;
    }
    try {
        // `IPluginHost::plugins` throws on a dead child / IPC timeout.
        // Catch and keep the existing rows visible — a transient
        // failure should not erase the UI; the user can retry.
        setPlugins(m_host->plugins());
    } catch (std::exception const& e) {
        qWarning("LoadedPluginsModel::refresh: %s", e.what());
    }
}

QString LoadedPluginsModel::trustLevelOf(plugins::PluginInfo const& info) {
    if (!info.signed_) {
        return QStringLiteral("unsigned");
    }
    // signed_ == true: distinguish trusted publisher (name from
    // trust_roots.json) from self-signed (key valid but unknown).
    // The host writes "self-signed" verbatim into publisher when the
    // verifier returned valid==true and the key didn't match any
    // trust roots; an empty publisher with signed_==true would mean
    // the wiring is broken — fail closed (treat as self-signed) rather
    // than mislabel.
    if (info.publisher.empty() || info.publisher == "self-signed") {
        return QStringLiteral("self-signed");
    }
    return QStringLiteral("trusted");
}

QString LoadedPluginsModel::platformStatusOf(plugins::PluginInfo const& info) {
    // WINPLG-03 (chip-only this phase). Mirror PluginInfo.winClass (the plain-int
    // verdict stamped at scan time by WINPLG-01/02; mapping documented on the
    // field): 0=NotWindowsOnly, 1=WsOnlyIpc, 2=VendorDll.
    //
    // DEFERRED: a real Wine launcher (WINPLG-03 launch) is out of scope this
    // phase. `wineAvailable` is therefore hard-false here. The wine-vs-unsupported
    // branch is kept EXPLICIT so a future hardware-gated phase flips this one
    // input rather than reshaping the derive (and the unit test pins the shape).
    constexpr bool kWineAvailable = false; // WINPLG-03 launch deferred — never bundle Wine.

    switch (info.winClass) {
    case 1: // WsOnlyIpc — runs natively on every platform (no Wine needed).
        return QStringLiteral("native");
    case 2: // VendorDll — needs Windows, or Wine (deferred).
        return kWineAvailable ? QStringLiteral("wine") : QStringLiteral("unsupported");
    case 0: // NotWindowsOnly — not a Windows-only plugin; no classification chip.
    default:
        return {};
    }
}

} // namespace ajazz::app
