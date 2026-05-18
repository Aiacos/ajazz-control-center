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

#if defined(Q_OS_WIN)
#include <QSettings>
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

#if defined(Q_OS_MACOS)
/// Path to the per-user LaunchAgent plist for our app. The macOS launchd
/// reads ~/Library/LaunchAgents/<label>.plist on login and either honors
/// the RunAtLoad key (launches the bundle once at login) or, when KeepAlive
/// is set, restarts it on crash. We deliberately stop at RunAtLoad: this
/// service is a login-time autolaunch, not a permanent supervisor.
[[nodiscard]] QString launchAgentPath() {
    QDir const dir(QDir::homePath() + QStringLiteral("/Library/LaunchAgents"));
    if (!dir.exists()) {
        dir.mkpath(QStringLiteral("."));
    }
    return dir.filePath(QStringLiteral(AJAZZ_APP_ID) + QStringLiteral(".plist"));
}

[[nodiscard]] QString launchAgentContents(bool startMinimised) {
    QString const path = QCoreApplication::applicationFilePath();
    QString const arg =
        startMinimised ? QStringLiteral("        <string>--minimized</string>\n") : QString{};
    // Conservative XML: only the keys we need (Label / ProgramArguments /
    // RunAtLoad / ProcessType / KeepAlive false). Encoded as UTF-8.
    return QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                          "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\""
                          " \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
                          "<plist version=\"1.0\">\n"
                          "<dict>\n"
                          "    <key>Label</key>\n"
                          "    <string>%1</string>\n"
                          "    <key>ProgramArguments</key>\n"
                          "    <array>\n"
                          "        <string>%2</string>\n"
                          "%3"
                          "    </array>\n"
                          "    <key>RunAtLoad</key>\n"
                          "    <true/>\n"
                          "    <key>KeepAlive</key>\n"
                          "    <false/>\n"
                          "    <key>ProcessType</key>\n"
                          "    <string>Interactive</string>\n"
                          "</dict>\n"
                          "</plist>\n")
        .arg(QStringLiteral(AJAZZ_APP_ID), path, arg);
}
#endif

#if defined(Q_OS_WIN)
/// Per-user HKCU registry key Windows uses for user-mode autostart. The
/// system reads `HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Run`
/// at login and launches every entry. We use NativeFormat so QSettings
/// writes the actual registry (not a flat INI), and quote the .exe path
/// to survive spaces.
[[nodiscard]] QString runKey() {
    return QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run");
}

[[nodiscard]] QString runValueName() {
    return QStringLiteral(AJAZZ_PRODUCT_NAME);
}

[[nodiscard]] QString runValueData(bool startMinimised) {
    // Path needs to be wrapped in double quotes so Windows's argv parser
    // keeps a space-containing path as a single argv[0]. Append the
    // --minimized flag outside the quotes.
    QString const path = QCoreApplication::applicationFilePath();
    QString out;
    out.append(QLatin1Char('"'));
    out.append(QDir::toNativeSeparators(path));
    out.append(QLatin1Char('"'));
    if (startMinimised) {
        out.append(QStringLiteral(" --minimized"));
    }
    return out;
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
#elif defined(Q_OS_MACOS)
    auto const path = launchAgentPath();
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
        out.setEncoding(QStringConverter::Utf8);
        out << launchAgentContents(startMinimised_);
        AJAZZ_LOG_INFO("autostart", "registered LaunchAgent {}", path.toStdString());
    } else if (QFile::exists(path)) {
        QFile::remove(path);
        AJAZZ_LOG_INFO("autostart", "removed LaunchAgent {}", path.toStdString());
    }
#elif defined(Q_OS_WIN)
    QSettings reg(runKey(), QSettings::NativeFormat);
    if (launchOnLogin_) {
        reg.setValue(runValueName(), runValueData(startMinimised_));
        reg.sync();
        if (reg.status() != QSettings::NoError) {
            AJAZZ_LOG_WARN("autostart",
                           "failed to write HKCU Run value '{}': QSettings status {}",
                           runValueName().toStdString(),
                           static_cast<int>(reg.status()));
            return;
        }
        AJAZZ_LOG_INFO("autostart", "registered HKCU Run value '{}'", runValueName().toStdString());
    } else {
        reg.remove(runValueName());
        reg.sync();
        AJAZZ_LOG_INFO("autostart", "removed HKCU Run value '{}'", runValueName().toStdString());
    }
#else
    // Other UNIX (BSD etc) without freedesktop / launchd / Windows
    // surface — log and persist the user preference only.
    AJAZZ_LOG_INFO("autostart",
                   "OS back-end not available on this platform; preference saved only");
#endif
}

} // namespace ajazz::app
