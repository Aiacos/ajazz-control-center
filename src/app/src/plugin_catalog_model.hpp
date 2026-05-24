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
#include <QtQmlIntegration>
#include <QUrl>
#include <QVariantMap>

class QNetworkAccessManager;

#include <memory>
#include <type_traits>
#include <vector>

class QJSEngine;
class QQmlEngine;

namespace ajazz::app {

class StreamdockCatalogFetcher;
class OpenDeckCatalogFetcher;

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
    /**
     * @brief Direct download URL for the plugin archive (.sdPlugin /
     *        .acplugin.zip).
     *
     * Populated by the catalogue fetchers from the upstream `download`
     * (Streamdock) or `releaseAsset.browserDownloadUrl` (OpenDeck)
     * fields. Empty when the source feed does not surface a direct
     * download (e.g. a community page that only exposes a landing-page
     * URL). When empty, @ref PluginCatalogModel::install falls back to
     * the @ref openUpstream browser bridge.
     */
    QUrl downloadUrl = {};
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
    QML_NAMED_ELEMENT(PluginCatalog)
    QML_SINGLETON
    Q_PROPERTY(int count READ rowCountSimple NOTIFY countChanged)
    Q_PROPERTY(int installedCount READ installedCount NOTIFY installedCountChanged)
    Q_PROPERTY(QString streamdockState READ streamdockState NOTIFY streamdockStateChanged)
    Q_PROPERTY(qint64 streamdockFetchedAtUnixMs READ streamdockFetchedAtUnixMs NOTIFY
                   streamdockStateChanged)
    Q_PROPERTY(int streamdockCount READ streamdockCount NOTIFY countChanged)
    Q_PROPERTY(QString opendeckState READ opendeckState NOTIFY opendeckStateChanged)
    Q_PROPERTY(
        qint64 opendeckFetchedAtUnixMs READ opendeckFetchedAtUnixMs NOTIFY opendeckStateChanged)
    Q_PROPERTY(int opendeckCount READ opendeckCount NOTIFY countChanged)
    Q_PROPERTY(bool onlineCatalogEnabled READ onlineCatalogEnabled WRITE setOnlineCatalogEnabled
                   NOTIFY onlineCatalogEnabledChanged)

public:
    /// QML singleton factory — see BrandingService::create for the pattern.
    static PluginCatalogModel* create(QQmlEngine* qml, QJSEngine* js);

    /// Hand the singleton instance to the QML factory.
    static void registerInstance(PluginCatalogModel* instance) noexcept;

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

    // No default on `parent`: see BrandingService — a default-constructible
    // QML_SINGLETON makes Qt 6 pick `Constructor` mode and silently bypass
    // the static `create()` factory, spawning a duplicate QML-side instance.
    explicit PluginCatalogModel(QObject* parent);
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

    /// Origin of the currently visible OpenDeck rows. One of
    /// `"loading"`, `"online"`, `"cached"`, `"offline"`. Drives the
    /// OpenDeck tab info banner.
    [[nodiscard]] QString opendeckState() const;

    /// Unix-ms timestamp of the last successful OpenDeck snapshot.
    /// Zero when no snapshot has loaded yet (bundled fallback).
    [[nodiscard]] qint64 opendeckFetchedAtUnixMs() const noexcept {
        return m_opendeckFetchedAtUnixMs;
    }

    /// Number of OpenDeck rows currently in the model.
    [[nodiscard]] int opendeckCount() const;

    /**
     * @brief Install a plugin from its catalogue entry.
     *
     * Behaviour by source:
     *
     *   - If the row's @ref CatalogEntry::downloadUrl is non-empty:
     *     issue an HTTPS GET, save the downloaded `.sdPlugin` archive
     *     under the user plugins directory
     *     (`QStandardPaths::AppDataLocation/plugins/<uuid>.sdPlugin`),
     *     extract its top-level entries via Qt's QZipReader, then flip
     *     the local install bit and emit `dataChanged`. Progress is
     *     reported via @ref installProgressChanged. The terminal
     *     outcome is delivered via @ref installFinished.
     *
     *   - If the row has no downloadUrl (e.g. a community entry that
     *     only surfaces a landing page): fall back to the
     *     @ref openUpstream browser bridge so the user can still grab
     *     the plugin from the upstream source.
     *
     * @param uuid Catalogue UUID; no-op when the row does not exist.
     * @return True when an install operation was started (download
     *         queued, or browser bridge opened); false on lookup
     *         failure. The actual outcome arrives asynchronously via
     *         the @ref installFinished signal.
     */
    Q_INVOKABLE bool install(QString const& uuid);

    /**
     * @brief Install a plugin from a local `.sdPlugin` or `.zip` file.
     *
     * Implements the staging→verify→promote sequence (T-22-toctou):
     *
     *   1. Read the file (capped at @c kMaxPluginDownloadBytes) and run
     *      @ref validateDownloadedArchive — on error, emit
     *      @c installFinished(path, false, err) and return false.
     *   2. @ref extractSdPluginArchive into a STAGING directory (never
     *      directly into @c installedPlugins/).
     *   3. @ref verifyStagedPlugin on the extracted @c manifest.json:
     *      - @c Refused: quarantine (remove) staging dir, emit
     *        @c installFinished(false, reason), return false.
     *      - @c SelfSigned without @p userConfirmedUnsigned: emit
     *        @c installFinished(false, "self-signed plugin -- confirm to install")
     *        so QML can show a warning dialog; return false.
     *      - @c SelfSigned with @p userConfirmedUnsigned=true, or
     *        @c Trusted: promote staging dir into the Phase-18
     *        @c installedPlugins/ layout (atomic rename).
     *   4. On promote: emit @c dataChanged + @c installedCountChanged +
     *      @c installFinished(path, true, "").
     *
     * @p localPathOrUrl accepts either a filesystem path or a @c file://
     *   URL (e.g. from @c FileDialog.selectedFile); it is normalised via
     *   @c QUrl::fromUserInput / @c toLocalFile internally.
     *
     * @param localPathOrUrl Local path or @c file:// URL of the archive.
     * @param userConfirmedUnsigned Pass @c true when the user has
     *        explicitly confirmed installation of a self-signed plugin
     *        via the QML warning dialog. Default is @c false (refuse
     *        without explicit confirmation).
     * @return False when the install was refused synchronously (size/
     *         magic check, signature Refused, self-signed without
     *         confirm). True when the install succeeded and the plugin
     *         was promoted into @c installedPlugins/.
     *
     * Terminal outcome is always delivered via @ref installFinished.
     * Uses the local path as the key (uuid equivalent) in that signal.
     */
    Q_INVOKABLE bool installFromFile(QString const& localPathOrUrl,
                                     bool userConfirmedUnsigned = false);

    /**
     * @brief Validate a freshly-downloaded `.sdPlugin` blob before it is
     *        written to disk (WR-04): enforces a size cap and the ZIP magic.
     *
     * Static + pure so the security gate is unit-testable without a network
     * round-trip. Used by the @ref install download path.
     *
     * @param body Raw downloaded bytes.
     * @return Empty string when acceptable; otherwise a user-facing error.
     */
    [[nodiscard]] static QString validateDownloadedArchive(QByteArray const& body);

    /// Mark a plugin as removed. Returns true on success.
    Q_INVOKABLE bool uninstall(QString const& uuid);

    // ------------------------------------------------------------------
    // No-phone-home opt-in (PLUGIN-14 anti-feature, T-22-phonehome).
    //
    // The online Streamdock + OpenDeck catalogue fetchers do NOT run
    // automatically on construction. The user must explicitly enable the
    // live fetch via a QSettings-backed flag (default: false). The
    // offline snapshot (cache + bundled fallback) still populates the
    // store rows regardless of the flag.
    // ------------------------------------------------------------------

    /**
     * @brief Whether the online catalogue fetch is enabled.
     *
     * When false (the default), only the cached / bundled snapshot is
     * served; no outbound request is made. When true, @ref reload()
     * triggers a live fetch from the upstream Streamdock / OpenDeck
     * endpoints.
     *
     * The value is persisted via @c QSettings under
     * @c plugins/onlineCatalogEnabled so the user's choice survives
     * app restarts.
     */
    [[nodiscard]] Q_INVOKABLE bool onlineCatalogEnabled() const;

    /**
     * @brief Set and persist the online-catalogue-enabled flag.
     *
     * Callable from QML as
     * @c PluginCatalog.setOnlineCatalogEnabled(true/false).
     * When enabled, immediately triggers @ref refreshOnline() to
     * populate the streamdock / opendeck rows.
     */
    Q_INVOKABLE void setOnlineCatalogEnabled(bool enabled);

    /**
     * @brief Trigger a live online catalogue refresh.
     *
     * Calls the underlying fetcher @c refresh() unconditionally, regardless
     * of the @ref onlineCatalogEnabled() setting. Intended for the QML
     * "Refresh catalogue" button so the user can force an update without
     * permanently enabling auto-fetch.
     *
     * Use @ref reload() to also reset the local / mock rows in addition
     * to the live fetch.
     */
    Q_INVOKABLE void refreshOnline();

    // ------------------------------------------------------------------
    // Test seam: override the plugins directory so unit tests write to a
    // temp dir instead of the real QStandardPaths::AppDataLocation.
    // ------------------------------------------------------------------

    /**
     * @brief Override the plugins directory for tests.
     *
     * When non-empty, @ref userPluginsDir() returns this path instead of
     * deriving it from @c QStandardPaths::AppDataLocation. Must be set
     * before any @ref installFromFile or @ref install call.
     *
     * @note This is a test seam — production callers must not call this.
     */
    static void setPluginsDirOverride(QString const& dir);

    /**
     * @brief Open the plugin's upstream catalogue page in the user's
     *        default web browser.
     *
     * Bridge to the upstream Stream Dock / OpenDeck stores while the
     * inline-download flow (P3.17 + P3.18) is still scaffolded. URL
     * construction follows the public catalogue conventions:
     *
     *   - `source="streamdock"` + non-empty `streamdockProductId` →
     *     `https://stream-dock.com/store/product?id=<productId>`
     *     (vendor's published URL pattern).
     *   - `source="streamdock"` without product id → fall back to the
     *     store landing page.
     *   - Any other source → open `iconUrl` (best-effort: the icon URL
     *     usually lives on the same origin as the plugin's listing
     *     page so the user lands "near" the right page even without
     *     a specific browse URL).
     *
     * @param uuid Catalogue UUID.
     * @return true if a browser-open request was issued, false when
     *         the UUID is unknown or no reasonable URL could be derived.
     * @invokable Callable from QML as `PluginCatalogModel.openUpstream(uuid)`.
     */
    Q_INVOKABLE bool openUpstream(QString const& uuid) const;

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
    /// Emitted whenever @ref opendeckState changes.
    void opendeckStateChanged();
    /// Emitted when the online-catalogue-enabled flag changes.
    void onlineCatalogEnabledChanged();

    /**
     * @brief Per-row download progress in [0, 100].
     *
     * Emitted while an install is in flight; QML tile delegates can
     * bind a QProgressBar / ProgressBar to this signal so users see
     * the download advance. Not emitted for instant-installs (mock
     * rows without a downloadUrl).
     */
    void installProgressChanged(QString const& uuid, int percent);

    /**
     * @brief Install operation terminal state.
     *
     * Fires exactly once per @ref install call when the operation
     * finishes (success: downloaded + saved + activated; failure:
     * network/IO/parse error). @p error is empty on success and
     * carries a human-readable message otherwise (safe to surface
     * in a toast).
     */
    void installFinished(QString const& uuid, bool success, QString const& error);

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

    /// Replace the OpenDeck-sourced rows with @p rows. Mirrors
    /// @ref replaceStreamdockRows with `source == "opendeck"`.
    void replaceOpendeckRows(std::vector<CatalogEntry> rows);

    /// rowCount() with no arguments, matching the Q_PROPERTY READ shape.
    [[nodiscard]] int rowCountSimple() const { return static_cast<int>(m_rows.size()); }

    std::vector<CatalogEntry> m_rows;       ///< Catalogue snapshot.
    QHash<QString, InstallState> m_install; ///< Install / enabled state by UUID.

    /// QSettings-backed opt-in flag; default false (no phone-home on launch).
    bool m_onlineCatalogEnabled = false;

    /// Shared QNetworkAccessManager for plugin downloads (install path).
    /// Created lazily on the first `install()` call so the cheap mock
    /// catalogue tests don't drag in the network stack. Owned and
    /// parented to this model.
    QNetworkAccessManager* m_downloader = nullptr;

    /// Owns the upstream HTTP fetch + on-disk cache. Created lazily so
    /// unit tests that exercise just the install bookkeeping don't need
    /// the network stack.
    std::unique_ptr<StreamdockCatalogFetcher> m_streamdockFetcher;

    /// Cached state surfaced via @ref streamdockState().
    QString m_streamdockStateString = QStringLiteral("loading");

    /// Last-known timestamp of the Streamdock snapshot.
    qint64 m_streamdockFetchedAtUnixMs = 0;

    /// OpenDeck mirror — same lifetime / lazy-creation contract.
    std::unique_ptr<OpenDeckCatalogFetcher> m_opendeckFetcher;
    QString m_opendeckStateString = QStringLiteral("loading");
    qint64 m_opendeckFetchedAtUnixMs = 0;
};

// See BrandingService static_assert — same QML_SINGLETON dual-instance trap.
static_assert(!std::is_default_constructible_v<PluginCatalogModel>,
              "PluginCatalogModel must not be default-constructible — see BrandingService.");

} // namespace ajazz::app
