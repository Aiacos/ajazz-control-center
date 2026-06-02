// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file sd_plugin_server.hpp
 * @brief Elgato Stream Deck v6-compatible WebSocket plugin server (P3.16 MVP).
 *
 * Implements the protocol surface vendor Stream Dock SDLibrary1.dll exposes
 * via `SDPluginServer::startListen()` — see
 * docs/protocols/streamdeck/akp_plugin_sdk.md. The Elgato Stream Deck v6
 * protocol is implemented verbatim (15 standard routed actions + 26 AJAZZ
 * extension actions = 41 total routed, per spec §4.3). All plugin->host
 * actions route via actionReceived; genuinely-unknown events still surface
 * via unhandledEventReceived for forward-compat tracing.
 *
 * **Security delta from vendor**: vendor binds to `QHostAddress::Any`
 * (0.0.0.0 — any local interface, security regression). We bind to
 * `QHostAddress::LocalHost` (127.0.0.1) exclusively — the LOOPBACK-ONLY
 * invariant is asserted by unit tests and pinned by the
 * `bindLoopbackOnly()` API. There is no opt-in to broaden the bind
 * address; future remote-control scenarios must use a separate transport.
 *
 * **Authentication** (PLUGIN-05, 17-03):
 *   After `registerPlugin`, the host sends `passHello` with a random
 *   per-connection NESTED `authentication:{challenge, salt}` payload (per
 *   CONTEXT.md — spec §4.5 top-level salt is superseded). When a password
 *   is configured, the plugin replies `{event:"authentication", challenge:
 *   sha256(password+salt)}`; the host verifies and closes the socket after
 *   5 bad attempts (T-17-BRUTE). Default (no password) = accepted after
 *   passHello. No TLS — loopback-only by design (accepted constraint).
 *   A test-only `setPasswordForTesting()` setter enables the auth test cases.
 *
 * **Lifecycle** (current scope):
 *   1. App creates one `SdPluginServer`, calls `start()` with port 0 (auto-assigned).
 *   2. Server creates `QWebSocketServer`, binds loopback, accepts connections.
 *   3. Each connecting `QWebSocket` runs through the JSON message dispatch.
 *   4. Server emits `pluginRegistered`/`pluginDisconnected`/`actionReceived`
 *      signals — the app layer wires these to the device backends.
 *
 * **Host-to-plugin event sender**: `sendEvent(uuid, eventName, payload)` writes a
 * compact JSON envelope to the named plugin's live socket (addressed by plugin uuid).
 * This is the seam Phase 19 calls with real device input (an encoder turn becomes
 * `sendEvent(uuid, "dialRotate", {...})`). The lookup re-resolves the live slot on
 * each call — never caches a raw QWebSocket* (T-17-UAF, Pitfall 4).
 *
 * **NOT YET IMPLEMENTED** (defer to follow-up commits):
 *   - Spawning plugin processes (QProcess child management for Node.js)
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

    /// @return Number of plugins that have completed the registerPlugin
    ///         handshake (i.e. have a non-empty UUID). Sockets that connected
    ///         but have not yet identified themselves are not counted here -
    ///         see Elgato v6 protocol semantics.
    [[nodiscard]] int connectedPluginCount() const noexcept;

    /// Send a host→plugin event to the registered plugin identified by
    /// @p targetUuid (the UUID used in the `registerPlugin` handshake).
    ///
    /// Serialises @p eventName and @p payload into a compact JSON envelope
    /// `{"event": eventName, "payload": payload}` (payload key omitted when
    /// empty) and writes it as a WebSocket text frame to the plugin's live
    /// socket.
    ///
    /// **Pitfall 4 / T-17-UAF guard**: the live socket is re-resolved on
    /// every call via `socketForUuid()` — the raw pointer is NEVER cached
    /// between calls. If the plugin disconnects between two calls (e.g.
    /// after an auth rejection), the lookup returns nullptr and
    /// this method returns false without crashing.
    ///
    /// This is the seam Phase 19 calls with real device input — e.g.
    /// an encoder rotation becomes
    /// `sendEvent(uuid, "dialRotate", {ticks, pressed, controller, …})`.
    /// Context-addressing (per action-instance) is deferred to Phase 19/20.
    ///
    /// @param targetUuid  Registered plugin UUID (from pluginRegistered signal).
    /// @param eventName   §4.4 event name, e.g. `"dialRotate"`, `"keyDown"`.
    /// @param payload     Optional event payload; omitted from envelope when empty.
    /// @return true if the frame was written; false if no live socket matches.
    bool
    sendEvent(QString const& targetUuid, QString const& eventName, QJsonObject const& payload = {});

    /// **Full-envelope sender** for Elgato/OpenDeck-shaped events that carry
    /// top-level `action` / `context` / `device` siblings to `event` (e.g.
    /// willAppear, keyDown, dialRotate). The caller passes the COMPLETE event
    /// object (must contain an `"event"` key); it is written verbatim. The
    /// 3-arg overload above only emits `{event, payload}` and is kept for simple
    /// events (deviceDidConnect, exitApp, sendToPlugin relay). Re-resolves the
    /// socket on every call (Pitfall 4); returns false if no live socket matches.
    ///
    /// @param targetUuid  Registered plugin UUID (from pluginRegistered signal).
    /// @param fullEvent   Complete event object including the `"event"` key.
    /// @return true if the frame was written; false if no live socket matches.
    bool sendEvent(QString const& targetUuid, QJsonObject const& fullEvent);

    /// **Test-only**: configure a password for the passHello/challenge auth
    /// handshake (PLUGIN-05 / T-17-BRUTE).
    ///
    /// Enables `PluginAuthTest::rejectsAfter5BadAttempts` — the test calls
    /// this before `start()` to exercise the rejection path. Production
    /// default is empty (no password = connection accepted after passHello).
    ///
    /// @warning Do NOT use in production code.  Call only from unit tests.
    void setPasswordForTesting(QString const& password);

    /// **Debug/simulation seam**: emit `actionReceived` as if a registered
    /// plugin had sent @p action over the WebSocket. Lets the opt-in debug
    /// channel (PluginDebugService::simulatePluginAction) drive the exact
    /// production fan-out — every `actionReceived` consumer (the device bridge's
    /// visual handler AND the host-level openUrl/logMessage handler) runs — so a
    /// plugin→host action path can be verified without a live plugin socket.
    /// Reachable only through the opt-in AJAZZ_DEBUG_CONTROL channel.
    void injectAction(QString const& pluginUuid, QJsonObject const& action);

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

    /// A host->plugin event was just written to a live socket (sendEvent
    /// succeeded). Surfaced so the debug console can record the OUTBOUND half of
    /// the protocol (the inbound half is already captured via actionReceived).
    /// @param targetUuid Recipient plugin UUID.
    /// @param eventName  The event name (willAppear / keyDown / dialRotate / …).
    /// @param payload    The payload object as sent (may be empty).
    void eventSent(QString const& targetUuid, QString const& eventName, QJsonObject const& payload);

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

    /// Look up the live WebSocket for the plugin registered with @p uuid.
    /// Returns nullptr when no matching live (non-null socket) slot exists.
    /// Called on EVERY sendEvent invocation — never cache the result (Pitfall 4).
    [[nodiscard]] QWebSocket* socketForUuid(QString const& uuid) const;

    std::unique_ptr<QWebSocketServer> m_server;
    // Plugin-UUID → connection map. Multiple plugins may register over the
    // same server lifetime; one WebSocket per plugin.
    struct PluginConnection {
        QString uuid;
        QWebSocket* socket{nullptr};
        // Auth state (PLUGIN-05 / 17-03):
        QString salt;              ///< Hex-encoded random per-connection salt.
        int authAttempts{0};       ///< Bad-challenge counter; socket closed at kMaxAuthAttempts.
        bool authenticated{false}; ///< True once the connection has passed auth (or no password).
    };
    std::vector<PluginConnection> m_connections;

    /// Configured password for the passHello/challenge auth gate.
    /// Empty (default) = no-password-accept: passHello is sent and the
    /// connection is immediately treated as authenticated.
    QString m_password;
};

} // namespace ajazz::app
