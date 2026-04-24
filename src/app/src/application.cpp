// SPDX-License-Identifier: GPL-3.0-or-later
#include "application.hpp"

#include "ajazz/core/logger.hpp"
#include "ajazz/keyboard/keyboard.hpp"
#include "ajazz/mouse/mouse.hpp"
#include "ajazz/streamdeck/streamdeck.hpp"

#include <QQmlApplicationEngine>
#include <QQmlContext>

namespace ajazz::app {

Application::Application(QObject* parent)
    : QObject(parent), m_deviceModel(std::make_unique<DeviceModel>(this)),
      m_profileController(std::make_unique<ProfileController>(this)) {}

Application::~Application() = default;

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
}

} // namespace ajazz::app
