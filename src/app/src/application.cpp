// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file application.cpp
 * @brief Application class implementation.
 *
 * Connects the backend bootstrap sequence (registerAll calls) to the QML
 * engine by forwarding DeviceModel, ProfileController, BrandingService and
 * TrayController as context properties. Also owns the cross-platform USB
 * hot-plug monitor and marshals its events back to the Qt main thread to
 * refresh the device list.
 */
#include "application.hpp"

#include "ajazz/core/hotplug_monitor.hpp"
#include "ajazz/core/logger.hpp"
#include "ajazz/keyboard/keyboard.hpp"
#include "ajazz/mouse/mouse.hpp"
#include "ajazz/streamdeck/streamdeck.hpp"

#include <QCoreApplication>
#include <QMetaObject>
#include <QQmlApplicationEngine>
#include <QQmlContext>

namespace ajazz::app {

Application::Application(QObject* parent)
    : QObject(parent), m_branding(std::make_unique<BrandingService>(this)),
      m_themeService(std::make_unique<ThemeService>(m_branding.get(), this)),
      m_autostart(std::make_unique<AutostartService>(this)),
      // Audit finding A1: the DeviceModel reads from this Application's
      // owned registry (`m_deviceRegistry`), not from a process-wide
      // singleton. The registry is declared first in the header so it
      // is constructed before — and destroyed after — the model that
      // holds a reference to it.
      m_deviceModel(std::make_unique<DeviceModel>(m_deviceRegistry, this)),
      m_profileController(std::make_unique<ProfileController>(this)),
      m_trayController(
          std::make_unique<TrayController>(m_branding.get(), m_profileController.get(), this)),
      m_pluginCatalog(std::make_unique<PluginCatalogModel>(this)),
      m_loadedPlugins(std::make_unique<LoadedPluginsModel>(this)),
      m_propertyInspector(std::make_unique<PropertyInspectorController>(this)),
      m_hotplug(std::make_unique<core::HotplugMonitor>()) {}

Application::~Application() {
    // Defensive shutdown ordering — the hot-plug worker queues
    // refresh() lambdas to m_deviceModel via Qt::QueuedConnection. Without
    // care, an in-flight queued event can fire during member destruction
    // and dereference an already-destroyed m_deviceModel.
    if (m_hotplug) {
        // 1. Block further callbacks before joining; an event firing after
        //    setCallback({}) returns is impossible by HotplugMonitor's contract.
        m_hotplug->setCallback({});
        // 2. Join the polling thread; no new events can be posted after this.
        m_hotplug->stop();
    }
    // 3. Drain events already in the main-thread queue that target
    //    m_deviceModel, so they cannot run after its unique_ptr destructor.
    if (m_deviceModel) {
        QCoreApplication::removePostedEvents(m_deviceModel.get());
    }
}

void Application::bootstrap() {
    core::setLogLevel(core::LogLevel::Info);

    // Audit finding A1: pass the owned registry into every backend
    // bootstrap rather than relying on the deprecated `instance()` shim.
    streamdeck::registerAll(m_deviceRegistry);
    keyboard::registerAll(m_deviceRegistry);
    mouse::registerAll(m_deviceRegistry);

    m_deviceModel->refresh();
    AJAZZ_LOG_INFO("app",
                   "bootstrap complete: {} supported devices",
                   static_cast<int>(m_deviceModel->rowCount()));
}

void Application::exposeToQml(QQmlApplicationEngine& engine) {
    // Services registered as QML singletons via QML_NAMED_ELEMENT + QML_SINGLETON.
    // Hand the app-owned instances to their factories before the engine loads.
    BrandingService::registerInstance(m_branding.get());
    ThemeService::registerInstance(m_themeService.get());
    AutostartService::registerInstance(m_autostart.get());
    TrayController::registerInstance(m_trayController.get());
    DeviceModel::registerInstance(m_deviceModel.get());
    ProfileController::registerInstance(m_profileController.get());
    PluginCatalogModel::registerInstance(m_pluginCatalog.get());
    LoadedPluginsModel::registerInstance(m_loadedPlugins.get());
    PropertyInspectorController::registerInstance(m_propertyInspector.get());
    // No more setContextProperty calls — every service is now a QML
    // singleton, statically resolvable by qmllint.
    Q_UNUSED(engine);
}

void Application::startBackgroundServices(QQmlApplicationEngine& engine) {
    // Tray must be created after the QML engine has loaded the root window so
    // the menu's Show/Hide actions have a window to operate on.
    m_trayController->ensureTray(&engine);

    // Quit signal: route to the global Qt application so we shut down cleanly
    // even when the main window is hidden to the tray.
    QObject::connect(
        m_trayController.get(), &TrayController::quitRequested, qApp, &QCoreApplication::quit);

    // Tray submenu "Switch profile": forward to the profile controller. The
    // controller owns the load semantics; the tray just emits the requested id.
    QObject::connect(m_trayController.get(),
                     &TrayController::profileSwitchRequested,
                     m_profileController.get(),
                     &ProfileController::loadProfileById);

    // USB hot-plug: callback runs on a background thread; marshal to the GUI
    // thread before touching the QAbstractListModel.
    m_hotplug->setCallback([this](core::HotplugEvent const& ev) { onHotplug(ev); });
    if (!m_hotplug->start()) {
        AJAZZ_LOG_INFO("app", "hot-plug monitor unavailable on this platform/session");
    }
}

void Application::onHotplug(core::HotplugEvent const& ev) {
    AJAZZ_LOG_INFO("app",
                   "hot-plug {}: {:04x}:{:04x}",
                   ev.action == core::HotplugAction::Arrived ? "+" : "-",
                   static_cast<int>(ev.vid),
                   static_cast<int>(ev.pid));
    // Refresh on the GUI thread.
    QMetaObject::invokeMethod(
        m_deviceModel.get(), [this]() { m_deviceModel->refresh(); }, Qt::QueuedConnection);
}

} // namespace ajazz::app
