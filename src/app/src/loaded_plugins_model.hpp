// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file loaded_plugins_model.hpp
 * @brief List model surfacing the currently-loaded plugins to QML.
 *
 * Sibling of @ref PluginCatalogModel, but aimed at *runtime* plugins
 * (the inventory @ref ajazz::plugins::IPluginHost::plugins returns)
 * rather than the *catalogue* the user can browse and install. The
 * two surfaces stay distinct because their semantics differ:
 *
 *   - **Catalogue** = remote / on-disk index of plugins available to
 *     install. Drives the Plugin Store grid; per-row state includes
 *     install / disabled bits.
 *   - **Loaded plugins** = plugins the @c OutOfProcessPluginHost has
 *     successfully imported in this session. Drives the U2 trust-chip
 *     UI from SEC-003 #51 (warning when a plugin is unsigned or
 *     self-signed) and any future per-instance health / reload UX.
 *
 * The model is data-source-pluggable: callers populate it through
 * @ref setPlugins, which copies the supplied @ref PluginInfo vector
 * and emits a full reset. Wiring an actual @c IPluginHost into
 * @c Application is a separate slice — until then this model exists
 * to give the UI a stable role contract regardless of where the
 * data comes from.
 *
 * @note Not thread-safe — must be used on the Qt main thread.
 */
#pragma once

#include "ajazz/plugins/i_plugin_host.hpp"

#include <QAbstractListModel>
#include <QString>
#include <QStringList>
#include <QtQmlIntegration>

#include <vector>

class QJSEngine;
class QQmlEngine;

namespace ajazz::app {

/**
 * @class LoadedPluginsModel
 * @brief Read-only list of currently-loaded plugins for QML.
 *
 * Roles map directly onto @ref ajazz::plugins::PluginInfo with one
 * derived field, @ref TrustLevelRole, that flattens the
 * @c signed_ + @c publisher pair into a single enum the QML chip
 * delegate switches on:
 *
 *   - @c "trusted"     — signed manifest, key matches @c trusted_publishers.json.
 *   - @c "self-signed" — signed manifest, but key not in trust roots.
 *   - @c "unsigned"    — no signature OR signature failed verification.
 *
 * Two QML properties surface aggregate state to the page header:
 *
 *   * @c count           — total loaded plugins (proxies @ref rowCount);
 *   * @c untrustedCount  — number of rows with trust level not equal to
 *                          @c "trusted", used to drive a header badge.
 */
class LoadedPluginsModel : public QAbstractListModel {
    Q_OBJECT
    QML_NAMED_ELEMENT(LoadedPlugins)
    QML_SINGLETON
    Q_PROPERTY(int count READ rowCountSimple NOTIFY countChanged)
    Q_PROPERTY(int untrustedCount READ untrustedCount NOTIFY countChanged)

public:
    /// QML singleton factory — see @c BrandingService::create for the
    /// pattern. The factory hands back the instance the C++ side
    /// registered through @ref registerInstance.
    static LoadedPluginsModel* create(QQmlEngine* qml, QJSEngine* js);

    /// Hand the singleton instance to the QML factory. Called once
    /// from @c Application during startup with the long-lived model
    /// owned by @c Application.
    static void registerInstance(LoadedPluginsModel* instance) noexcept;

    /// Custom data roles available to QML delegates.
    enum Roles {
        IdRole = Qt::UserRole + 1, ///< Reverse-DNS plugin id (string, primary key).
        NameRole,                  ///< Display name.
        VersionRole,               ///< Semver version string.
        AuthorsRole,               ///< Author list (free-form string).
        PermissionsRole,           ///< QStringList of declared permissions.
        SignedRole,                ///< Bool — manifest signature verified.
        PublisherRole,             ///< Trust roots match name, "self-signed", or empty.
        TrustLevelRole,            ///< Derived enum string (see class doc).
    };

    // No default on `parent`: see BrandingService — a default-constructible
    // QML_SINGLETON makes Qt 6 pick `Constructor` mode and silently bypass
    // the static `create()` factory, spawning a duplicate QML-side instance.
    explicit LoadedPluginsModel(QObject* parent);
    ~LoadedPluginsModel() override = default;

    [[nodiscard]] int rowCount(QModelIndex const& parent = {}) const override;
    [[nodiscard]] QVariant data(QModelIndex const& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    /// Row count without the @c QModelIndex parameter so the
    /// @c count Q_PROPERTY can read it directly.
    [[nodiscard]] int rowCountSimple() const noexcept;

    /// Number of rows whose trust level is @b not @c "trusted".
    /// Drives the AppHeader badge that signals "you have plugins to
    /// review".
    [[nodiscard]] int untrustedCount() const noexcept;

    /**
     * @brief Replace the model contents with @p plugins.
     *
     * Performs a full @c beginResetModel / @c endResetModel cycle so
     * the QML view re-binds every delegate. Use this when refreshing
     * after @c IPluginHost::plugins or when wiring a fresh host
     * after @c loadAll. Empty input is valid (clears the model).
     */
    void setPlugins(std::vector<plugins::PluginInfo> plugins);

    /**
     * @brief Wire a long-lived plugin host so QML can trigger reloads.
     *
     * The pointer is non-owning — Application keeps the host alive
     * for the application's lifetime. Pass @c nullptr to detach.
     * After this call, @ref refresh becomes a usable Q_INVOKABLE for
     * QML "Reload" buttons.
     */
    void setPluginHost(plugins::IPluginHost* host) noexcept;

    /**
     * @brief Re-pull the plugin inventory from the wired host.
     *
     * No-op when no host is wired. On IPC failure (host died, child
     * crashed) the call leaves the model untouched and logs a
     * warning — the existing rows stay visible so the UI never
     * "disappears" on a transient error.
     */
    Q_INVOKABLE void refresh();

signals:
    void countChanged();

private:
    /// Collapse @c (signed_, publisher) into one of @c "trusted",
    /// @c "self-signed", @c "unsigned". Static + private because the
    /// rule is fixed by the @ref PluginInfo contract; the QML side
    /// only sees the resulting string. Static so unit tests can call
    /// it without constructing a model instance.
    [[nodiscard]] static QString trustLevelOf(plugins::PluginInfo const& info);

    std::vector<plugins::PluginInfo> m_plugins;
    /// Non-owning pointer to the plugin host. The host is owned by
    /// @c Application and outlives the model. Null until
    /// @ref setPluginHost runs (on a build without
    /// @c AJAZZ_PYTHON_HOST the model stays detached and
    /// @ref refresh is a no-op).
    plugins::IPluginHost* m_host{nullptr};
};

} // namespace ajazz::app
