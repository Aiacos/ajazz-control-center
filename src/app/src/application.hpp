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

#include "ajazz/core/action_engine.hpp"
#include "ajazz/core/device_registry.hpp"
#include "app_update_service.hpp"
#include "autostart_service.hpp"
#include "battery_service.hpp"
#include "branding_service.hpp"
#include "builtin_actions_service.hpp"
#include "device_model.hpp"
#include "firmware_update_service.hpp"
#include "lighting_service.hpp"
#include "loaded_plugins_model.hpp"
#include "plugin_catalog_model.hpp"
#include "plugin_debug_service.hpp"
#include "profile_controller.hpp"
#include "property_inspector_controller.hpp"
#include "qt_executor.hpp"
#include "settings_service.hpp"
#include "stream_dock_control_service.hpp"
#include "stream_dock_input_service.hpp"

#ifdef AJAZZ_HAVE_WEBSOCKETS
#include "plugin_device_bridge.hpp"
#include "plugin_manager.hpp"
#include "sd_plugin_server.hpp"
#endif
#include "theme_service.hpp"
#include "time_sync_service.hpp"
#include "tray_controller.hpp"

#include <QObject>
#include <QString>

#include <memory>

class QQmlApplicationEngine;

namespace ajazz::core {
class HotplugMonitor;
struct HotplugEvent;
class RingBufferSink;
} // namespace ajazz::core

namespace ajazz::app {
class HotplugDebouncer;
class DebugControlServer;
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

    /// In-memory ring of the most recent log records from every source (the
    /// project's AJAZZ_LOG_* and Qt's qDebug/qCDebug, unified in bootstrap()).
    /// Backs the out-of-process debug log stream; valid and non-null after
    /// bootstrap().
    [[nodiscard]] core::RingBufferSink* logRing() const noexcept { return m_logRing.get(); }

    /// Absolute path of the append-only log file written since bootstrap().
    [[nodiscard]] QString const& logFilePath() const noexcept { return m_logFilePath; }

    // ---- Subsystem accessors for the debug control facade ----------------
    // Non-owning; valid for the Application lifetime. Used by
    // registerDebugControlMethods() to expose control over each subsystem.
    [[nodiscard]] StreamDockControlService* streamDockControl() const noexcept {
        return m_streamDockControl.get();
    }
    [[nodiscard]] StreamDockInputService* streamDockInput() const noexcept {
        return m_streamDockInput.get();
    }
    [[nodiscard]] PluginDebugService* pluginDebug() const noexcept { return m_pluginDebug.get(); }
    [[nodiscard]] ProfileController* profileController() const noexcept {
        return m_profileController.get();
    }
    [[nodiscard]] BuiltinActionsService* builtinActions() const noexcept {
        return m_builtinActions.get();
    }
#ifdef AJAZZ_HAVE_WEBSOCKETS
    [[nodiscard]] SdPluginServer* pluginServer() const noexcept { return m_pluginServer.get(); }
    /// Live .sdPlugin discover/spawn manager (debug channel: plugin.rediscover).
    [[nodiscard]] PluginManager* pluginManager() const noexcept { return m_pluginManager.get(); }
#endif
    /// Plugin Store catalogue / install pipeline (debug channel:
    /// plugin.installFromFile). Always present (unguarded).
    [[nodiscard]] PluginCatalogModel* pluginCatalog() const noexcept {
        return m_pluginCatalog.get();
    }

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
    std::unique_ptr<AppUpdateService>
        m_appUpdate; ///< 2026-05-18: GitHub-Releases-driven in-app update checker
                     ///< (notify-only per docs/architecture/APP-AUTO-UPDATE.md).
                     ///< Self-disables when FLATPAK_ID is set; otherwise polls
                     ///< the api.github.com /releases/latest endpoint every 24 h
                     ///< and surfaces a Material banner in Main.qml when a newer
                     ///< tag is observed. Declared after m_battery so the init
                     ///< list (constructed after m_battery in application.cpp)
                     ///< stays in member-declaration order, satisfying GCC's
                     ///< -Wreorder on the Linux CI matrix.
    std::unique_ptr<FirmwareUpdateService>
        m_firmwareUpdate; ///< Vendor firmware-update deep-link (detect + launch the
                          ///< vendor tool, or open the download page). Read-only /
                          ///< delegate-only per docs/architecture/FIRMWARE-UPDATES.md;
                          ///< no in-app flashing. Declared after m_appUpdate to keep
                          ///< the init list in member-declaration order (-Wreorder).
    std::unique_ptr<StreamDockControlService>
        m_streamDockControl; ///< Phase 14: app-layer Stream Dock panel control
                             ///< (DISPLAY-06/07/08, DOCK-01/02). Holds the active
                             ///< AKP05E open for the session, lights the panel at
                             ///< open (LIG), pushes key images via a coalesced QTimer
                             ///< drain (BAT->chunks->ULEND, no manual flush), repaints
                             ///< all keys on profileChanged, and surfaces the cached
                             ///< firmware VER string. Declared after m_firmwareUpdate
                             ///< to keep the init list in member-declaration order
                             ///< (-Wreorder). Reuse surface for Phases 15/16/19.
    // Phase 15 Plan 15-02 — ActionEngine + QtExecutor + StreamDockInputService.
    // Declaration order matches the init-list construction order below; GCC
    // -Wreorder is -Werror so these three members MUST stay in this sequence
    // (qtExecutor -> actionEngine -> streamDockInput) and after m_streamDockControl
    // so the input service can receive the control service's held handle on arrival.
    //
    // m_qtExecutor must outlive m_actionEngine (qt_executor.hpp lifetime note).
    // m_actionEngine must outlive m_streamDockInput (the service calls engine->run()).
    std::unique_ptr<QtExecutor>
        m_qtExecutor; ///< Phase 15: non-blocking Executor for ActionEngine Sleep steps
                      ///< (audit A2 — prevents Sleep from blocking the HID poll thread).
                      ///< Owned here so it outlives m_actionEngine.
    std::unique_ptr<core::ActionEngine>
        m_actionEngine; ///< Phase 15: first ActionEngine instantiation in the app.
                        ///< Constructed with the real app ActionExecutors (keyPress
                        ///< stub / runCommand QProcess::startDetached / openUrl
                        ///< QDesktopServices / plugin stub) and m_qtExecutor as the
                        ///< Executor so Sleep defers via QTimer (T-15-04 mitigated).
                        ///< Phase 19 replaces the plugin stub with the real bridge.
    std::unique_ptr<StreamDockInputService>
        m_streamDockInput; ///< Phase 15: app-layer input dispatch service (INPUT-03/04/05).
                           ///< Holds the same shared_ptr<IDevice> as m_streamDockControl
                           ///< (ARCH-03 single-handle invariant — no second open()). Pumps
                           ///< poll() on an 8 ms QTimer and routes DeviceEvents to bound
                           ///< ActionChains in the active Profile via m_actionEngine.
    std::unique_ptr<BuiltinActionsService>
        m_builtinActions; ///< Phase 21-03 (PLUGIN-12): built-in in-process action dispatcher.
                          ///< Populates the BuiltinActionRegistry with the ~24 locked
                          ///< com.hotspot.streamdock.* UUID handlers and replaces the Phase-15
                          ///< plugin executor stub with the registry short-circuit (handles ->
                          ///< dispatch; non-builtin UUIDs forward to the Phase-19 path).
                          ///< Declared after m_streamDockInput to keep the init list in
                          ///< member-declaration order (-Wreorder).
#ifdef AJAZZ_HAVE_WEBSOCKETS
    std::unique_ptr<SdPluginServer>
        m_pluginServer; ///< Phase 17: Elgato-compatible WebSocket plugin server (loopback-only).
                        ///< Declared after m_streamDockInput to keep the init list in
                        ///< member-declaration order (-Wreorder).
    std::unique_ptr<PluginDeviceBridge>
        m_pluginBridge; ///< Phase 19-02: bridge connecting SdPluginServer::actionReceived
                        ///< to StreamDockControlService::assignKeyImage (setImage inbound
                        ///< round-trip, PLUGIN-10). Declared after m_pluginServer to stay
                        ///< in construction order (-Wreorder). Non-owning seam pointers
                        ///< point at server/control/input (all Application members with
                        ///< longer lifetimes per member-declaration order).
    std::unique_ptr<PluginManager>
        m_pluginManager; ///< Elgato .sdPlugin (node/html/native) discover + spawn +
                         ///< crash lifecycle. Constructed in startBackgroundServices()
                         ///< AFTER m_pluginServer is listening (spawn() reads its port).
                         ///< Declared last so it is destroyed first (shutdown() sends
                         ///< exitApp to children before the server/bridge tear down).
#endif
    /// Developer debug console: logs plugin/device protocol traffic and injects
    /// simulated device input + plugin->host actions. Always present (logging
    /// works without WebSockets); declared after the plugin server/bridge so
    /// its non-owning taps outlive nothing (destroyed before them).
    std::unique_ptr<PluginDebugService> m_pluginDebug;

    /// Unified log ring (stderr+file+ring tee installed in bootstrap()).
    /// shared_ptr because the same instance is held by the active core
    /// LogSink; this handle lets logRing() expose its tail to the debug
    /// control channel. The accompanying file path is retained for reporting.
    std::shared_ptr<core::RingBufferSink> m_logRing;
    QString m_logFilePath;

    /// Opt-in out-of-process control channel (Unix domain socket JSON-RPC).
    /// Null unless AJAZZ_DEBUG_CONTROL was set at launch; constructed and
    /// started last in startBackgroundServices() so its handlers can reach
    /// every other subsystem.
    std::unique_ptr<DebugControlServer> m_debugControl;

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
