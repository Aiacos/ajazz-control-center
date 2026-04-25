// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file tray_controller.hpp
 * @brief System-tray icon plus startup-mode (window vs minimized) policy.
 *
 * Encapsulates the QSystemTrayIcon, a context menu, and the rule that decides
 * whether the app starts visible or minimized to the tray. Exposed to QML so
 * that the Main window can choose its initial visibility.
 */
#pragma once

#include <QObject>
#include <QPointer>
#include <QString>

class QAction;

class QQmlApplicationEngine;
class QSystemTrayIcon;
class QMenu;

namespace ajazz::app {

class BrandingService;
class ProfileController;

/**
 * @brief Owns the tray icon and surfaces the "start minimized" preference.
 *
 * Lifetime: owned by `Application`, lives for the whole process. The tray
 * icon is created lazily on the first call to @ref ensureTray() because some
 * Linux desktop environments need an active D-Bus session before
 * QSystemTrayIcon::isSystemTrayAvailable() returns true.
 *
 * QML reads @ref startMinimized to decide whether to call `Window.show()` or
 * `Window.hide()` at startup. Either way, the user can toggle the window
 * from the tray's context menu.
 */
class TrayController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool startMinimized READ startMinimized WRITE setStartMinimized NOTIFY
                   startMinimizedChanged)
    Q_PROPERTY(bool trayAvailable READ trayAvailable CONSTANT)

public:
    /**
     * @brief Construct the controller without yet creating the tray icon.
     *
     * @param branding Provides the icon URL and tooltip product name.
     *                 Must outlive this object.
     * @param profiles Optional ProfileController used to populate the
     *                 "Switch profile" submenu (#24). May be nullptr; the
     *                 submenu is hidden when not supplied.
     */
    explicit TrayController(BrandingService* branding,
                            ProfileController* profiles = nullptr,
                            QObject* parent = nullptr);

    ~TrayController() override;

    /// True if the OS reports a tray area is available right now.
    [[nodiscard]] bool trayAvailable() const noexcept;

    /**
     * @brief User preference: start the app minimized to tray.
     *
     * Default: `true`. Persisted in `QSettings("Window/StartMinimized")`.
     */
    [[nodiscard]] bool startMinimized() const noexcept { return startMinimized_; }

    void setStartMinimized(bool v);

    /**
     * @brief Create the tray icon if it does not yet exist.
     *
     * @param engine The QML engine — used to locate the main window so the
     *               tray menu can show/hide it.
     */
    void ensureTray(QQmlApplicationEngine* engine);

    /**
     * @brief Update the tray tooltip + warning glyph for low-battery devices.
     *
     * Closes #34 (battery awareness in tray). Called by ProfileController /
     * DeviceModel whenever a wireless device polls a fresh battery reading.
     *
     * @param deviceName User-visible name (e.g. "AJ339 Pro").
     * @param percent    0..100; values < 20 colour the tooltip with a warning
     *                   prefix and emit a single notification.
     */
    Q_INVOKABLE void setDeviceBattery(QString const& deviceName, int percent);

    /**
     * @brief Toggle the global "paused" state.
     *
     * When paused, the tray icon dims and the action engine drops every
     * incoming event. The tray menu's Pause/Resume entry mirrors the state.
     */
    Q_INVOKABLE void setPaused(bool paused);

    [[nodiscard]] bool paused() const noexcept { return paused_; }

signals:
    /// Emitted when @ref startMinimized changes.
    void startMinimizedChanged();

    /// Emitted when the user picks "Show window" from the tray menu.
    void showWindowRequested();

    /// Emitted when the user picks "Quit" from the tray menu.
    void quitRequested();

    /// Emitted when the paused state changes.
    void pausedChanged(bool paused);

    /// Emitted when the user picks a profile from the Switch-profile submenu.
    void profileSwitchRequested(QString const& profileId);

private:
    /// Build the tray context menu (Show / Pause / Switch / Quit).
    void buildMenu();

    /// Refresh the Switch-profile submenu after ProfileController changes.
    void rebuildProfileSubmenu();

    BrandingService* branding_;
    ProfileController* profiles_;
    QPointer<QSystemTrayIcon> tray_;
    QPointer<QMenu> menu_;
    QPointer<QMenu> profileMenu_;
    QPointer<QAction> pauseAction_;
    bool startMinimized_;
    bool paused_{false};
    QString batteryTooltip_; ///< Last battery summary sent via setDeviceBattery().
};

} // namespace ajazz::app
