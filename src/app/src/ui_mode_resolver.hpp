// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file ui_mode_resolver.hpp
 * @brief Resolve which UI implementation to load at startup: the native QML
 *        OpenDeck-style UI, or the embedded OpenDeck Svelte web UI.
 *
 * Resolution order (first hit wins):
 *   1. env `AJAZZ_UI_MODE` (dev / CI / debug-channel override)
 *   2. QSettings key `ui/mode` — the human-editable config file
 *      (`$XDG_CONFIG_HOME/Aiacos/AJAZZ Control Center.conf`, `[ui] mode=...`)
 *   3. default `webui` (the embedded OpenDeck web UI)
 *
 * Matching is case-insensitive and trims whitespace; unknown tokens fall
 * through to the next source. See docs/opendeck-ui/02-architecture.md.
 */
#pragma once

#include <QString>

#include <cstdint>

namespace ajazz::app {

/// The selectable UI front-ends. Both drive the same backend singletons.
enum class UiMode : std::uint8_t {
    Qml,   ///< Native QML OpenDeck-style UI.
    WebUi, ///< OpenDeck Svelte SPA embedded in QtWebEngine + Tauri-compat bridge.
};

/// Pure, side-effect-free resolution from already-read string inputs.
/// @param envValue      value of AJAZZ_UI_MODE (may be empty)
/// @param settingsValue value of the `ui/mode` config key (may be empty)
/// @return the resolved mode; defaults to UiMode::WebUi when neither matches.
UiMode normalizeUiMode(QString const& envValue, QString const& settingsValue);

/// Canonical lowercase token for a mode ("qml" | "webui"). Round-trips with the
/// tokens accepted by normalizeUiMode().
QString uiModeToString(UiMode mode);

/// Read the process env (`AJAZZ_UI_MODE`) and QSettings (`ui/mode`) and resolve.
/// Has side effects (touches env + QSettings); thin wrapper over
/// normalizeUiMode(). QApplication org/app name must already be set so QSettings
/// points at the right config file.
UiMode resolveUiMode();

} // namespace ajazz::app
