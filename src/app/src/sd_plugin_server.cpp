// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file sd_plugin_server.cpp
 * @brief Elgato Stream Deck v6-compatible WebSocket plugin server (P3.16 MVP).
 */
#include "sd_plugin_server.hpp"

#include "ajazz/core/logger.hpp"

#include <QByteArray>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QEvent>
#include <QJsonDocument>
#include <QRandomGenerator>
#include <QWebSocket>
#include <QWebSocketServer>

#include <algorithm>

namespace ajazz::app {

namespace {

/// The single accepted server name we advertise during the WebSocket
/// handshake. Mirrors the vendor's `Stream Dock` literal used in
/// SDPluginServer::startListen so an Elgato plugin connecting blind sees
/// the expected server identity.
constexpr char const* kServerName = "Stream Dock";

/// Maximum bad authentication attempts before the socket is closed (T-17-BRUTE).
/// PLUGIN-05: brute-force mitigation — after 5 wrong challenges the host
/// closes the connection (PluginAuthTest::rejectsAfter5BadAttempts).
constexpr int kMaxAuthAttempts = 5;

/// Generate a cryptographically-random hex-encoded salt for the passHello
/// auth handshake (T-17-REPLAY: per-connection salt makes captured challenges
/// useless on a new connection). Uses QRandomGenerator::system() — the
/// OS-seeded generator, not rand().
static QString makeRandomSaltHex() {
    QByteArray bytes(16, Qt::Uninitialized);
    // Fill 4 uint32_t (= 16 bytes) from the system entropy source.
    for (int i = 0; i < 4; ++i) {
        quint32 const word = QRandomGenerator::system()->generate();
        bytes[i * 4 + 0] = static_cast<char>((word >> 24) & 0xFF);
        bytes[i * 4 + 1] = static_cast<char>((word >> 16) & 0xFF);
        bytes[i * 4 + 2] = static_cast<char>((word >> 8) & 0xFF);
        bytes[i * 4 + 3] = static_cast<char>(word & 0xFF);
    }
    return QString::fromLatin1(bytes.toHex());
}

/// Compute sha256(password + saltHex) as a lower-case hex string.
///
/// This is the challenge both sides must agree on. The concatenation is
/// UTF-8(password) + UTF-8(saltHex). The "+" is byte-level concatenation;
/// saltHex is the hex string itself (not binary bytes) — our OWN contract
/// verified by PluginAuthTest::acceptsCorrectChallenge (§17-RESEARCH A3).
/// Mirrors the in-tree idiom from single_instance_guard.cpp:85 exactly.
static QString challengeFor(QString const& password, QString const& saltHex) {
    QByteArray const data = password.toUtf8() + saltHex.toUtf8();
    return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
}

} // namespace

SdPluginServer::SdPluginServer(QObject* parent)
    : QObject(parent),
      m_server(
          std::make_unique<QWebSocketServer>(kServerName, QWebSocketServer::NonSecureMode, this)) {
    connect(
        m_server.get(), &QWebSocketServer::newConnection, this, &SdPluginServer::onNewConnection);
}

SdPluginServer::~SdPluginServer() {
    // Stop closes the server and disconnects all clients. The vector cleanup
    // is implicit via Qt's parent-child ownership (clients are parented to
    // this server).
    stop();
}

bool SdPluginServer::start(std::uint16_t port) {
    if (isListening()) {
        AJAZZ_LOG_WARN(
            "plugin-server", "start() called while already listening on port {}", serverPort());
        return true;
    }
    // SECURITY-CRITICAL invariant: loopback-only binding. Vendor uses
    // QHostAddress::Any (0.0.0.0) which exposes the plugin server to every
    // network interface on the host — confirmed regression per
    // akp_plugin_sdk.md §6 "anti-features". We never widen this.
    auto const bound = m_server->listen(QHostAddress::LocalHost, port);
    if (!bound) {
        AJAZZ_LOG_WARN("plugin-server",
                       "listen failed on 127.0.0.1:{} — {}",
                       port,
                       m_server->errorString().toStdString());
        return false;
    }
    AJAZZ_LOG_INFO("plugin-server", "listening on 127.0.0.1:{}", m_server->serverPort());
    emit started(static_cast<std::uint16_t>(m_server->serverPort()));
    return true;
}

void SdPluginServer::stop() {
    if (!m_server || !isListening()) {
        return;
    }
    for (auto& conn : m_connections) {
        if (conn.socket) {
            // Disconnect all our slots first so the impending close() does
            // not re-enter onClientDisconnected on a half-shut socket.
            conn.socket->disconnect(this);
            conn.socket->close();
            conn.socket->deleteLater();
            conn.socket = nullptr;
        }
    }
    m_connections.clear();
    m_server->close();
    // Drain pending deleteLater() calls so the underlying QWebSocketServer
    // (held by m_server) is not destroyed while sockets queued for deletion
    // still hold back-pointers into it. Without this the next SdPluginServer
    // instance in the same QCoreApplication can segfault when the event
    // loop dispatches the stale DeferredDelete events.
    if (QCoreApplication::instance() != nullptr) {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
    AJAZZ_LOG_INFO("plugin-server", "stopped");
    emit stopped();
}

bool SdPluginServer::isListening() const noexcept {
    return m_server && m_server->isListening();
}

std::uint16_t SdPluginServer::serverPort() const noexcept {
    if (!m_server || !m_server->isListening()) {
        return 0;
    }
    return static_cast<std::uint16_t>(m_server->serverPort());
}

QHostAddress SdPluginServer::bindAddress() const noexcept {
    // Always loopback. Returned for inspection / assertion only — there is
    // no setter that would allow widening this.
    return QHostAddress(QHostAddress::LocalHost);
}

int SdPluginServer::connectedPluginCount() const noexcept {
    return static_cast<int>(
        std::count_if(m_connections.begin(), m_connections.end(), [](auto const& c) {
            return c.socket != nullptr && !c.uuid.isEmpty();
        }));
}

void SdPluginServer::onNewConnection() {
    while (m_server->hasPendingConnections()) {
        QWebSocket* client = m_server->nextPendingConnection();
        if (!client) {
            continue;
        }
        // Parent the socket to the server so it shares lifetime; we drop our
        // reference cleanly via deleteLater() at disconnect time.
        client->setParent(this);
        connect(
            client, &QWebSocket::textMessageReceived, this, &SdPluginServer::onClientTextMessage);
        connect(client, &QWebSocket::disconnected, this, &SdPluginServer::onClientDisconnected);
        // Empty UUID until the first registerPlugin message arrives.
        // Salt and auth state are filled when registerPlugin arrives.
        m_connections.push_back({QString{}, client, QString{}, 0, false});
        AJAZZ_LOG_INFO("plugin-server",
                       "client connected (pending registration), total slots {}",
                       m_connections.size());
    }
}

void SdPluginServer::onClientDisconnected() {
    auto* client = qobject_cast<QWebSocket*>(sender());
    if (!client) {
        return;
    }
    QString const uuid = uuidForClient(client); // capture before erasing the slot
    // Erase the slot rather than nulling its socket (WR-07): a nulled entry
    // lingers forever, so a long-lived session accumulates dead {uuid, nullptr}
    // rows that connectedPluginCount/uuidForClient/registerPlugin must linear-
    // scan, and a same-UUID reconnect would leave two entries.
    m_connections.erase(std::remove_if(m_connections.begin(),
                                       m_connections.end(),
                                       [client](auto const& c) { return c.socket == client; }),
                        m_connections.end());
    client->deleteLater();
    if (!uuid.isEmpty()) {
        AJAZZ_LOG_INFO("plugin-server", "plugin disconnected: uuid={}", uuid.toStdString());
        emit pluginDisconnected(uuid);
    } else {
        AJAZZ_LOG_INFO("plugin-server", "unregistered client disconnected");
    }
}

void SdPluginServer::onClientTextMessage(QString const& message) {
    auto* client = qobject_cast<QWebSocket*>(sender());
    if (!client) {
        return;
    }
    QJsonParseError parseError{};
    auto const doc = QJsonDocument::fromJson(message.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        AJAZZ_LOG_WARN("plugin-server",
                       "malformed JSON from client: {}",
                       parseError.errorString().toStdString());
        return;
    }
    dispatchClientMessage(client, doc.object());
}

void SdPluginServer::dispatchClientMessage(QWebSocket* client, QJsonObject const& msg) {
    // Elgato v6 protocol: every plugin message has an "event" field naming
    // the message type. See akp_plugin_sdk.md §3 for the full event list.
    QString const eventName = msg.value(QStringLiteral("event")).toString();
    QString const uuid = msg.value(QStringLiteral("uuid")).toString();

    if (eventName == QStringLiteral("registerPlugin") ||
        eventName == QStringLiteral("registerPropertyInspector")) {
        if (uuid.isEmpty()) {
            AJAZZ_LOG_WARN("plugin-server",
                           "{} message missing uuid field; ignoring",
                           eventName.toStdString());
            return;
        }
        // Bind this WebSocket to the supplied plugin UUID.
        auto it = std::find_if(m_connections.begin(), m_connections.end(), [client](auto const& c) {
            return c.socket == client;
        });
        if (it != m_connections.end()) {
            // --- CR-03: UUID collision guard (T-17-IMPERSONATION) ---
            // Reject a new client that tries to claim a UUID already held by a
            // different live socket. Without this guard, two entries share the
            // same UUID, socketForUuid returns the first match (the legitimate
            // one), and the impostor can emit routed actions as the real plugin.
            auto existing = std::find_if(
                m_connections.begin(), m_connections.end(), [&uuid, client](auto const& c) {
                    return c.uuid == uuid && c.socket != nullptr && c.socket != client;
                });
            if (existing != m_connections.end()) {
                AJAZZ_LOG_WARN("plugin-server",
                               "registerPlugin for uuid={} already held by another socket; "
                               "closing impostor",
                               uuid.toStdString());
                client->close();
                return;
            }

            // --- WR-02: re-registration clean-up ---
            // If this socket already holds a different UUID, emit pluginDisconnected
            // for the old UUID before overwriting so the app layer does not retain
            // a stale UUID binding.
            if (!it->uuid.isEmpty() && it->uuid != uuid) {
                AJAZZ_LOG_WARN("plugin-server",
                               "socket re-registering: old uuid={} replaced by uuid={}",
                               it->uuid.toStdString(),
                               uuid.toStdString());
                emit pluginDisconnected(it->uuid);
            }

            it->uuid = uuid;
            // --- PLUGIN-05: passHello + auth handshake (17-03) ---
            // Generate a random per-connection salt and store it on the slot.
            // T-17-REPLAY: a new salt each connection makes captured challenges
            // useless on reconnect.
            it->salt = makeRandomSaltHex();
            // Default (no password) = accepted immediately after passHello.
            it->authenticated = m_password.isEmpty();

            // Build the passHello payload with NESTED authentication:{challenge,salt}.
            // Spec §4.5 step 2 shows top-level salt, but §4.4 + CONTEXT.md both
            // show authentication:{challenge, salt} nested under payload — the CONTEXT
            // locked decision wins. The host-side challenge is empty when no password
            // is configured (only the salt matters; the plugin is the one that computes
            // sha256(password+salt) in its challenge reply).
            QJsonObject const authObj{
                {QStringLiteral("challenge"), QString{}}, // empty host-side challenge (§4.5 Q2)
                {QStringLiteral("salt"), it->salt},
            };
            QJsonObject const helloPayload{
                {QStringLiteral("device"), msg.value(QStringLiteral("device"))},
                {QStringLiteral("deviceInfo"), QJsonObject{}}, // placeholder; Phase 19 fills this
                {QStringLiteral("authentication"), authObj},
            };
            sendEvent(uuid, QStringLiteral("passHello"), helloPayload);
            AJAZZ_LOG_DEBUG("plugin-server",
                            "passHello sent to uuid={} (password={})",
                            uuid.toStdString(),
                            m_password.isEmpty() ? "none" : "set");

            AJAZZ_LOG_INFO("plugin-server",
                           "plugin registered: uuid={} event={}",
                           uuid.toStdString(),
                           eventName.toStdString());
            // --- CR-02: defer pluginRegistered until auth completes (T-17-PREAUTH) ---
            // When a password is configured, the app layer must not wire device
            // backends to this UUID until authentication succeeds. Emit now only
            // in the no-password (open) path; the authentication success branch
            // below emits for the password path.
            if (m_password.isEmpty()) {
                emit pluginRegistered(uuid);
            }
        } else {
            AJAZZ_LOG_WARN(
                "plugin-server",
                "registerPlugin for uuid={} but socket not in connection table; ignoring",
                uuid.toStdString());
        }
        return;
    }

    // --- PLUGIN-05: authentication challenge verification (17-03) ---
    // Handle BEFORE the 39-action routing set so "authentication" is never
    // treated as a routed action and never reaches unhandledEventReceived
    // (T-17-PREAUTH).
    if (eventName == QStringLiteral("authentication")) {
        auto connIt = std::find_if(m_connections.begin(),
                                   m_connections.end(),
                                   [client](auto const& c) { return c.socket == client; });
        if (connIt == m_connections.end()) {
            AJAZZ_LOG_WARN("plugin-server", "authentication from unknown client; ignoring");
            return;
        }
        if (m_password.isEmpty()) {
            // No password configured — loopback-only, so mark authenticated.
            connIt->authenticated = true;
            return;
        }
        // Password configured: verify sha256(password+salt) == msg["challenge"].
        // T-17-TIMING: plain QString == is acceptable for this loopback/no-TLS
        // threat model; a constant-time compare is not warranted here (documented,
        // not a blocker — see T-17-TIMING in the plan threat register).
        QString const expected = challengeFor(m_password, connIt->salt);
        QString const received = msg.value(QStringLiteral("challenge")).toString();
        if (expected == received) {
            connIt->authenticated = true;
            AJAZZ_LOG_INFO(
                "plugin-server", "authentication accepted for uuid={}", connIt->uuid.toStdString());
            // --- CR-02: deferred pluginRegistered (T-17-PREAUTH) ---
            // Only now is the connection fully authenticated — safe to tell the app
            // layer that this plugin is live and may receive device events.
            emit pluginRegistered(connIt->uuid);
        } else {
            ++connIt->authAttempts;
            AJAZZ_LOG_WARN("plugin-server",
                           "bad authentication challenge from uuid={} (attempt {}/{})",
                           connIt->uuid.toStdString(),
                           connIt->authAttempts,
                           kMaxAuthAttempts);
            if (connIt->authAttempts >= kMaxAuthAttempts) {
                AJAZZ_LOG_WARN("plugin-server",
                               "max auth attempts reached for uuid={}; closing socket (T-17-BRUTE)",
                               connIt->uuid.toStdString());
                // Close the socket. Do NOT touch connIt after close() — the
                // onClientDisconnected slot will erase the entry (T-17-UAF /
                // Pitfall 4: sendEvent re-resolves each call and returns false).
                connIt->socket->close();
            }
        }
        return;
    }

    // Full routed-action set per akp_plugin_sdk.md §4.3 (spec 4.3 table).
    // 15 standard Elgato routed + 26 AJAZZ-only = 41 total.
    // The array is intentionally SIZELESS (CTAD) so the count is derived from
    // the literal and can never drift out of sync with the list.
    // NOTE: registerPlugin / registerPropertyInspector are NOT here — they are
    // handled in the earlier branch (above, with early return).
    // NOTE: `authentication` is handled in the branch directly above (17-03).
    static constexpr std::array kRoutedActions = {
        // --- Standard Elgato routed (15) — spec 4.3 "Standard Elgato? yes",
        //     minus registerPlugin / registerPropertyInspector (handled above).
        "setTitle",
        "setImage",
        "setState",
        "showAlert",
        "showOk",
        "getSettings",
        "setSettings",
        "getGlobalSettings",
        "setGlobalSettings",
        "switchToProfile",
        "sendToPropertyInspector",
        "sendToPlugin",
        "openUrl",
        "logMessage",
        "setFeedback", // Stream Deck Plus encoder feedback — standard per spec 4.3
        // --- AJAZZ-only routed (26) — spec 4.3 "Standard Elgato? AJAZZ-only".
        //     setFeedback is NOT in this group (it is standard).
        "setBG",
        "setBackground",
        "clearIcon",
        "sendToDevice",
        "openTouchbarSecondaryMenu",
        "exitTouchbarSecondaryMenu",
        "enterGatheringEvent",
        "registrationScreenSaverEvent",
        "unRegistrationScreenSaverEvent",
        "setText",
        "lockScreen",
        "unLockScreen",
        "getScreenshot",
        "getSystemAudioVolume",
        "getUserInfo",
        "setAcImgTop",
        "onSwitchToFolderProfile",
        "onSwitchFromFolderProfile",
        "deleteAction",
        "stopBackground",
        "exitFullScreen",
        "touchTap",
        "getDetectedSensorsData",
        "startAudioCapture",
        "stopAudioCapture",
        "sendUserInfo",
    };
    // --- CR-01: pre-auth gate for routed actions (T-17-PREAUTH extension) ---
    // A connection that has completed registerPlugin but has NOT yet sent a
    // correct authentication reply must not reach the routed-action set.
    // Without this gate, any local process can inject setTitle/sendToDevice/etc.
    // into the app layer the moment registerPlugin completes, before auth.
    // In the no-password path, authenticated is set to true in registerPlugin
    // so this gate is a no-op (open-mode behaviour is preserved).
    auto connForAuth = std::find_if(m_connections.begin(),
                                    m_connections.end(),
                                    [client](auto const& c) { return c.socket == client; });
    if (connForAuth != m_connections.end() && !connForAuth->authenticated) {
        AJAZZ_LOG_WARN("plugin-server",
                       "unauthenticated client uuid={} sent action '{}'; ignoring",
                       connForAuth->uuid.toStdString(),
                       eventName.toStdString());
        return;
    }

    bool isAction = false;
    for (auto const* known : kRoutedActions) {
        if (eventName == QLatin1String(known)) {
            isAction = true;
            break;
        }
    }
    QString const senderUuid = uuidForClient(client);
    if (isAction) {
        emit actionReceived(senderUuid, msg);
        return;
    }
    // Genuinely-unknown events still surface via unhandledEventReceived for
    // forward-compat tracing (T-17-FWD). The 41 routed actions above narrow
    // this surface; any event the spec does not yet define still reaches here.
    AJAZZ_LOG_INFO("plugin-server",
                   "unhandled event '{}' from plugin uuid={}",
                   eventName.toStdString(),
                   senderUuid.toStdString());
    emit unhandledEventReceived(senderUuid, eventName);
}

QString SdPluginServer::uuidForClient(QWebSocket* client) const {
    auto it = std::find_if(m_connections.begin(), m_connections.end(), [client](auto const& c) {
        return c.socket == client;
    });
    if (it == m_connections.end()) {
        return {};
    }
    return it->uuid;
}

QWebSocket* SdPluginServer::socketForUuid(QString const& uuid) const {
    // Mirror of uuidForClient, but in the forward direction (uuid -> socket).
    // Only returns a slot whose socket pointer is non-null (live connection).
    // Called on every sendEvent — re-resolves the live slot each time so a
    // future auth rejection that closes the socket cannot produce a dangling
    // pointer (T-17-UAF, Pitfall 4).
    auto it = std::find_if(m_connections.begin(), m_connections.end(), [&uuid](auto const& c) {
        return c.uuid == uuid && c.socket != nullptr;
    });
    if (it == m_connections.end()) {
        return nullptr;
    }
    return it->socket;
}

void SdPluginServer::setPasswordForTesting(QString const& password) {
    m_password = password;
}

void SdPluginServer::injectAction(QString const& pluginUuid, QJsonObject const& action) {
    // Re-emit on the exact production signal so every actionReceived consumer
    // runs identically to a real plugin message (debug/simulation only).
    emit actionReceived(pluginUuid, action);
}

bool SdPluginServer::sendEvent(QString const& targetUuid,
                               QString const& eventName,
                               QJsonObject const& payload) {
    // Re-resolve the live socket on every call (Pitfall 4 / T-17-UAF guard).
    QWebSocket* sock = socketForUuid(targetUuid);
    if (!sock) {
        AJAZZ_LOG_DEBUG("plugin-server",
                        "sendEvent '{}' -> uuid='{}': no live socket (plugin not registered or "
                        "disconnected)",
                        eventName.toStdString(),
                        targetUuid.toStdString());
        return false;
    }
    // Build the JSON envelope: {"event": eventName} + optional "payload" key.
    QJsonObject env;
    env.insert(QStringLiteral("event"), eventName);
    if (!payload.isEmpty()) {
        env.insert(QStringLiteral("payload"), payload);
    }
    auto const frame = QString::fromUtf8(QJsonDocument(env).toJson(QJsonDocument::Compact));
    sock->sendTextMessage(frame);
    AJAZZ_LOG_DEBUG("plugin-server",
                    "sendEvent '{}' -> uuid='{}' ({} bytes)",
                    eventName.toStdString(),
                    targetUuid.toStdString(),
                    frame.size());
    return true;
}

} // namespace ajazz::app
