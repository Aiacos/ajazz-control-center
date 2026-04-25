// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file single_instance_guard.hpp
 * @brief Cross-platform "only one instance allowed" gatekeeper for the
 *        AJAZZ Control Center GUI.
 *
 * The guard is built on top of `QLocalServer` / `QLocalSocket` (Qt's portable
 * named-pipe abstraction). On Linux this becomes a Unix domain socket under
 * `/tmp/`; on Windows it becomes a named pipe; on macOS another Unix socket.
 *
 * Usage:
 *
 *   if (SingleInstanceGuard::tryActivateExisting(socketName)) {
 *       return 0;        // primary instance found and asked to show; exit
 *   }
 *   SingleInstanceGuard guard(socketName);
 *   if (!guard.isPrimary()) {
 *       return 1;        // race: someone else grabbed the socket; bail out
 *   }
 *   QObject::connect(&guard, &SingleInstanceGuard::showRequested,
 *                    [&engine] { ... raise the QML window ... });
 *
 * The socket name embeds the current user's identity so concurrent sessions
 * by different users on the same host get independent instances.
 */
#pragma once

#include <QLocalServer>
#include <QObject>
#include <QString>

namespace ajazz::app {

class SingleInstanceGuard : public QObject {
    Q_OBJECT
public:
    /// Listens on @p name; @ref isPrimary() reports whether ownership succeeded.
    explicit SingleInstanceGuard(QString name, QObject* parent = nullptr);

    /// True when this process owns the lock and is the primary instance.
    [[nodiscard]] bool isPrimary() const noexcept { return owns_; }

    /**
     * @brief Connect to an existing primary instance and ask it to show.
     *
     * @param name      Socket / pipe name to probe.
     * @param timeoutMs Per-step deadline (connect, write, flush).
     * @return true if a primary was found and the "show" request was sent;
     *         false if no primary was found (caller should become primary).
     */
    [[nodiscard]] static bool tryActivateExisting(QString const& name, int timeoutMs = 200);

    /// Stable, per-user socket name suitable for both QLocalServer/QLocalSocket.
    [[nodiscard]] static QString defaultSocketName();

signals:
    /// Emitted on the GUI thread when a secondary launch asks to be shown.
    void showRequested();

private:
    QString name_;
    QLocalServer server_;
    bool owns_{false};
};

} // namespace ajazz::app
