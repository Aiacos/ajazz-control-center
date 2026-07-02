// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file obs_client.hpp
 * @brief obs-websocket v5 client for the obsstudio built-in action.
 *
 * Implements the obs-websocket v5 Hello/Identify handshake with **auth
 * default-on** (the LOCKED anti-feature mitigation): when OBS demands
 * authentication the client REQUIRES a configured password — it refuses to
 * send Identify and emits authFailed when none is set. Connecting to an
 * unauthenticated OBS without a password is allowed (pass empty string to
 * connectToObs), but connecting to an auth-demanding OBS without a password
 * is rejected (PLUGIN-12 / T-21-obsauth).
 *
 * **Auth algorithm** (obs-websocket v5 protocol.md):
 *   auth = Base64( SHA256( Base64( SHA256( password + salt ) ) + challenge ) )
 * Computed via QCryptographicHash::Sha256 — no bundled crypto (T-21-obscrypto).
 *
 * **COD-031**: all JSON is app-tier QJsonDocument/QJsonObject (the vendored
 * JSON library is not permitted in any app-tier OBS client code).
 *
 * **Compile-out when AJAZZ_HAVE_WEBSOCKETS is unset**: mirrors sd_plugin_server.hpp
 * so the app builds on minimal Qt installs. 21-03 wires the obsstudio built-in
 * to this client; do NOT expose it to the QML module.
 *
 * **QSettings password-at-rest (T-21-obspw)**: the OBS password is stored by
 * 21-03 via QSettings — plaintext at rest (no OS keychain). Documented known
 * limitation; no encryption is claimed.
 */
#pragma once

#if defined(AJAZZ_HAVE_WEBSOCKETS)

#include <QJsonObject>
#include <QObject>
#include <QString>

QT_BEGIN_NAMESPACE
class QWebSocket;
QT_END_NAMESPACE

namespace ajazz::app {

/**
 * @brief obs-websocket v5 client (auth default-on; COD-031-clean).
 *
 * Lifecycle:
 *   1. Caller creates one ObsClient instance.
 *   2. Caller calls connectToObs(host, port, password).
 *   3. Client opens a QWebSocket to OBS.
 *   4. On Hello(op:0):
 *      - If d.authentication present + password set: compute auth, send Identify(op:1).
 *      - If d.authentication present + password EMPTY: emit authFailed, close — DO NOT send
 * Identify.
 *      - If d.authentication absent: send Identify(op:1) without authentication field.
 *   5. On Identified(op:2): emit connected().
 *   6. Caller calls setScene/setPreviewScene/toggleRecord/toggleStream; each emits an op:6 Request.
 *
 * ObsClient is NOT QML-exposed (21-03 wires it via the BuiltinActionsService).
 */
class ObsClient : public QObject {
    Q_OBJECT

public:
    explicit ObsClient(QObject* parent = nullptr);
    ~ObsClient() override;

    /**
     * @brief Pure auth function: Base64(SHA256(Base64(SHA256(password+salt))+challenge)).
     *
     * Deterministic and stateless — safe to call from tests without a socket.
     * Uses QCryptographicHash::Sha256 (never hand-rolled crypto; Qt ships it).
     * The password is consumed but never logged.
     */
    [[nodiscard]] static QString
    computeObsAuth(QString const& password, QString const& salt, QString const& challenge);

    /**
     * @brief Connect to OBS WebSocket server and perform the v5 handshake.
     *
     * @param host      OBS host (e.g. "127.0.0.1" or "localhost").
     * @param port      OBS WebSocket port (default 4455 per v5 spec).
     * @param password  OBS authentication password.
     *                  May be empty ONLY when connecting to a server that
     *                  omits d.authentication in its Hello (auth disabled).
     *                  If the server Hello carries d.authentication and this
     *                  is empty, authFailed is emitted and Identify is NOT sent
     *                  (auth default-on, LOCKED anti-feature T-21-obsauth).
     *                  NEVER logged.
     */
    void connectToObs(QString const& host, quint16 port, QString const& password);

    // --- Request subset (op:6) — call after connected() ---

    /// SetCurrentProgramScene: switch the active scene by name.
    void setScene(QString const& sceneName);

    /// SetCurrentPreviewScene: switch the Studio Mode preview scene by name.
    void setPreviewScene(QString const& sceneName);

    /// ToggleRecord: toggle OBS recording on/off.
    void toggleRecord();

    /// ToggleStream: toggle OBS streaming on/off.
    void toggleStream();

signals:
    /// Emitted when Identified(op:2) is received — the client is ready to send Requests.
    void connected();

    /**
     * @brief Emitted when auth is required but no password is configured (auth default-on).
     *
     * The client closes the socket without sending Identify.
     * @param reason Human-readable explanation (never the password).
     */
    void authFailed(QString reason);

    /**
     * @brief Emitted on socket error or protocol error.
     * @param reason Human-readable explanation.
     */
    void errorOccurred(QString reason);

    /**
     * @brief Optionally emitted when a RequestResponse(op:7) arrives for a sent Request.
     * @param requestId The requestId from the emitted op:6 message.
     */
    void requestSucceeded(QString requestId);

private slots:
    void onTextMessageReceived(QString const& message);
    void onSocketError();

private:
    /**
     * @brief Build and send an op:6 Request frame.
     *
     * Mints a fresh QUuid requestId. Builds:
     *   { "op": 6, "d": { "requestType": requestType, "requestId": <uuid>,
     *                      "requestData": requestData } }
     * serialised as compact JSON via QJsonDocument.
     */
    void sendRequest(QString const& requestType, QJsonObject const& requestData = QJsonObject{});

    std::unique_ptr<QWebSocket> m_socket;
    QString m_password; ///< NEVER logged. Cleared on close.

    /// Internal connection state.
    enum class State { Disconnected, Connecting, Identified };
    State m_state{State::Disconnected};
};

} // namespace ajazz::app

#endif // defined(AJAZZ_HAVE_WEBSOCKETS)
