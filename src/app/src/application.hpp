// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "device_model.hpp"
#include "profile_controller.hpp"

#include <QObject>
#include <memory>

class QQmlApplicationEngine;

namespace ajazz::app {

class Application : public QObject {
    Q_OBJECT
public:
    explicit Application(QObject* parent = nullptr);
    ~Application() override;

    /// Register all device backends and start the discovery loop.
    void bootstrap();

    /// Expose app controllers to QML under the `Ajazz` namespace.
    void exposeToQml(QQmlApplicationEngine& engine);

private:
    std::unique_ptr<DeviceModel>        m_deviceModel;
    std::unique_ptr<ProfileController>  m_profileController;
};

}  // namespace ajazz::app
