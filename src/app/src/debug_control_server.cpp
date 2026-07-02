// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file debug_control_server.cpp
 * @brief Implementation of the opt-in JSON-RPC control channel.
 */
#include "debug_control_server.hpp"

#include "ajazz/core/logger.hpp"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLocalServer>
#include <QLocalSocket>
#include <QStandardPaths>
#include <QStringList>

#include <utility>

namespace ajazz::app {
namespace {

constexpr char kModule[] = "debug-control";

/// Serialise an object to a single newline-terminated UTF-8 line.
QByteArray toLine(QJsonObject const& obj) {
    return QJsonDocument(obj).toJson(QJsonDocument::Compact) + '\n';
}

QJsonObject errorResponse(QJsonValue const& id, QString const& message) {
    return QJsonObject{{"id", id}, {"ok", false}, {"error", message}};
}

} // namespace

DebugControlServer::DebugControlServer(QObject* parent) : QObject(parent) {
    // Built-in discovery method so a client can enumerate the surface.
    registerMethod("methods", [this](QJsonObject const&, QString&) {
        QJsonArray arr;
        for (auto const& name : methodNames()) {
            arr.append(name);
        }
        return QJsonObject{{"methods", arr}};
    });
    registerMethod("ping", [](QJsonObject const& params, QString&) {
        QJsonObject out{{"pong", true}};
        if (params.contains("echo")) {
            out.insert("echo", params.value("echo"));
        }
        return out;
    });
}

DebugControlServer::~DebugControlServer() {
    stop();
}

void DebugControlServer::registerMethod(QString const& method, Handler handler) {
    m_methods.insert(method, std::move(handler));
}

QStringList DebugControlServer::methodNames() const {
    QStringList names = m_methods.keys();
    names.sort();
    return names;
}

bool DebugControlServer::enabledFromEnv() {
    return !qEnvironmentVariable("AJAZZ_DEBUG_CONTROL").isEmpty();
}

QString DebugControlServer::defaultSocketPath() {
    // Allow an explicit absolute path via the env var (contains '/').
    auto const env = qEnvironmentVariable("AJAZZ_DEBUG_CONTROL");
    if (env.contains(QLatin1Char('/'))) {
        return env;
    }
    QString base = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (base.isEmpty()) {
        base = QDir::tempPath();
    }
    return base + QStringLiteral("/ajazz-control-center-debug.sock");
}

bool DebugControlServer::start(QString const& socketPath) {
    if (m_server != nullptr) {
        stop();
    }
    m_socketPath = socketPath;

    // A stale socket file from a crashed run blocks listen(); QLocalServer
    // provides removeServer() to clear it. This is safe because the channel
    // is single-owner (per-user runtime dir, owner-only mode).
    QLocalServer::removeServer(socketPath);

    m_server = new QLocalServer(this);
    // Owner-only socket (mode 0600 on Linux): no group / world access.
    m_server->setSocketOptions(QLocalServer::UserAccessOption);

    if (!m_server->listen(socketPath)) {
        AJAZZ_LOG_ERROR(kModule,
                        "failed to listen on {}: {}",
                        socketPath.toStdString(),
                        m_server->errorString().toStdString());
        m_server->deleteLater();
        m_server = nullptr;
        return false;
    }

    connect(m_server, &QLocalServer::newConnection, this, &DebugControlServer::onNewConnection);
    AJAZZ_LOG_INFO(kModule,
                   "control channel listening on {} ({} methods)",
                   socketPath.toStdString(),
                   m_methods.size());
    return true;
}

void DebugControlServer::stop() {
    if (m_server != nullptr) {
        m_server->close();
        m_server->deleteLater();
        m_server = nullptr;
    }
    m_buffers.clear();
    if (!m_socketPath.isEmpty()) {
        QLocalServer::removeServer(m_socketPath);
    }
}

bool DebugControlServer::isListening() const {
    return m_server != nullptr && m_server->isListening();
}

void DebugControlServer::onNewConnection() {
    while (m_server != nullptr && m_server->hasPendingConnections()) {
        QLocalSocket* socket = m_server->nextPendingConnection();
        m_buffers.insert(socket, QByteArray());
        connect(socket, &QLocalSocket::readyRead, this, [this, socket]() { onReadyRead(socket); });
        connect(socket, &QLocalSocket::disconnected, this, [this, socket]() {
            m_buffers.remove(socket);
            socket->deleteLater();
        });
    }
}

void DebugControlServer::onReadyRead(QLocalSocket* socket) {
    auto it = m_buffers.find(socket);
    if (it == m_buffers.end()) {
        return;
    }
    it->append(socket->readAll());

    // Process every complete (newline-terminated) line; keep any partial
    // tail in the buffer for the next readyRead.
    qsizetype newline = it->indexOf('\n');
    while (newline >= 0) {
        QByteArray const line = it->left(newline);
        it->remove(0, newline + 1);
        QByteArray const response = processRequestLine(line);
        socket->write(response);
        socket->flush();
        newline = it->indexOf('\n');
    }
}

QByteArray DebugControlServer::processRequestLine(QByteArray const& line) {
    QByteArray const trimmed = line.trimmed();
    if (trimmed.isEmpty()) {
        return {}; // ignore blank lines
    }

    QJsonParseError parseError{};
    QJsonDocument const doc = QJsonDocument::fromJson(trimmed, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        return toLine(
            errorResponse(QJsonValue::Null,
                          QStringLiteral("malformed JSON request: ") + parseError.errorString()));
    }

    QJsonObject const request = doc.object();
    QJsonValue const id = request.value("id");
    QString const method = request.value("method").toString();
    if (method.isEmpty()) {
        return toLine(errorResponse(id, QStringLiteral("missing 'method'")));
    }

    auto handlerIt = m_methods.constFind(method);
    if (handlerIt == m_methods.constEnd()) {
        return toLine(errorResponse(id, QStringLiteral("unknown method: ") + method));
    }

    QJsonObject const params = request.value("params").toObject();
    QString error;
    QJsonObject result;
    // Handlers are trusted (registered in-process); a thrown exception would
    // be a programming error, but the channel must never take the GUI down,
    // so guard defensively.
    try {
        result = (*handlerIt)(params, error);
    } catch (std::exception const& ex) {
        error = QStringLiteral("handler threw: ") + QString::fromUtf8(ex.what());
    } catch (...) {
        error = QStringLiteral("handler threw a non-std exception");
    }

    if (!error.isEmpty()) {
        return toLine(errorResponse(id, error));
    }
    return toLine(QJsonObject{{"id", id}, {"ok", true}, {"result", result}});
}

} // namespace ajazz::app
