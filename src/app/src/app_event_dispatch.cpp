// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file app_event_dispatch.cpp
 * @brief Host-level outbound plugin-event fan-out helpers (Phase 34-04).
 */
#include "app_event_dispatch.hpp"

#include "sd_plugin_server.hpp"

#include <QJsonObject>

namespace ajazz::app {

QString boundApplicationToken(QString const& appId) {
    constexpr int kMaxAppChars = 2048; // mirrors the logMessage cap (application.cpp)
    return appId.length() > kMaxAppChars
               ? appId.left(kMaxAppChars) + QStringLiteral("...(truncated)")
               : appId;
}

int dispatchSystemWakeTo(SdPluginServer* server, QSet<QString> const& registered) {
    if (server == nullptr) {
        return 0;
    }
    int attempted = 0;
    for (QString const& uuid : registered) {
        // sendEvent re-resolves the live socket each call (Pitfall 4 / T-17-UAF):
        // never cache the QWebSocket*. systemDidWakeUp carries no payload.
        server->sendEvent(uuid, QStringLiteral("systemDidWakeUp"), {});
        ++attempted;
    }
    return attempted;
}

namespace {

int dispatchAppEvent(SdPluginServer* server,
                     QSet<QString> const& registered,
                     QString const& eventName,
                     QString const& appId,
                     PluginAppMonitorFilter const& filter) {
    if (server == nullptr || appId.isEmpty()) {
        return 0;
    }
    QString const bounded = boundApplicationToken(appId);
    int attempted = 0;
    for (QString const& uuid : registered) {
        // WR-02: when a filter is supplied, deliver only to plugins whose
        // ApplicationsToMonitor list covers this app. An unset filter preserves
        // the legacy all-registered fan-out.
        if (filter && !filter(uuid, appId)) {
            continue;
        }
        server->sendEvent(uuid, eventName, QJsonObject{{QStringLiteral("application"), bounded}});
        ++attempted;
    }
    return attempted;
}

} // namespace

int dispatchApplicationLaunchTo(SdPluginServer* server,
                                QSet<QString> const& registered,
                                QString const& appId,
                                PluginAppMonitorFilter const& filter) {
    return dispatchAppEvent(
        server, registered, QStringLiteral("applicationDidLaunch"), appId, filter);
}

int dispatchApplicationTerminateTo(SdPluginServer* server,
                                   QSet<QString> const& registered,
                                   QString const& appId,
                                   PluginAppMonitorFilter const& filter) {
    return dispatchAppEvent(
        server, registered, QStringLiteral("applicationDidTerminate"), appId, filter);
}

} // namespace ajazz::app
