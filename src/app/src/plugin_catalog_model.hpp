// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file plugin_catalog_model.hpp
 * @brief List model surfacing the Plugin Store catalogue to QML.
 *
 * PluginCatalogModel exposes a flat list of plugin entries to the
 * `PluginStore.qml` page. The model is deliberately backend-agnostic:
 *
 *   * the row data is a plain @ref CatalogEntry struct;
 *   * the source is pluggable via @ref setSource(), which today returns a
 *     hard-coded mock fixture and tomorrow will fetch a signed JSON
 *     catalogue (see docs/architecture/PLUGIN-SDK.md, section "Plugin
 *     Store") from `https://store.aiacos.dev/catalogue/v1/index.json`.
 *
 * Per-row install / disable state is held in a side map keyed by UUID;
 * QML-invokable mutators flip those bits and emit `dataChanged` so the
 * grid delegate updates without a full reset.
 *
 * @note Not thread-safe — must be used on the Qt main thread.
 *
 * @see docs/architecture/PLUGIN-SDK.md
 * @see docs/schemas/plugin_manifest.schema.json
 */
#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariantMap>

#include <memory>
#include <vector>

namespace ajazz::app {

class StreamdockCatalogFetcher;

/// Single catalogue entry shown by the Plugin Store grid.
///
/// Field shape mirrors the catalogue index documented in
/// docs/architecture/PLUGIN-SDK.md so this struct can be deserialised
/// directly from the eventual signed JSON feed without further mapping.
struct CatalogEntry {
    QString uuid;          ///< Reverse-DNS plugin id (matches manifest UUID).
    QString name;          ///< Display name shown in the grid tile title.
    QString version;       ///< Semver string ("1.2.0").
    QString author;        ///< Author / organisation displayed in the byline.
    QString description;   ///< One-line summary used as the tile body.
    QUrl iconUrl;          ///< Tile icon (https URL or qrc: alias for mocks).
    QString category;      ///< Optional grouping label ("Streaming", "System").
    QStringList tags;      ///< Free-form tags used by the search filter.
    QStringList devices;   ///< Codenames the plugin declares compatibility with.
    QString compatibility; ///< "native" | "opendeck" | "streamdeck" | "streamdock".
    QString sizeBytes;     ///< Pre-formatted download size for the tile footer.
    bool verified = false; ///< True when the catalogue entry has a Sigstore bundle.
    /**
     * @brief Catalogue origin used to drive the Plugin Store source tabs.
     *
     * One of:
     *   * `"local"`     — first-party AJAZZ Control Center catalogue (default).
     *   * `"community"` — community-maintained third-party plugins.
     *   * `"streamdock"` — mirrored from the official AJAZZ Streamdock store.
     *
     * The `"streamdock"` value pairs with @ref compatibility = "streamdock"
     * and is the bridge to the upstream catalogue described in
     * docs/architecture/PLUGIN-SDK.md, section “Compatibility modes”.
     */
    QString source = QStringLiteral("local");
    /// Upstream product identifier when @ref source = "streamdock". Empty
    /// for first-party / community catalogue rows. The default member
    /// initializer doubles as a `-Wmissing-field-initializers` suppressor
    /// so callers can keep the historical positional-initialization style
    /// without listing every trailing optional field explicitly.
    QString streamdockProductId = {};
};

/**
 * @class PluginCatalogModel
 * @brief Read model + per-row install state for the Plugin Store UI.
 *
 * The model exposes one row per catalogue entry. QML delegates read fields
 * via the role names defined in @ref roleNames(); install / disable state
 * is mutated through the Q_INVOKABLE setters below, which emit `dataChanged`
 * for the affected row.
 *
 * Two QML properties surface aggregate state to the page header:
 *
 *   * `installedCount` — number of currently-installed plugins;
 *   * `count`          — total catalogue size (proxies @ref rowCount).
 *
 * The model owns no networking yet; @ref reload() simply re-applies the
 * mock fixture. When the real catalogue lands it will swap the fixture
 * for a `QNetworkAccessManager`-backed fetch.
 */
class PluginCatalogModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ rowCountSimple NOTIFY countChanged)
    Q_PROPERTY(int installedCount READ installedCount NOTIFY installedCountChanged)
    Q_PROPERTY(QString streamdockState READ streamdockState NOTIFY streamdockStateChanged)
    Q_PROPERTY(qint64 streamdockFetchedAtUnixMs READ streamdockFetchedAtUnixMs NOTIFY
                   streamdockStateChanged)
    Q_PROPERTY(int streamdockCount READ streamdockCount NOTIFY countChanged)

public:
    /// Custom data roles available to QML delegates.
    enum Roles {
        UuidRole = Qt::UserRole + 1, ///< Reverse-DNS plugin id (string, primary key).
        NameRole,                    ///< Display name.
        VersionRole,                 ///< Semver version string.
        AuthorRole,                  ///< Author / organisation.
        DescriptionRole,             ///< One-line summary.
        IconUrlRole,                 ///< Tile icon URL (https or qrc).
        CategoryRole,                ///< Grouping label.
        TagsRole,                    ///< QStringList of free-form tags.
        DevicesRole,                 ///< QStringList of supported device codenames.
        CompatibilityRole,           ///< "native" | "opendeck" | "streamdeck" | "streamdock".
        SizeBytesRole,               ///< Pre-formatted download size string.
        VerifiedRole,                ///< Sigstore-verified flag (bool).
        InstalledRole,               ///< True when the local plugin list contains this UUID.
        EnabledRole,                 ///< True when the installed plugin is currently enabled.
        SourceRole,                  ///< "local" | "community" | "streamdock".
        StreamdockProductIdRole,     ///< Upstream Streamdock product id (when source==streamdock).
    };

    explicit PluginCatalogModel(QObject* parent = nullptr);
    /// Out-of-line destructor: the @c unique_ptr<StreamdockCatalogFetcher>
    /// member needs the fetcher's full type at the point of destruction,
    /// and the header only forward-declares it to keep the include graph
    /// shallow (the network stack is a heavy include).
    ~PluginCatalogModel() override;

    [[nodiscard]] int rowCount(QModelIndex const& parent = {}) const override;
    [[nodiscard]] QVariant data(QModelIndex const& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    /// Number of installed plugins; surfaces the @c installedCount QML property.
    [[nodiscard]] int installedCount() const;

    /**
     * @brief Re-populate the model from the current source.
     *
     * Re-applies the built-in mock fixture for the first-party /
     * community rows and triggers the live Streamdock catalogue
     * refresh. The Streamdock rows are merged into the model whenever
     * the @ref StreamdockCatalogFetcher emits a fresh snapshot, so the
     * grid updates in place once the network round-trip returns.
     */
    Q_INVOKABLE void reload();

    /**
     * @brief Origin of the currently visible Streamdock rows.
     *
     * One of: `"loading"`, `"online"`, `"cached"`, `"offline"`. Drives
     * the AJAZZ Streamdock tab info banner.
     */
    [[nodiscard]] QString streamdockState() const;

    /**
     * @brief Unix-ms timestamp of the last successful Streamdock snapshot.
     *
     * Zero when no snapshot has loaded yet (e.g. the bundled fallback).
     * The QML banner formats this as a relative time ("updated 3 min ago").
     */
    [[nodiscard]] qint64 streamdockFetchedAtUnixMs() const noexcept {
        return m_streamdockFetchedAtUnixMs;
    }

    /// Number of Streamdock rows currently in the model.
    [[nodiscard]] int streamdockCount() const;

    /**
     * @brief Mark a plugin as installed and enabled.
     * @param uuid Catalogue UUID; no-op if the row does not exist.
     * @return True when the row was found and updated.
     *
     * Installation is mocked: we flip the local install bit and emit
     * `dataChanged` for the affected row. The real installer will
     * download, verify and unpack the `.acplugin.zip` archive (see
     * docs/architecture/PLUGIN-SDK.md, section "Sandboxing").
     */
    Q_INVOKABLE bool install(QString const& uuid);

    /// Mark a plugin as removed. Returns true on success.
    Q_INVOKABLE bool uninstall(QString const& uuid);

    /// Toggle the enabled flag for an installed plugin. Returns the new value.
    Q_INVOKABLE bool toggleEnabled(QString const& uuid);

    /**
     * @brief Look up a single row as a flat QVariantMap.
     * @param uuid Catalogue UUID.
     * @return Map keyed by role name, or an empty map when the UUID is unknown.
     *
     * The Plugin Store details pane uses this to render the side panel
     * for the currently-selected tile without re-walking the model.
     */
    [[nodiscard]] Q_INVOKABLE QVariantMap entryFor(QString const& uuid) const;

signals:
    /// Emitted when the catalogue size changes (after @ref reload()).
    void countChanged();
    /// Emitted whenever an install / uninstall flips a row's state.
    void installedCountChanged();
    /// Emitted whenever @ref streamdockState changes.
    void streamdockStateChanged();

private:
    /// Per-row install bookkeeping kept outside @ref CatalogEntry so the
    /// catalogue feed (which is read-only) and the local user state stay
    /// cleanly separated.
    struct InstallState {
        bool installed = false; ///< True when the user has installed this plugin.
        bool enabled = false;   ///< Active flag (only meaningful while installed).
    };

    /// Fixed mock fixture used in dev builds. The shape mirrors the
    /// signed catalogue index defined in docs/architecture/PLUGIN-SDK.md.
    static std::vector<CatalogEntry> mockFixture();

    /// Replace the Streamdock-sourced rows with @p rows, emitting the
    /// minimal `dataChanged` / model reset surface required.
    void replaceStreamdockRows(std::vector<CatalogEntry> rows);

    /// rowCount() with no arguments, matching the Q_PROPERTY READ shape.
    [[nodiscard]] int rowCountSimple() const { return static_cast<int>(m_rows.size()); }

    std::vector<CatalogEntry> m_rows;       ///< Catalogue snapshot.
    QHash<QString, InstallState> m_install; ///< Install / enabled state by UUID.

    /// Owns the upstream HTTP fetch + on-disk cache. Created lazily so
    /// unit tests that exercise just the install bookkeeping don't need
    /// the network stack.
    std::unique_ptr<StreamdockCatalogFetcher> m_streamdockFetcher;

    /// Cached state surfaced via @ref streamdockState().
    QString m_streamdockStateString = QStringLiteral("loading");

    /// Last-known timestamp of the Streamdock snapshot.
    qint64 m_streamdockFetchedAtUnixMs = 0;
};

} // namespace ajazz::app
