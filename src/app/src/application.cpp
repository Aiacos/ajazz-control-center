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
      m_deviceModel(std::make_unique<DeviceModel>(this)),
      m_profileController(std::make_unique<ProfileController>(this)),
      m_trayController(std::make_unique<TrayController>(m_branding.get(), this)),
      m_hotplug(std::make_unique<core::HotplugMonitor>()) {}

Application::~Application() {
    if (m_hotplug) {
        m_hotplug->stop();
    }
}

void Application::bootstrap() {
    core::setLogLevel(core::LogLevel::Info);

    streamdeck::registerAll();
    keyboard::registerAll();
    mouse::registerAll();

    m_deviceModel->refresh();
    AJAZZ_LOG_INFO("app",
                   "bootstrap complete: {} supported devices",
                   static_cast<int>(m_deviceModel->rowCount()));
}

void Application::exposeToQml(QQmlApplicationEngine& engine) {
    engine.rootContext()->setContextProperty("deviceModel", m_deviceModel.get());
    engine.rootContext()->setContextProperty("profileController", m_profileController.get());
    engine.rootContext()->setContextProperty("branding", m_branding.get());
    engine.rootContext()->setContextProperty("themeService", m_themeService.get());
    engine.rootContext()->setContextProperty("tray", m_trayController.get());
}

void Application::startBackgroundServices(QQmlApplicationEngine& engine) {
    // Tray must be created after the QML engine has loaded the root window so
    // the menu's Show/Hide actions have a window to operate on.
    m_trayController->ensureTray(&engine);

    // Quit signal: route to the global Qt application so we shut down cleanly
    // even when the main window is hidden to the tray.
    QObject::connect(
        m_trayController.get(), &TrayController::quitRequested, qApp, &QCoreApplication::quit);

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
