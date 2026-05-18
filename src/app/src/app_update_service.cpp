// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file app_update_service.cpp
 * @brief Implementation of @ref ajazz::app::AppUpdateService.
 *
 * GitHub Releases JSON parse uses Qt's @c QJsonDocument / @c QJsonObject
 * directly — @c nlohmann::json is forbidden in @c ajazz_core / @c app
 * targets (COD-031 boundary). The fields we touch are documented at
 * https://docs.github.com/en/rest/releases/releases.
 */
#include "app_update_service.hpp"

#include "ajazz/core/logger.hpp"

#include <QAbstractEventDispatcher>
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QQmlEngine>
#include <QRegularExpression>
#include <QSettings>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QVariant>

namespace ajazz::app {

namespace {

AppUpdateService* g_instance = nullptr;

/// GitHub Releases API endpoint for the stable channel.
constexpr char kLatestEndpoint[] =
    "https://api.github.com/repos/Aiacos/ajazz-control-center/releases/latest";

/// GitHub Releases API endpoint for the rolling nightly tag.
constexpr char kNightlyEndpoint[] =
    "https://api.github.com/repos/Aiacos/ajazz-control-center/releases/tags/nightly";

/// 24 hours in milliseconds — auto-check cadence per design doc.
constexpr int kPollIntervalMs = 24 * 60 * 60 * 1000;

/// Delay before the first auto-check fires after construction (don't
/// block the splash). Mirrors KeePassXC's `Config::GUI_CheckForUpdatesNextCheck`.
constexpr int kFirstCheckDelayMs = 5000;

/// QSettings key holding the auto-check user preference.
constexpr char kAutoCheckKey[] = "AppUpdate/autoCheck";

/// QSettings key holding the include-nightly user preference.
constexpr char kIncludeNightlyKey[] = "AppUpdate/includeNightly";

/// QSettings key holding the most-recently-dismissed tag so we don't
/// re-surface the same release until next launch.
constexpr char kDismissedTagKey[] = "AppUpdate/dismissedTag";

/// Strip a leading 'v' / 'V' from a version-ish string.
QString stripLeadingV(QString s) {
    if (!s.isEmpty() && (s.front() == QLatin1Char('v') || s.front() == QLatin1Char('V'))) {
        s.remove(0, 1);
    }
    return s;
}

/// Split a semver-ish core (no leading 'v') into numeric components and
/// the alphabetic suffix after the first '-'. e.g. "1.2.3-rc1" -> ({1,2,3}, "rc1").
struct VersionParts {
    QList<int> components;
    QString suffix; // empty for a "stable" release.
    bool isNightly = false;
};

VersionParts parseVersion(QString const& raw) {
    VersionParts out;
    QString core = stripLeadingV(raw.trimmed());
    if (core.startsWith(QStringLiteral("nightly"), Qt::CaseInsensitive)) {
        out.isNightly = true;
        out.suffix = core;
        return out;
    }
    int const dashIdx = core.indexOf(QLatin1Char('-'));
    QString head = core;
    if (dashIdx >= 0) {
        head = core.left(dashIdx);
        out.suffix = core.mid(dashIdx + 1);
    }
    auto const parts = head.split(QLatin1Char('.'), Qt::SkipEmptyParts);
    out.components.reserve(parts.size());
    for (auto const& p : parts) {
        bool ok = false;
        int const v = p.toInt(&ok);
        out.components.append(ok ? v : 0);
    }
    return out;
}

} // namespace

AppUpdateService::AppUpdateService(QObject* parent) : QObject(parent) {
    // Load user prefs first so the auto-check timer wiring below respects
    // a previously-set autoCheckEnabled = false.
    QSettings settings;
    m_autoCheckEnabled =
        settings.value(QString::fromLatin1(kAutoCheckKey), true).toBool();
    m_includeNightly =
        settings.value(QString::fromLatin1(kIncludeNightlyKey), false).toBool();

    // Flatpak self-disable: Flathub manages updates via GNOME Software /
    // KDE Discover. Setting Status::Disabled here AND not allocating the
    // QTimer means checkNow() is a no-op and no outbound network from
    // this service is possible. Tested via QProcessEnvironment / env var
    // in tests/unit/test_app_update_service.cpp.
    if (qEnvironmentVariableIsSet("FLATPAK_ID")) {
        m_status = Status::Disabled;
        AJAZZ_LOG_INFO("app-update",
                       "self-disabled: FLATPAK_ID set, deferring to Flathub for updates");
        return;
    }

    // 24-hour periodic auto-check. Owned by `this`; destroyed automatically.
    // The timer is unconditionally created so a later setAutoCheckEnabled(true)
    // call (e.g. user flips the toggle in Settings) can simply start() it
    // without re-allocating. Note: starting a QTimer without a running
    // QCoreApplication event loop is harmless but noisy on stderr; we
    // therefore gate the start() call on the event-loop-runner being a
    // QApplication / QCoreApplication (true in production, false in the
    // headless unit-test harness).
    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(kPollIntervalMs);
    QObject::connect(m_pollTimer, &QTimer::timeout, this, &AppUpdateService::performCheck);
    bool const haveEventLoop = QCoreApplication::instance() != nullptr &&
                               QCoreApplication::eventDispatcher() != nullptr;
    if (m_autoCheckEnabled && haveEventLoop) {
        m_pollTimer->start();
        // First check 5 s after construction so the splash isn't blocked.
        QTimer::singleShot(kFirstCheckDelayMs, this, &AppUpdateService::checkNow);
    }
}

AppUpdateService::~AppUpdateService() = default;

AppUpdateService* AppUpdateService::create(QQmlEngine*, QJSEngine*) {
    if (g_instance != nullptr) {
        QQmlEngine::setObjectOwnership(g_instance, QQmlEngine::CppOwnership);
    }
    return g_instance;
}

void AppUpdateService::registerInstance(AppUpdateService* instance) noexcept {
    g_instance = instance;
}

void AppUpdateService::setAutoCheckEnabled(bool enabled) {
    if (m_autoCheckEnabled == enabled) {
        return;
    }
    m_autoCheckEnabled = enabled;
    QSettings settings;
    settings.setValue(QString::fromLatin1(kAutoCheckKey), enabled);
    if (m_pollTimer != nullptr) {
        bool const haveEventLoop = QCoreApplication::instance() != nullptr &&
                                   QCoreApplication::eventDispatcher() != nullptr;
        if (enabled && haveEventLoop) {
            m_pollTimer->start();
        } else {
            m_pollTimer->stop();
        }
    }
    emit autoCheckEnabledChanged(enabled);
}

void AppUpdateService::setIncludeNightly(bool enabled) {
    if (m_includeNightly == enabled) {
        return;
    }
    m_includeNightly = enabled;
    QSettings settings;
    settings.setValue(QString::fromLatin1(kIncludeNightlyKey), enabled);
    emit includeNightlyChanged(enabled);
}

QString AppUpdateService::currentVersion() const {
    // qApp->applicationVersion() is set in main.cpp via
    // QApplication::setApplicationVersion("0.1.0"). When the harness
    // doesn't construct a QCoreApplication (defensive) fall back to a
    // safe sentinel so QML bindings stay well-typed.
    if (QCoreApplication::instance() != nullptr) {
        QString const v = QCoreApplication::applicationVersion();
        if (!v.isEmpty()) {
            return v;
        }
    }
    return QStringLiteral("0.0.0");
}

QString AppUpdateService::platformLabel() const {
    if (qEnvironmentVariableIsSet("FLATPAK_ID")) {
        return QStringLiteral("Flatpak");
    }
    if (qEnvironmentVariableIsSet("APPIMAGE")) {
        return QStringLiteral("AppImage");
    }
#if defined(Q_OS_MACOS)
    return QStringLiteral("macOS DMG");
#elif defined(Q_OS_WIN)
    return QStringLiteral("Windows MSI");
#elif defined(Q_OS_LINUX)
    return QStringLiteral("Linux .deb/.rpm");
#else
    return QStringLiteral("Source build");
#endif
}

void AppUpdateService::checkNow() {
    if (m_status == Status::Disabled) {
        // Flatpak self-disable: never fire outbound network.
        return;
    }
    if (m_status == Status::Checking) {
        // Already in flight. Avoid duplicate requests.
        return;
    }
    performCheck();
}

void AppUpdateService::dismissCurrentUpdate() {
    // Persist the dismissed tag so we don't re-surface the same release
    // banner on the next auto-check this session. A newer release lands
    // -> a different tag -> we surface again. Cleared on next launch
    // implicitly since startup pulls the latest tag fresh from upstream.
    if (!m_latestVersion.isEmpty()) {
        QSettings settings;
        settings.setValue(QString::fromLatin1(kDismissedTagKey), m_latestVersion);
    }
    setStatus(Status::Idle);
}

bool AppUpdateService::isNewerThan(QString const& candidate, QString const& current) {
    auto const cand = parseVersion(candidate);
    auto const curr = parseVersion(current);

    // Nightly handling: nightly-* is always newer than any stable build
    // (caller layers the includeNightly policy on top of this helper).
    if (cand.isNightly && !curr.isNightly) {
        return true;
    }
    if (!cand.isNightly && curr.isNightly) {
        return false;
    }
    if (cand.isNightly && curr.isNightly) {
        // Compare nightly suffixes lexicographically (nightly-YYYYMMDD-sha
        // sorts naturally because the date prefix is fixed-width).
        return cand.suffix > curr.suffix;
    }

    // Numeric component compare. Treat missing trailing components as 0
    // so "1.2" < "1.2.1" and "1.2.0" == "1.2".
    int const n = std::max(cand.components.size(), curr.components.size());
    for (int i = 0; i < n; ++i) {
        int const a = i < cand.components.size() ? cand.components[i] : 0;
        int const b = i < curr.components.size() ? curr.components[i] : 0;
        if (a != b) {
            return a > b;
        }
    }

    // Equal numeric components — rank suffix. An empty suffix
    // (release) outranks any non-empty suffix (rc1, alpha, beta).
    if (cand.suffix.isEmpty() && !curr.suffix.isEmpty()) {
        return true; // release > rc
    }
    if (!cand.suffix.isEmpty() && curr.suffix.isEmpty()) {
        return false; // rc < release
    }
    if (cand.suffix.isEmpty() && curr.suffix.isEmpty()) {
        return false; // exact equal
    }
    return cand.suffix > curr.suffix;
}

void AppUpdateService::performCheck() {
    if (m_status == Status::Disabled) {
        return;
    }
    setStatus(Status::Checking);

    if (m_nam == nullptr) {
        m_nam = new QNetworkAccessManager(this);
    }

    QNetworkRequest req{QUrl{QString::fromLatin1(kLatestEndpoint)}};
    req.setRawHeader("Accept", "application/vnd.github+json");
    req.setRawHeader("User-Agent", "ajazz-control-center (+https://github.com/Aiacos/ajazz-control-center)");
    if (!m_etag.isEmpty()) {
        req.setRawHeader("If-None-Match", m_etag.toUtf8());
    }

    auto* const reply = m_nam->get(req);
    QObject::connect(reply, &QNetworkReply::finished, this,
                     [this, reply]() { onLatestReplyFinished(reply); });
}

void AppUpdateService::onLatestReplyFinished(QNetworkReply* reply) {
    reply->deleteLater();

    int const httpStatus =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    // 304 Not Modified — the cached ETag is still valid. Don't churn the
    // status; remain UpToDate (or whatever the previous state was).
    if (httpStatus == 304) {
        if (m_status == Status::Checking) {
            setStatus(m_latestVersion.isEmpty() ? Status::UpToDate
                                                : Status::UpdateAvailable);
        }
        return;
    }

    if (reply->error() != QNetworkReply::NoError) {
        AJAZZ_LOG_WARN("app-update",
                       "update check failed: {} (HTTP {})",
                       reply->errorString().toStdString(),
                       httpStatus);
        setStatus(Status::Error);
        return;
    }

    QByteArray const newEtag = reply->rawHeader("ETag");
    if (!newEtag.isEmpty()) {
        m_etag = QString::fromUtf8(newEtag);
    }

    QByteArray const body = reply->readAll();
    QJsonParseError parseErr{};
    QJsonDocument const doc = QJsonDocument::fromJson(body, &parseErr);
    if (parseErr.error != QJsonParseError::NoError || !doc.isObject()) {
        AJAZZ_LOG_WARN("app-update",
                       "update check JSON parse failed: {}",
                       parseErr.errorString().toStdString());
        setStatus(Status::Error);
        return;
    }

    QJsonObject const obj = doc.object();
    QString const tag = obj.value(QStringLiteral("tag_name")).toString();
    QString const notes = obj.value(QStringLiteral("body")).toString();
    QUrl const url{obj.value(QStringLiteral("html_url")).toString()};

    if (tag.isEmpty()) {
        // Empty response shape — treat as error so the user can retry.
        setStatus(Status::Error);
        return;
    }

    // If nightly mode is on, also query the rolling nightly tag and
    // pick whichever is newer. We chain rather than parallel-fetch so
    // the rate-limit budget stays predictable (1 req per check, 2 with
    // nightly).
    if (m_includeNightly) {
        applyRelease(tag, notes, url);
        QNetworkRequest req{QUrl{QString::fromLatin1(kNightlyEndpoint)}};
        req.setRawHeader("Accept", "application/vnd.github+json");
        req.setRawHeader(
            "User-Agent",
            "ajazz-control-center (+https://github.com/Aiacos/ajazz-control-center)");
        auto* const nightlyReply = m_nam->get(req);
        QObject::connect(nightlyReply, &QNetworkReply::finished, this,
                         [this, nightlyReply]() { onNightlyReplyFinished(nightlyReply); });
        return;
    }

    applyRelease(tag, notes, url);
}

void AppUpdateService::onNightlyReplyFinished(QNetworkReply* reply) {
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
        // Nightly tag not present (yet) is non-fatal — the stable result
        // we already applied stands. Don't flip to Error.
        return;
    }
    QByteArray const body = reply->readAll();
    QJsonParseError parseErr{};
    QJsonDocument const doc = QJsonDocument::fromJson(body, &parseErr);
    if (parseErr.error != QJsonParseError::NoError || !doc.isObject()) {
        return;
    }
    QJsonObject const obj = doc.object();
    QString const tag = obj.value(QStringLiteral("tag_name")).toString();
    QString const notes = obj.value(QStringLiteral("body")).toString();
    QUrl const url{obj.value(QStringLiteral("html_url")).toString()};
    if (tag.isEmpty()) {
        return;
    }
    // Surface the nightly only if it's strictly newer than the stable
    // record we just applied.
    if (isNewerThan(tag, m_latestVersion)) {
        applyRelease(tag, notes, url);
    }
}

void AppUpdateService::applyRelease(QString const& tag, QString const& notes, QUrl const& url) {
    if (tag != m_latestVersion) {
        m_latestVersion = tag;
        emit latestVersionChanged(m_latestVersion);
    }
    if (notes != m_latestReleaseNotes) {
        m_latestReleaseNotes = notes;
        emit latestReleaseNotesChanged(m_latestReleaseNotes);
    }
    if (url != m_latestReleaseUrl) {
        m_latestReleaseUrl = url;
        emit latestReleaseUrlChanged(m_latestReleaseUrl);
    }

    // Honour the persisted dismissed tag: if the user clicked "Later" on
    // this exact release in a prior session, stay Idle until a newer
    // release lands.
    QSettings settings;
    QString const dismissedTag =
        settings.value(QString::fromLatin1(kDismissedTagKey)).toString();

    if (isNewerThan(tag, currentVersion())) {
        if (tag == dismissedTag) {
            setStatus(Status::Idle);
        } else {
            setStatus(Status::UpdateAvailable);
        }
    } else {
        setStatus(Status::UpToDate);
    }
}

void AppUpdateService::setStatus(Status newStatus) {
    if (m_status == newStatus) {
        return;
    }
    m_status = newStatus;
    emit statusChanged(m_status);
}

} // namespace ajazz::app
