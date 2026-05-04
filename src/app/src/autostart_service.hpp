// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file autostart_service.hpp
 * @brief Cross-platform launch-on-login + start-minimised toggles.
 *
 * Closes #35 (autostart) and complements the persistent user instruction
 * "avviarsi ridotto ad icona (default) e il riconoscimento automation delle
 * periferiche".
 *
 * Backends:
 *   - Linux : XDG `~/.config/autostart/<vendor-id>.desktop`.
 *   - macOS : `~/Library/LaunchAgents/<vendor-id>.plist` (LaunchAgent).
 *   - Windows : `HKCU\Software\Microsoft\Windows\CurrentVersion\Run` value.
 *
 * The class is a thin Q_OBJECT wrapper so the QML layer can bind the
 * "Launch on login" / "Start minimised" check-boxes directly.
 */
#pragma once

#include <QObject>
#include <QString>
#include <QtQmlIntegration>

class QJSEngine;
class QQmlEngine;

namespace ajazz::app {

/**
 * @brief Manages the user's "launch at login" and "start minimised"
 *        preferences and reflects them onto the host OS.
 */
class AutostartService : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(Autostart)
    QML_SINGLETON
    Q_PROPERTY(
        bool launchOnLogin READ launchOnLogin WRITE setLaunchOnLogin NOTIFY launchOnLoginChanged)
    Q_PROPERTY(bool startMinimised READ startMinimised WRITE setStartMinimised NOTIFY
                   startMinimisedChanged)

public:
    // No default on `parent`: see BrandingService — a default-constructible
    // QML_SINGLETON makes Qt 6 pick `Constructor` mode and silently bypass
    // the static `create()` factory, spawning a duplicate QML-side instance.
    explicit AutostartService(QObject* parent);

    /// QML singleton factory — see BrandingService::create for the pattern.
    static AutostartService* create(QQmlEngine* qml, QJSEngine* js);

    /// Hand the singleton instance to the QML factory.
    static void registerInstance(AutostartService* instance) noexcept;

    /// @return true if launch-on-login is currently enabled in the OS.
    [[nodiscard]] bool launchOnLogin() const noexcept { return launchOnLogin_; }

    /// @return true if the app should start minimised to the tray.
    [[nodiscard]] bool startMinimised() const noexcept { return startMinimised_; }

public Q_SLOTS:
    /**
     * @brief Toggle the launch-on-login OS hook.
     *
     * On Linux this writes / removes a `.desktop` entry under
     * `~/.config/autostart`. The path is derived from
     * `AJAZZ_APP_ID` (CMake-provided) so branded builds get a unique
     * autostart entry without colliding with the upstream binary.
     */
    void setLaunchOnLogin(bool enabled);

    /// Toggle the start-minimised user preference (QSettings-backed).
    void setStartMinimised(bool enabled);

Q_SIGNALS:
    void launchOnLoginChanged(bool enabled);
    void startMinimisedChanged(bool enabled);

private:
    /// Apply the OS-specific representation of `launchOnLogin_`.
    void applyToOs();

    bool launchOnLogin_{false};
    bool startMinimised_{true};
};

} // namespace ajazz::app
