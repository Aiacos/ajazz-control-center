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

#include <QHash>
#include <QIcon>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QtQmlIntegration>

#include <type_traits>

class QAction;

class QJSEngine;
class QQmlApplicationEngine;
class QQmlEngine;
class QSystemTrayIcon;
class QMenu;

namespace ajazz::app {

class BrandingService;
class BatteryService;
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
    QML_NAMED_ELEMENT(Tray)
    QML_SINGLETON
    Q_PROPERTY(bool startMinimized READ startMinimized WRITE setStartMinimized NOTIFY
                   startMinimizedChanged)
    Q_PROPERTY(bool trayAvailable READ trayAvailable CONSTANT)

public:
    /// QML singleton factory — see BrandingService::create for the pattern.
    static TrayController* create(QQmlEngine* qml, QJSEngine* js);

    /// Hand the singleton instance to the QML factory.
    static void registerInstance(TrayController* instance) noexcept;

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
     * @brief Forward battery service updates to the tray presentation.
     *
     * @param service Application-owned service that emits per-device readings.
     */
    void trackBatteryService(BatteryService* service);

    /**
     * @brief Format the latest readings for all battery-capable devices.
     *
     * @return One device and percentage per line, or an empty string if no
     *         current readings are available.
     */
    [[nodiscard]] QString batterySummary() const;

    /**
     * @brief Store a reading and refresh the tray tooltip and menu summary.
     *
     * Values below 20 percent are labelled as low battery. A warning
     * notification is shown only when a device first crosses below that level.
     *
     * @param deviceName Device codename emitted by @ref BatteryService.
     * @param percent Battery charge, clamped to 0..100.
     */
    Q_INVOKABLE void setDeviceBattery(QString const& deviceName, int percent);

    /**
     * @brief Remove a reading after the service reports the device unavailable.
     * @param deviceName Device codename used by BatteryService.
     */
    Q_INVOKABLE void clearDeviceBattery(QString const& deviceName);

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

    /// Emitted after the stored per-device battery state changes.
    void batterySummaryChanged(QString const& summary);

private:
    /// Build the tray context menu (Show / Pause / Switch / Quit).
    void buildMenu();

    /// Refresh the Switch-profile submenu after ProfileController changes.
    void rebuildProfileSubmenu();

    /// Refresh tooltip and menu status from the cached readings.
    void refreshBatteryPresentation();

    BrandingService* branding_;
    ProfileController* profiles_;
    QPointer<QSystemTrayIcon> tray_;
    QIcon baseTrayIcon_;
    QPointer<QMenu> menu_;
    QPointer<QMenu> profileMenu_;
    QPointer<QAction> pauseAction_;
    QPointer<QAction> batteryAction_;
    bool startMinimized_;
    bool paused_{false};
    QHash<QString, int> deviceBattery_; ///< Device codename -> latest percent.
};

// QML_SINGLETON dual-instance trap: TrayController stays safe today only
// because the required `BrandingService*` first ctor parameter has no
// default value. Defaulting it (or making `profiles` the first parameter)
// silently re-arms the bug — Qt 6 picks Constructor over Factory and
// spawns a duplicate QML-side instance. See BrandingService / e221b21.
static_assert(!std::is_default_constructible_v<TrayController>,
              "TrayController must not be default-constructible — see BrandingService.");

} // namespace ajazz::app
