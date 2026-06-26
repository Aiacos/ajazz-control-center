// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui_mode_resolver.hpp"

#include <QSettings>
#include <QString>

namespace ajazz::app {

namespace {

/// Try to parse one token into a mode. Returns true on a recognised token.
bool parseToken(QString const& raw, UiMode& out) {
    QString const t = raw.trimmed().toLower();
    if (t == QStringLiteral("qml")) {
        out = UiMode::Qml;
        return true;
    }
    if (t == QStringLiteral("webui") || t == QStringLiteral("web")) {
        out = UiMode::WebUi;
        return true;
    }
    return false;
}

} // namespace

UiMode normalizeUiMode(QString const& envValue, QString const& settingsValue) {
    UiMode mode = UiMode::WebUi;
    if (parseToken(envValue, mode)) {
        return mode;
    }
    if (parseToken(settingsValue, mode)) {
        return mode;
    }
    return UiMode::WebUi; // default: the embedded OpenDeck web UI
}

QString uiModeToString(UiMode mode) {
    return mode == UiMode::WebUi ? QStringLiteral("webui") : QStringLiteral("qml");
}

UiMode resolveUiMode() {
    QString const env = qEnvironmentVariable("AJAZZ_UI_MODE");
    QSettings settings;
    QString const cfg = settings.value(QStringLiteral("ui/mode")).toString();
    return normalizeUiMode(env, cfg);
}

} // namespace ajazz::app
