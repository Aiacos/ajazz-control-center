// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file qt_app_fixture.hpp
 * @brief Shared QCoreApplication bootstrap for unit tests.
 *
 * Several unit-test TUs need a `QCoreApplication` to be running (Qt
 * resource lookup, `QStandardPaths`, signals/slots, the event loop
 * for `QTimer::singleShot`). Each TU used to define its own
 * defensive `qtApp()`; the helpers diverged in subtle ways and
 * Qt enforces a global single-instance contract on
 * `QCoreApplication`, so the second TU's static would attempt to
 * construct a duplicate and fail. This header centralises the
 * pattern: whichever TU calls `qtApp()` first constructs the
 * singleton; subsequent callers reuse it via
 * `QCoreApplication::instance()`.
 *
 * Usage:
 *
 * @code
 * #include "qt_app_fixture.hpp"
 *
 * TEST_CASE("...") {
 *     ajazz::tests::qtApp();
 *     // QSettings, QStandardPaths, QTimer::singleShot all work now.
 * }
 * @endcode
 */
#pragma once

#include <QCoreApplication>
#include <QGuiApplication>
#include <QStandardPaths>
#include <QString>

#include <array>
#include <cstdlib>

#ifdef _WIN32
#include <process.h>
#define AJAZZ_TEST_GETPID() _getpid()
#else
#include <unistd.h>
#define AJAZZ_TEST_GETPID() ::getpid()
#endif

namespace ajazz::tests {

/// Boot (or reuse) the suite-wide QCoreApplication. Safe to call from
/// any TU; the first call wins, the rest reuse the singleton.
///
/// `QStandardPaths::setTestModeEnabled(true)` keeps QSettings out of
/// the developer's real config tree.
///
/// The application name is suffixed with the current PID so that
/// `ctest -j N` (which spawns N parallel binary invocations, one per
/// `TEST_CASE` after `catch_discover_tests`) gives each process its
/// own QSettings backing file. Without this, two ThemeService tests
/// running concurrently would write to the same
/// `Appearance/Mode` key and one would observe the other's value
/// after its own `clearThemeSettings()` — a real flake we observed
/// in the wild after the Toast polish landed.

/// Pin the plugin-catalogue fetchers offline for this test process.
///
/// Constructing a `PluginCatalogModel` calls `reload()`, which — with the
/// online catalogue ON by default (PLUGIN-14) — fires a live
/// `QNetworkAccessManager` POST to the real Stream Dock / OpenDeck
/// catalogue URLs. A unit test never spins the event loop, so that
/// `QNetworkReply` is left in flight and torn down with the fetcher on
/// process exit; on CI (ubuntu/macOS) that teardown SegFaults
/// non-deterministically. (It passed on the dev box only because the
/// reply never reached a crashing state there.) Unit tests must not depend
/// on outbound network, so every fixture caller pins both fetchers to the
/// documented `disabled` override — the same mechanism
/// test_catalog_offline.cpp already uses explicitly. `overwrite = 0` lets a
/// test that genuinely wants a mock URL set the env var itself first.
inline void disableLiveCatalogs() {
#ifdef _WIN32
    // MSVC /W4 /WX rejects std::getenv (C4996); use the Annex K getenv_s to
    // probe presence (buffer == nullptr / size 0 returns the required length,
    // 0 == not set) so a test that set a mock URL first still wins.
    auto setIfUnset = [](char const* name) {
        size_t len = 0;
        getenv_s(&len, nullptr, 0, name);
        if (len == 0) {
            _putenv_s(name, "disabled");
        }
    };
    setIfUnset("ACC_STREAMDOCK_CATALOG_URL");
    setIfUnset("ACC_OPENDECK_CATALOG_URL");
#else
    ::setenv("ACC_STREAMDOCK_CATALOG_URL", "disabled", /*overwrite*/ 0);
    ::setenv("ACC_OPENDECK_CATALOG_URL", "disabled", /*overwrite*/ 0);
#endif
}

inline QCoreApplication& qtApp() {
    disableLiveCatalogs();
    if (QCoreApplication::instance() == nullptr) {
        static int argc = 0;
        static std::array<char*, 1> argv{nullptr};
        static QCoreApplication app{argc, argv.data()};
        QStandardPaths::setTestModeEnabled(true);
    }
    QCoreApplication::setApplicationName(
        QStringLiteral("ajazz-control-center-tests-%1").arg(AJAZZ_TEST_GETPID()));
    QCoreApplication::setOrganizationName(QStringLiteral("Aiacos"));
    return *QCoreApplication::instance();
}

/// Boot (or reuse) a QGuiApplication for tests that need QPainter /
/// QFont / QFontMetrics (the mouse TFT clock+DPI face renderer is the
/// current consumer).
///
/// We force `QT_QPA_PLATFORM=offscreen` before constructing the
/// application so the test process runs without a display — no xvfb
/// dependency, no Wayland/X11 setup required on CI. The instance must
/// be GuiApplication (not CoreApplication) for QFontDatabase to load
/// the platform fonts; on macOS that's why earlier QCoreApplication-only
/// tests SIGABRT'd inside QPainter::drawText.
///
/// Like `qtApp()`, the singleton is constructed on the first call and
/// reused thereafter. The two helpers are mutually exclusive — call
/// only one per binary; subsequent calls to the other reuse whichever
/// landed first via `QCoreApplication::instance()`.
inline QGuiApplication& qtGuiApp() {
    if (QCoreApplication::instance() == nullptr) {
        // Force offscreen platform so QPainter works without a display.
        // Setting before construction is load-bearing: Qt reads
        // QT_QPA_PLATFORM exactly once when the platform plugin loads.
#ifdef _WIN32
        _putenv_s("QT_QPA_PLATFORM", "offscreen");
#else
        ::setenv("QT_QPA_PLATFORM", "offscreen", /*overwrite*/ 1);
#endif
        static int argc = 0;
        static std::array<char*, 1> argv{nullptr};
        static QGuiApplication app{argc, argv.data()};
        QStandardPaths::setTestModeEnabled(true);
    }
    QCoreApplication::setApplicationName(
        QStringLiteral("ajazz-control-center-tests-%1").arg(AJAZZ_TEST_GETPID()));
    QCoreApplication::setOrganizationName(QStringLiteral("Aiacos"));
    // Static_cast is safe: we constructed a QGuiApplication above OR
    // some other TU did. If a TU mixed qtApp() + qtGuiApp() the cast
    // returns nullptr deref on the first QPainter call — diagnose by
    // running ctest with --output-on-failure.
    return *static_cast<QGuiApplication*>(QCoreApplication::instance());
}

} // namespace ajazz::tests

#undef AJAZZ_TEST_GETPID
