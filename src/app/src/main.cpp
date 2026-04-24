// SPDX-License-Identifier: GPL-3.0-or-later
#include "application.hpp"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>

int main(int argc, char* argv[]) {
    QGuiApplication::setOrganizationName("AJAZZ Control Center");
    QGuiApplication::setOrganizationDomain("github.com/Aiacos/ajazz-control-center");
    QGuiApplication::setApplicationName("AJAZZ Control Center");
    QGuiApplication::setApplicationVersion("0.1.0");

    QGuiApplication app(argc, argv);
    QQuickStyle::setStyle("Fusion");

    ajazz::app::Application controller;
    controller.bootstrap();

    QQmlApplicationEngine engine;
    controller.exposeToQml(engine);
    engine.loadFromModule("AjazzControlCenter", "Main");
    if (engine.rootObjects().isEmpty()) {
        return -1;
    }
    return app.exec();
}
