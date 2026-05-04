// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_branding_service.cpp
 * @brief Unit tests for @ref ajazz::app::BrandingService.
 *
 * Pins the contract that the QML `branding` context property relies on:
 *
 *   - Default-constructed service exposes the canonical AJAZZ palette
 *     (the values baked in via `loadEmbeddedDefaults`).
 *   - `loadThemeFile` applies a valid override file in full and emits
 *     `themeChanged` exactly once.
 *   - Malformed / missing / non-object JSON is rejected without
 *     mutating any color, and the method returns `false`.
 *   - Partial themes leave unspecified fields at their previous value
 *     (the JSON apply is field-by-field, not atomic — a theme that
 *     ships only `accent` keeps the previous `bgBase`).
 *
 * `QStandardPaths::setTestModeEnabled(true)` (set in `qtApp()`) keeps
 * QSettings out of the developer's real config tree; the constructor
 * looks up `Branding/ThemeOverride` and would otherwise pick up
 * whatever the developer has in `~/.config/Aiacos/...`.
 */
#include "branding_service.hpp"
#include "qt_app_fixture.hpp"

#include <QColor>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTextStream>

#include <catch2/catch_test_macros.hpp>

using ajazz::app::BrandingService;
using ajazz::tests::qtApp;

namespace {

/// Write @p contents to a fresh `theme.json` inside @p dir and return
/// the absolute path. Captures the QFile open error inside a REQUIRE
/// so test failure points at the line that failed to write.
QString writeThemeJson(QTemporaryDir const& dir, QByteArray const& contents) {
    QString const path = dir.filePath(QStringLiteral("theme.json"));
    QFile out(path);
    REQUIRE(out.open(QIODevice::WriteOnly | QIODevice::Truncate));
    out.write(contents);
    out.close();
    return path;
}

} // namespace

TEST_CASE("BrandingService: parent=nullptr construction exposes the canonical AJAZZ palette",
          "[branding]") {
    qtApp();
    BrandingService svc(nullptr);
    // The hardcoded fallback palette (loadEmbeddedDefaults) — these are
    // the AJAZZ dark theme baked into the binary. The embedded
    // `theme.json` resource is NOT linked into the test binary, so
    // these are the values that survive without an override file.
    REQUIRE(svc.accent() == QColor("#41CD52"));
    REQUIRE(svc.accent2() == QColor("#0A82FA"));
    REQUIRE(svc.bgBase() == QColor("#14141a"));
    REQUIRE(svc.bgSidebar() == QColor("#1e1e23"));
    REQUIRE(svc.bgRowHover() == QColor("#2c2c34"));
    REQUIRE(svc.fgPrimary() == QColor("#f0f0f0"));
    // fgMuted's hardcoded fallback is #888888 (the embedded theme.json
    // would override to #aaaaaa, but we don't load that resource here).
    REQUIRE(svc.fgMuted() == QColor("#888888"));
}

TEST_CASE("BrandingService: loadThemeFile applies a valid override and emits themeChanged",
          "[branding][theme-load]") {
    qtApp();
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    auto const path = writeThemeJson(tmp, R"({
      "accent":     "#2e7d32",
      "accent2":    "#1565c0",
      "bgBase":     "#f4f4f8",
      "bgSidebar":  "#ebebef",
      "bgRowHover": "#dedee5",
      "fgPrimary":  "#14141a",
      "fgMuted":    "#4a4a55"
    })");

    BrandingService svc(nullptr);
    QSignalSpy spy(&svc, &BrandingService::themeChanged);
    REQUIRE(spy.isValid());

    REQUIRE(svc.loadThemeFile(path));
    // All seven branding-contract colors must have flipped.
    REQUIRE(svc.accent() == QColor("#2e7d32"));
    REQUIRE(svc.accent2() == QColor("#1565c0"));
    REQUIRE(svc.bgBase() == QColor("#f4f4f8"));
    REQUIRE(svc.bgSidebar() == QColor("#ebebef"));
    REQUIRE(svc.bgRowHover() == QColor("#dedee5"));
    REQUIRE(svc.fgPrimary() == QColor("#14141a"));
    REQUIRE(svc.fgMuted() == QColor("#4a4a55"));

    // themeChanged must fire exactly once on success — the QML side
    // uses this to invalidate every dependent binding (e.g.
    // Theme.tile / tileHover / borderSubtle which derive from
    // bgSidebar + fgPrimary).
    REQUIRE(spy.count() == 1);
}

TEST_CASE("BrandingService: loadThemeFile rejects a missing file without mutation",
          "[branding][theme-load]") {
    qtApp();
    BrandingService svc(nullptr);
    auto const accentBefore = svc.accent();
    auto const bgBefore = svc.bgBase();

    QSignalSpy spy(&svc, &BrandingService::themeChanged);
    REQUIRE_FALSE(svc.loadThemeFile(QStringLiteral("/nonexistent/path/to/theme.json")));

    REQUIRE(svc.accent() == accentBefore);
    REQUIRE(svc.bgBase() == bgBefore);
    REQUIRE(spy.count() == 0);
}

TEST_CASE("BrandingService: loadThemeFile rejects malformed JSON without mutation",
          "[branding][theme-load]") {
    qtApp();
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    auto const path = writeThemeJson(tmp, QByteArray("{ this is not valid json"));

    BrandingService svc(nullptr);
    auto const accentBefore = svc.accent();

    QSignalSpy spy(&svc, &BrandingService::themeChanged);
    REQUIRE_FALSE(svc.loadThemeFile(path));
    REQUIRE(svc.accent() == accentBefore);
    REQUIRE(spy.count() == 0);
}

TEST_CASE("BrandingService: loadThemeFile rejects non-object JSON (array / scalar)",
          "[branding][theme-load]") {
    qtApp();
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    auto const arrPath = writeThemeJson(tmp, QByteArray("[1, 2, 3]"));

    BrandingService svc(nullptr);
    auto const accentBefore = svc.accent();
    QSignalSpy spy(&svc, &BrandingService::themeChanged);
    REQUIRE_FALSE(svc.loadThemeFile(arrPath));
    REQUIRE(svc.accent() == accentBefore);
    REQUIRE(spy.count() == 0);
}

TEST_CASE("BrandingService: loadThemeFile preserves unspecified fields", "[branding][theme-load]") {
    qtApp();
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    // A theme that only touches `accent` — the apply must leave bgBase
    // / bgSidebar / fgPrimary / etc. at their pre-load values.
    auto const path = writeThemeJson(tmp, R"({"accent": "#ff0000"})");

    BrandingService svc(nullptr);
    auto const bgBefore = svc.bgBase();
    auto const sidebarBefore = svc.bgSidebar();
    auto const fgBefore = svc.fgPrimary();

    REQUIRE(svc.loadThemeFile(path));
    REQUIRE(svc.accent() == QColor("#ff0000"));
    REQUIRE(svc.bgBase() == bgBefore);
    REQUIRE(svc.bgSidebar() == sidebarBefore);
    REQUIRE(svc.fgPrimary() == fgBefore);
}

TEST_CASE("BrandingService: invalid color string is silently ignored", "[branding][theme-load]") {
    qtApp();
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    // `accent` is a non-hex / non-named string — readColor must reject
    // it without mutating the field. Other valid keys still apply.
    auto const path = writeThemeJson(tmp, R"({
      "accent":  "not-a-color",
      "bgBase":  "#000000"
    })");

    BrandingService svc(nullptr);
    auto const accentBefore = svc.accent();

    REQUIRE(svc.loadThemeFile(path));
    REQUIRE(svc.accent() == accentBefore);      // rejected
    REQUIRE(svc.bgBase() == QColor("#000000")); // applied
}
