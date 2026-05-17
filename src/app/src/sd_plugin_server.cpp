// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file sd_plugin_server.cpp
 * @brief Elgato Stream Deck v6-compatible WebSocket plugin server (P3.16 MVP).
 */
#include "sd_plugin_server.hpp"

#include "ajazz/core/logger.hpp"

#include <QJsonDocument>
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

} // namespace

SdPluginServer::SdPluginServer(QObject* parent)
    : QObject(parent)
    , m_server(std::make_unique<QWebSocketServer>(kServerName,
                                                   QWebSocketServer::NonSecureMode,
                                                   this)) {
    connect(m_server.get(),
            &QWebSocketServer::newConnection,
            this,
            &SdPluginServer::onNewConnection);
}

SdPluginServer::~SdPluginServer() {
    // Stop closes the server and disconnects all clients. The vector cleanup
    // is implicit via Qt's parent-child ownership (clients are parented to
    // this server).
    stop();
}

bool SdPluginServer::start(std::uint16_t port) {
    if (isListening()) {
        AJAZZ_LOG_WARN("plugin-server", "start() called while already listening on port {}",
                       serverPort());
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
            conn.socket->close();
            conn.socket->deleteLater();
            conn.socket = nullptr;
        }
    }
    m_connections.clear();
    m_server->close();
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
    return static_cast<int>(std::count_if(m_connections.begin(), m_connections.end(),
                                           [](auto const& c) { return c.socket != nullptr; }));
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
        connect(client,
                &QWebSocket::textMessageReceived,
                this,
                &SdPluginServer::onClientTextMessage);
        connect(client,
                &QWebSocket::disconnected,
                this,
                &SdPluginServer::onClientDisconnected);
        // Empty UUID until the first registerPlugin message arrives.
        m_connections.push_back({QString{}, client});
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
    QString const uuid = uuidForClient(client);
    auto it = std::find_if(m_connections.begin(), m_connections.end(),
                            [client](auto const& c) { return c.socket == client; });
    if (it != m_connections.end()) {
        it->socket = nullptr;
    }
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
        auto it = std::find_if(m_connections.begin(), m_connections.end(),
                                [client](auto const& c) { return c.socket == client; });
        if (it != m_connections.end()) {
            it->uuid = uuid;
        }
        AJAZZ_LOG_INFO("plugin-server", "plugin registered: uuid={} event={}",
                       uuid.toStdString(), eventName.toStdString());
        emit pluginRegistered(uuid);
        return;
    }

    // For action-class messages (setTitle / setImage / showAlert / ... — see
    // akp_plugin_sdk.md §4) the app layer wires emit-target lookups based on
    // the action's "context" field. MVP just surfaces the JSON for the app
    // layer; full dispatch tables land in follow-up commits.
    static constexpr std::array<char const*, 13> kStandardActions = {
        "setTitle",        "setImage",      "showAlert",       "showOk",
        "getSettings",     "setSettings",   "getGlobalSettings",
        "setGlobalSettings", "switchToProfile", "openUrl",
        "logMessage",      "registerPlugin", "registerPropertyInspector"};
    bool isAction = false;
    for (auto const* known : kStandardActions) {
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
    // Anything else surfaces as unhandled — useful for tracing the 26
    // AJAZZ-extension events (setBG, screenColorSent, etc.) we don't yet
    // implement.
    AJAZZ_LOG_INFO("plugin-server",
                   "unhandled event '{}' from plugin uuid={}",
                   eventName.toStdString(),
                   senderUuid.toStdString());
    emit unhandledEventReceived(senderUuid, eventName);
}

QString SdPluginServer::uuidForClient(QWebSocket* client) const {
    auto it = std::find_if(m_connections.begin(), m_connections.end(),
                            [client](auto const& c) { return c.socket == client; });
    if (it == m_connections.end()) {
        return {};
    }
    return it->uuid;
}

} // namespace ajazz::app
