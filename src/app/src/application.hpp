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

#include "ajazz/core/device_registry.hpp"
#include "autostart_service.hpp"
#include "branding_service.hpp"
#include "device_model.hpp"
#include "loaded_plugins_model.hpp"
#include "plugin_catalog_model.hpp"
#include "profile_controller.hpp"
#include "property_inspector_controller.hpp"
#include "theme_service.hpp"
#include "tray_controller.hpp"

#include <QObject>

#include <memory>

class QQmlApplicationEngine;

namespace ajazz::core {
class HotplugMonitor;
struct HotplugEvent;
} // namespace ajazz::core

#ifdef AJAZZ_PYTHON_HOST
namespace ajazz::plugins {
class IPluginHost;
} // namespace ajazz::plugins
#endif

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

    /// Owned device registry; injected into the DeviceModel and into every
    /// `registerAll(DeviceRegistry&)` backend bootstrap. Audit finding A1
    /// replaced the previous Meyers-singleton with this owned instance.
    [[nodiscard]] core::DeviceRegistry& deviceRegistry() noexcept { return m_deviceRegistry; }

private:
    /// Forwarded to DeviceModel when the hot-plug monitor sees a change.
    void onHotplug(core::HotplugEvent const& ev);

#ifdef AJAZZ_PYTHON_HOST
    /// Spawn the @c OutOfProcessPluginHost, register the user-level
    /// search path (XDG @c AppLocalDataLocation @c /plugins) and pull
    /// the initial inventory into @c m_loadedPlugins. Failure is
    /// logged + swallowed: the rest of the app keeps running, the
    /// "Loaded" drawer just shows the empty state.
    void initPluginHost();
#endif

    /// Audit finding A1 — registry is constructor-owned, not a singleton.
    /// Declared first so members further down (DeviceModel) can hold a
    /// reference to it that is guaranteed to outlive them.
    core::DeviceRegistry m_deviceRegistry;

    std::unique_ptr<BrandingService> m_branding;            ///< Theme + product strings.
    std::unique_ptr<ThemeService> m_themeService;           ///< Light / dark / auto switcher.
    std::unique_ptr<AutostartService> m_autostart;          ///< Launch-at-login toggle (#35).
    std::unique_ptr<DeviceModel> m_deviceModel;             ///< List model of registered devices.
    std::unique_ptr<ProfileController> m_profileController; ///< Profile load/save controller.
    std::unique_ptr<TrayController> m_trayController;       ///< System tray icon + menu.
    std::unique_ptr<PluginCatalogModel> m_pluginCatalog; ///< Plugin Store catalogue (mock for now).
    std::unique_ptr<LoadedPluginsModel>
        m_loadedPlugins; ///< Runtime loaded-plugins surface (SEC-003 #51).
    std::unique_ptr<PropertyInspectorController>
        m_propertyInspector; ///< Plugin HTML PI host (Qt WebEngine, optional).
    std::unique_ptr<core::HotplugMonitor> m_hotplug; ///< USB arrival/removal watcher.
#ifdef AJAZZ_PYTHON_HOST
    /// Long-lived plugin host. Spawns the Python child + invokes the
    /// Ed25519 verifier so @c LoadedPluginsModel rows carry trust
    /// state. nullptr when the host failed to spawn (e.g. missing
    /// Python or `cryptography`); the UI then shows "no plugins".
    std::unique_ptr<plugins::IPluginHost> m_pluginHost;
#endif
};

} // namespace ajazz::app
