// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_theme_service.cpp
 * @brief Unit tests for @ref ajazz::app::ThemeService.
 *
 * Covers the user-facing contract:
 *
 *   - `mode()` returns the lowercase string the QML side sees.
 *   - `setMode("light"/"dark"/"auto")` flips the mode, persists it via
 *     QSettings, and emits `modeChanged` exactly once.
 *   - `setMode` to the current mode is a no-op (no signal).
 *   - Invalid strings parse to "auto" (the safe fallback).
 *   - Mode parsing is case-insensitive ("LIGHT" == "light").
 *   - Constructor restores the persisted mode from QSettings on startup.
 *
 * Test mode (set in `qtApp()`) keeps QSettings out of the developer's
 * real config tree. Each test that exercises persistence clears the
 * `Appearance/Mode` key first to start from a known state — Catch2
 * tests share the QSettings backing dir within one process.
 */
#include "branding_service.hpp"
#include "qt_app_fixture.hpp"
#include "theme_service.hpp"

#include <QSettings>
#include <QSignalSpy>
#include <QString>

#include <catch2/catch_test_macros.hpp>

using ajazz::app::BrandingService;
using ajazz::app::ThemeService;
using ajazz::tests::qtApp;

namespace {

/// Wipe the persisted Appearance/Mode value so the test starts from
/// the documented "Auto" default. QSettings sync is implicit — Qt
/// flushes on destruction, but our usage is inside a single process.
void clearThemeSettings() {
    QSettings settings;
    settings.remove(QStringLiteral("Appearance/Mode"));
    settings.sync();
}

} // namespace

TEST_CASE("ThemeService: default mode is 'auto' when QSettings has no value", "[theme]") {
    qtApp();
    clearThemeSettings();

    BrandingService branding(nullptr);
    ThemeService theme(&branding);
    REQUIRE(theme.mode() == QStringLiteral("auto"));
}

TEST_CASE("ThemeService: setMode('light') flips mode and emits modeChanged", "[theme]") {
    qtApp();
    clearThemeSettings();

    BrandingService branding(nullptr);
    ThemeService theme(&branding);
    QSignalSpy spy(&theme, &ThemeService::modeChanged);
    REQUIRE(spy.isValid());

    theme.setMode(QStringLiteral("light"));
    REQUIRE(theme.mode() == QStringLiteral("light"));
    REQUIRE(spy.count() == 1);
}

TEST_CASE("ThemeService: setMode('dark') flips mode and emits modeChanged", "[theme]") {
    qtApp();
    clearThemeSettings();

    BrandingService branding(nullptr);
    ThemeService theme(&branding);
    QSignalSpy spy(&theme, &ThemeService::modeChanged);

    theme.setMode(QStringLiteral("dark"));
    REQUIRE(theme.mode() == QStringLiteral("dark"));
    REQUIRE(spy.count() == 1);
}

TEST_CASE("ThemeService: setMode to the current mode is a no-op", "[theme]") {
    qtApp();
    clearThemeSettings();

    BrandingService branding(nullptr);
    ThemeService theme(&branding);

    // Default is "auto"; setMode("auto") must not emit.
    QSignalSpy spy(&theme, &ThemeService::modeChanged);
    theme.setMode(QStringLiteral("auto"));
    REQUIRE(spy.count() == 0);

    // Flip to light, then setting light again must not emit.
    theme.setMode(QStringLiteral("light"));
    REQUIRE(spy.count() == 1);
    theme.setMode(QStringLiteral("light"));
    REQUIRE(spy.count() == 1); // unchanged
}

TEST_CASE("ThemeService: invalid strings fall back to 'auto'", "[theme]") {
    qtApp();
    clearThemeSettings();

    BrandingService branding(nullptr);
    ThemeService theme(&branding);

    // Move off the default first so we can detect the fallback.
    theme.setMode(QStringLiteral("light"));
    REQUIRE(theme.mode() == QStringLiteral("light"));

    // Anything that's not a known mode resolves to Auto. Empty string,
    // gibberish, and look-alike spellings all collapse to the same
    // safe default — protects the QML side from typos.
    theme.setMode(QStringLiteral("system"));
    REQUIRE(theme.mode() == QStringLiteral("auto"));

    theme.setMode(QStringLiteral("light"));
    theme.setMode(QStringLiteral(""));
    REQUIRE(theme.mode() == QStringLiteral("auto"));
}

TEST_CASE("ThemeService: mode parsing is case-insensitive", "[theme]") {
    qtApp();
    clearThemeSettings();

    BrandingService branding(nullptr);
    ThemeService theme(&branding);

    theme.setMode(QStringLiteral("LIGHT"));
    REQUIRE(theme.mode() == QStringLiteral("light"));
    theme.setMode(QStringLiteral("Dark"));
    REQUIRE(theme.mode() == QStringLiteral("dark"));
    theme.setMode(QStringLiteral("AUTO"));
    REQUIRE(theme.mode() == QStringLiteral("auto"));
}

TEST_CASE("ThemeService: setMode persists to QSettings and the next instance restores it",
          "[theme][persistence]") {
    qtApp();
    clearThemeSettings();

    BrandingService branding(nullptr);
    {
        ThemeService theme(&branding);
        theme.setMode(QStringLiteral("light"));
        REQUIRE(theme.mode() == QStringLiteral("light"));
    } // theme destroyed; QSettings has the value persisted

    // A fresh instance must read back the persisted mode without us
    // having to call setMode again — this is the "remembers your
    // choice across launches" promise the QML Settings page makes.
    ThemeService restored(&branding);
    REQUIRE(restored.mode() == QStringLiteral("light"));
}
