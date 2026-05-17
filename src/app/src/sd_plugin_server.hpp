// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file sd_plugin_server.hpp
 * @brief Elgato Stream Deck v6-compatible WebSocket plugin server (P3.16 MVP).
 *
 * Implements the protocol surface vendor Stream Dock SDLibrary1.dll exposes
 * via `SDPluginServer::startListen()` — see
 * docs/protocols/streamdeck/akp_plugin_sdk.md. The Elgato Stream Deck v6
 * protocol is implemented verbatim (13 standard events + 13 standard
 * actions); the 26 AJAZZ extensions (including the `setBG` per-key
 * background extension) land in follow-up commits.
 *
 * **Security delta from vendor**: vendor binds to `QHostAddress::Any`
 * (0.0.0.0 — any local interface, security regression). We bind to
 * `QHostAddress::LocalHost` (127.0.0.1) exclusively — the LOOPBACK-ONLY
 * invariant is asserted by unit tests and pinned by the
 * `bindLoopbackOnly()` API. There is no opt-in to broaden the bind
 * address; future remote-control scenarios must use a separate transport.
 *
 * **Authentication**: vendor uses a `passHello`/`salt`/`challenge`
 * handshake on plugin spawn — see roadmap §3.16 + akp_plugin_sdk.md §6.
 * MVP scope here ships only the standard Elgato `registerPlugin`
 * handshake; the AJAZZ auth challenge lands once the plugin-process
 * spawn surface is implemented (which is deferred — see "Lifecycle"
 * below).
 *
 * **Lifecycle** (MVP scope):
 *   1. App creates one `SdPluginServer`, calls `start()` with port 0 (auto-assigned).
 *   2. Server creates `QWebSocketServer`, binds loopback, accepts connections.
 *   3. Each connecting `QWebSocket` runs through the JSON message dispatch.
 *   4. Server emits `pluginRegistered`/`pluginDisconnected`/`actionReceived`
 *      signals — the app layer wires these to the device backends.
 *
 * **NOT YET IMPLEMENTED** (defer to follow-up commits):
 *   - Spawning plugin processes (QProcess child management for Node.js)
 *   - 26 AJAZZ extensions (`setBG`, `screenColorSent`, etc.)
 *   - passHello/salt/challenge auth handshake
 *   - Per-plugin Property Inspector WebView integration
 *   - Persistence (settings cache + global settings)
 *   - Plugin store catalogue parsing (P3.17 carry-over)
 */
#pragma once

#include <QHostAddress>
#include <QJsonObject>
#include <QObject>
#include <QString>

#include <cstdint>
#include <memory>

QT_BEGIN_NAMESPACE
class QWebSocket;
class QWebSocketServer;
QT_END_NAMESPACE

namespace ajazz::app {

/**
 * @brief Elgato Stream Deck v6-compatible WebSocket server.
 *
 * Loopback-only by design; the bind address is hard-coded to
 * `QHostAddress::LocalHost` and the `bindAddress()` accessor returns it
 * solely for inspection. Tests assert the invariant.
 */
class SdPluginServer : public QObject {
    Q_OBJECT

public:
    explicit SdPluginServer(QObject* parent = nullptr);
    ~SdPluginServer() override;

    /// Start listening on a loopback-bound TCP port.
    ///
    /// @param port  Preferred port; pass `0` to let the OS pick a free one.
    ///              The actual port is queryable via @ref serverPort() once
    ///              `started()` fires.
    /// @return      `true` on successful listen; `false` if bind failed
    ///              (port already in use, permission denied, etc.) — the
    ///              app layer should react by logging and disabling plugin
    ///              functionality rather than escalating to user-facing UI.
    bool start(std::uint16_t port = 0);

    /// Stop listening and close all active plugin connections.
    void stop();

    /// @return `true` if the server is currently listening.
    [[nodiscard]] bool isListening() const noexcept;

    /// @return Actual TCP port the server is bound to, or 0 if not listening.
    [[nodiscard]] std::uint16_t serverPort() const noexcept;

    /// @return Bind address — always `QHostAddress::LocalHost` per the
    ///         loopback-only security invariant.
    [[nodiscard]] QHostAddress bindAddress() const noexcept;

    /// @return Number of currently-connected plugins (alive WebSocket clients).
    [[nodiscard]] int connectedPluginCount() const noexcept;

signals:
    /// Server started successfully and is now accepting plugin connections.
    void started(std::uint16_t port);

    /// Server stopped (cleanly via stop() or via underlying socket error).
    void stopped();

    /// A plugin completed the `registerPlugin` handshake with the supplied UUID.
    /// The app layer wires this UUID to the plugin's metadata + device targets.
    void pluginRegistered(QString const& pluginUuid);

    /// A previously-registered plugin disconnected.
    void pluginDisconnected(QString const& pluginUuid);

    /// A plugin sent an `action`-class message (setTitle / setImage / etc.).
    /// The app layer routes the action to the appropriate device backend.
    /// @param pluginUuid Sender UUID (matches earlier pluginRegistered emission).
    /// @param action     Action JSON object (verbatim from the WebSocket frame).
    void actionReceived(QString const& pluginUuid, QJsonObject const& action);

    /// A plugin sent a raw event we don't yet handle. Surface for debugging
    /// / extensibility before the dispatch table grows to cover it.
    void unhandledEventReceived(QString const& pluginUuid, QString const& eventName);

private slots:
    void onNewConnection();
    void onClientDisconnected();
    void onClientTextMessage(QString const& message);

private:
    /// Dispatch a single parsed JSON object received from @p client.
    void dispatchClientMessage(QWebSocket* client, QJsonObject const& msg);

    /// Look up the plugin UUID associated with a connected WebSocket, or
    /// return an empty string if the client hasn't registered yet.
    [[nodiscard]] QString uuidForClient(QWebSocket* client) const;

    std::unique_ptr<QWebSocketServer> m_server;
    // Plugin-UUID → connection map. Multiple plugins may register over the
    // same server lifetime; one WebSocket per plugin.
    struct PluginConnection {
        QString uuid;
        QWebSocket* socket{nullptr};
    };
    std::vector<PluginConnection> m_connections;
};

} // namespace ajazz::app
