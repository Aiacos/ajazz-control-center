// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_ui_mode_resolver.cpp
 * @brief Unit tests for the UI-mode config resolution (Phase 0 of the OpenDeck
 *        UI integration; see docs/opendeck-ui/02-architecture.md).
 *
 * Exercises the PURE resolver normalizeUiMode(env, settings) only — no env or
 * QSettings side effects — so the precedence + parsing rules are pinned without
 * process/global state. TEST_CASE titles are ASCII-only (ctest Win32 codepage).
 */
#include "ui_mode_resolver.hpp"

#include <catch2/catch_test_macros.hpp>

using ajazz::app::normalizeUiMode;
using ajazz::app::UiMode;
using ajazz::app::uiModeToString;

TEST_CASE("normalizeUiMode defaults to webui when nothing matches", "[ui_mode]") {
    REQUIRE(normalizeUiMode(QString{}, QString{}) == UiMode::WebUi);
    REQUIRE(normalizeUiMode(QStringLiteral("garbage"), QStringLiteral("nonsense")) ==
            UiMode::WebUi);
}

TEST_CASE("normalizeUiMode falls back to the config value when env is empty", "[ui_mode]") {
    REQUIRE(normalizeUiMode(QString{}, QStringLiteral("webui")) == UiMode::WebUi);
    REQUIRE(normalizeUiMode(QString{}, QStringLiteral("qml")) == UiMode::Qml);
}

TEST_CASE("normalizeUiMode lets env override the config value", "[ui_mode]") {
    REQUIRE(normalizeUiMode(QStringLiteral("qml"), QStringLiteral("webui")) == UiMode::Qml);
    REQUIRE(normalizeUiMode(QStringLiteral("webui"), QStringLiteral("qml")) == UiMode::WebUi);
}

TEST_CASE("normalizeUiMode is case-insensitive and trims whitespace", "[ui_mode]") {
    REQUIRE(normalizeUiMode(QStringLiteral("  WebUI "), QString{}) == UiMode::WebUi);
    REQUIRE(normalizeUiMode(QStringLiteral("QML"), QString{}) == UiMode::Qml);
    REQUIRE(normalizeUiMode(QStringLiteral("web"), QString{}) == UiMode::WebUi);
}

TEST_CASE("normalizeUiMode ignores an unknown env and uses config", "[ui_mode]") {
    REQUIRE(normalizeUiMode(QStringLiteral("bogus"), QStringLiteral("webui")) == UiMode::WebUi);
}

TEST_CASE("uiModeToString returns canonical tokens", "[ui_mode]") {
    REQUIRE(uiModeToString(UiMode::Qml) == QStringLiteral("qml"));
    REQUIRE(uiModeToString(UiMode::WebUi) == QStringLiteral("webui"));
}
