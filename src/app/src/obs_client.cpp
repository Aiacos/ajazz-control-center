// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file obs_client.cpp
 * @brief obs-websocket v5 client implementation.
 *
 * Protocol references:
 *   Hello   (op 0): server -> client; d.rpcVersion; d.authentication{challenge,salt} present ONLY
 *                   when auth enabled.
 *   Identify(op 1): client -> server; d.rpcVersion=1; d.authentication=<computed> (omit when server
 *                   omits authentication); d.eventSubscriptions=0.
 *   Identified(op 2): server -> client on success.
 *   Request (op 6): client -> server; d.requestType; d.requestId; d.requestData{...}.
 *   RequestResponse(op 7): server -> client; d.requestId; d.requestStatus.
 *
 * Auth algorithm (obs-websocket v5 protocol.md):
 *   secret   = Base64( SHA256( password + salt ) )
 *   authHash = SHA256( secret + challenge )
 *   auth     = Base64( authHash )
 *
 * COD-031: all JSON is app-tier QJsonDocument/QJsonObject (app-tier boundary; never the
 * vendored JSON library used in core — see CLAUDE.md COD-031).
 * AJAZZ_HAVE_WEBSOCKETS gate: this entire TU compiles away when the module is absent.
 */

#if defined(AJAZZ_HAVE_WEBSOCKETS)

#include "obs_client.hpp"

#include "ajazz/core/logger.hpp"

#include <QByteArray>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>
#include <QWebSocket>

namespace ajazz::app {

// ---- obs-websocket v5 opcodes ----
namespace {
constexpr int kOpHello = 0;
constexpr int kOpIdentify = 1;
constexpr int kOpIdentified = 2;
constexpr int kOpRequest = 6;
constexpr int kOpRequestResponse = 7;

constexpr int kRpcVersion = 1;

/// Build the Identify(op:1) frame.
///
/// @param auth  Computed authentication string, or a null QString when the
///              server Hello omitted d.authentication (auth-disabled OBS).
static QString buildIdentify(QString const& auth) {
    QJsonObject d;
    d[QStringLiteral("rpcVersion")] = kRpcVersion;
    d[QStringLiteral("eventSubscriptions")] = 0;
    if (!auth.isNull()) {
        d[QStringLiteral("authentication")] = auth;
    }
    QJsonObject frame;
    frame[QStringLiteral("op")] = kOpIdentify;
    frame[QStringLiteral("d")] = d;
    return QString::fromUtf8(QJsonDocument(frame).toJson(QJsonDocument::Compact));
}

} // namespace

// ---- ObsClient ----

ObsClient::ObsClient(QObject* parent) : QObject(parent), m_socket(std::make_unique<QWebSocket>()) {
    connect(
        m_socket.get(), &QWebSocket::textMessageReceived, this, &ObsClient::onTextMessageReceived);
    connect(m_socket.get(),
            &QWebSocket::errorOccurred,
            this,
            [this](QAbstractSocket::SocketError /*err*/) { onSocketError(); });
}

ObsClient::~ObsClient() {
    m_password.clear();
    if (m_socket) {
        m_socket->close();
    }
}

// static
QString
ObsClient::computeObsAuth(QString const& password, QString const& salt, QString const& challenge) {
    // Step 1: secret = Base64( SHA256( password + salt ) )
    QByteArray const secretHash =
        QCryptographicHash::hash((password + salt).toUtf8(), QCryptographicHash::Sha256);
    QByteArray const secret = secretHash.toBase64();

    // Step 2: authHash = SHA256( secret_base64_string + challenge )
    //   The challenge is appended as a UTF-8 string (per protocol.md).
    QByteArray const authHashBytes =
        QCryptographicHash::hash(secret + challenge.toUtf8(), QCryptographicHash::Sha256);

    // Step 3: auth = Base64( authHash )
    return QString::fromLatin1(authHashBytes.toBase64());
}

void ObsClient::connectToObs(QString const& host, quint16 port, QString const& password) {
    if (m_state != State::Disconnected) {
        AJAZZ_LOG_WARN("obs-client", "connectToObs called while not Disconnected — ignoring");
        return;
    }
    m_password = password;
    m_state = State::Connecting;

    QUrl url;
    url.setScheme(QStringLiteral("ws"));
    url.setHost(host);
    url.setPort(port);
    m_socket->open(url);
}

// ----- Request helpers -----

void ObsClient::setScene(QString const& sceneName) {
    QJsonObject data;
    data[QStringLiteral("sceneName")] = sceneName;
    sendRequest(QStringLiteral("SetCurrentProgramScene"), data);
}

void ObsClient::setPreviewScene(QString const& sceneName) {
    QJsonObject data;
    data[QStringLiteral("sceneName")] = sceneName;
    sendRequest(QStringLiteral("SetCurrentPreviewScene"), data);
}

void ObsClient::toggleRecord() {
    sendRequest(QStringLiteral("ToggleRecord"));
}

void ObsClient::toggleStream() {
    sendRequest(QStringLiteral("ToggleStream"));
}

// ----- Private -----

void ObsClient::sendRequest(QString const& requestType, QJsonObject const& requestData) {
    if (m_state != State::Identified) {
        AJAZZ_LOG_WARN("obs-client",
                       "sendRequest({}) called before Identified — dropping (state={})",
                       requestType.toStdString(),
                       static_cast<int>(m_state));
        return;
    }

    QString const requestId = QUuid::createUuid().toString(QUuid::WithoutBraces);

    QJsonObject d;
    d[QStringLiteral("requestType")] = requestType;
    d[QStringLiteral("requestId")] = requestId;
    d[QStringLiteral("requestData")] = requestData;

    QJsonObject frame;
    frame[QStringLiteral("op")] = kOpRequest;
    frame[QStringLiteral("d")] = d;

    QString const json = QString::fromUtf8(QJsonDocument(frame).toJson(QJsonDocument::Compact));
    m_socket->sendTextMessage(json);
}

void ObsClient::onTextMessageReceived(QString const& message) {
    QJsonParseError parseError;
    QJsonDocument const doc = QJsonDocument::fromJson(message.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        AJAZZ_LOG_WARN("obs-client", "Received malformed JSON from OBS: {}", message.toStdString());
        return;
    }

    QJsonObject const root = doc.object();
    int const op = root.value(QStringLiteral("op")).toInt(-1);
    QJsonObject const d = root.value(QStringLiteral("d")).toObject();

    switch (op) {
    case kOpHello: {
        // d.authentication present ONLY when OBS requires auth.
        // Per obs-websocket v5 protocol.md: the discriminant is ABSENT vs PRESENT —
        // any defined value (including null, bool, string) signals auth-required.
        // Only a genuine isUndefined() means auth-disabled (T-21-obsauth).
        QJsonValue const authVal = d.value(QStringLiteral("authentication"));
        if (!authVal.isUndefined()) {
            // Auth required.
            if (!authVal.isObject()) {
                // Malformed Hello: authentication key present but not a proper object.
                // Treat as auth-required and refuse (T-21-obsauth: never send Identify
                // to a server that signals auth-required, even if malformed).
                AJAZZ_LOG_WARN("obs-client",
                               "OBS Hello has d.authentication but it is not an object — "
                               "treating as auth-required (T-21-obsauth)");
                m_state = State::Disconnected;
                m_socket->close();
                emit authFailed(QStringLiteral(
                    "OBS sent malformed authentication field; treating as auth-required"));
                return;
            }
            if (m_password.isEmpty()) {
                // Auth default-on (LOCKED): REFUSE to send Identify — emit authFailed.
                // T-21-obsauth: the client never connects plaintext to an auth-demanding OBS.
                AJAZZ_LOG_WARN("obs-client",
                               "OBS requires authentication but no password is configured — "
                               "refusing to connect (T-21-obsauth auth default-on)");
                m_state = State::Disconnected;
                m_socket->close();
                emit authFailed(
                    QStringLiteral("OBS requires authentication but no password is configured"));
                return;
            }
            // Password set: compute auth and send Identify.
            QJsonObject const authObj = authVal.toObject();
            QString const salt = authObj.value(QStringLiteral("salt")).toString();
            QString const challenge = authObj.value(QStringLiteral("challenge")).toString();
            QString const auth = computeObsAuth(m_password, salt, challenge);
            m_socket->sendTextMessage(buildIdentify(auth));
        } else {
            // Auth disabled on the OBS server: send Identify without authentication field.
            // Log at INFO (not WARN) — this is a valid configuration.
            AJAZZ_LOG_INFO("obs-client",
                           "OBS Hello has no d.authentication — sending Identify without auth "
                           "(auth-disabled server)");
            m_socket->sendTextMessage(buildIdentify(QString{})); // null QString -> no auth field
        }
        break;
    }

    case kOpIdentified:
        // Handshake complete — client is ready to send Requests.
        m_state = State::Identified;
        AJAZZ_LOG_INFO("obs-client", "OBS connection established (Identified)");
        emit connected();
        break;

    case kOpRequestResponse: {
        QString const reqId = d.value(QStringLiteral("requestId")).toString();
        if (!reqId.isEmpty()) {
            emit requestSucceeded(reqId);
        }
        break;
    }

    default:
        // Ignore unknown opcodes (forward-compat).
        break;
    }
}

void ObsClient::onSocketError() {
    QString const errStr = m_socket ? m_socket->errorString() : QStringLiteral("unknown error");
    AJAZZ_LOG_WARN("obs-client", "WebSocket error: {}", errStr.toStdString());
    m_state = State::Disconnected;
    emit errorOccurred(errStr);
}

} // namespace ajazz::app

#endif // defined(AJAZZ_HAVE_WEBSOCKETS)
