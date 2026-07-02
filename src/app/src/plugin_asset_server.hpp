// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>

class QTcpServer;

namespace ajazz::app {

/**
 * @class PluginAssetServer
 * @brief Tiny localhost HTTP/1.1 server that serves installed-plugin asset files
 *        (icons, PI resources) to the embedded OpenDeck SPA.
 *
 * The OpenDeck SPA constructs every non-`data:` icon URL as
 * `http://localhost:<portBase+2>/<path>` (see ports.ts `getWebserverUrl`) — its
 * design assumes a local asset webserver. In the embedded host there was none,
 * so plugin icons 404'd and rendered blank across the action list, the device
 * canvas, and the plugin-store tiles alike. This server fills exactly that role:
 * it listens on 127.0.0.1:<port> and serves `GET /__pluginasset__/<dir>/<rel>`
 * from `userPluginsDir()/<dir>/<rel>`, with a canonical-path containment guard
 * (it never serves anything outside the plugins directory). Read-only, GET-only,
 * loopback-only.
 *
 * @note Not thread-safe — lives on and is driven by the Qt GUI thread event loop.
 */
class PluginAssetServer : public QObject {
    Q_OBJECT

public:
    explicit PluginAssetServer(QObject* parent = nullptr);
    ~PluginAssetServer() override;

    /**
     * @brief Start listening on 127.0.0.1:@p port.
     * @return true if the socket bound; false (logged) otherwise — a bind
     *         failure leaves icons blank but never crashes the app.
     */
    bool start(quint16 port);

    /// The port passed to @ref start (0 until started). Matches the SPA's
    /// `getWebserverUrl` base = get_port_base + 2.
    [[nodiscard]] quint16 port() const { return m_port; }

private:
    void onNewConnection();

    QTcpServer* m_server = nullptr;
    quint16 m_port = 0;
};

} // namespace ajazz::app
