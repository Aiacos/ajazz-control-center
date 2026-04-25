// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file single_instance_guard.cpp
 * @brief Implementation of @ref ajazz::app::SingleInstanceGuard.
 */
#include "single_instance_guard.hpp"

#include "ajazz/core/logger.hpp"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QLocalSocket>
#include <QStandardPaths>

#ifndef AJAZZ_APP_ID
#define AJAZZ_APP_ID "io.github.Aiacos.AjazzControlCenter"
#endif

namespace ajazz::app {

namespace {
constexpr char const* kShowToken = "show\n";
constexpr int kReadDeadlineMs = 250;
} // namespace

SingleInstanceGuard::SingleInstanceGuard(QString name, QObject* parent)
    : QObject(parent), name_(std::move(name)) {
    // Drop any leftover socket file from a previous instance that crashed
    // before its destructor could run. listen() would otherwise refuse with
    // AddressInUseError on Linux.
    QLocalServer::removeServer(name_);

    if (!server_.listen(name_)) {
        AJAZZ_LOG_WARN("single-instance",
                       "could not listen on {}: {}",
                       name_.toStdString(),
                       server_.errorString().toStdString());
        return;
    }
    owns_ = true;
    AJAZZ_LOG_INFO("single-instance", "primary instance is listening on {}", name_.toStdString());

    QObject::connect(&server_, &QLocalServer::newConnection, this, [this]() {
        // newConnection may fire multiple times in a tight loop (e.g. autostart
        // race); drain the backlog rather than processing only one.
        while (auto* sock = server_.nextPendingConnection()) {
            // Read whatever the secondary sends, up to a small timeout.
            if (sock->waitForReadyRead(kReadDeadlineMs)) {
                QByteArray const data = sock->readAll();
                if (data.contains("show")) {
                    emit showRequested();
                }
            }
            sock->disconnectFromServer();
            sock->deleteLater();
        }
    });
}

bool SingleInstanceGuard::tryActivateExisting(QString const& name, int timeoutMs) {
    QLocalSocket sock;
    sock.connectToServer(name);
    if (!sock.waitForConnected(timeoutMs)) {
        return false;
    }
    sock.write(kShowToken, static_cast<qint64>(std::char_traits<char>::length(kShowToken)));
    sock.flush();
    sock.waitForBytesWritten(timeoutMs);
    sock.disconnectFromServer();
    if (sock.state() != QLocalSocket::UnconnectedState) {
        sock.waitForDisconnected(timeoutMs);
    }
    return true;
}

QString SingleInstanceGuard::defaultSocketName() {
    // Embed a hash of the per-user runtime location so two users on the same
    // host get distinct sockets (and a portable Windows fallback). Only the
    // hash is exposed in /tmp, not the username, which keeps `/tmp` listings
    // free of identity-leak hints.
    QString const userScope = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    QString const fallback = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    QByteArray const seed = (userScope.isEmpty() ? fallback : userScope).toUtf8();
    QString const tag = QString::fromLatin1(
        QCryptographicHash::hash(seed, QCryptographicHash::Md5).toHex().left(8));
    return QStringLiteral(AJAZZ_APP_ID "-") + tag;
}

} // namespace ajazz::app
