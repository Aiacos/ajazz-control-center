// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file debug_control_server.hpp
 * @brief Opt-in, loopback-only JSON-RPC control channel over a Unix domain
 *        socket.
 *
 * This is the transport the out-of-process debug tooling (the
 * `scripts/ajazz-debug` client) uses to drive and observe a *running*
 * instance: read the unified log tail, query state, command subsystems,
 * and (Phase 3) introspect/drive the QML UI.
 *
 * Security posture — this channel can invoke arbitrary actions, so it is:
 *   - **off by default**; enabled only when @ref enabledFromEnv (the
 *     @c AJAZZ_DEBUG_CONTROL environment variable) is set;
 *   - bound to a **Unix domain socket** under @c XDG_RUNTIME_DIR, never a
 *     TCP port — no network exposure;
 *   - created with @c QLocalServer::UserAccessOption so the socket is
 *     owner-only (mode 0600).
 * This mirrors the trust model the plugin WebSocket server already relies on
 * (loopback + per-user isolation).
 *
 * Wire protocol: newline-delimited JSON. One request object per line:
 *   `{"id": <any>, "method": "<name>", "params": {<object>}}`
 * One response object per line:
 *   `{"id": <echoed>, "ok": true,  "result": {<object>}}`
 *   `{"id": <echoed>, "ok": false, "error": "<message>"}`
 */
#pragma once

#include <QHash>
#include <QObject>
#include <QString>

#include <functional>

class QLocalServer;
class QLocalSocket;
class QByteArray;
class QJsonObject;

namespace ajazz::app {

class DebugControlServer : public QObject {
    Q_OBJECT
public:
    /**
     * @brief A method handler: receives the request's @c params object and
     *        returns a result object. To signal failure, set @p error to a
     *        non-empty message (the result is then ignored and an
     *        `{ok:false, error}` reply is sent).
     */
    using Handler = std::function<QJsonObject(QJsonObject const& params, QString& error)>;

    explicit DebugControlServer(QObject* parent = nullptr);
    ~DebugControlServer() override;

    DebugControlServer(DebugControlServer const&) = delete;
    DebugControlServer& operator=(DebugControlServer const&) = delete;
    DebugControlServer(DebugControlServer&&) = delete;
    DebugControlServer& operator=(DebugControlServer&&) = delete;

    /// Register (or replace) the handler for @p method. Names are matched
    /// verbatim; convention is dotted namespaces ("log.tail", "device.list").
    void registerMethod(QString const& method, Handler handler);

    /// Sorted list of currently registered method names (also served by the
    /// built-in "methods" RPC so a client can discover the surface).
    [[nodiscard]] QStringList methodNames() const;

    /**
     * @brief Begin listening on @p socketPath (an absolute filesystem path
     *        for the Unix domain socket). A stale socket file left by a
     *        crashed run is removed first. The socket is owner-only.
     * @return true if listening; false (and logs) on failure.
     */
    bool start(QString const& socketPath);

    void stop();
    [[nodiscard]] bool isListening() const;
    [[nodiscard]] QString socketPath() const { return m_socketPath; }

    /**
     * @brief Process one request line and produce one response line.
     *
     * Exposed (and side-effect-free beyond invoking the handler) so the
     * framing + dispatch contract can be unit-tested without a live socket.
     * Malformed JSON / missing method yields a well-formed error response.
     */
    [[nodiscard]] QByteArray processRequestLine(QByteArray const& line);

    /// True when the @c AJAZZ_DEBUG_CONTROL environment variable is set
    /// (to any non-empty value). The single gate for the whole channel.
    [[nodiscard]] static bool enabledFromEnv();

    /// Default socket path: `$XDG_RUNTIME_DIR/ajazz-control-center-debug.sock`
    /// (falling back to the temp dir). If @c AJAZZ_DEBUG_CONTROL holds an
    /// absolute path (contains '/'), that path is used verbatim.
    [[nodiscard]] static QString defaultSocketPath();

private:
    void onNewConnection();
    void onReadyRead(QLocalSocket* socket);

    QLocalServer* m_server{nullptr};
    QHash<QString, Handler> m_methods;
    QHash<QLocalSocket*, QByteArray> m_buffers;
    QString m_socketPath;
};

} // namespace ajazz::app
