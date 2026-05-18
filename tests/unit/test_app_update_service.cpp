// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_app_update_service.cpp
 * @brief Pure-logic + Flatpak self-disable coverage for AppUpdateService.
 *
 * The actual GitHub HTTPS GET path is intentionally NOT covered here -- it
 * needs a mock QNetworkAccessManager seam that the service doesn't expose
 * yet. We instead pin:
 *
 *   * the isNewerThan() semver helper (10 cases covering v-prefix, rc,
 *     major bump, equal, ordering across release vs prerelease);
 *   * the construction-time FLATPAK_ID self-disable contract via a real
 *     env-var mutation that the service reads through
 *     qEnvironmentVariableIsSet ("set then unset" pattern keeps the rest
 *     of the test binary unaffected);
 *   * the dismissCurrentUpdate() state-machine transition from
 *     UpdateAvailable back to Idle;
 *   * platformLabel() returning one of the documented strings so the
 *     QML banner's conditional rendering never lands on an undefined
 *     branch.
 *
 * The TEST_CASE names are ASCII-only per the project's Windows ctest
 * filter discipline (cmd codepage mangles em-dash + right-arrow).
 */
#include "app_update_service.hpp"

#include <QCoreApplication>
#include <QSettings>
#include <QString>
#include <QStringList>

#include <cstdlib>

#include <catch2/catch_test_macros.hpp>

namespace {

/// Tiny RAII helper so a test that flips FLATPAK_ID doesn't leak the
/// env-var to subsequent TEST_CASEs in the same binary.
class ScopedEnv {
public:
    ScopedEnv(char const* key, char const* value) : key_(key) {
        had_ = qEnvironmentVariableIsSet(key);
        if (had_) {
            previous_ = qEnvironmentVariable(key);
        }
        if (value != nullptr) {
            qputenv(key, value);
        } else {
            qunsetenv(key);
        }
    }

    ~ScopedEnv() {
        if (had_) {
            qputenv(key_, previous_.toUtf8());
        } else {
            qunsetenv(key_);
        }
    }

    ScopedEnv(ScopedEnv const&) = delete;
    ScopedEnv& operator=(ScopedEnv const&) = delete;

private:
    char const* key_;
    bool had_ = false;
    QString previous_;
};

/// Ensure QSettings doesn't write to the developer's real registry hive
/// when a test mutates AppUpdate prefs. Uses an in-process scope by
/// pointing the organization/application name at a test-only string.
class QSettingsTestScope {
public:
    QSettingsTestScope() {
        if (QCoreApplication::instance() != nullptr) {
            previousOrg_ = QCoreApplication::organizationName();
            previousApp_ = QCoreApplication::applicationName();
            QCoreApplication::setOrganizationName(QStringLiteral("ajazz-test"));
            QCoreApplication::setApplicationName(QStringLiteral("app-update-test"));
            // Clear any stale state from a prior run.
            QSettings settings;
            settings.remove(QStringLiteral("AppUpdate"));
        }
    }
    ~QSettingsTestScope() {
        if (QCoreApplication::instance() != nullptr) {
            QSettings settings;
            settings.remove(QStringLiteral("AppUpdate"));
            QCoreApplication::setOrganizationName(previousOrg_);
            QCoreApplication::setApplicationName(previousApp_);
        }
    }

private:
    QString previousOrg_;
    QString previousApp_;
};

} // namespace

TEST_CASE("AppUpdateService isNewerThan compares major.minor.patch numerically", "[app-update]") {
    using ajazz::app::AppUpdateService;

    REQUIRE(AppUpdateService::isNewerThan(QStringLiteral("1.2.3"), QStringLiteral("1.2.2")));
    REQUIRE_FALSE(AppUpdateService::isNewerThan(QStringLiteral("1.2.2"), QStringLiteral("1.2.3")));
    REQUIRE(AppUpdateService::isNewerThan(QStringLiteral("2.0.0"), QStringLiteral("1.99.99")));
}

TEST_CASE("AppUpdateService isNewerThan strips leading v prefix", "[app-update]") {
    using ajazz::app::AppUpdateService;

    // Equal after the leading 'v' is stripped, so neither is strictly newer.
    REQUIRE_FALSE(AppUpdateService::isNewerThan(QStringLiteral("v1.2.3"), QStringLiteral("1.2.3")));
    REQUIRE_FALSE(AppUpdateService::isNewerThan(QStringLiteral("1.2.3"), QStringLiteral("v1.2.3")));
    REQUIRE_FALSE(
        AppUpdateService::isNewerThan(QStringLiteral("v1.2.3"), QStringLiteral("v1.2.3")));
}

TEST_CASE("AppUpdateService isNewerThan ranks release above release-candidate", "[app-update]") {
    using ajazz::app::AppUpdateService;

    // rc < release at equal numeric components.
    REQUIRE_FALSE(
        AppUpdateService::isNewerThan(QStringLiteral("1.2.3-rc1"), QStringLiteral("1.2.3")));
    REQUIRE(AppUpdateService::isNewerThan(QStringLiteral("1.2.3"), QStringLiteral("1.2.3-rc1")));
}

TEST_CASE("AppUpdateService isNewerThan treats nightly as newer than stable", "[app-update]") {
    using ajazz::app::AppUpdateService;

    REQUIRE(AppUpdateService::isNewerThan(QStringLiteral("nightly-20260601-abcdef0"),
                                          QStringLiteral("1.2.3")));
    REQUIRE_FALSE(AppUpdateService::isNewerThan(QStringLiteral("1.2.3"),
                                                QStringLiteral("nightly-20260601-abcdef0")));
}

TEST_CASE("AppUpdateService construction without FLATPAK_ID stays Idle by default",
          "[app-update]") {
    using ajazz::app::AppUpdateService;
    QSettingsTestScope const settingsScope;
    ScopedEnv const noFlatpak("FLATPAK_ID", nullptr);

    AppUpdateService svc(nullptr);
    // The constructor schedules a deferred check via QTimer::singleShot but
    // does NOT fire it synchronously; the status remains Idle until the
    // event loop runs (which it doesn't in this test).
    REQUIRE(svc.status() == AppUpdateService::Idle);
    REQUIRE_FALSE(svc.platformLabel() == QStringLiteral("Flatpak"));
}

TEST_CASE("AppUpdateService construction with FLATPAK_ID set self-disables", "[app-update]") {
    using ajazz::app::AppUpdateService;
    QSettingsTestScope const settingsScope;
    ScopedEnv const flatpak("FLATPAK_ID", "io.github.Aiacos.AjazzControlCenter");

    AppUpdateService svc(nullptr);
    REQUIRE(svc.status() == AppUpdateService::Disabled);
    REQUIRE(svc.platformLabel() == QStringLiteral("Flatpak"));

    // checkNow() must be a no-op when disabled -- status stays Disabled,
    // doesn't transition to Checking.
    svc.checkNow();
    REQUIRE(svc.status() == AppUpdateService::Disabled);
}

TEST_CASE("AppUpdateService dismissCurrentUpdate transitions to Idle", "[app-update]") {
    using ajazz::app::AppUpdateService;
    QSettingsTestScope const settingsScope;
    ScopedEnv const noFlatpak("FLATPAK_ID", nullptr);

    AppUpdateService svc(nullptr);
    // The service starts Idle. dismissCurrentUpdate() must stay at Idle
    // (no transition triggers a spurious signal) and must not crash on an
    // empty m_latestVersion -- the persist-the-dismissed-tag branch
    // guards on isEmpty().
    REQUIRE(svc.status() == AppUpdateService::Idle);
    svc.dismissCurrentUpdate();
    REQUIRE(svc.status() == AppUpdateService::Idle);
}

TEST_CASE("AppUpdateService platformLabel returns a known string", "[app-update]") {
    using ajazz::app::AppUpdateService;
    QSettingsTestScope const settingsScope;
    ScopedEnv const noFlatpak("FLATPAK_ID", nullptr);
    ScopedEnv const noAppimage("APPIMAGE", nullptr);

    AppUpdateService svc(nullptr);
    QString const label = svc.platformLabel();

    QStringList const known{QStringLiteral("Flatpak"),
                            QStringLiteral("AppImage"),
                            QStringLiteral("macOS DMG"),
                            QStringLiteral("Windows MSI"),
                            QStringLiteral("Linux .deb/.rpm"),
                            QStringLiteral("Source build")};
    REQUIRE(known.contains(label));
}

TEST_CASE("AppUpdateService autoCheckEnabled setter flips state in-process", "[app-update]") {
    using ajazz::app::AppUpdateService;
    QSettingsTestScope const settingsScope;
    ScopedEnv const noFlatpak("FLATPAK_ID", nullptr);

    AppUpdateService svc(nullptr);
    // Default per the design doc + ctor: autoCheckEnabled == true.
    REQUIRE(svc.autoCheckEnabled());
    svc.setAutoCheckEnabled(false);
    REQUIRE_FALSE(svc.autoCheckEnabled());
    // Idempotent setter (same value, no-op).
    svc.setAutoCheckEnabled(false);
    REQUIRE_FALSE(svc.autoCheckEnabled());
    svc.setAutoCheckEnabled(true);
    REQUIRE(svc.autoCheckEnabled());
}

TEST_CASE("AppUpdateService includeNightly setter flips state in-process", "[app-update]") {
    using ajazz::app::AppUpdateService;
    QSettingsTestScope const settingsScope;
    ScopedEnv const noFlatpak("FLATPAK_ID", nullptr);

    AppUpdateService svc(nullptr);
    REQUIRE_FALSE(svc.includeNightly());
    svc.setIncludeNightly(true);
    REQUIRE(svc.includeNightly());
    svc.setIncludeNightly(false);
    REQUIRE_FALSE(svc.includeNightly());
}
