// SPDX-License-Identifier: GPL-3.0-or-later
#include "plugin_asset_server.hpp"

#include "ajazz/core/logger.hpp"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QMimeDatabase>
#include <QStandardPaths>
#include <QString>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>

namespace ajazz::app {

namespace {

/// Plugins root — mirrors plugin_catalog_model.cpp userPluginsDir() (the
/// non-override path; the test override does not apply to the runtime server).
[[nodiscard]] QString pluginsRoot() {
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
           QStringLiteral("/plugins");
}

void sendStatus(QTcpSocket* sock, char const* status) {
    sock->write(QByteArrayLiteral("HTTP/1.1 "));
    sock->write(status);
    sock->write(QByteArrayLiteral("\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"));
}

} // namespace

PluginAssetServer::PluginAssetServer(QObject* parent) : QObject(parent) {}

PluginAssetServer::~PluginAssetServer() = default;

bool PluginAssetServer::start(quint16 port) {
    if (m_server != nullptr) {
        return true;
    }
    m_server = new QTcpServer(this);
    // Loopback only — never expose plugin files to the network.
    if (!m_server->listen(QHostAddress::LocalHost, port)) {
        AJAZZ_LOG_WARN("plugin-asset-server",
                       "failed to listen on 127.0.0.1:{}: {} (plugin icons will be blank)",
                       port,
                       m_server->errorString().toStdString());
        m_server->deleteLater();
        m_server = nullptr;
        return false;
    }
    m_port = port;
    connect(m_server, &QTcpServer::newConnection, this, &PluginAssetServer::onNewConnection);
    AJAZZ_LOG_INFO("plugin-asset-server", "serving plugin assets on 127.0.0.1:{}", port);
    return true;
}

void PluginAssetServer::onNewConnection() {
    while (m_server != nullptr && m_server->hasPendingConnections()) {
        QTcpSocket* sock = m_server->nextPendingConnection();
        connect(sock, &QTcpSocket::disconnected, sock, &QObject::deleteLater);
        connect(sock, &QTcpSocket::readyRead, sock, [sock]() {
            // Minimal HTTP/1.1: we only need the request line `GET <path> HTTP/1.x`.
            // Wait until at least the first line is available.
            if (!sock->canReadLine()) {
                return;
            }
            QByteArray const line = sock->readLine(8192).trimmed();
            QList<QByteArray> const parts = line.split(' ');
            auto const finish = [sock]() {
                sock->flush();
                sock->disconnectFromHost();
            };
            if (parts.size() < 2 || parts.at(0) != QByteArrayLiteral("GET")) {
                sendStatus(sock, "400 Bad Request");
                finish();
                return;
            }
            // Decode + strip the query string.
            QString reqPath = QUrl::fromPercentEncoding(parts.at(1));
            qsizetype const q = reqPath.indexOf(QLatin1Char('?'));
            if (q >= 0) {
                reqPath = reqPath.left(q);
            }
            // Only the plugin-asset namespace is served; reject anything else and
            // any traversal attempt outright.
            QString const kPrefix = QStringLiteral("/__pluginasset__/");
            if (!reqPath.startsWith(kPrefix) || reqPath.contains(QStringLiteral(".."))) {
                sendStatus(sock, "404 Not Found");
                finish();
                return;
            }
            QString const root = pluginsRoot();
            QFileInfo const target(QDir(root).filePath(reqPath.mid(kPrefix.size())));
            QString const canonical = target.canonicalFilePath();
            QString const canonicalRoot = QFileInfo(root).canonicalFilePath();
            if (canonical.isEmpty() || canonicalRoot.isEmpty() ||
                !canonical.startsWith(canonicalRoot + QLatin1Char('/'))) {
                sendStatus(sock, "404 Not Found");
                finish();
                return;
            }
            QFile f(canonical);
            if (!f.open(QIODevice::ReadOnly)) {
                sendStatus(sock, "404 Not Found");
                finish();
                return;
            }
            QByteArray const body = f.readAll();
            f.close();
            QByteArray ctype = QMimeDatabase().mimeTypeForFile(canonical).name().toUtf8();
            if (ctype.isEmpty()) {
                ctype = QByteArrayLiteral("application/octet-stream");
            }
            QByteArray header = QByteArrayLiteral("HTTP/1.1 200 OK\r\nContent-Type: ");
            header += ctype;
            header += QByteArrayLiteral("\r\nContent-Length: ");
            header += QByteArray::number(body.size());
            // The SPA origin is opendeck://app — allow it to read the asset.
            header += QByteArrayLiteral(
                "\r\nAccess-Control-Allow-Origin: *\r\nConnection: close\r\n\r\n");
            sock->write(header);
            sock->write(body);
            finish();
        });
    }
}

} // namespace ajazz::app
