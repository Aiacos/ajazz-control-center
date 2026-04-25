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

class QQmlApplicationEngine;
class QSystemTrayIcon;
class QMenu;

namespace ajazz::app {

class BrandingService;

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
     * @param branding Provides the icon URL and tooltip product name. Must
     *                 outlive this object.
     */
    explicit TrayController(BrandingService* branding, QObject* parent = nullptr);

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

signals:
    /// Emitted when @ref startMinimized changes.
    void startMinimizedChanged();

    /// Emitted when the user picks "Show window" from the tray menu.
    void showWindowRequested();

    /// Emitted when the user picks "Quit" from the tray menu.
    void quitRequested();

private:
    /// Build the tray context menu (Show / Hide / Quit).
    void buildMenu();

    BrandingService* branding_;
    QPointer<QSystemTrayIcon> tray_;
    QPointer<QMenu> menu_;
    bool startMinimized_;
};

} // namespace ajazz::app
