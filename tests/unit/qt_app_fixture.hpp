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
#include <QStandardPaths>
#include <QString>

#include <array>

namespace ajazz::tests {

/// Boot (or reuse) the suite-wide QCoreApplication. Safe to call from
/// any TU; the first call wins, the rest reuse the singleton.
///
/// Also enables `QStandardPaths::setTestModeEnabled(true)` so QSettings
/// and friends route to an isolated per-test directory tree instead of
/// the user's real `~/.config/Aiacos/...` — the BrandingService and
/// ThemeService tests would otherwise inherit whatever the developer
/// has stored locally (e.g. `Branding/ThemeOverride`) and behave
/// non-deterministically.
inline QCoreApplication& qtApp() {
    if (QCoreApplication::instance() == nullptr) {
        static int argc = 0;
        static std::array<char*, 1> argv{nullptr};
        static QCoreApplication app{argc, argv.data()};
        QStandardPaths::setTestModeEnabled(true);
    }
    QCoreApplication::setApplicationName(QStringLiteral("ajazz-control-center-tests"));
    QCoreApplication::setOrganizationName(QStringLiteral("Aiacos"));
    return *QCoreApplication::instance();
}

} // namespace ajazz::tests
