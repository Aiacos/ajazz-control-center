// SPDX-License-Identifier: GPL-3.0-or-later
//
// pi_bootstrap.hpp — build the modern Property Inspector WebSocket bootstrap (T024).
//
// A modern Elgato Property Inspector defines `connectElgatoStreamDeckSocket`
// (the Elgato boilerplate) and then waits for the host to CALL it — exactly like
// the HTML-plugin self-bootstrap in plugin_manager.cpp. A WS-only PI that does
// `new WebSocket(...)` never registers until this call runs. PropertyInspectorController
// builds the JS in loadInspector and PIWebView runs it once the document loads.
//
// The function is pure (no Qt object state) so the construction is unit-testable
// in isolation without booting WebEngine.
#pragma once

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

#include <cstdint>

namespace ajazz::app {

/// Build the modern-PI bootstrap JS, or an empty string when @p wsPort is 0 or
/// @p contextUuid is empty (no WS port / no inspector → the legacy $SD bridge
/// carries the PI alone).
///
/// The result is a self-invoking function that calls
/// `connectElgatoStreamDeckSocket(port, context, "registerPropertyInspector",
/// JSON.stringify(info), JSON.stringify(actionInfo))`. inInfo / inActionInfo are
/// JSON strings the PI JSON.parses (mirrors plugin_manager.cpp:606).
[[nodiscard]] inline QString buildModernPiBootstrapJs(std::uint16_t wsPort,
                                                      QString const& pluginUuid,
                                                      QString const& actionUuid,
                                                      QString const& contextUuid) {
    if (wsPort == 0 || contextUuid.isEmpty()) {
        return {};
    }
    // deviceId is the leading component of the wire context
    // (deviceId#page#controller#row#col).
    QString const deviceId = contextUuid.section(QLatin1Char('#'), 0, 0);
    QJsonObject const info{
        {QStringLiteral("application"),
         QJsonObject{{QStringLiteral("platform"), QStringLiteral("linux")},
                     {QStringLiteral("version"), QCoreApplication::applicationVersion()},
                     {QStringLiteral("language"), QStringLiteral("en")}}},
        {QStringLiteral("plugin"), QJsonObject{{QStringLiteral("uuid"), pluginUuid}}},
        {QStringLiteral("devicePixelRatio"), 1},
    };
    QJsonObject const actionInfo{
        {QStringLiteral("action"), actionUuid},
        {QStringLiteral("context"), contextUuid},
        {QStringLiteral("device"), deviceId},
        {QStringLiteral("payload"), QJsonObject{{QStringLiteral("settings"), QJsonObject{}}}},
    };
    auto const compact = [](QJsonObject const& o) {
        return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
    };
    return QStringLiteral("(function(){var f=window.connectElgatoStreamDeckSocket;"
                          "if(typeof f==='function'){f(%1,'%2','registerPropertyInspector',"
                          "JSON.stringify(%3),JSON.stringify(%4));}})();")
        .arg(QString::number(wsPort), contextUuid, compact(info), compact(actionInfo));
}

} // namespace ajazz::app
