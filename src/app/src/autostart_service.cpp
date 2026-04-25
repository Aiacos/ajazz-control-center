// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file autostart_service.cpp
 * @brief Implementation of @ref ajazz::app::AutostartService.
 *
 * Closes #35.
 */
#include "autostart_service.hpp"

#include "ajazz/core/logger.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QStandardPaths>
#include <QTextStream>

#ifndef AJAZZ_APP_ID
#define AJAZZ_APP_ID "io.github.Aiacos.AjazzControlCenter"
#endif
#ifndef AJAZZ_PRODUCT_NAME
#define AJAZZ_PRODUCT_NAME "AJAZZ Control Center"
#endif

namespace ajazz::app {

namespace {

/// Fully-qualified name of the OS-level autostart entry.
[[nodiscard]] QString autostartEntryName() {
    return QStringLiteral(AJAZZ_APP_ID);
}

#if defined(Q_OS_LINUX)
[[nodiscard]] QString xdgAutostartPath() {
    QDir const dir(QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) +
                   QStringLiteral("/autostart"));
    if (!dir.exists()) {
        dir.mkpath(QStringLiteral("."));
    }
    return dir.filePath(autostartEntryName() + QStringLiteral(".desktop"));
}

[[nodiscard]] QString desktopEntryContents(bool startMinimised) {
    QString const exec = QCoreApplication::applicationFilePath() +
                         (startMinimised ? QStringLiteral(" --minimized") : QString{});
    return QStringLiteral("[Desktop Entry]\n"
                          "Type=Application\n"
                          "Name=%1\n"
                          "Exec=%2\n"
                          "Terminal=false\n"
                          "X-GNOME-Autostart-enabled=true\n")
        .arg(QStringLiteral(AJAZZ_PRODUCT_NAME), exec);
}
#endif

} // namespace

AutostartService::AutostartService(QObject* parent) : QObject(parent) {
    QSettings settings;
    startMinimised_ = settings.value(QStringLiteral("autostart/startMinimised"), true).toBool();
    launchOnLogin_ = settings.value(QStringLiteral("autostart/launchOnLogin"), false).toBool();
}

void AutostartService::setLaunchOnLogin(bool enabled) {
    if (launchOnLogin_ == enabled) {
        return;
    }
    launchOnLogin_ = enabled;
    QSettings settings;
    settings.setValue(QStringLiteral("autostart/launchOnLogin"), enabled);
    applyToOs();
    Q_EMIT launchOnLoginChanged(enabled);
}

void AutostartService::setStartMinimised(bool enabled) {
    if (startMinimised_ == enabled) {
        return;
    }
    startMinimised_ = enabled;
    QSettings settings;
    settings.setValue(QStringLiteral("autostart/startMinimised"), enabled);
    if (launchOnLogin_) {
        applyToOs(); // Refresh the .desktop Exec line with the new flag.
    }
    Q_EMIT startMinimisedChanged(enabled);
}

void AutostartService::applyToOs() {
#if defined(Q_OS_LINUX)
    auto const path = xdgAutostartPath();
    if (launchOnLogin_) {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            AJAZZ_LOG_WARN("autostart",
                           "cannot write {}: {}",
                           path.toStdString(),
                           file.errorString().toStdString());
            return;
        }
        QTextStream out(&file);
        out << desktopEntryContents(startMinimised_);
        AJAZZ_LOG_INFO("autostart", "registered {}", path.toStdString());
    } else if (QFile::exists(path)) {
        QFile::remove(path);
        AJAZZ_LOG_INFO("autostart", "removed {}", path.toStdString());
    }
#else
    // macOS / Windows back-ends are TODO; surface a debug log so the
    // settings UI still toggles cleanly.
    AJAZZ_LOG_INFO("autostart", "OS back-end not yet implemented; preference saved only");
#endif
}

} // namespace ajazz::app
