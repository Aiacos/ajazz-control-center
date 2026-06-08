// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file app_event_dispatch.hpp
 * @brief Host-level outbound plugin-event fan-out helpers (Phase 34-04).
 *
 * APROF-04 (applicationDidLaunch/Terminate) and EVENT-03 (systemDidWakeUp) are
 * host-sourced events that must reach ONLY registered plugins over the loopback
 * WebSocket — never broadcast (V4 access control / T-34-04-02 information
 * disclosure). The application-name payload is length-bounded before send (V5 /
 * T-34-04-05 — mirrors the logMessage 2048 cap).
 *
 * The fan-out is factored here as free functions over a SdPluginServer* + the
 * registered-plugin set so the security-relevant delivery contract is unit-
 * testable against a live server WITHOUT constructing the whole Application.
 * Application::dispatchSystemWake / dispatchApplicationLaunch / Terminate are
 * thin wrappers that pass m_pluginServer + m_pluginBridge->registeredPlugins().
 *
 * COD-031: app-layer Qt/QJson only; never nlohmann. The server pointer is
 * re-resolved per call inside sendEvent (Pitfall 4 / T-17-UAF) — these helpers
 * never cache a QWebSocket*.
 */
#pragma once

#include <QSet>
#include <QString>

namespace ajazz::app {

class SdPluginServer;

/// Length-bound a host-sourced application token before it crosses the WS to a
/// plugin (V5 / T-34-04-05). Returns @p appId unchanged when within the cap;
/// otherwise truncates to 2048 chars + a "...(truncated)" marker.
[[nodiscard]] QString boundApplicationToken(QString const& appId);

/// Send @c systemDidWakeUp (no payload) to each registered plugin via
/// @p server->sendEvent. No-op when @p server is null. Never broadcasts beyond
/// @p registered. Returns the number of plugins the event was attempted for.
int dispatchSystemWakeTo(SdPluginServer* server, QSet<QString> const& registered);

/// Send @c applicationDidLaunch with a length-bounded @c {application:<appId>}
/// payload to each registered plugin only (V4). No-op when @p server is null or
/// @p appId is empty. Returns the number of plugins the event was attempted for.
int dispatchApplicationLaunchTo(SdPluginServer* server,
                                QSet<QString> const& registered,
                                QString const& appId);

/// applicationDidTerminate counterpart of @ref dispatchApplicationLaunchTo.
int dispatchApplicationTerminateTo(SdPluginServer* server,
                                   QSet<QString> const& registered,
                                   QString const& appId);

} // namespace ajazz::app
