// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file application.hpp
 * @brief Top-level application controller that owns all subsystems.
 *
 * Application is the single QObject that wires together the device registry,
 * the device model, and the profile controller. It is instantiated in main()
 * before the QML engine is created.
 *
 * @see DeviceModel, ProfileController
 */
#pragma once

#include "branding_service.hpp"
#include "device_model.hpp"
#include "profile_controller.hpp"
#include "tray_controller.hpp"

#include <QObject>

#include <memory>

class QQmlApplicationEngine;

namespace ajazz::core {
class HotplugMonitor;
} // namespace ajazz::core

namespace ajazz::app {

/**
 * @class Application
 * @brief Top-level controller that bootstraps all subsystems and owns them.
 *
 * Lifetime: created on the stack in main() before the Qt event loop starts.
 * All child QObjects receive `this` as their parent so they are destroyed
 * in the correct order when Application goes out of scope.
 *
 * @note Not thread-safe; must be used on the Qt main thread.
 */
class Application : public QObject {
    Q_OBJECT
public:
    explicit Application(QObject* parent = nullptr);
    ~Application() override;

    /**
     * @brief Register all device backends and populate the device model.
     *
     * Calls the registerAll() bootstrap function of every backend module
     * (streamdeck, keyboard, mouse), then triggers the initial device
     * enumeration and sets the default log level.
     */
    void bootstrap();

    /**
     * @brief Expose application controllers to a QML engine.
     *
     * Registers DeviceModel and ProfileController as context properties so
     * QML components can access them by name without importing a module.
     *
     * @param engine QML engine to attach the context properties to.
     */
    void exposeToQml(QQmlApplicationEngine& engine);

    /**
     * @brief Start the system tray icon and the USB hot-plug monitor.
     *
     * Must be called *after* the QML engine has loaded its root component
     * because the tray menu's "Show window" action needs a window to act on.
     *
     * @param engine The QML engine that owns the main window.
     */
    void startBackgroundServices(QQmlApplicationEngine& engine);

    /// Accessor used by main.cpp to honor the start-minimized preference.
    [[nodiscard]] TrayController* trayController() const noexcept { return m_trayController.get(); }

private:
    /// Forwarded to DeviceModel when the hot-plug monitor sees a change.
    void onHotplug(class core::HotplugEvent const& ev);

    std::unique_ptr<BrandingService> m_branding;            ///< Theme + product strings.
    std::unique_ptr<DeviceModel> m_deviceModel;             ///< List model of registered devices.
    std::unique_ptr<ProfileController> m_profileController; ///< Profile load/save controller.
    std::unique_ptr<TrayController> m_trayController;       ///< System tray icon + menu.
    std::unique_ptr<core::HotplugMonitor> m_hotplug;        ///< USB arrival/removal watcher.
};

} // namespace ajazz::app
