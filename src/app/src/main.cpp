// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file main.cpp
 * @brief Application entry point.
 *
 * Sets Qt application metadata from the compile-time branding defines, creates
 * the Application controller, bootstraps all device backends, loads the QML
 * root component, then starts the system tray and the USB hot-plug monitor.
 * If the user has set "start minimized" (default), the QML root window is
 * hidden until the user clicks the tray icon.
 */
#include "application.hpp"
#include "branding_service.hpp"
#include "tray_controller.hpp"

#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QWindow>

#ifndef AJAZZ_PRODUCT_NAME
#define AJAZZ_PRODUCT_NAME "AJAZZ Control Center"
#endif
#ifndef AJAZZ_VENDOR_NAME
#define AJAZZ_VENDOR_NAME "Aiacos"
#endif
#ifndef AJAZZ_APP_ID
#define AJAZZ_APP_ID "io.github.Aiacos.AjazzControlCenter"
#endif

int main(int argc, char* argv[]) {
    // Use QApplication (not QGuiApplication) because TrayController relies on
    // QSystemTrayIcon + QMenu which are part of the QtWidgets module.
    QApplication::setOrganizationName(AJAZZ_VENDOR_NAME);
    QApplication::setOrganizationDomain("github.com/Aiacos/ajazz-control-center");
    QApplication::setApplicationName(AJAZZ_PRODUCT_NAME);
    QApplication::setApplicationDisplayName(AJAZZ_PRODUCT_NAME);
    QApplication::setApplicationVersion("0.1.0");
    QApplication::setDesktopFileName(AJAZZ_APP_ID);
    // Keep running when the last window is closed (tray-only mode).
    QApplication::setQuitOnLastWindowClosed(false);

    QApplication app(argc, argv);
    QQuickStyle::setStyle("Fusion");

    ajazz::app::Application controller;
    controller.bootstrap();

    QQmlApplicationEngine engine;
    controller.exposeToQml(engine);
    engine.loadFromModule("AjazzControlCenter", "Main");
    if (engine.rootObjects().isEmpty()) {
        return -1;
    }

    // Start tray + hot-plug *after* QML is loaded; the tray needs a window.
    controller.startBackgroundServices(engine);

    // Honor the "start minimized to tray" preference: hide the root window
    // unless the tray is unavailable (in which case showing the window is the
    // only way the user can interact with the app).
    auto* tray = controller.trayController();
    if (tray && tray->startMinimized() && tray->trayAvailable()) {
        for (QObject* obj : engine.rootObjects()) {
            if (auto* win = qobject_cast<QWindow*>(obj)) {
                win->hide();
            }
        }
    }

    return app.exec();
}
