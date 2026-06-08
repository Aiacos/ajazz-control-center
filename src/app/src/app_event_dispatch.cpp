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
                     QString const& appId) {
    if (server == nullptr || appId.isEmpty()) {
        return 0;
    }
    QString const bounded = boundApplicationToken(appId);
    int attempted = 0;
    for (QString const& uuid : registered) {
        server->sendEvent(uuid, eventName, QJsonObject{{QStringLiteral("application"), bounded}});
        ++attempted;
    }
    return attempted;
}

} // namespace

int dispatchApplicationLaunchTo(SdPluginServer* server,
                                QSet<QString> const& registered,
                                QString const& appId) {
    return dispatchAppEvent(server, registered, QStringLiteral("applicationDidLaunch"), appId);
}

int dispatchApplicationTerminateTo(SdPluginServer* server,
                                   QSet<QString> const& registered,
                                   QString const& appId) {
    return dispatchAppEvent(server, registered, QStringLiteral("applicationDidTerminate"), appId);
}

} // namespace ajazz::app
