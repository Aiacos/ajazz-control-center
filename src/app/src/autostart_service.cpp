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
#include <QQmlEngine>
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

#if defined(Q_OS_LINUX)
/// Fully-qualified name of the OS-level autostart entry.
[[nodiscard]] QString autostartEntryName() {
    return QStringLiteral(AJAZZ_APP_ID);
}

[[nodiscard]] QString xdgAutostartPath() {
    QDir const dir(QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) +
                   QStringLiteral("/autostart"));
    if (!dir.exists()) {
        dir.mkpath(QStringLiteral("."));
    }
    return dir.filePath(autostartEntryName() + QStringLiteral(".desktop"));
}

/**
 * @brief Quote a string for a Desktop Entry Spec `Exec=` value.
 *
 * Per Desktop Entry Specification §1.5: arguments containing reserved
 * characters (space, tab, double-quote, backslash, dollar, backtick, etc.)
 * must be enclosed in double quotes; inside the quotes, backslash, double
 * quote, dollar sign and backtick must each be backslash-escaped.
 *
 * Without this, an installation path containing a space (e.g.
 * `/opt/AJAZZ Control Center/bin/ajazz`) splits at the space and the
 * launcher tries to run `/opt/AJAZZ` with the rest as arguments — and
 * paths containing `;` or shell metacharacters could be parsed as
 * subsequent commands.
 */
[[nodiscard]] QString quoteForDesktopExec(QString const& s) {
    QString out;
    out.reserve(s.size() + 2);
    out.append(QLatin1Char('"'));
    for (QChar const c : s) {
        if (c == QLatin1Char('"') || c == QLatin1Char('\\') || c == QLatin1Char('$') ||
            c == QLatin1Char('`')) {
            out.append(QLatin1Char('\\'));
        }
        out.append(c);
    }
    out.append(QLatin1Char('"'));
    return out;
}

[[nodiscard]] QString desktopEntryContents(bool startMinimised) {
    QString const path = QCoreApplication::applicationFilePath();
    if (path.contains(QLatin1Char('\n')) || path.contains(QLatin1Char('\r'))) {
        // A newline in the binary path would corrupt the .desktop file
        // structure (newlines are key/value separators). Refuse to write a
        // malformed entry; the caller turns autostart off.
        AJAZZ_LOG_ERROR("autostart", "binary path contains a newline; refusing to write .desktop");
        return {};
    }
    QString const exec =
        quoteForDesktopExec(path) + (startMinimised ? QStringLiteral(" --minimized") : QString{});
    return QStringLiteral("[Desktop Entry]\n"
                          "Type=Application\n"
                          "Name=%1\n"
                          "Exec=%2\n"
                          "Terminal=false\n"
                          "X-GNOME-Autostart-enabled=true\n")
        .arg(QStringLiteral(AJAZZ_PRODUCT_NAME), exec);
}
#endif

/// Pointer set by AutostartService::registerInstance, consumed by ::create.
AutostartService* s_autostartInstance = nullptr;

} // namespace

AutostartService* AutostartService::create(QQmlEngine* /*qml*/, QJSEngine* /*js*/) {
    Q_ASSERT_X(s_autostartInstance != nullptr,
               "AutostartService::create",
               "registerInstance() must be called before the QML engine loads");
    QQmlEngine::setObjectOwnership(s_autostartInstance, QQmlEngine::CppOwnership);
    return s_autostartInstance;
}

void AutostartService::registerInstance(AutostartService* instance) noexcept {
    s_autostartInstance = instance;
}

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
