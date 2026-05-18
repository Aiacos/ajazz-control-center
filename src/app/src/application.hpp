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
#include "battery_service.hpp"
#include "branding_service.hpp"
#include "device_model.hpp"
#include "lighting_service.hpp"
#include "loaded_plugins_model.hpp"
#include "plugin_catalog_model.hpp"
#include "profile_controller.hpp"
#include "property_inspector_controller.hpp"
#include "settings_service.hpp"
#include "theme_service.hpp"
#include "time_sync_service.hpp"
#include "tray_controller.hpp"

#include <QObject>

#include <memory>

class QQmlApplicationEngine;

namespace ajazz::core {
class HotplugMonitor;
struct HotplugEvent;
} // namespace ajazz::core

namespace ajazz::app {
class HotplugDebouncer;
} // namespace ajazz::app

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
    std::unique_ptr<TimeSyncService>
        m_timeSync; ///< Phase 5: per-row Sync time + auto-sync hook. Owns the
                    ///< DeviceLookup lambda that captures m_deviceRegistry by
                    ///< reference and (per A-04 / D-01 amendment 3) holds the
                    ///< shared_ptr<IDevice> in its own stack frame across the
                    ///< dynamic_cast → setTime sequence.
    std::unique_ptr<LightingService>
        m_lighting; ///< 2026-05-18: AK980 PRO 20-mode firmware lighting
                    ///< picker. Same DeviceLookup pattern as TimeSyncService;
                    ///< dynamic_cast to IFirmwareLightingCapable inside the
                    ///< service to enumerate / activate modes.
    std::unique_ptr<SettingsService>
        m_settings; ///< 2026-05-18: AK980 PRO ISettingsCapable bridge
                    ///< (issue #57). Same DeviceLookup pattern as
                    ///< LightingService; dynamic_cast to ISettingsCapable
                    ///< inside the service to push / read the AK-series
                    ///< settings batch (fn-layer / sleep / response).
    std::unique_ptr<BatteryService>
        m_battery; ///< 2026-05-18: per-device battery polling for wireless
                   ///< IBatteryCapable devices (AK980 PRO today). Owns a
                   ///< 15-s QTimer that calls a per-codename query lambda
                   ///< filtered by descriptor.hasBattery && connected.
                   ///< Surfaces results to QML via batteryQueried /
                   ///< batteryUnavailable; QML BatteryIndicator mounts in
                   ///< DeviceRow.
    std::unique_ptr<core::HotplugMonitor> m_hotplug; ///< USB arrival/removal watcher.

    /// Per-key 300ms trailing-edge debouncer for hot-plug events (D-05).
    ///
    /// Declaration order matters — the debouncer is declared **after**
    /// m_hotplug so it is destroyed **before** m_hotplug. This matches
    /// the runtime invariant we want at shutdown: stop the OS event
    /// source (m_hotplug) first, *then* tear down the debouncer (so
    /// any in-flight QTimer is freed without firing into a destroyed
    /// downstream consumer like m_deviceModel — which itself is
    /// declared earlier and thus destroyed even later).
    std::unique_ptr<HotplugDebouncer> m_debouncer;
#ifdef AJAZZ_PYTHON_HOST
    /// Long-lived plugin host. Spawns the Python child + invokes the
    /// Ed25519 verifier so @c LoadedPluginsModel rows carry trust
    /// state. nullptr when the host failed to spawn (e.g. missing
    /// Python or `cryptography`); the UI then shows "no plugins".
    std::unique_ptr<plugins::IPluginHost> m_pluginHost;
#endif
};

} // namespace ajazz::app
