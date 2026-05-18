// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file app_update_service.hpp
 * @brief QML-facing GitHub-Releases-driven application update checker.
 *
 * Mirrors the design in @c docs/architecture/APP-AUTO-UPDATE.md
 * (commit `4e624fd`, decided 2026-05-18). Notify-only: hits
 * `https://api.github.com/repos/Aiacos/ajazz-control-center/releases/latest`
 * via @c QNetworkAccessManager, parses the JSON envelope, and surfaces a
 * banner / settings toggles to QML. The download itself happens via
 * @c Qt.openUrlExternally on a release-page URL — there is no silent
 * install path in v1.
 *
 * Self-disable matrix:
 *   * `FLATPAK_ID` env var present -> @c Status::Disabled at construction;
 *     `checkNow()` is a no-op and the 24-hour timer never starts. Flathub
 *     owns the update path for Flatpak users.
 *   * Linux .deb/.rpm distribution -> we still hit the API and surface a
 *     banner, but the banner action text changes to "Run your system
 *     updater" in QML (see @c platformLabel).
 *   * Windows .msi / macOS .dmg -> banner action opens the release page.
 *   * Source builds (`platformLabel == "Source build"`) -> banner suggests
 *     `git pull + rebuild`.
 *
 * Pattern follows @c LightingService / @c SettingsService:
 *   * `QML_NAMED_ELEMENT(AppUpdate)` + `QML_SINGLETON`
 *   * QML factory `create()` returns the @ref Application-owned instance
 *     registered via @ref registerInstance() (Pitfall 4 build-break lock).
 *   * Non-default-constructible so QML cannot bypass the factory.
 */
#pragma once

#include <QObject>
#include <QString>
#include <QtQmlIntegration>
#include <QUrl>

#include <type_traits>

class QJSEngine;
class QNetworkAccessManager;
class QNetworkReply;
class QQmlEngine;
class QTimer;

namespace ajazz::app {

/**
 * @class AppUpdateService
 * @brief QML singleton checking GitHub Releases for newer versions of the app.
 *
 * Exposed as `AppUpdate` in QML. The service owns its own
 * @c QNetworkAccessManager (created lazily on first @ref checkNow) and a
 * 24-hour @c QTimer that fires `performCheck()` periodically when
 * @ref autoCheckEnabled is true. The very first auto-check is scheduled
 * 5 s after construction via @c QTimer::singleShot so the splash isn't
 * blocked.
 */
class AppUpdateService : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(AppUpdate)
    QML_SINGLETON

    Q_PROPERTY(bool autoCheckEnabled READ autoCheckEnabled WRITE setAutoCheckEnabled NOTIFY
                   autoCheckEnabledChanged)
    Q_PROPERTY(bool includeNightly READ includeNightly WRITE setIncludeNightly NOTIFY
                   includeNightlyChanged)
    Q_PROPERTY(QString currentVersion READ currentVersion CONSTANT)
    Q_PROPERTY(QString latestVersion READ latestVersion NOTIFY latestVersionChanged)
    Q_PROPERTY(QString latestReleaseNotes READ latestReleaseNotes NOTIFY latestReleaseNotesChanged)
    Q_PROPERTY(QUrl latestReleaseUrl READ latestReleaseUrl NOTIFY latestReleaseUrlChanged)
    Q_PROPERTY(Status status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString platformLabel READ platformLabel CONSTANT)

public:
    /// Banner state — Q_ENUM so QML can compare against `AppUpdate.Status.*`.
    enum Status {
        Idle = 0,        ///< Constructed; nothing in flight; no banner.
        Checking,        ///< Network request in flight.
        UpToDate,        ///< Last successful check found no newer release.
        UpdateAvailable, ///< Last successful check found a newer release.
        Error,           ///< Last check failed (transport / parse / 4xx-5xx).
        Disabled,        ///< Self-disabled (Flatpak runtime). No outbound network.
    };
    Q_ENUM(Status)

    explicit AppUpdateService(QObject* parent);
    ~AppUpdateService() override;

    static AppUpdateService* create(QQmlEngine* qml, QJSEngine* js);
    static void registerInstance(AppUpdateService* instance) noexcept;

    [[nodiscard]] bool autoCheckEnabled() const noexcept { return m_autoCheckEnabled; }
    void setAutoCheckEnabled(bool enabled);

    [[nodiscard]] bool includeNightly() const noexcept { return m_includeNightly; }
    void setIncludeNightly(bool enabled);

    /// Compile-time / runtime app version. Pulled from @c qApp->applicationVersion()
    /// (set in @c main.cpp via @c QApplication::setApplicationVersion).
    [[nodiscard]] QString currentVersion() const;

    [[nodiscard]] QString latestVersion() const { return m_latestVersion; }
    [[nodiscard]] QString latestReleaseNotes() const { return m_latestReleaseNotes; }
    [[nodiscard]] QUrl latestReleaseUrl() const { return m_latestReleaseUrl; }
    [[nodiscard]] Status status() const noexcept { return m_status; }

    /// Runtime-detected packaging format. One of: "Flatpak", "AppImage",
    /// "macOS DMG", "Windows MSI", "Linux .deb/.rpm", "Source build".
    /// Drives the banner action text on the QML side.
    [[nodiscard]] QString platformLabel() const;

    /**
     * @brief Trigger a manual check.
     *
     * No-op when @ref status() is @c Status::Disabled (Flatpak) or
     * @c Status::Checking (already in flight). Otherwise transitions to
     * @c Status::Checking and fires the HTTPS GET. Always safe to invoke
     * from QML.
     */
    Q_INVOKABLE void checkNow();

    /**
     * @brief Dismiss the current update banner.
     *
     * Sets @ref status() to @c Status::Idle (banner hides) and records the
     * dismissed tag in @c QSettings under `AppUpdate/dismissedTag` so we
     * don't re-surface the same release until the next application launch
     * (or until a newer release lands upstream).
     */
    Q_INVOKABLE void dismissCurrentUpdate();

    /**
     * @brief Pure semver comparison used internally + exposed for testing.
     *
     * Accepts tags like `v1.2.3`, `1.2.3`, `1.2.3-rc1`, `nightly-YYYYMMDD-<sha>`.
     * Strips a leading `v`, splits on `.`, compares integer components
     * numerically, then ranks the alphabetic suffix:
     *   * empty suffix > rc/alpha/beta suffix (a release is newer than its rc).
     *   * suffixes compare lexicographically.
     *
     * Nightly handling: `nightly-YYYYMMDD-*` is treated as "newer than every
     * stable release"; caller layers `includeNightly` policy on top.
     *
     * @param candidate Tag returned by the GitHub Releases API.
     * @param current   The currently-running build version.
     * @return true iff @p candidate represents a newer release.
     */
    [[nodiscard]] static bool isNewerThan(QString const& candidate, QString const& current);

signals:
    void autoCheckEnabledChanged(bool enabled);
    void includeNightlyChanged(bool enabled);
    void latestVersionChanged(QString const& version);
    void latestReleaseNotesChanged(QString const& notes);
    void latestReleaseUrlChanged(QUrl const& url);
    void statusChanged(Status status);

private:
    /// Fire the HTTPS GET against the GitHub Releases endpoint.
    void performCheck();

    /// Handle the reply for `/releases/latest` (stable channel).
    void onLatestReplyFinished(QNetworkReply* reply);

    /// Handle the reply for `/releases/tags/nightly` (nightly channel).
    void onNightlyReplyFinished(QNetworkReply* reply);

    /// Set the status and emit @ref statusChanged on transition.
    void setStatus(Status newStatus);

    /// Apply a freshly-parsed release record to the m_latest* fields,
    /// emitting all relevant @ref Q_PROPERTY notifiers.
    void applyRelease(QString const& tag, QString const& notes, QUrl const& url);

    bool m_autoCheckEnabled = true;
    bool m_includeNightly = false;
    Status m_status = Status::Idle;
    QString m_latestVersion;
    QString m_latestReleaseNotes;
    QUrl m_latestReleaseUrl;
    QString m_etag;                         ///< Cached If-None-Match value.
    QNetworkAccessManager* m_nam = nullptr; ///< Lazily allocated on first checkNow().
    QTimer* m_pollTimer = nullptr;          ///< 24-hour periodic check; nullptr on Flatpak.
};

// Pitfall 4 build-break lock — co-located with QML_SINGLETON.
static_assert(
    !std::is_default_constructible_v<AppUpdateService>,
    "AppUpdateService must not be default-constructible — see ctor @note and BrandingService.");

} // namespace ajazz::app
